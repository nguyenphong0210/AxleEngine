#include <Axle/SceneRenderBridge.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/SkinningPipeline.hpp>
#include <Axle/ParticleRenderer.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <algorithm>

namespace Axle {

    std::shared_ptr<VertexArray> CreateVertexArrayFromMeshSource(const AxleMeshSource& source) {
        if (source.Vertices.empty()) return nullptr;

        glm::vec3 minBound(1e9f);
        glm::vec3 maxBound(-1e9f);
        for (const auto& v : source.Vertices) {
            minBound = glm::min(minBound, v);
            maxBound = glm::max(maxBound, v);
        }
        glm::vec3 size = glm::max(maxBound - minBound, glm::vec3(0.01f));

        std::vector<f32> vertexBufferData;
        vertexBufferData.reserve(source.Vertices.size() * 5);
        for (size_t i = 0; i < source.Vertices.size(); ++i) {
            const auto& v = source.Vertices[i];
            vertexBufferData.push_back(v.x);
            vertexBufferData.push_back(v.y);
            vertexBufferData.push_back(v.z);
            float u = source.UVs.size() == source.Vertices.size() ? source.UVs[i].x : (v.x - minBound.x) / size.x;
            float v_coord = source.UVs.size() == source.Vertices.size() ? source.UVs[i].y : (v.z - minBound.z) / size.z;
            vertexBufferData.push_back(u);
            vertexBufferData.push_back(v_coord);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertexBufferData.data(), static_cast<u32>(vertexBufferData.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(source.Indices.data(), static_cast<u32>(source.Indices.size()));
        va->SetIndexBuffer(ib);
        return va;
    }

    static bool GetPrimaryDirectionalLightInternal(const SceneData& scene, DirectionalLightComponent& outLight) {
        auto view = scene.GetRegistry().view<DirectionalLightComponent>();
        for (auto entity : view) {
            outLight = view.get<DirectionalLightComponent>(entity);
            return true;
        }
        return false;
    }

    static bool GetPrimarySkyboxInternal(const SceneData& scene, SkyboxComponent& outSkybox) {
        auto view = scene.GetRegistry().view<SkyboxComponent>();
        for (auto entity : view) {
            outSkybox = view.get<SkyboxComponent>(entity);
            return true;
        }
        return false;
    }

    void SceneRenderBridge::RenderGeometryPass(SceneData& scene, const PerspectiveCamera& camera, Shader& shader,
                                               const glm::mat4& lightSpaceMatrix, const glm::vec3& lightDir, int shadowMapSlot,
                                               const std::vector<glm::mat4>& cascadeMatrices,
                                               const std::vector<float>& cascadeSplitDistances) {
        auto& registry = scene.GetRegistry();

        // 1. Query active directional sun light in scene
        DirectionalLightComponent sunLight;
        bool hasSunLight = GetPrimaryDirectionalLightInternal(scene, sunLight);

        glm::vec3 activeLightDir = hasSunLight ? sunLight.GetDirection() : lightDir;
        glm::vec3 activeLightColor = hasSunLight ? sunLight.Color : glm::vec3(0.0f);
        float activeIntensity = hasSunLight ? sunLight.Intensity : 0.0f;
        float activeAmbient = hasSunLight ? sunLight.AmbientIntensity : 0.0f;
        int activeShadows = (hasSunLight && sunLight.CastShadows) ? 1 : 0;
        float activeExposure = hasSunLight ? sunLight.Exposure : 1.0f;
        int activeToneMapping = hasSunLight ? sunLight.ToneMappingMode : 0;
        float activeGamma = hasSunLight ? sunLight.Gamma : 2.2f;

        // 2. Bind shader & setup camera, CSM, and light uniforms
        shader.Bind();
        shader.SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
        shader.SetMat4("u_View", camera.GetViewMatrix());
        shader.SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        if (!cascadeMatrices.empty()) {
            shader.SetMat4Array("u_CascadeLightSpaceMatrices", cascadeMatrices.data(), static_cast<u32>(cascadeMatrices.size()));
        }
        if (!cascadeSplitDistances.empty()) {
            shader.SetFloatArray("u_CascadeSplitDistances", cascadeSplitDistances.data(), static_cast<u32>(cascadeSplitDistances.size()));
        }

        shader.SetFloat3("u_LightDir", activeLightDir);
        shader.SetFloat3("u_LightColor", activeLightColor);
        shader.SetFloat("u_LightIntensity", activeIntensity);
        shader.SetFloat("u_AmbientIntensity", activeAmbient);
        shader.SetInt("u_ShadowsEnabled", activeShadows);
        shader.SetInt("u_ShadowMap", shadowMapSlot);
        shader.SetFloat("u_ShadowBias", (hasSunLight && sunLight.ShadowBias > 0.0f) ? sunLight.ShadowBias : 0.0008f);
        shader.SetFloat("u_ShadowSlopeBias", (hasSunLight && sunLight.ShadowSlopeBias > 0.0f) ? sunLight.ShadowSlopeBias : 0.0020f);
        shader.SetFloat("u_ShadowNormalBias", (hasSunLight && sunLight.ShadowNormalBias > 0.0f) ? sunLight.ShadowNormalBias : 0.0150f);
        shader.SetFloat("u_ShadowFilterRadius", (hasSunLight && sunLight.ShadowFilterRadius > 0.0f) ? sunLight.ShadowFilterRadius : 1.0f);

        shader.SetFloat("u_Exposure", activeExposure);
        shader.SetInt("u_ToneMappingMode", activeToneMapping);
        shader.SetFloat("u_Gamma", activeGamma);

        // 3. Query active atmospheric skybox for hemisphere ambient irradiance
        SkyboxComponent skybox;
        bool hasSkybox = GetPrimarySkyboxInternal(scene, skybox);
        int useHemisphereAmbient = (hasSkybox && skybox.Enabled && skybox.HemisphereAmbient) ? 1 : 0;
        glm::vec3 skyZenith = hasSkybox ? skybox.ZenithColor : glm::vec3(0.18f, 0.42f, 0.78f);
        glm::vec3 skyHorizon = hasSkybox ? skybox.HorizonColor : glm::vec3(0.70f, 0.82f, 0.95f);
        glm::vec3 skyGround = hasSkybox ? skybox.GroundColor : glm::vec3(0.18f, 0.16f, 0.14f);

        shader.SetFloat3("u_CameraPos", camera.GetPosition());
        shader.SetInt("u_UseHemisphereAmbient", useHemisphereAmbient);
        shader.SetFloat3("u_SkyZenithColor", skyZenith);
        shader.SetFloat3("u_SkyHorizonColor", skyHorizon);
        shader.SetFloat3("u_SkyGroundColor", skyGround);

        // 4. Upload dynamic Point Lights
        auto pointLightView = registry.view<PointLightComponent, TransformComponent>();
        int numPointLights = 0;
        for (auto entity : pointLightView) {
            if (numPointLights >= 4) break;
            const auto& plc = pointLightView.get<PointLightComponent>(entity);
            if (plc.Intensity <= 0.0f) continue;
            glm::mat4 worldTransform = scene.GetWorldTransform(entity);
            glm::vec3 worldPos = glm::vec3(worldTransform[3]);

            std::string prefix = "u_PointLights[" + std::to_string(numPointLights) + "].";
            shader.SetFloat3(prefix + "Position", worldPos);
            shader.SetFloat3(prefix + "Color", plc.Color);
            shader.SetFloat(prefix + "Intensity", plc.Intensity);
            shader.SetFloat(prefix + "Radius", plc.Radius);
            numPointLights++;
        }
        shader.SetInt("u_NumPointLights", numPointLights);

        // 5. Upload dynamic Spot Lights
        auto spotLightView = registry.view<SpotLightComponent, TransformComponent>();
        int numSpotLights = 0;
        for (auto entity : spotLightView) {
            if (numSpotLights >= 4) break;
            const auto& slc = spotLightView.get<SpotLightComponent>(entity);
            if (!slc.Enabled || slc.Intensity <= 0.0f) continue;
            glm::mat4 worldTransform = scene.GetWorldTransform(entity);
            glm::vec3 worldPos = glm::vec3(worldTransform[3]);
            glm::vec3 worldDir = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));

            std::string prefix = "u_SpotLights[" + std::to_string(numSpotLights) + "].";
            shader.SetFloat3(prefix + "Position", worldPos);
            shader.SetFloat3(prefix + "Direction", worldDir);
            shader.SetFloat3(prefix + "Color", slc.Color);
            shader.SetFloat(prefix + "Intensity", slc.Intensity);
            shader.SetFloat(prefix + "Range", slc.Range);
            shader.SetFloat(prefix + "InnerCutOff", std::cos(glm::radians(slc.InnerConeAngle)));
            shader.SetFloat(prefix + "OuterCutOff", std::cos(glm::radians(slc.OuterConeAngle)));
            numSpotLights++;
        }
        shader.SetInt("u_NumSpotLights", numSpotLights);

        // Texture sampler slot assignments
        shader.SetInt("u_AlbedoMap", 0);
        shader.SetInt("u_NormalMap", 2);
        shader.SetInt("u_RoughnessMap", 3);
        shader.SetInt("u_MetallicMap", 4);
        shader.SetInt("u_IrradianceMap", 5);
        shader.SetInt("u_PrefilterMap", 6);
        shader.SetInt("u_BRDFLUT", 7);

        int useIBL = (hasSkybox && skybox.Enabled && skybox.Type == SkyboxType::HDRCubemap && !skybox.HDRPath.empty()) ? 1 : 0;
        shader.SetInt("u_UseIBL", useIBL);
        shader.SetFloat("u_EnvironmentIntensity", hasSkybox ? skybox.Intensity : 1.0f);

        // Default texture & material parameters
        shader.SetInt("u_UseAlbedoMap", 0);
        shader.SetInt("u_UseNormalMap", 0);
        shader.SetInt("u_UseRoughnessMap", 0);
        shader.SetInt("u_UseMetallicMap", 0);
        shader.SetFloat2("u_UVScale", glm::vec2(1.0f, 1.0f));
        shader.SetFloat("u_Roughness", 0.5f);
        shader.SetFloat("u_Metallic", 0.0f);

        // 6. Render all entities with MeshInstance3D and TransformComponent
        auto renderView = registry.view<MeshInstance3D, TransformComponent>();
        for (auto entity : renderView) {
            auto& mesh = registry.get<MeshInstance3D>(entity);
            if (mesh.VAO) {
                glm::mat4 worldTransform = scene.GetWorldTransform(entity);
                shader.SetMat4("u_Transform", worldTransform);

                if (registry.all_of<MaterialComponent>(entity)) {
                    auto& mat = registry.get<MaterialComponent>(entity);
                    mat.Bind(shader);
                    shader.SetInt("u_PatternMode", 3); // Solid Color PBR Material
                    shader.SetFloat4("u_Color1", mat.GetAlbedoColor());
                    shader.SetFloat4("u_Color2", mat.GetAlbedoColor());
                } else {
                    shader.SetInt("u_PatternMode", mesh.PatternMode);
                    shader.SetFloat("u_TileScale", mesh.TileScale);
                    Material::BindDefault(shader, mesh.Color1, (mesh.PatternMode == 3) ? mesh.Color1 : mesh.Color2);
                }

                RenderCommand::DrawIndexed(mesh.VAO);
            }
        }
    }

