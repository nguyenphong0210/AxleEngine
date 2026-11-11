#include <Axle/SceneRenderer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Log.hpp>
#include <Axle/ParticleRenderer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/SceneLighting.hpp>
#include <Axle/BuiltinSceneShader.hpp>
#include <Axle/RenderParticleData.hpp>
#include <Axle/PhysicsEngine.hpp>

#include <algorithm>
#include <filesystem>

namespace Axle {

    SceneRenderer::SceneRenderer() {
        Init();
    }

    bool SceneRenderer::IsReady() const {
        const auto valid = [](const auto& shader) { return shader && shader->GetRendererID() != 0; };
        return Window::HasActiveContext() && valid(m_Shader) && valid(m_ShadowShader) &&
            m_ShadowMap && m_ShadowMap->GetDepthTextureID() && m_ShadowMap->GetRendererID() &&
            m_Skybox && valid(m_Skybox->GetShader()) && m_SkinningPipeline &&
            valid(m_SkinningPipeline->GetShader()) && valid(m_SkinningPipeline->GetShadowShader()) &&
            m_ParticleRenderer && valid(m_ParticleRenderer->GetShader());
    }

    void SceneRenderer::Init() {
        InitShaders();

        // Initialize Shadow Mapping (2048x2048)
        m_ShadowMap = std::make_shared<ShadowMap>();

        // Initialize Procedural Atmospheric Skybox
        m_Skybox = std::make_shared<Skybox>();

        // Initialize GPU-Instanced Particle Renderer (Phase 31)
        m_ParticleRenderer = std::make_shared<ParticleRenderer>();

        // Initialize internal Physics Debug Renderer fallback
        if (!m_InternalDebugRenderer && Window::HasActiveContext()) {
            if (!PhysicsEngine::IsInitialized()) PhysicsEngine::Init();
            m_InternalDebugRenderer = std::make_unique<PhysicsDebugRenderer>();
        }

        AXLE_CORE_INFO("SceneRenderer: Decoupled Multi-Pass Graphics Pipeline successfully initialized.");
    }

    void SceneRenderer::InitShaders() {
        // 1. Load or fallback PBR lighting shader
        auto vertPath = FileSystem::ResolveAssetPath("shaders/Checkerboard.vert");
        auto fragPath = FileSystem::ResolveAssetPath("shaders/Checkerboard.frag");
        std::string vertexSrc = FileSystem::ReadTextFile(vertPath);
        std::string fragmentSrc = FileSystem::ReadTextFile(fragPath);

        if (vertexSrc.empty() || fragmentSrc.empty()) {
            AXLE_CORE_WARN("SceneRenderer: Shader files not found at '{0}'. Using fallback embedded PBR shaders.", vertPath.string());
            vertexSrc=std::string(GetBuiltinSceneVertexShader());
            fragmentSrc=std::string(GetBuiltinSceneFragmentShader());
        } else {
            AXLE_CORE_INFO("SceneRenderer: Loaded PBR shaders from '{0}' and '{1}'", vertPath.string(), fragPath.string());
        }

        m_Shader = std::make_shared<Shader>(vertexSrc, fragmentSrc);
        m_Shader->Bind();
        m_Shader->SetInt("u_AlbedoMap", 0);
        m_Shader->SetInt("u_ShadowMap", 1);
        m_Shader->SetInt("u_NormalMap", 2);
        m_Shader->SetInt("u_RoughnessMap", 3);
        m_Shader->SetInt("u_MetallicMap", 4);
        m_Shader->SetInt("u_IrradianceMap", 5);
        m_Shader->SetInt("u_PrefilterMap", 6);
        m_Shader->SetInt("u_BRDFLUT", 7);
        m_Shader->SetInt("u_UseIBL", 0);
        m_Shader->SetFloat("u_EnvironmentIntensity", 1.0f);
        m_Shader->SetFloat("u_NormalStrength", 1.0f);
        m_Shader->SetFloat3("u_EmissionColor", glm::vec3(0.0f));
        m_Shader->SetFloat("u_EmissionIntensity", 0.0f);
        m_Shader->SetFloat("u_Exposure", 1.0f);
        m_Shader->SetInt("u_ToneMappingMode", 0);
        m_Shader->SetFloat("u_Gamma", 2.2f);
        m_Shader->SetInt("u_NumPointLights", 0);
        m_Shader->SetInt("u_NumSpotLights", 0);

        // 2. Load or fallback Shadow Depth shader
        std::string shadowVertSrc;
        std::string shadowFragSrc;
        auto shadowVertPath = FileSystem::ResolveAssetPath("shaders/ShadowDepth.vert");
        auto shadowFragPath = FileSystem::ResolveAssetPath("shaders/ShadowDepth.frag");
        if (FileSystem::Exists(shadowVertPath) && FileSystem::Exists(shadowFragPath)) {
            shadowVertSrc = FileSystem::ReadTextFile(shadowVertPath);
            shadowFragSrc = FileSystem::ReadTextFile(shadowFragPath);
        }
        if (shadowVertSrc.empty() || shadowFragSrc.empty()) {
            shadowVertSrc = R"(
                #version 450 core
                layout (location = 0) in vec3 a_Position;
                uniform mat4 u_LightSpaceMatrix;
                uniform mat4 u_Transform;
                void main() {
                    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
                }
            )";
            shadowFragSrc = R"(
                #version 450 core
                void main() {}
            )";
        }
        m_ShadowShader = std::make_shared<Shader>(shadowVertSrc, shadowFragSrc);

        // 3. Initialize GPU Skinning Pipeline for Skeletal Meshes
        m_SkinningPipeline = std::make_shared<SkinningPipeline>();
        m_SkinningPipeline->Init();
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                   const PerspectiveCamera& camera,
                                   const std::shared_ptr<Framebuffer>& targetFBO,
                                   PhysicsScene* physicsScene,
                                   PhysicsDebugRenderer* physicsDebugRenderer,
                                   const SceneRenderSettings& settings) {
        if (!scene) return;
        auto world = ExtractRenderWorld(*scene);
        if (!ResolveRenderWorldResources(world)) return;
        RenderWorldView(world, camera, targetFBO, physicsScene, physicsDebugRenderer, settings);
    }

    void SceneRenderer::RenderWorldView(const RenderWorld& world,
                                        const PerspectiveCamera& camera,
                                        const std::shared_ptr<Framebuffer>& targetFBO,
                                        PhysicsScene* physicsScene,
                                        PhysicsDebugRenderer* physicsDebugRenderer,
                                        const SceneRenderSettings& settings) {
        m_PhysicsDebugLineCount=0;
        if (!Window::HasActiveContext() || !m_Shader) return;

        // 1. Lighting setup from extracted RenderWorld
        const auto lighting=ResolveSceneLighting(world);
        const auto lightDirection=lighting.SunDirection;
        // IBL generation uses its own shaders, framebuffers and viewports. Finish
        // it before binding any view target or uploading that view's uniforms.
        if(lighting.Environment&&world.Skybox.Type==1&&!world.Skybox.HDRPath.empty()&&m_Skybox&&
           m_Skybox->GetSpecification().HDRPath!=world.Skybox.HDRPath)
            m_Skybox->LoadHDR(world.Skybox.HDRPath);

        bool castShadows = settings.EnableShadows && world.DirectionalLight.has_value() && world.DirectionalLight->CastsShadows;

        // 2. Cascaded Shadow Map Pass
        if (castShadows && m_ShadowMap && m_ShadowShader) {
            uint32_t targetRes = world.DirectionalLight->ShadowQuality == 0 ? 1024 : world.DirectionalLight->ShadowQuality == 2 ? 4096 : 2048;
            if (m_ShadowMap->GetWidth() != targetRes || m_ShadowMap->GetHeight() != targetRes) {
                m_ShadowMap->Resize(targetRes, targetRes);
            }

            m_ShadowMap->CalculateCascades(camera, lightDirection, world.DirectionalLight->MaxShadowDistance, world.DirectionalLight->CascadeSplitLambda);
            const auto& cascadeMatrices = m_ShadowMap->GetCascadeMatrices();

            m_ShadowShader->Bind();
            for (uint32_t c = 0; c < cascadeMatrices.size(); ++c) {
                m_ShadowMap->BindCascade(c);
                m_ShadowShader->SetMat4("u_LightSpaceMatrix", cascadeMatrices[c]);
                for (const auto& meshInst : world.Meshes) {
                    if (!meshInst.GLMesh || !meshInst.CastsShadows) continue;
                    m_ShadowShader->SetMat4("u_Transform", meshInst.WorldTransform);
                    meshInst.GLMesh->Bind();
                    RenderCommand::DrawIndexed(meshInst.GLMesh);
                    meshInst.GLMesh->Unbind();
                }
                if (m_SkinningPipeline && m_SkinningPipeline->GetShadowShader()) {
                    auto shader = m_SkinningPipeline->GetShadowShader(); shader->Bind();
                    shader->SetMat4("u_LightSpaceMatrix", cascadeMatrices[c]);
                    for (const auto& skin : world.Skins) {
                        if (!skin.GLMesh || !skin.CastsShadows) continue;
                        shader->SetMat4("u_Transform", skin.WorldTransform);
                        if (skin.BoneMatrices.empty()) m_SkinningPipeline->UploadIdentityPalette(*shader);
                        else m_SkinningPipeline->UploadBonePalette(*shader, skin.BoneMatrices);
                        RenderCommand::DrawIndexed(skin.GLMesh);
                    }
                }
            }
            m_ShadowMap->Unbind();
            m_LightSpaceMatrix = m_ShadowMap->GetLightSpaceMatrix(0);
        }

        // 3. Geometry & Lighting Pass
        u32 renderWidth = m_ViewportWidth;
        u32 renderHeight = m_ViewportHeight;

        if (targetFBO) {
            renderWidth = targetFBO->GetSpecification().Width;
            renderHeight = targetFBO->GetSpecification().Height;
            targetFBO->Bind();
        } else {
            RenderCommand::BindDefaultFramebuffer();
        }

        RenderCommand::SetViewport(0, 0, renderWidth, renderHeight);
        RenderCommand::SetClearColor(settings.ClearColor);
        RenderCommand::Clear();

        Texture2D::GetWhiteTexture()->Bind(0);
        if (m_ShadowMap) {
            m_ShadowMap->BindTexture(1);
        }
        Texture2D::GetFlatNormalTexture()->Bind(2);
        Texture2D::GetDefaultRoughnessTexture()->Bind(3);
        Texture2D::GetBlackTexture()->Bind(4);

        m_Shader->Bind();
        m_Shader->SetMat4("u_View", camera.GetViewMatrix());
        m_Shader->SetMat4("u_Projection", camera.GetProjectionMatrix());
        m_Shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
        m_Shader->SetFloat3("u_CameraPos", camera.GetPosition());

        const auto& cascadeMatrices = (castShadows && m_ShadowMap) ? m_ShadowMap->GetCascadeMatrices() : std::vector<glm::mat4>{};
        const auto& splitDists = (castShadows && m_ShadowMap) ? m_ShadowMap->GetCascadeSplitDistances() : std::vector<float>{};
        const auto sun = world.DirectionalLight.value_or(RenderDirectionalLight{});
        auto applyLighting=[&](Shader& shader) {
            if (world.DirectionalLight.has_value()) {
                const auto& dl = *world.DirectionalLight;
                shader.SetFloat3("u_LightDir", dl.Direction);
                shader.SetFloat3("u_LightColor", dl.Color);
                shader.SetFloat("u_LightIntensity", lighting.SunIntensity);
                shader.SetFloat("u_AmbientIntensity", lighting.AmbientIntensity);
                shader.SetFloat3("u_AmbientColor",lighting.AmbientColor);
            } else {
                shader.SetFloat3("u_LightDir", lightDirection);
                shader.SetFloat3("u_LightColor", glm::vec3(1.0f));
                shader.SetFloat("u_LightIntensity", 0.0f);
                shader.SetFloat("u_AmbientIntensity", lighting.AmbientIntensity);
                shader.SetFloat3("u_AmbientColor",lighting.AmbientColor);
            }


            if (!cascadeMatrices.empty()) {
                shader.SetMat4Array("u_CascadeLightSpaceMatrices", cascadeMatrices.data(), static_cast<u32>(cascadeMatrices.size()));
                shader.SetFloatArray("u_CascadeSplitDistances", splitDists.data(), static_cast<u32>(splitDists.size()));
            }
            shader.SetInt("u_ShadowsEnabled", castShadows ? 1 : 0);

            shader.SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrix);
            shader.SetInt("u_ShadowMap", 1); shader.SetInt("u_AlbedoMap", 0);
            shader.SetInt("u_NormalMap", 2); shader.SetInt("u_RoughnessMap", 3); shader.SetInt("u_MetallicMap", 4);
            shader.SetInt("u_IrradianceMap", 5); shader.SetInt("u_PrefilterMap", 6); shader.SetInt("u_BRDFLUT", 7);
            shader.SetFloat("u_ShadowBias", sun.ShadowBias); shader.SetFloat("u_ShadowSlopeBias", sun.ShadowSlopeBias);
            shader.SetFloat("u_ShadowNormalBias", sun.ShadowNormalBias); shader.SetFloat("u_ShadowFilterRadius", sun.ShadowFilterRadius);
            shader.SetFloat("u_Exposure", settings.Exposure); shader.SetFloat("u_Gamma", settings.Gamma);
            shader.SetInt("u_ToneMappingMode", settings.ToneMappingMode);
            bool useIBL = false;
            if (lighting.EnvironmentLighting && world.Skybox.Type == 1 && !world.Skybox.HDRPath.empty() && m_Skybox) {
                if (m_Skybox->GetIrradianceMap() && m_Skybox->GetPrefilterMap() && m_Skybox->GetBRDFLUT()) {
                    m_Skybox->GetIrradianceMap()->Bind(5); m_Skybox->GetPrefilterMap()->Bind(6); m_Skybox->GetBRDFLUT()->Bind(7); useIBL = true;
                }
            }
            shader.SetInt("u_UseIBL", useIBL); shader.SetFloat("u_EnvironmentIntensity", lighting.AmbientIntensity);

            const auto extraLights=std::min<size_t>(4,world.AdditionalDirectionalLights.size());
            shader.SetInt("u_NumExtraDirectionalLights",static_cast<int>(extraLights));
            for(size_t i=0;i<extraLights;++i) {
                const auto& light=world.AdditionalDirectionalLights[i];
                shader.SetFloat3("u_ExtraLightDirection["+std::to_string(i)+"]",light.Direction);
                shader.SetFloat3("u_ExtraLightColor["+std::to_string(i)+"]",light.Color*(light.Intensity>100.0f?light.Intensity:light.Intensity*100000.0f)*0.000035f);
            }

            // Point lights
            int numPointLights = std::min(static_cast<int>(world.PointLights.size()), 4);
            shader.SetInt("u_NumPointLights", numPointLights);
            for (int i = 0; i < numPointLights; ++i) {
                std::string prefix = "u_PointLights[" + std::to_string(i) + "].";
                shader.SetFloat3(prefix + "Position", world.PointLights[i].Position);
                shader.SetFloat3(prefix + "Color", world.PointLights[i].Color);
                shader.SetFloat(prefix + "Intensity", (world.PointLights[i].Intensity>10?world.PointLights[i].Intensity:world.PointLights[i].Intensity*1000.0f)*0.05f);
                shader.SetFloat(prefix + "Radius", world.PointLights[i].Radius);
                shader.SetFloat(prefix + "Falloff",world.PointLights[i].Falloff);
            }

            // Spot lights
            int numSpotLights = std::min(static_cast<int>(world.SpotLights.size()), 4);
            shader.SetInt("u_NumSpotLights", numSpotLights);
            for (int i = 0; i < numSpotLights; ++i) {
                std::string prefix = "u_SpotLights[" + std::to_string(i) + "].";
                shader.SetFloat3(prefix + "Position", world.SpotLights[i].Position);
                shader.SetFloat3(prefix + "Direction", world.SpotLights[i].Direction);
                shader.SetFloat3(prefix + "Color", world.SpotLights[i].Color);
                shader.SetFloat(prefix + "Intensity", (world.SpotLights[i].Intensity>10?world.SpotLights[i].Intensity:world.SpotLights[i].Intensity*500.0f)*0.05f);
                shader.SetFloat(prefix + "Range", world.SpotLights[i].Range);
                shader.SetFloat(prefix + "InnerCutOff", std::cos(glm::radians(world.SpotLights[i].InnerAngle)));
                shader.SetFloat(prefix + "OuterCutOff", std::cos(glm::radians(world.SpotLights[i].OuterAngle)));
            }

        };
        applyLighting(*m_Shader);

        if (settings.Wireframe) {
            RenderCommand::SetWireframeMode(true);
        }

        std::vector<const RenderMeshInstance*> opaque, transparent;
        for(const auto& mesh:world.Meshes) {
            if(!mesh.GLMesh)continue;
            const auto material=mesh.Material.MaterialAsset;
            const auto surface=material?material->GetSurfaceType():SurfaceType::Opaque;
            (surface==SurfaceType::Transparent||surface==SurfaceType::Additive?transparent:opaque).push_back(&mesh);
        }
        auto queue=[](const auto* mesh){return mesh->Material.MaterialAsset?mesh->Material.MaterialAsset->GetRenderQueue():2000;};
        std::stable_sort(opaque.begin(),opaque.end(),[&](auto* a,auto* b){return queue(a)<queue(b);});
        std::stable_sort(transparent.begin(),transparent.end(),[&](auto* a,auto* b){
            if(queue(a)!=queue(b))return queue(a)<queue(b);
            const auto da=glm::vec3(a->WorldTransform[3])-camera.GetPosition(), db=glm::vec3(b->WorldTransform[3])-camera.GetPosition();
            return glm::dot(da,da)>glm::dot(db,db);
        });
        auto drawMesh=[&](const RenderMeshInstance& meshInst) {
            glm::vec4 color1 = meshInst.Material.AlbedoColor;
            if (meshInst.Selected) {
                color1 = glm::mix(color1, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 0.35f);
            }

            m_Shader->Bind();
            const auto material=meshInst.Material.MaterialAsset;
            RenderCommand::SetMaterialState(material?material->GetSurfaceType():SurfaceType::Opaque,
                material?material->GetCullMode():CullMode::None,material?material->GetDepthWrite():true);
            m_Shader->SetMat4("u_Transform", meshInst.WorldTransform);
            if (meshInst.Material.MaterialAsset) {
                meshInst.Material.MaterialAsset->Bind(*m_Shader);
                m_Shader->SetInt("u_PatternMode", 3);
                m_Shader->SetFloat4("u_Color1", color1);
                m_Shader->SetFloat4("u_Color2", color1);
            } else {
                Material::BindDefault(*m_Shader, color1, meshInst.Material.SecondaryColor);
                m_Shader->SetFloat("u_TileScale", meshInst.Material.TileScale.x);
                m_Shader->SetInt("u_PatternMode", meshInst.Material.PatternMode);
            }

            meshInst.GLMesh->Bind();
            applyLighting(*m_Shader);
            RenderCommand::DrawIndexed(meshInst.GLMesh);
            meshInst.GLMesh->Unbind();
        };
        for(auto* mesh:opaque)drawMesh(*mesh);
        RenderCommand::SetMaterialState(SurfaceType::Opaque,CullMode::None,true);

        if (settings.Wireframe) {
            RenderCommand::SetWireframeMode(false);
        }

        auto renderSkins = [&](bool shadow, const glm::mat4& lightMatrix) {
            if (!m_SkinningPipeline) return;
            auto shader = shadow ? m_SkinningPipeline->GetShadowShader() : m_SkinningPipeline->GetShader();
            if (!shader) return;
            shader->Bind(); shader->SetMat4("u_LightSpaceMatrix", lightMatrix);
            shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix()); shader->SetMat4("u_View", camera.GetViewMatrix());
            shader->SetFloat3("u_LightDir", lightDirection); shader->SetFloat3("u_CameraPos", camera.GetPosition());
            if(!shadow){Material::BindDefault(*shader);applyLighting(*shader);shader->SetInt("u_PatternMode",3);}
            shader->SetInt("u_ShadowsEnabled", castShadows); shader->SetInt("u_ShadowMap", 1);
            if (!cascadeMatrices.empty()) shader->SetMat4Array("u_CascadeLightSpaceMatrices", cascadeMatrices.data(), static_cast<u32>(cascadeMatrices.size()));
            if (!splitDists.empty()) shader->SetFloatArray("u_CascadeSplitDistances", splitDists.data(), static_cast<u32>(splitDists.size()));
            for (const auto& skin : world.Skins) {
                if (!skin.GLMesh || (shadow && !skin.CastsShadows)) continue;
                shader->SetMat4("u_Transform", skin.WorldTransform); shader->SetFloat4("u_Color1", skin.Color);
                if (skin.BoneMatrices.empty()) m_SkinningPipeline->UploadIdentityPalette(*shader);
                else m_SkinningPipeline->UploadBonePalette(*shader, skin.BoneMatrices);
                RenderCommand::DrawIndexed(skin.GLMesh);
            }
        };
        renderSkins(false, m_LightSpaceMatrix);
        // 4. Skybox pass
        if (settings.EnableSkybox && m_Skybox && world.Skybox.Enabled) {
            m_Skybox->SetMode(world.Skybox.Type == 1 ? SkyboxMode::HDRCubemap : SkyboxMode::ProceduralAtmospheric);
            m_Skybox->SetHDRIntensity(world.Skybox.Intensity); m_Skybox->SetExposure(world.Skybox.Exposure);
            m_Skybox->SetToneMapping(settings.ToneMappingMode,settings.Gamma);
            auto& spec = m_Skybox->GetSpecification();
            spec.ZenithColor = glm::vec3(world.Skybox.ZenithColor);
            spec.HorizonColor = glm::vec3(world.Skybox.HorizonColor);
            spec.GroundColor = glm::vec3(world.Skybox.GroundColor);
            spec.SunSize = world.Skybox.SunSize; spec.SunGlow = world.Skybox.SunGlow; spec.Turbidity = world.Skybox.Turbidity;
            m_Skybox->SetSunDirection(world.DirectionalLight && world.Skybox.SyncWithSun
                ? lighting.SunDirection : glm::vec3(world.Skybox.SunDirection));
            m_Skybox->SetSunColor(world.DirectionalLight
                ? lighting.SunColor * std::max(0.0f,world.DirectionalLight->Intensity) : glm::vec3(0));
            m_Skybox->Render(camera.GetViewMatrix(), camera.GetProjectionMatrix());
        }

        if(settings.Wireframe)RenderCommand::SetWireframeMode(true);
        for(auto* mesh:transparent)drawMesh(*mesh);
        if(settings.Wireframe)RenderCommand::SetWireframeMode(false);
        RenderCommand::SetMaterialState(SurfaceType::Opaque,CullMode::None,true);
        if (settings.EnableParticles && m_ParticleRenderer && !world.Particles.empty()) {
            ParticleLightEnvironment particleLight;
            particleLight.SunDirection=lighting.SunDirection;particleLight.SunColor=lighting.SunColor*lighting.SunIntensity;
            particleLight.AmbientColor=lighting.AmbientColor*lighting.AmbientIntensity;
            for(const auto& point:world.PointLights)if(particleLight.PointLights.size()<4)
                particleLight.PointLights.push_back({point.Position,point.Color,point.Radius,point.Intensity>10?point.Intensity:point.Intensity*1000.0f});
            m_ParticleRenderer->SetLightEnvironment(particleLight);
            m_ParticleRenderer->Begin(camera, targetFBO, camera.GetNearClip(), camera.GetFarClip());
            for (const auto& batch : world.Particles)
                m_ParticleRenderer->Render(batch->Pool,batch->Config,batch->Texture,batch->Normal,batch->Config.SoftnessDistance,
                    batch->Position,batch->Rotation,batch->Scale,batch->MaterialResolved?&batch->Material:nullptr);
            m_ParticleRenderer->End();
        }

        // 5. Physics debug wireframes
        PhysicsDebugRenderer* activeDebug = physicsDebugRenderer ? physicsDebugRenderer : m_InternalDebugRenderer.get();
        if (activeDebug && ((settings.ShowPhysicsDebug && physicsScene) || !world.DebugLines.empty())) {
            activeDebug->Begin(camera.GetPosition(),settings.PhysicsDepthBias,settings.PhysicsAlphaMultiplier);
            if (settings.ShowPhysicsDebug && physicsScene) physicsScene->DrawDebug(*activeDebug, settings.PhysicsDrawSettings);
            for (const auto& line : world.DebugLines) activeDebug->DrawLine(
                JPH::RVec3(line.From.x, line.From.y, line.From.z), JPH::RVec3(line.To.x, line.To.y, line.To.z),
                JPH::Color(static_cast<uint8_t>(line.Color.r * 255), static_cast<uint8_t>(line.Color.g * 255), static_cast<uint8_t>(line.Color.b * 255), static_cast<uint8_t>(line.Color.a * 255)));
            activeDebug->End(camera.GetViewProjectionMatrix());
            m_PhysicsDebugLineCount=activeDebug->GetLineCount();
        }

        // 6. Cleanup
        Texture2D::GetWhiteTexture()->Unbind(0);
        if (m_ShadowMap) {
            m_ShadowMap->UnbindTexture(1);
        }
        Texture2D::GetFlatNormalTexture()->Unbind(2);
        Texture2D::GetDefaultRoughnessTexture()->Unbind(3);
        Texture2D::GetBlackTexture()->Unbind(4);

        if (targetFBO) {
            targetFBO->Unbind();
        }
    }

}