    void SceneRenderBridge::RenderShadowPass(SceneData& scene, Shader& shadowShader, const glm::mat4& lightSpaceMatrix) {
        shadowShader.Bind();
        shadowShader.SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        auto& registry = scene.GetRegistry();
        auto renderView = registry.view<MeshInstance3D, TransformComponent>();
        for (auto entity : renderView) {
            auto& mesh = registry.get<MeshInstance3D>(entity);
            if (mesh.VAO) {
                glm::mat4 worldTransform = scene.GetWorldTransform(entity);
                shadowShader.SetMat4("u_Transform", worldTransform);
                RenderCommand::DrawIndexed(mesh.VAO);
            }
        }
    }

    void SceneRenderBridge::RenderSkeletalMeshes(SceneData& scene, SkinningPipeline& skinningPipeline, const PerspectiveCamera& camera,
                                                 const glm::mat4& lightSpaceMatrix, const glm::vec3& lightDir, bool shadowPass) {
        auto& registry = scene.GetRegistry();
        auto view = registry.view<SkeletalMeshComponent, TransformComponent>();
        if (view.begin() == view.end()) return;

        auto shader = shadowPass ? skinningPipeline.GetShadowShader() : skinningPipeline.GetShader();
        if (!shader) return;

        shader->Bind();
        if (shadowPass) {
            shader->SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);
        } else {
            shader->SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
            shader->SetMat4("u_View", camera.GetViewMatrix());
            shader->SetFloat3("u_LightDir", lightDir);
            shader->SetFloat3("u_CameraPos", camera.GetPosition());
        }

        for (auto entity : view) {
            const auto& smc = view.get<SkeletalMeshComponent>(entity);
            if (!smc.Visible || !smc.Mesh || !smc.Mesh->GetVAO()) continue;
            if (shadowPass && !smc.CastShadows) continue;

            glm::mat4 worldTransform = scene.GetWorldTransform(entity);
            shader->SetMat4("u_Transform", worldTransform);

            if (!shadowPass) {
                shader->SetFloat4("u_Color1", smc.Color1);
            }

            if (registry.all_of<AnimatorComponent>(entity)) {
                const auto& ac = registry.get<AnimatorComponent>(entity);
                if (ac.AnimatorInstance) {
                    const auto& skinningMatrices = ac.AnimatorInstance->GetSkinningMatrices();
                    skinningPipeline.UploadBonePalette(*shader, skinningMatrices);
                } else {
                    skinningPipeline.UploadIdentityPalette(*shader);
                }
            } else {
                skinningPipeline.UploadIdentityPalette(*shader);
            }

            RenderCommand::DrawIndexed(smc.Mesh->GetVAO());
        }
    }

    void SceneRenderBridge::RenderParticles(SceneData& scene, ParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                                            const std::shared_ptr<Framebuffer>& targetFBO) {
        auto& registry = scene.GetRegistry();
        auto view = registry.view<ParticleEmitterComponent, TransformComponent>();

        bool hasParticles = false;
        for (auto entityID : view) {
            auto& pec = view.get<ParticleEmitterComponent>(entityID);
            if (!pec.Visible) continue;
            for (const auto& stack : pec.EmitterStacks) {
                if (stack.Visible && stack.Enabled && stack.System && stack.System->GetActiveParticleCount() > 0) {
                    hasParticles = true;
                    break;
                }
            }
            if (hasParticles) break;
        }
        if (!hasParticles) {
            auto cvfxView = registry.view<CharacterVFXComponent>();
            for (auto entityID : cvfxView) {
                auto& cvfx = cvfxView.get<CharacterVFXComponent>(entityID);
                if (cvfx.Enabled && cvfx.RuntimeVFX && cvfx.RuntimeVFX->GetTotalActiveParticles() > 0) {
                    hasParticles = true;
                    break;
                }
            }
        }

        if (!hasParticles) return;

        particleRenderer.Begin(camera, targetFBO, camera.GetNearClip(), camera.GetFarClip());

        // 1. Render standalone ParticleEmitterComponents
        for (auto entityID : view) {
            auto& pec = view.get<ParticleEmitterComponent>(entityID);
            if (!pec.Visible) continue;
            for (const auto& stack : pec.EmitterStacks) {
                if (stack.Visible && stack.Enabled && stack.System && stack.System->GetActiveParticleCount() > 0) {
                    auto tex = stack.Texture ? stack.Texture : pec.Texture;
                    auto norm = stack.NormalMap ? stack.NormalMap : pec.NormalMap;
                    particleRenderer.Render(*stack.System, tex, norm, stack.Config.SoftnessDistance);
                }
            }
        }

        // 2. Render Character VFX particle systems
        auto cvfxView = registry.view<CharacterVFXComponent>();
        for (auto entityID : cvfxView) {
            auto& cvfx = cvfxView.get<CharacterVFXComponent>(entityID);
            if (!cvfx.Enabled || !cvfx.RuntimeVFX) continue;

            auto footstep = cvfx.RuntimeVFX->GetFootstepSystem();
            if (footstep && footstep->GetActiveParticleCount() > 0) {
                particleRenderer.Render(*footstep, nullptr, nullptr, 0.5f);
            }
            auto shockwave = cvfx.RuntimeVFX->GetLandingShockwaveSystem();
            if (shockwave && shockwave->GetActiveParticleCount() > 0) {
                particleRenderer.Render(*shockwave, nullptr, nullptr, 0.5f);
            }
        }

        particleRenderer.End();
    }

    void SceneRenderBridge::RenderParticles(SceneData& scene, RHIParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                                            RHI::RHICommandBuffer& cmd, MaterialTextureCache* textures) {
        (void)camera;
        auto& registry = scene.GetRegistry();
        auto view = registry.view<ParticleEmitterComponent, TransformComponent>();

        bool hasParticles = false;
        for (auto entityID : view) {
            auto& pec = view.get<ParticleEmitterComponent>(entityID);
            if (!pec.Visible) continue;
            for (const auto& stack : pec.EmitterStacks) {
                if (stack.Visible && stack.Enabled && stack.System && stack.System->GetActiveParticleCount() > 0) {
                    hasParticles = true;
                    break;
                }
            }
            if (hasParticles) break;
        }
        if (!hasParticles) {
            auto cvfxView = registry.view<CharacterVFXComponent>();
            for (auto entityID : cvfxView) {
                auto& cvfx = cvfxView.get<CharacterVFXComponent>(entityID);
                if (cvfx.Enabled && cvfx.RuntimeVFX && cvfx.RuntimeVFX->GetTotalActiveParticles() > 0) {
                    hasParticles = true;
                    break;
                }
            }
        }

        if (!hasParticles) return;

        // 1. Render standalone ParticleEmitterComponents
        for (auto entityID : view) {
            auto& pec = view.get<ParticleEmitterComponent>(entityID);
            if (!pec.Visible) continue;
            for (const auto& stack : pec.EmitterStacks) {
                if (stack.Visible && stack.Enabled && stack.System && stack.System->GetActiveParticleCount() > 0) {
                    auto resources = stack.GetDefinition().Resources;
                    if (resources.MaterialPath.empty()) resources.MaterialPath = stack.Config.MaterialPath;
                    const auto settings = ParticleMaterialAdapter::Resolve(stack.Config, resources, [](const auto& path) { return MaterialCache::GetMaterial(FileSystem::ResolveAssetPath(path)); });
                    auto texture = textures && settings.UseTexture ? textures->Get(settings.TexturePath, true) : nullptr;
                    auto normal = textures && settings.UseNormalMap ? textures->Get(settings.NormalMapPath, false) : nullptr;
                    auto config = stack.Config;
                    config.BlendMode = settings.BlendMode;
                    config.LightingEnabled = settings.LightingEnabled;
                    config.WrapLighting = settings.WrapLighting;
                    config.ScatteringFactor = settings.ScatteringFactor;
                    config.NormalMapStrength = settings.NormalMapStrength;
                    auto owner = scene.GetWorldTransform(entityID);
                    glm::vec3 scale(glm::length(glm::vec3(owner[0])), glm::length(glm::vec3(owner[1])), glm::length(glm::vec3(owner[2])));
                    auto axes = glm::mat3(owner);
                    for (int a = 0; a < 3; ++a) axes[a] /= std::max(scale[a], 0.0001f);
                    const auto transform = stack.GetWorldTransform(glm::vec3(owner[3]), glm::quat_cast(axes), scale);
                    particleRenderer.Render(cmd, stack.System->GetPool(), config, texture.get(), normal.get(), config.SoftnessDistance, transform.Translation, transform.Rotation, transform.Scale, &settings);
                }
            }
        }

        // 2. Render Character VFX particle systems
        auto cvfxView = registry.view<CharacterVFXComponent>();
        for (auto entityID : cvfxView) {
            auto& cvfx = cvfxView.get<CharacterVFXComponent>(entityID);
            if (!cvfx.Enabled || !cvfx.RuntimeVFX) continue;

            auto footstep = cvfx.RuntimeVFX->GetFootstepSystem();
            if (footstep && footstep->GetActiveParticleCount() > 0) {
                particleRenderer.Render(cmd, *footstep, nullptr, nullptr, 0.5f);
            }
            auto shockwave = cvfx.RuntimeVFX->GetLandingShockwaveSystem();
            if (shockwave && shockwave->GetActiveParticleCount() > 0) {
                particleRenderer.Render(cmd, *shockwave, nullptr, nullptr, 0.5f);
            }
        }
    }

}
