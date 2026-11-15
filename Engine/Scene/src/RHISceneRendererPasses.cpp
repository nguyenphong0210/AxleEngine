#include "RHISceneRendererPasses.hpp"
#include "Passes/GTAOPass.hpp"
#include "Passes/HiZGenerationPass.hpp"
#include "Passes/ScreenSpaceReflectionPass.hpp"
#include "Passes/FroxelFogPass.hpp"
#include "Passes/PostProcessPass.hpp"
#include <Axle/Path.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/SceneLighting.hpp>
#include <Axle/ShaderSurface.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/RHIPhysicsDebugRenderer.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/RenderParticleData.hpp>
#include <algorithm>

namespace Axle {
    using namespace RHI;
    static_assert(MaxLightingPointLights==MAX_POINT_LIGHTS && MaxLightingSpotLights==MAX_SPOT_LIGHTS);
    static_assert(MaxLightingRectLights==sizeof(RHIMultiLightBufferData{}.RectLights)/sizeof(RHIGPURectLight));
    static constexpr uint32_t ForwardTextureCount = static_cast<uint32_t>(RHITextureSlot::Count) + 7;

    struct DrawItem {
        std::shared_ptr<const RHIMeshView> Mesh;
        RHIObjectPushConstants PushConsts;
        Axle::SurfaceType SurfaceType = Axle::SurfaceType::Opaque;
        Axle::CullMode CullMode = Axle::CullMode::None;
        bool DepthWrite = true;
        float DistanceSq = 0.0f;
        int RenderQueue = 2000;
        uint64_t BaseSurfaceGeneration = 0;
        ResolvedSurface Surface;
        uint32_t DrawResourceIndex = 0;
        nri::Pipeline* PreparedPipeline = nullptr;
        RHIResourceLease PipelineLease;
        bool UseCustomShader = false;
        std::shared_ptr<const CustomReadyGeneration> CustomGeneration;
        PackedMaterialResultData PackedConstants;
        std::vector<TextureRequest> TextureRequests;
        entt::entity DiagnosticEntity = entt::null;
    };

    bool RHISceneRenderer::RecordFramePasses(const FramePassInputs& inputs) {
        const auto& [
            world, reflectionFrame, camera, settings, physicsScene,
            viewTargets, colorTarget, colorRTV, width,
            height, useMSAA, useToneMap, useFXAA,
            activeSampleCount, sceneTargetTexture, sceneTargetRTV, sceneDepthTexture,
            sceneDepthDSV, sceneTargetFormat, activePipeline,
            projZO, viewProjZO, jitterX,
            jitterY, giFrame, capabilities
        ] = inputs;
        uint32_t dispatchCount = 0;
        // Declare the actual sequential passes before recording. Attachments
        // start undefined for this view; only an explicit CLEAR/write defines
        // their contents. Imported targets retain their external owners.
        using Graph = RenderGraph;
        using Access = Graph::Access;
        using Load = Graph::Load;
        using Layout = RHIResourceLayout;
        Graph graph;
        const auto sceneColor = graph.Import("SceneColor", sceneTargetTexture);
        const auto sceneDepth = graph.Import("SceneDepth", sceneDepthTexture);
        const auto output = graph.Import("Output", colorTarget);
        const auto sceneNormal = m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()
            ? graph.Import("SceneNormal", m_OpaqueSceneNormal->GetNative()) : output;
        const auto hdr = useToneMap ? graph.Import("HDR", m_IntermediateHDRTarget->GetNative()) : output;
        const auto fxaa = useFXAA ? graph.Import("FXAA", m_IntermediateFXAATarget->GetNative()) : output;
        const bool copyRefraction = activeSampleCount == 1 && sceneDepthDSV && sceneDepthTexture &&
            std::any_of(world.Meshes.begin(), world.Meshes.end(), [](const auto& mesh) {
                const auto& material = mesh.Material.RHIMaterialAsset;
                return mesh.NativeView && mesh.NativeView->IsValid() && material &&
                    material->GetSurfaceType() == SurfaceType::Transparent && material->GetDesc().RefractionEnabled;
            });
        const bool copyParticleDepth = settings.EnableParticles && m_ParticleRenderer &&
            std::any_of(world.Particles.begin(), world.Particles.end(), [](const auto& particle) {
                return particle && particle->Config.SoftnessDistance > 0.0f;
            }) && activeSampleCount == 1 && sceneDepthDSV && sceneDepthTexture;
        const auto opaqueColor = copyRefraction ? graph.Import("OpaqueColor", m_OpaqueSceneColor->GetNative()) : output;
        const auto opaqueDepth = copyRefraction ? graph.Import("OpaqueDepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto particleDepthImage = copyParticleDepth ? graph.Import("ParticleDepth", m_ParticleSceneDepth->GetNative()) : sceneDepth;
        const bool useGTAO = (settings.EnableGTAO || settings.EnableContactShadows) && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid() && m_OpaqueSceneDepth && m_OpaqueSceneDepth->IsValid() &&
            viewTargets->GetGTAO() && viewTargets->GetGTAO()->IsValid() && viewTargets->GetGTAO()->GetUAV() != nullptr;
        const auto gtaoInput = useGTAO ? graph.Import("GTAOInput", m_OpaqueSceneColor->GetNative()) : output;
        const auto gtaoDepth = useGTAO ? graph.Import("GTAODepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto gtaoStorage = useGTAO ? graph.Import("GTAOStorage", viewTargets->GetGTAO()->GetNative()) : output;

        const bool useSSR = settings.EnableSSR && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_SSRComputePipeline && m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid() &&
            m_OpaqueSceneDepth && m_OpaqueSceneDepth->IsValid() &&
            m_ScreenStorageTarget && m_ScreenStorageTarget->IsValid() && m_ScreenStorageTarget->GetUAV() != nullptr;
        const bool useSurfaceNormal=(useGTAO || useSSR) && m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid();
        const auto ssrInput = useSSR ? graph.Import("SSRInput", m_OpaqueSceneColor->GetNative()) : output;
        const auto ssrDepth = useSSR ? graph.Import("SSRDepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto ssrStorage = useSSR ? graph.Import("SSRStorage", m_ScreenStorageTarget->GetNative()) : output;
        std::array<Graph::Resource, 8> hizResources{};
        if (useSSR) {
            for (uint32_t level = 0; level < hizResources.size(); ++level) {
                auto* hiz = viewTargets->GetHiZ(level);
                if (!hiz || !hiz->IsValid()) { return false; }
                hizResources[level] = graph.Import("HiZ" + std::to_string(level), hiz->GetNative());
            }
        }

        const bool useFog = settings.EnableVolumetricFog && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_FogComputePipeline && m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid() &&
            m_OpaqueSceneDepth && m_OpaqueSceneDepth->IsValid() &&
            m_ScreenStorageTarget && m_ScreenStorageTarget->IsValid() && m_ScreenStorageTarget->GetUAV() != nullptr;
        const auto fogInput = useFog ? graph.Import("FogInput", m_OpaqueSceneColor->GetNative()) : output;
        const auto fogDepth = useFog ? graph.Import("FogDepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto fogStorage = useFog ? graph.Import("FogStorage", m_ScreenStorageTarget->GetNative()) : output;

        const bool useBloom = settings.EnableBloom && activeSampleCount == 1 &&
            m_BloomComputePipeline && m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid() &&
            m_ScreenStorageTarget && m_ScreenStorageTarget->IsValid() && m_ScreenStorageTarget->GetUAV() != nullptr;
        const auto bloomInput = useBloom ? graph.Import("BloomInput", m_OpaqueSceneColor->GetNative()) : output;
        std::array<Graph::Resource, 6> bloomResources{};
        if (useBloom) {
            for (uint32_t level = 0; level < bloomResources.size(); ++level) {
                auto* bloom = viewTargets->GetBloom(level);
                if (!bloom || !bloom->IsValid()) { return false; }
                bloomResources[level] = graph.Import("Bloom" + std::to_string(level), bloom->GetNative());
            }
        }
        const bool useAutoExposure = settings.EnableAutoExposure && activeSampleCount == 1 && m_AutoExposurePipeline &&
            viewTargets->GetExposure() && viewTargets->GetExposure()->IsValid() &&
            viewTargets->GetExposure()->GetUAV() && viewTargets->GetExposure()->GetSRV();
        const bool useDDGI = giFrame.UpdateScheduled && activeSampleCount == 1 && m_DDGIPipeline && sceneDepthTexture && sceneDepthDSV &&
            viewTargets->GetDDGI() && viewTargets->GetDDGI()->IsValid() && viewTargets->GetDDGI()->GetUAV() != nullptr;
        if (giFrame.UpdateScheduled && !useDDGI) {
            m_LastGIFrame.UpdateScheduled=false;
            m_LastGIFrame.Status=GIFrameStatus::Fallback;
            m_LastGIFrame.Diagnostic="ddgi_native_update_unavailable: IBL diffuse fallback";
            m_ViewContext.Diagnostics().AddFallback(m_LastGIFrame.Diagnostic);
        }
        const auto ddgiInput = useDDGI ? graph.Import("DDGIInput", m_OpaqueSceneColor->GetNative()) : output;
        const auto ddgiDepth = useDDGI ? graph.Import("DDGIDepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto ddgiOutput = useDDGI ? graph.Import("DDGIOutput", viewTargets->GetDDGI()->GetNative()) : output;
        const auto exposureInput = useAutoExposure ? graph.Import("ExposureInput", useToneMap ? m_IntermediateHDRTarget->GetNative() : m_OpaqueSceneColor->GetNative()) : output;
        const auto exposureOutput = useAutoExposure ? graph.Import("ExposureOutput", viewTargets->GetExposure()->GetNative()) : output;

        RHITexture* history0 = viewTargets ? viewTargets->Get(RHIViewTarget::History0) : nullptr;
        RHITexture* history1 = viewTargets ? viewTargets->Get(RHIViewTarget::History1) : nullptr;
        RHITexture* historyRead = nullptr;
        RHITexture* historyWrite = nullptr;
        const bool useTAA = settings.EnableTAA && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_TAAPipelineSDR && history0 && history0->IsValid() && history1 && history1->IsValid();
        auto temporalSelection = m_ViewContext.Temporal().Begin(useTAA ? 2 : 0, { jitterX, jitterY });
        historyRead = (temporalSelection.ReadSlot == 0) ? history0 : history1;
        historyWrite = (temporalSelection.WriteSlot == 0) ? history0 : history1;
        // The first frame deliberately reads an uninitialized slot but the
        // shader bypasses it through the explicit HistoryValid flag below.
        const auto taaHistoryRead = useTAA ? graph.Import("TAAHistoryRead", historyRead->GetNative(), true) : output;
        const auto taaHistoryWrite = useTAA ? graph.Import("TAAHistoryWrite", historyWrite->GetNative()) : output;
        auto attachments = [&](Load load) {
            std::vector<Graph::Use> uses{{sceneColor, Layout::ColorAttachment, Access::Write, load}};
            if (sceneDepthDSV) uses.push_back({sceneDepth, Layout::DepthStencilWrite, Access::Write, load});
            return uses;
        };
        RHITexture* shadowAtlas = nullptr;
        RHITexture* localAtlas = nullptr;
        Graph::Resource shadows = output, localShadows = output;
        auto lightingFrame = m_LightingService.BuildFrame(world, settings);
        const auto& lighting = lightingFrame.Scene;
        ReflectionPublication reflectionPublication;
        RHISceneConstants sceneConstants;
        // 3. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = sceneTargetRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;
        colorAttachment.clearValue.color.f = { settings.ClearColor.r, settings.ClearColor.g, settings.ClearColor.b, settings.ClearColor.a };

        nri::AttachmentDesc depthAttachment = {};
        if (sceneDepthDSV) {
            depthAttachment.descriptor = sceneDepthDSV;
            depthAttachment.loadOp = nri::LoadOp::CLEAR;
            depthAttachment.storeOp = nri::StoreOp::STORE;
            depthAttachment.clearValue.depthStencil.depth = 1.0f;
            depthAttachment.clearValue.depthStencil.stencil = 0;
        }

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;
        if (sceneDepthDSV) {
            renderingDesc.depth = depthAttachment;
        }

        // Viewport & Scissor Setup
        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };


        bool diagnosticsPassActive = false;
        std::string activePass;
        struct PreparedParticle {
            std::shared_ptr<const RenderParticleData> Batch;
            EffectiveParticleRenderSettings Material;
            std::shared_ptr<RHITexture> Texture, Normal;
        };
        std::vector<PreparedParticle> preparedParticles;
        std::string activeGraphPass;
        std::unordered_map<std::string, std::unique_ptr<RHIBuffer>> passUniforms;
        auto passUniform = [&]() -> RHIBuffer& { return *passUniforms.at(activeGraphPass); };
            auto uniform = [&](const std::string& name, size_t size) {
                auto buffer=std::make_unique<RHIBuffer>();
                if (!buffer->Init(RHIBufferDesc::Constant(size,RHIMemoryLocation::HostUpload,name+"Constants")) ||
                    !graph.DeclareRead(name,buffer->GetCBV())) return false;
                passUniforms.emplace(name,std::move(buffer)); return true;
            };
        auto beginPass = [&](std::string_view name) {
            EndGPUTimestampPass();
            if (diagnosticsPassActive) {
                m_ViewContext.Diagnostics().EndPass();
                diagnosticsPassActive = false;
            }
            activePass = name;
            if (graph.BeginPass(name, Commands())) {
                activeGraphPass = name;
                BeginGPUTimestampPass(name);
                m_ViewContext.Diagnostics().BeginPass(std::string(name));
                diagnosticsPassActive = true;
                return true;
            }
            m_ViewContext.Diagnostics().AddError("graph_begin_failed:" + std::string(name));
            return false;
        };
        auto fail = [&] {
            return RenderPassResult::Failed(activePass + ": recording failed" +
                (graph.GetError().empty() ? std::string{} : " (" + graph.GetError() + ")"));
        };
        auto bindDescriptorSet = [&](uint32_t setIndex, nri::DescriptorSet* set,
                                     nri::BindPoint bindPoint = nri::BindPoint::GRAPHICS) {
            if (!graph.DeclareDescriptorRead(set, Commands())) return false;
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), setIndex, set, bindPoint);
            return Commands().IsRecording();
        };
        bool renderingActive = false;
        auto beginRendering = [&](const nri::RenderingDesc& desc) {
            Commands().BeginRendering(desc);
            renderingActive = true;
        };
        auto endRendering = [&] {
            if (renderingActive) Commands().EndRendering();
            renderingActive = false;
        };
        std::vector<DrawItem> opaqueQueue;
        std::vector<DrawItem> transparentQueue;
        auto RenderSkybox=[&]() -> bool {
        // 5. Procedural Atmospheric Skybox Pass (Rayleigh & Mie scattering at infinity)
        if (settings.EnableSkybox && m_SkyboxPipeline && lighting.Environment) {
            SkyboxComponent activeSkybox = m_SkyboxParams;
            if (world.Skybox.Authored) {
                activeSkybox.Enabled = world.Skybox.Enabled;
                activeSkybox.Type = static_cast<SkyboxType>(world.Skybox.Type);
                activeSkybox.Intensity = world.Skybox.Intensity; activeSkybox.Exposure = world.Skybox.Exposure;
                activeSkybox.ZenithColor = glm::vec3(world.Skybox.ZenithColor);
                activeSkybox.HorizonColor = glm::vec3(world.Skybox.HorizonColor);
                activeSkybox.GroundColor = glm::vec3(world.Skybox.GroundColor);
                activeSkybox.SunSize = world.Skybox.SunSize; activeSkybox.SunGlow = world.Skybox.SunGlow;
                activeSkybox.Turbidity = world.Skybox.Turbidity;
                activeSkybox.SyncWithSun = world.Skybox.SyncWithSun;
                activeSkybox.CustomAzimuth = world.Skybox.CustomAzimuth;
                activeSkybox.CustomElevation = world.Skybox.CustomElevation;
                activeSkybox.EnvironmentRotation = world.Skybox.EnvironmentRotation;
            }

            const bool hdrEnvironment=activeSkybox.Type==SkyboxType::HDRCubemap && bool(m_Environment.Ready());
            if (activeSkybox.Enabled && (activeSkybox.Type == SkyboxType::ProceduralAtmospheric || hdrEnvironment)) {
                // 1. Determine normalized Sun direction
                glm::vec3 sunDir = world.DirectionalLight && activeSkybox.SyncWithSun
                    ? lighting.SunDirection : glm::vec3(world.Skybox.SunDirection);
                if (glm::length(sunDir) > 1e-4f) {
                    sunDir = glm::normalize(sunDir);
                } else {
                    sunDir = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                // 2. Solar elevation, day factor and sunset horizon warming factors
                float sunElev = sunDir.y;
                float dayFactor = std::clamp(sunElev * 3.0f + 0.5f, 0.0f, 1.0f);
                float sunsetFactor = std::clamp(1.0f - std::abs(sunElev) * 2.5f, 0.0f, 1.0f);

                // 3. Setup 256-byte CBV aligned constant buffer
                RHISkyboxConstants skyboxConstants{};
                skyboxConstants.InvViewProjection = glm::inverse(sceneConstants.ViewProjection);
                skyboxConstants.CameraPosition = sceneConstants.CameraPosition;
                skyboxConstants.SunDirection = glm::vec4(sunDir, sunElev);
                const float sunIntensity = world.DirectionalLight.has_value()
                    ? std::max(0.0f, world.DirectionalLight->Intensity) : 0.0f;
                skyboxConstants.SunColor = glm::vec4(glm::vec3(lighting.SunColor) * sunIntensity * activeSkybox.Intensity, dayFactor);
                skyboxConstants.ZenithColor = glm::vec4(activeSkybox.ZenithColor, sunsetFactor);
                skyboxConstants.HorizonColor = glm::vec4(activeSkybox.HorizonColor, activeSkybox.Turbidity);
                skyboxConstants.GroundColor = glm::vec4(activeSkybox.GroundColor, activeSkybox.Exposure);
                // If Tone Mapping is enabled, output linear HDR sky radiance (-1.0f mode)
                float skyboxToneMode = settings.EnableToneMapping ? -1.0f : static_cast<float>(settings.ToneMappingMode);
                skyboxConstants.Params = glm::vec4(activeSkybox.SunSize, activeSkybox.SunGlow, skyboxToneMode, settings.EnableToneMapping ? 1.0f : settings.Gamma);

                if(hdrEnvironment) {
                    const double authoredScale=std::isfinite(activeSkybox.Intensity)&&std::isfinite(activeSkybox.Exposure)
                        ? std::clamp(double(activeSkybox.Intensity)*std::max(0.0,double(activeSkybox.Exposure)),0.0,1e12) : 0.0;
                    const float rotation=std::isfinite(activeSkybox.EnvironmentRotation)
                        ? glm::radians(activeSkybox.EnvironmentRotation) : 0.0f;
                    skyboxConstants.Reserved[0]=glm::vec4(1,static_cast<float>(authoredScale*m_Environment.SourceExposure()),rotation,0);
                    skyboxConstants.Params.z=settings.EnableToneMapping?-1.f:0.f;
                    skyboxConstants.Params.w=settings.EnableToneMapping?1.f:2.2f;
                }
                if (!m_SkyboxConstantBuffer.UpdateData(&skyboxConstants, sizeof(RHISkyboxConstants))) return false;

                // 4. Bind Skybox Graphics Pipeline matching active color & depth formats
                nri::Pipeline* activeSkyboxPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_SkyboxPipelineHDR && activeSampleCount == 1)
                                                          ? m_SkyboxPipelineHDR
                                                          : (sceneTargetFormat == nri::Format::RGBA8_UNORM && m_SkyboxPipeline && activeSampleCount == 1)
                                                                ? m_SkyboxPipeline
                                                                : GetOrCreateSkyboxPipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
                Commands().SetPipeline(activeSkyboxPipeline ? activeSkyboxPipeline : m_SkyboxPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_SkyboxPipelineLayout);
                if (m_SkyboxConstantBuffer.IsValid() && m_SkyboxConstantBuffer.GetCBV()) {
                    Commands().SetRootDescriptor(0, m_SkyboxConstantBuffer.GetCBV());
                }

                m_DescriptorPool.Bind(Commands().GetNative());
            if (!bindDescriptorSet(0, m_SkyboxDescriptorSet)) return false;
                // 5. Draw full-screen triangle on far plane (SV_VertexID unproject)
                Commands().Draw(3, 1, 0, 0);
                m_LastDrawCallCount++;
            }
        }

        return true;
        };

        size_t materialDrawIndex = 0, customDrawIndex = 0;
        bool drawFailed = false;
        auto declareMesh = [&](std::string_view pass, const RHIMeshView& mesh) {
            auto& resources = m_Device->GetResources();
            return graph.DeclareBuffer(pass, resources.Acquire(mesh.GetVertexHandle()), RHIAccess::VertexBuffer) &&
                graph.DeclareBuffer(pass, resources.Acquire(mesh.GetIndexHandle()), RHIAccess::IndexBuffer);
        };
        // Resolve resources and freeze descriptor inputs before graph compilation.
        auto PrepareDrawItem = [&](DrawItem& item, std::string_view pass, bool refractionReady) {
            if (!declareMesh(pass, *item.Mesh)) { drawFailed = true; return; }
            if (item.UseCustomShader && item.CustomGeneration) {
                auto prepareCustom = [&]() -> bool {
                        const auto& generation=*item.CustomGeneration;
                        auto* pipeline=GetOrCreateCustomPipeline(generation,sceneTargetFormat,m_CurrentDepthFormat,activeSampleCount,item.SurfaceType,item.CullMode,item.DepthWrite);
                        if (!pipeline || !generation.PipelineLayout || !generation.PipelineLayout->IsValid()) return false;
                        if (customDrawIndex==m_CustomDraws.size()) m_CustomDraws.push_back(std::make_unique<CustomDrawResources>());
                        item.DrawResourceIndex = static_cast<uint32_t>(customDrawIndex);
                        auto& resource=*m_CustomDraws[customDrawIndex++];
                        resource.Generation=item.CustomGeneration;
                        resource.Textures.clear(); resource.Sets.clear();
                        const auto& buffers=item.PackedConstants.Buffers;
                        resource.ConstantBuffers.resize(buffers.size());
                        for(size_t i=0;i<buffers.size();++i) {
                            const auto size=(buffers[i].Bytes.size()+255u)&~size_t(255u);
                            auto& buffer=resource.ConstantBuffers[i];
                            if ((!buffer.IsValid() || buffer.GetDesc().Size<size) &&
                                !buffer.Init(RHIBufferDesc::Constant(size,RHIMemoryLocation::HostUpload,"CustomMaterialBuffer"))) return false;
                            if (!buffer.UpdateData(buffers[i].Bytes.data(),buffers[i].Bytes.size())) return false;
                        }
                        for(const auto& request:item.TextureRequests) {
                            std::shared_ptr<const RHITexture> texture = request.Native;
                            if (!texture) {
                                std::string diagnostic;
                                texture=m_MaterialTextures.Get(request.ResolvedPath,request.Role==TextureRole::Color,&diagnostic);
                                if (!diagnostic.empty()) { item.Surface.Fallback=true; item.Surface.Diagnostic=std::move(diagnostic); }
                            }
                            if (!texture) texture=request.Role==TextureRole::TangentNormal?RHIMaterial::GetDefaultFlatNormalTexture():RHIMaterial::GetDefaultWhiteTexture();
                            if (!texture || !texture->GetSRV()) return false;
                            resource.Textures.push_back(std::move(texture));
                        }
                        for(size_t setIndex=0;setIndex<generation.LayoutDescription.DescriptorSets.size();++setIndex) {
                            auto* set=m_CustomDescriptorPool.AllocateDescriptorSet(*generation.PipelineLayout,static_cast<uint32_t>(setIndex));
                            if (!set) return false;
                            resource.Sets.push_back(set);
                            const auto& description=generation.LayoutDescription.DescriptorSets[setIndex];
                            for(size_t rangeIndex=0;rangeIndex<description.Ranges.size();++rangeIndex) {
                                const auto& range=description.Ranges[rangeIndex];
                                nri::Descriptor* descriptor=nullptr;
                                ShaderAddress address{description.RegisterSpace,range.baseRegisterIndex};
                                if (range.descriptorType==nri::DescriptorType::CONSTANT_BUFFER) {
                                    for(size_t i=0;i<buffers.size();++i) if(buffers[i].Address==address) descriptor=resource.ConstantBuffers[i].GetCBV();
                                } else if(range.descriptorType==nri::DescriptorType::TEXTURE) {
                                    for(size_t i=0;i<item.TextureRequests.size();++i) if(item.TextureRequests[i].Address==address) descriptor=resource.Textures[i]->GetSRV();
                                } else if(range.descriptorType==nri::DescriptorType::SAMPLER) {
                                    descriptor=m_CustomSampler.GetNative();
                                    for (const auto& sampler:generation.BindingPlan.Samplers) if(sampler.Address==address && sampler.Policy!=ShaderSamplerPolicy::Default) {
                                        const auto found=m_CustomPolicySamplers.find(sampler.Policy);
                                        if(found==m_CustomPolicySamplers.end())return false;
                                        descriptor=found->second.GetNative();
                                    }
                                }
                                if (!descriptor || range.descriptorNum!=1) return false;
                                if (!m_CustomDescriptorPool.UpdateTexture(set,static_cast<uint32_t>(rangeIndex),0,descriptor)) return false;
                            }
                        }
                    if (!FinalizeSurfaceTextureBindings(item.Surface, resource.Textures, item.Surface.TextureFlags)) return false;
                    item.PreparedPipeline = pipeline;
                    item.PipelineLease = m_Device->GetResources().AcquireNative(pipeline);
                    for (const auto& buffer : resource.ConstantBuffers)
                        if (!graph.DeclareRead(pass, buffer.GetCBV())) return false;
                    for (auto* set : resource.Sets) if (!graph.DeclareRead(pass, set)) return false;
                    return item.PipelineLease.IsLive();
                };
                if (prepareCustom()) return;
                if (!graph.GetError().empty()) { drawFailed = true; return; }
                    item.UseCustomShader = false;
                    item.Surface.Valid = true;
                    item.Surface.Fallback = true;
                    item.Surface.Evaluation = SurfaceEvaluation::BuiltIn;
                    item.Surface.ShaderGeneration = 0;
                    item.Surface.UnsupportedFeatures = 0;
                    item.Surface.Generation = item.BaseSurfaceGeneration;
                    item.Surface.Diagnostic = "CUSTOM_DRAW_FALLBACK: pipeline/resource preparation failed; using built-in material";
                    const auto entity = static_cast<uint32_t>(item.DiagnosticEntity);
                    m_MaterialDiagnostics[entity] = item.Surface.Diagnostic;
                    if (auto feedback = m_MaterialFeedback.find(entity); feedback != m_MaterialFeedback.end()) feedback->second.Generation = 0;
                }
                nri::Pipeline* needed = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && item.SurfaceType == Axle::SurfaceType::Opaque && item.CullMode == Axle::CullMode::None && item.DepthWrite && activeSampleCount == 1 && m_PipelineHDR)
                    ? m_PipelineHDR
                    : (sceneTargetFormat == nri::Format::RGBA8_UNORM && item.SurfaceType == Axle::SurfaceType::Opaque && item.CullMode == Axle::CullMode::None && item.DepthWrite && activeSampleCount == 1 && m_Pipeline)
                        ? m_Pipeline
                        : GetOrCreatePipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount, item.SurfaceType, item.CullMode, item.DepthWrite);

                if (!needed) { drawFailed = true; return; }
                if (materialDrawIndex == m_MaterialDraws.size()) {
                    auto resource = std::make_unique<MaterialDrawResources>();
                    if (!resource->Constants.Init(RHIBufferDesc::Constant(sizeof(RHIMaterialConstants), RHIMemoryLocation::HostUpload, "MaterialConstants"))) { drawFailed = true; return; }
                    resource->Set = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 0);
                    if (!resource->Set) {
                        AXLE_CORE_ERROR("Material descriptor capacity exhausted; view rejected.");
                        drawFailed = true; return;
                    }
                    m_MaterialDraws.push_back(std::move(resource));
                }
                item.DrawResourceIndex = static_cast<uint32_t>(materialDrawIndex);
                auto& resource = *m_MaterialDraws[materialDrawIndex++];
                RHIMaterialConstants materialData = item.Surface.ToMaterialConstants();
                std::vector<nri::Descriptor*> srvs(ForwardTextureCount);
                for (uint32_t slot = 0; slot < static_cast<uint32_t>(RHITextureSlot::Count); ++slot) {
                    std::shared_ptr<const RHITexture> texture;
                    if (materialData.Flags.x & (1u << slot)) {
                        const auto& binding = item.Surface.Textures[slot];
                        texture = binding.Native;
                        if (!texture) {
                            std::string diagnostic;
                            texture = m_MaterialTextures.Get(binding.Path, slot == 0 || slot == 5, &diagnostic);
                            if (!diagnostic.empty()) { item.Surface.Fallback=true; item.Surface.Diagnostic=std::move(diagnostic); }
                        }
                    }
                    if (!texture) {
                        materialData.Flags.x &= ~(1u << slot);
                        texture = RHIMaterial::GetFallbackTexture(static_cast<RHITextureSlot>(slot));
                    }
                    if (!texture || !texture->GetSRV()) { drawFailed = true; return; }
                    resource.Textures[slot] = texture;
                    item.Surface.Textures[slot].Native = texture;
                    srvs[slot<6?slot:slot+3]=texture->GetSRV();
                }
                srvs[6]=ActiveIrradiance()->GetSRV();
                srvs[7]=ActivePrefilter()->GetSRV();
                srvs[8]=ActiveBRDF()->GetSRV();
                srvs[13]=refractionReady?m_OpaqueSceneColor->GetSRV():RHIMaterial::GetDefaultBlackTexture()->GetSRV();
                srvs[14]=refractionReady?m_OpaqueSceneDepth->GetSRV():RHIMaterial::GetDefaultWhiteTexture()->GetSRV();
                srvs[15]=m_CascadedShadows.GetSRV();
                srvs[16]=m_LocalShadows.GetSRV();
                materialData.RefractionFrame=glm::vec4(width,height,refractionReady?1.0f:0.0f,0.0f);
                if (!FinalizeSurfaceTextureBindings(item.Surface, resource.Textures, materialData.Flags.x)) { drawFailed = true; return; }
                if (!m_DescriptorPool.UpdateTextureRange(resource.Set, 0, 0, srvs)) { drawFailed = true; return; }
                if (!resource.Constants.UpdateData(&materialData, sizeof(materialData))) { drawFailed = true; return; }
                item.PreparedPipeline = needed;
                item.PipelineLease = m_Device->GetResources().AcquireNative(needed);
                if (!item.PipelineLease.IsLive() || !graph.DeclareRead(pass, resource.Constants.GetCBV()) ||
                    !graph.DeclareRead(pass, resource.Set)) drawFailed = true;
        };
        auto RenderDrawItem = [&](const DrawItem& item) {
            if (item.UseCustomShader) {
                const auto& generation = *item.CustomGeneration;
                const auto& resource = *m_CustomDraws[item.DrawResourceIndex];
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, *generation.PipelineLayout);
                m_CustomDescriptorPool.Bind(Commands().GetNative());
                for (size_t i=0; i<resource.Sets.size(); ++i)
                    if (!bindDescriptorSet(static_cast<uint32_t>(i), resource.Sets[i])) { drawFailed = true; return; }
                Commands().SetPipeline(item.PreparedPipeline);
                item.Mesh->Bind(Commands()); item.Mesh->Draw(Commands());
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);
                m_DescriptorPool.Bind(Commands().GetNative());
                Commands().SetRootDescriptor(0, m_SceneConstantBuffer.GetCBV());
                Commands().SetRootDescriptor(1, m_LightBuffer.GetCBV());
                Commands().SetRootDescriptor(3, m_CascadedShadows.GetConstantsCBV());
                Commands().SetRootDescriptor(4, m_LocalShadows.GetConstantsCBV());
            } else {
                const auto& resource = *m_MaterialDraws[item.DrawResourceIndex];
                Commands().SetPipeline(item.PreparedPipeline);
                Commands().SetRootDescriptor(2, resource.Constants.GetCBV());
                if (!bindDescriptorSet(0, resource.Set)) { drawFailed = true; return; }
                Commands().SetRootConstants(0, &item.PushConsts, sizeof(item.PushConsts));
                m_LastStreamedTransformBytes += sizeof(item.PushConsts);
                item.Mesh->Bind(Commands()); item.Mesh->Draw(Commands());
            }
            if (!Commands().IsRecording()) { drawFailed = true; return; }
            m_LastDrawCallCount += static_cast<uint32_t>(item.Mesh->GetSubmeshCount());
            ++m_LastRenderedEntityCount;
        };

        // All adapters borrow this frame's inputs. Registration order is shared
        // by declaration, preparation and recording, including interleaved
        // normal/refraction/transparent work.
        m_PassScheduler.Clear();
        auto releaseFrame = [&](RenderPassScheduler* scheduler) {
            endRendering();
            scheduler->Clear();
        };
        // Also covers an exception while registering adapters, before Execute.
        std::unique_ptr<RenderPassScheduler, decltype(releaseFrame)> frameScope(&m_PassScheduler, releaseFrame);
        auto addPass = [&](const char* name, CallbackRenderPass::Callback record,
                           CallbackRenderPass::DeclareCallback declare,
                           CallbackRenderPass::Callback prepare = {},
                           CallbackRenderPass::Callback configure = {}) {
            m_PassScheduler.Add(std::make_unique<CallbackRenderPass>(name,
                std::move(prepare), std::move(record), std::move(declare),
                [name, configure = std::move(configure)](const RenderPassContext& context) {
                    if (!context.World || !context.Camera || !context.Settings || !context.View ||
                        !context.Graph || !context.Commands || !context.Commands->IsRecording())
                        return RenderPassResult::Failed(std::string(name) + ": incomplete active frame context");
                    if (!context.Capabilities.NativeRHI)
                        return RenderPassResult::Failed(std::string(name) + ": native RHI unavailable");
                    return configure ? configure(context) : RenderPassResult{};
                }));
        };
        addPass("LightingAndShadows", [&](const RenderPassContext&) -> RenderPassResult {
            if (m_CascadedShadows.IsActive() &&
                (!beginPass("CascadedShadows") || !m_CascadedShadows.Record(Commands())))
                return RenderPassResult::Failed("CascadedShadows: recording failed" +
                    (m_CascadedShadows.GetDiagnostic().empty() ? std::string{} : " (" + m_CascadedShadows.GetDiagnostic() + ")"));
            m_LastDrawCallCount += m_CascadedShadows.GetDrawCount();
            if (m_LocalShadows.NeedsRender() &&
                (!beginPass("LocalShadows") || !m_LocalShadows.Record(Commands())))
                return RenderPassResult::Failed("LocalShadows: recording failed" +
                    (m_LocalShadows.GetDiagnostic().empty() ? std::string{} : " (" + m_LocalShadows.GetDiagnostic() + ")"));
            m_LastDrawCallCount += m_LocalShadows.GetDrawCount();
            return RenderPassResult{};
        }, [&](RenderGraph& graph) {
            shadowAtlas = m_CascadedShadows.GetSampledAtlas();
            localAtlas = m_LocalShadows.GetSampledAtlas();
            shadows = graph.Import("CascadeAtlas", shadowAtlas ? shadowAtlas->GetNative() : nullptr, !m_CascadedShadows.IsActive());
            localShadows = graph.Import("LocalShadowAtlas", localAtlas ? localAtlas->GetNative() : nullptr, !m_LocalShadows.NeedsRender());
            if (m_CascadedShadows.IsActive()) {
                graph.AddPass("CascadedShadows", {{shadows,Layout::DepthStencilWrite,Access::Write,Load::Clear}});
                m_CascadedShadows.DeclareInputs(graph,"CascadedShadows");
            }
            if (m_LocalShadows.NeedsRender()) {
                graph.AddPass("LocalShadows", {{localShadows,Layout::DepthStencilWrite,Access::Write,Load::Clear}});
                m_LocalShadows.DeclareInputs(graph,"LocalShadows");
            }
        }, [&](const RenderPassContext&) -> RenderPassResult {
            const ShadowPublication shadowPublication{
                m_CascadedShadows.IsActive(), m_LocalShadows.IsActive(),
                shadowAtlas ? shadowAtlas->GetHandle() : RHIResourceHandle{},
                localAtlas ? localAtlas->GetHandle() : RHIResourceHandle{}};
            lightingFrame = m_LightingService.BuildFrame(world, settings, shadowPublication);
            m_LastLightingFrame=lightingFrame;
            if (lightingFrame.Status == LightingFrameStatus::Fallback && !lightingFrame.Diagnostic.empty())
                m_ViewContext.Diagnostics().AddFallback(lightingFrame.Diagnostic);
            const auto localIdentity=m_Device->GetResources().AcquireNative(m_LocalShadows.GetSRV()).GetHandle();
            if(localIdentity!=m_BoundLocalAtlas) {
                if(!m_DescriptorPool.UpdateTexture(m_DefaultDescriptorSet,0,16,m_LocalShadows.GetSRV()))return RenderPassResult::Failed("lighting/environment preparation failed");
                for(auto& draw:m_MaterialDraws)if(!m_DescriptorPool.UpdateTexture(draw->Set,0,16,m_LocalShadows.GetSRV()))return RenderPassResult::Failed("lighting/environment preparation failed");
                m_BoundLocalAtlas=localIdentity;
            }
            if (!m_Environment.Prepare(world.Skybox)) return RenderPassResult::Failed("lighting/environment preparation failed");
            m_PreparedManualIBL=m_UseIBL && world.Skybox.Type==1 && world.Skybox.HDRPath.empty();
            const bool skyIBLAvailable=m_IBLAllowed && (m_Environment.Ready() || m_PreparedManualIBL) && lighting.EnvironmentLighting;
            m_LastReflectionFrame=m_ReflectionService.ResolvePublication(reflectionFrame, reflectionPublication, useSSR, skyIBLAvailable);
            if (m_LastReflectionFrame.State==ReflectionState::Fallback || !m_LastReflectionFrame.Diagnostic.empty())
                m_ViewContext.Diagnostics().AddFallback(m_LastReflectionFrame.Diagnostic);
            const std::array<std::shared_ptr<RHITexture>,3> ibl={ActiveIrradiance(),ActivePrefilter(),ActiveBRDF()};
            std::array<RHIResourceHandle,3> identities;
            std::vector<nri::Descriptor*> descriptors;
            for(size_t i=0;i<3;++i){if(!ibl[i]||!ibl[i]->GetSRV())return RenderPassResult::Failed("lighting/environment preparation failed");identities[i]=ibl[i]->GetHandle();descriptors.push_back(ibl[i]->GetSRV());}
            if(identities!=m_BoundIBL) {
                if(!m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet,0,6,descriptors))return RenderPassResult::Failed("lighting/environment preparation failed");
                for(auto& draw:m_MaterialDraws)if(!m_DescriptorPool.UpdateTextureRange(draw->Set,0,6,descriptors))return RenderPassResult::Failed("lighting/environment preparation failed");
                m_BoundIBL=identities;
            }
            const auto skyTexture=m_Environment.Ready()?m_Environment.Ready()->Radiance:RHIMaterial::GetDefaultIrradianceCubemap();
            if(!skyTexture||!m_DescriptorPool.UpdateTexture(m_SkyboxDescriptorSet,0,0,skyTexture->GetSRV()))return RenderPassResult::Failed("lighting/environment preparation failed");
            const auto shadowIdentity=m_Device->GetResources().AcquireNative(m_CascadedShadows.GetSRV()).GetHandle();
            if (shadowIdentity!=m_BoundCascadeAtlas) {
                // Every cached set owns its last texture dependency. Refresh unused
                // sets too, so a removed mesh cannot retain a retired Ultra atlas.
                if (!m_DescriptorPool.UpdateTexture(m_DefaultDescriptorSet,0,15,m_CascadedShadows.GetSRV())) return RenderPassResult::Failed("lighting/environment preparation failed");
                for (auto& draw:m_MaterialDraws)
                    if (!m_DescriptorPool.UpdateTexture(draw->Set,0,15,m_CascadedShadows.GetSRV())) return RenderPassResult::Failed("lighting/environment preparation failed");
                m_BoundCascadeAtlas=shadowIdentity;
            }
            sceneConstants.ViewProjection = viewProjZO;
            sceneConstants.View = camera.GetViewMatrix();
            sceneConstants.Projection = projZO;
            sceneConstants.CameraPosition = glm::vec4(camera.GetPosition(), settings.EnableToneMapping ? 1.0f : 0.0f);
            sceneConstants.AmbientLight = glm::vec4(0.22f, 0.25f, 0.32f, lighting.AmbientIntensity);
            sceneConstants.SunDirection = glm::vec4(lighting.SunDirection,0.0f);
            sceneConstants.SunColor = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);

            RHIMultiLightBufferData lightData{};
            lightData.CameraPosAndExposure = glm::vec4(camera.GetPosition(), 1.0f);
            lightData.ReservedHeader[0].x = settings.EnableStudioLightLobes ? 0.0f : 1.0f;

            const auto& lightingSun=lightingFrame.Sun;
            lightData.SunLight.DirectionAndIlluminance = glm::vec4(lightingSun.Direction, lightingSun.Illuminance);
            lightData.SunLight.ColorAndAmbient = glm::vec4(lightingSun.Color, lightingSun.AmbientIntensity);
            lightData.SunLight.AmbientColor = glm::vec4(lightingSun.AmbientColor, 1.0f);
            lightData.SunLight.ShadowParams = glm::vec4(lightingSun.ShadowBias, lightingSun.ShadowFilterRadius,
                lightingSun.SolarAngularRadius, 0.0f);
            lightData.LightCounts.z = 1;

            for (uint32_t i=0; i<lightingFrame.PointCount; ++i) {
                const auto& source=lightingFrame.PointLights[i];
                auto& target=lightData.PointLights[i];
                target.PositionAndRadius = glm::vec4(source.Position, source.Radius);
                target.ColorAndIntensity = glm::vec4(source.Color, source.Intensity);
                target.Params = glm::vec4(source.Falloff, source.MinRadius * source.MinRadius,
                    source.Directional ? 1.0f : 0.0f, 0.0f);
            }
            lightData.LightCounts.x = lightingFrame.PointCount;

            for (uint32_t i=0; i<lightingFrame.SpotCount; ++i) {
                const auto& source=lightingFrame.SpotLights[i];
                auto& target=lightData.SpotLights[i];
                target.PositionAndRange = glm::vec4(source.Position, source.Range);
                target.DirectionAndIntensity = glm::vec4(source.Direction, source.Intensity);
                target.ColorAndInnerAngle = glm::vec4(source.Color, source.CosInnerAngle);
                target.AngleParams = glm::vec4(source.CosOuterAngle, source.Falloff, 0.0f, 0.0f);
            }
            lightData.LightCounts.y = lightingFrame.SpotCount;

            for (uint32_t i=0; i<lightingFrame.RectCount; ++i) {
                const auto& source=lightingFrame.RectLights[i];
                auto& target=lightData.RectLights[i];
                target.PositionAndWidth = glm::vec4(source.Position, source.Width);
                target.DirectionAndHeight = glm::vec4(source.Direction, source.Height);
                target.UpAndRange = glm::vec4(source.Up, source.Range);
                target.ColorAndIntensity = glm::vec4(source.Color, source.SignedIntensity);
            }
            lightData.ReservedHeader[1].x = static_cast<float>(lightingFrame.RectCount);

            bool useIBL = m_IBLAllowed && (m_Environment.Ready() || m_PreparedManualIBL) && lighting.EnvironmentLighting;
            lightData.ReservedHeader[0].y = lighting.AmbientIntensity*(lighting.EnvironmentLighting ? m_Environment.SourceExposure() : 1.0f);
            lightData.ReservedHeader[0].z = lighting.EnvironmentLighting && !useIBL ? 1.f : 0.f;
            lightData.ReservedHeader[0].w = lighting.EnvironmentLighting && useIBL && world.Skybox.Type==1 && std::isfinite(world.Skybox.EnvironmentRotation)
                ? glm::radians(world.Skybox.EnvironmentRotation) : 0.0f;
            lightData.LightCounts.w = useIBL ? 1u : 0u;

            if (!m_LightBuffer.Update(lightData))
                return RenderPassResult::Failed("light uniform update failed");

                if (m_ParticleRenderer) {
                    ParticleLightEnvironment environment;
                    environment.SunDirection = glm::vec3(lightData.SunLight.DirectionAndIlluminance);
                    environment.SunColor = glm::vec3(lightData.SunLight.ColorAndAmbient) *
                                           lightData.SunLight.DirectionAndIlluminance.w * 0.000035f;
                    environment.AmbientColor =
                        glm::vec3(lightData.SunLight.AmbientColor) * lightData.SunLight.ColorAndAmbient.w;
                    for (uint32_t i = 0; i < lightData.LightCounts.x && environment.PointLights.size() < 4; ++i) {
                        const auto &point = lightData.PointLights[i];
                        if (point.Params.z > 0.5f)
                            continue;
                        environment.PointLights.push_back({glm::vec3(point.PositionAndRadius),
                                                           glm::vec3(point.ColorAndIntensity),
                                                           point.PositionAndRadius.w, point.ColorAndIntensity.w});
                    }
                    m_ParticleRenderer->SetLightEnvironment(environment);
                }

            const auto& sun = lightData.SunLight;
            sceneConstants.SunDirection = glm::vec4(sun.DirectionAndIlluminance.x,
                                                    sun.DirectionAndIlluminance.y,
                                                    sun.DirectionAndIlluminance.z,
                                                    sun.DirectionAndIlluminance.w * 0.0005f);
            sceneConstants.SunColor = sun.ColorAndAmbient;
            sceneConstants.AmbientLight = glm::vec4(sun.AmbientColor.r, sun.AmbientColor.g, sun.AmbientColor.b, sun.ColorAndAmbient.w);

            if (!m_SceneConstantBuffer.IsValid() ||
                !m_SceneConstantBuffer.UpdateData(&sceneConstants, sizeof(RHISceneConstants)))
                return RenderPassResult::Failed("scene uniform update failed");

            return m_LastReflectionFrame.State==ReflectionState::Fallback
                ? RenderPassResult::Fallback(m_LastReflectionFrame.Diagnostic) : RenderPassResult{};
        }, [&](const RenderPassContext&) -> RenderPassResult {
            reflectionPublication=PrepareReflectionProbe(reflectionFrame);
            if (!m_CascadedShadows.Prepare(world, camera, settings, m_MaterialTextures))
                return RenderPassResult::Failed(m_CascadedShadows.GetDiagnostic());
            if (!m_LocalShadows.Prepare(world, camera, settings, m_MaterialTextures))
                return RenderPassResult::Failed(m_LocalShadows.GetDiagnostic());
            return {};
        });

        addPass("OpaqueAndSky", [&](const RenderPassContext&) -> RenderPassResult {
            if (!beginPass("OpaqueAndSky")) return fail();
            beginRendering(renderingDesc);
            Commands().SetViewport(vp);
            Commands().SetScissor(scissor);
            // Bind Graphics Pipeline & Layout matching active color & depth formats and sample count
            Commands().SetPipeline(activePipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);

            // Bind Root Descriptors: [0] SceneConstants (b1), [1] MultiLightBuffer (b2)
            if (m_SceneConstantBuffer.IsValid() && m_SceneConstantBuffer.GetCBV()) {
                Commands().SetRootDescriptor(0, m_SceneConstantBuffer.GetCBV());
            }
            if (m_LightBuffer.IsValid() && m_LightBuffer.GetCBV()) {
                Commands().SetRootDescriptor(1, m_LightBuffer.GetCBV());
            }

            Commands().SetRootDescriptor(3,m_CascadedShadows.GetConstantsCBV());
            Commands().SetRootDescriptor(4,m_LocalShadows.GetConstantsCBV());

            // Bind Descriptor Pool & Material Descriptor Set (Space 1: 6 PBR textures + 16x Aniso Sampler)
            if (m_DescriptorPool.IsValid()) {
                m_DescriptorPool.Bind(Commands().GetNative());
                if (m_DefaultDescriptorSet && !bindDescriptorSet(0, m_DefaultDescriptorSet)) {
                    endRendering();
                    return fail();
                }
            }

            std::sort(opaqueQueue.begin(), opaqueQueue.end(), [](const DrawItem& a, const DrawItem& b) {
                return a.RenderQueue < b.RenderQueue;
            });
            // 4a. Render Opaque & Cutout Queue
            for (auto& item : opaqueQueue) {
                RenderDrawItem(item);
                if (drawFailed) { endRendering(); return fail(); }
            }

            if (!RenderSkybox()) { endRendering(); return fail(); }
            endRendering();

            return {};
        }, [&](RenderGraph& graph) {
            auto opaqueUses=attachments(Load::Clear);
            if(localAtlas)opaqueUses.push_back({localShadows,Layout::ShaderResource});
            if (shadowAtlas) opaqueUses.push_back({shadows,Layout::ShaderResource});
            graph.AddPass("OpaqueAndSky", std::move(opaqueUses));
        }, [&](const RenderPassContext&) -> RenderPassResult {
            std::unordered_map<CustomProgramKey,std::string,CustomProgramKeyHash> customFingerprints;

            glm::vec3 camPos = camera.GetPosition();

            for (const auto& meshInst : world.Meshes) {
                const auto entityID = static_cast<entt::entity>(meshInst.EntityID);
                auto mesh = meshInst.NativeView;
                if (mesh && mesh->IsValid()) {
                    glm::mat4 worldMat = meshInst.WorldTransform;

                    DrawItem item;
                    item.DiagnosticEntity=entityID;
                    item.Mesh = mesh;
                    item.PushConsts.Model = worldMat;
                    item.Surface = MaterialResolver::Resolve(meshInst.Material);
                    item.BaseSurfaceGeneration = item.Surface.Generation;
                    if (!item.Surface.Valid) {
                        m_MaterialDiagnostics[meshInst.EntityID] = item.Surface.Diagnostic;
                        m_ViewContext.Diagnostics().AddMaterial(meshInst.EntityID, item.Surface);
                        return RenderPassResult::Failed(item.Surface.Diagnostic);
                    }
                    glm::vec3 diff = glm::vec3(worldMat[3]) - camPos;
                    item.DistanceSq = glm::dot(diff, diff);

                    // Apply PBR Material parameters if present
                    // Entity material overrides the mesh resource material.
                    if (meshInst.Material.RHIMaterialAsset) {
                        auto mat = meshInst.Material;
                        auto diagnostic=[&](std::string message) {
                            m_MaterialDiagnostics[meshInst.EntityID] = std::move(message);
                        };
                        diagnostic("");
                        if (mat.UseCustomShader) {
                            CustomProgramKey key;
                            key.ShaderPath=mat.CustomShaderPath.empty()?"":PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(mat.CustomShaderPath)).lexically_normal());
                            key.Backend=m_Device->GetBackend();
                            auto [fingerprint,inserted]=customFingerprints.try_emplace(key);
                            if(inserted && !key.ShaderPath.empty())fingerprint->second=m_CustomProgramCache.ComputeFingerprint(key);
                            auto fp=fingerprint->second;
                            auto selected=m_CustomProgramCache.SelectProgram(true,key.ShaderPath,key,fp);
                            if(const auto* entry=m_CustomProgramCache.FindEntry(key))fingerprint->second=entry->LastAttemptFingerprint;
                            diagnostic(selected.Diagnostic);
                            if (selected.Kind==ProgramSelectionKind::Custom && selected.Generation) {
                                const auto& plan=selected.Generation->BindingPlan;
                                mat.CustomProperties=SynchronizeCustomProperties(plan.PropertySchema,mat.CustomProperties);
                                m_MaterialFeedback[meshInst.EntityID] = {mat.CustomProperties, selected.Generation->GenerationID};
                                EngineMaterialValues engine;
                                engine.Model=worldMat; engine.ViewProjection=sceneConstants.ViewProjection;
                                engine.View=sceneConstants.View; engine.Projection=sceneConstants.Projection;
                                engine.CameraPosition=sceneConstants.CameraPosition;
                                engine.SunDirection=sceneConstants.SunDirection; engine.SunColor=sceneConstants.SunColor;
                                auto resolved = ResolveShaderSurface(item.Surface, SurfaceEvaluation::CustomShader,
                                    selected.Generation->GenerationID, plan, engine, mat.CustomProperties);
                                if (auto* surface = std::get_if<ResolvedShaderSurface>(&resolved)) {
                                    item.Surface = std::move(surface->Surface);
                                    item.PackedConstants = std::move(surface->Constants);
                                    item.TextureRequests = std::move(surface->Textures);
                                    item.CustomGeneration = selected.Generation;
                                    item.UseCustomShader = true;
                                    if (!item.Surface.Diagnostic.empty()) diagnostic(item.Surface.Diagnostic);
                                } else {
                                    const auto& error = std::get<MaterialBindingError>(resolved);
                                    diagnostic(error.Code + ": " + error.Message);
                                    m_MaterialFeedback[meshInst.EntityID].Generation = 0;
                                }
                            }
                        }
                    } else if (meshInst.Material.UseCustomShader) {
                        m_MaterialDiagnostics[meshInst.EntityID] = "CUSTOM_SURFACE_INPUT_MISSING: native material input unavailable; using procedural adapter";
                    }
                    if (const auto diagnostic = m_MaterialDiagnostics.find(meshInst.EntityID); diagnostic != m_MaterialDiagnostics.end())
                        item.Surface.Diagnostic = diagnostic->second;
                    if (!item.Surface.Diagnostic.empty()) item.Surface.Fallback=true;
                    {
                        item.Surface.ApplyToPushConstants(item.PushConsts);
                        item.PushConsts.Model = worldMat;
                        item.SurfaceType = item.Surface.Surface;
                        item.CullMode = item.Surface.Cull;
                        item.DepthWrite = item.Surface.DepthWrite;
                        item.RenderQueue = item.Surface.RenderQueue;
                    }

                    if (item.SurfaceType == Axle::SurfaceType::Transparent || item.SurfaceType == Axle::SurfaceType::Additive) {
                        transparentQueue.push_back(std::move(item));
                    } else {
                        opaqueQueue.push_back(std::move(item));
                    }
                }
            }

            for (auto& item : opaqueQueue) PrepareDrawItem(item,"OpaqueAndSky",false);
            for (auto& item : transparentQueue) PrepareDrawItem(item,"Transparent",copyRefraction);
            if (drawFailed) return RenderPassResult::Failed("material input preparation failed: " + graph.GetError());
            for (const auto pass : {"OpaqueAndSky", "Transparent"}) {
                for (auto* cbv : {m_SceneConstantBuffer.GetCBV(), m_LightBuffer.GetCBV(),
                    m_CascadedShadows.GetConstantsCBV(), m_LocalShadows.GetConstantsCBV()})
                    if (!graph.DeclareRead(pass,cbv)) return fail();
            }
            if (!graph.DeclareRead("OpaqueAndSky",m_DefaultDescriptorSet)) return fail();
            if (settings.EnableSkybox && m_SkyboxPipeline && lighting.Environment &&
                (!graph.DeclareRead("OpaqueAndSky",m_SkyboxDescriptorSet) ||
                 !graph.DeclareRead("OpaqueAndSky",m_SkyboxConstantBuffer.GetCBV()))) return fail();
            if (useSurfaceNormal) {
                if (!graph.DeclareRead("SurfaceNormal",m_DefaultDescriptorSet) ||
                    !graph.DeclareRead("SurfaceNormal",m_SceneConstantBuffer.GetCBV())) return fail();
                for (const auto& item : opaqueQueue) if (!declareMesh("SurfaceNormal",*item.Mesh)) return fail();
            }
            return {};
        });

        addPass("ScreenSpace", [&](const RenderPassContext&) -> RenderPassResult {
            if (!useSurfaceNormal)
                return RenderPassResult{};
            if (!beginPass("SurfaceNormal"))
                return RenderPassResult::Failed("SurfaceNormal: graph begin failed");
            auto* normalPipeline = GetOrCreateSurfaceNormalPipeline(
                RHI::ToNRIFormat(m_OpaqueSceneNormal->GetFormat()), m_CurrentDepthFormat);
            if (!normalPipeline)
                return RenderPassResult::Failed("SurfaceNormal: pipeline creation failed");

            nri::AttachmentDesc normalAttachment = {};
            normalAttachment.descriptor = m_OpaqueSceneNormal->GetRTV();
            normalAttachment.loadOp = nri::LoadOp::CLEAR;
            normalAttachment.storeOp = nri::StoreOp::STORE;
            normalAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 0.0f };

            nri::AttachmentDesc normalDepthAttachment = depthAttachment;
            normalDepthAttachment.loadOp = nri::LoadOp::LOAD;
            normalDepthAttachment.storeOp = nri::StoreOp::STORE;
            nri::RenderingDesc normalRenderingDesc = {};
            normalRenderingDesc.colors = &normalAttachment;
            normalRenderingDesc.colorNum = 1;
            if (sceneDepthDSV) normalRenderingDesc.depth = normalDepthAttachment;
            beginRendering(normalRenderingDesc);
            Commands().SetViewport(vp);
            Commands().SetScissor(scissor);
            Commands().SetPipeline(normalPipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);
            Commands().SetRootDescriptor(0, m_SceneConstantBuffer.GetCBV());
            if (m_DescriptorPool.IsValid()) {
                m_DescriptorPool.Bind(Commands().GetNative());
                if (m_DefaultDescriptorSet && !bindDescriptorSet(0, m_DefaultDescriptorSet)) {
                    endRendering();
                    return RenderPassResult::Failed("SurfaceNormal: descriptor binding failed");
                }
            }
            for (const auto& item : opaqueQueue) {
                Commands().SetRootConstants(0, &item.PushConsts, sizeof(item.PushConsts));
                item.Mesh->Bind(Commands());
                item.Mesh->Draw(Commands());
                m_LastDrawCallCount += static_cast<uint32_t>(item.Mesh->GetSubmeshCount());
            }
            endRendering();
            return RenderPassResult{};
        }, [&](RenderGraph& graph) {
            if (useSurfaceNormal) {
                graph.AddPass("SurfaceNormal", {
                    {sceneDepth, Layout::DepthStencilWrite, Access::Write, Load::Load},
                    {sceneNormal, Layout::ColorAttachment, Access::Write, Load::Clear}});
            }
        });

        addPass("Transparent", [&](const RenderPassContext&) -> RenderPassResult {
            if (copyRefraction) {
                if (!beginPass("RefractionCopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);
            }
            if (!beginPass("Transparent")) return fail();
            colorAttachment.loadOp = nri::LoadOp::LOAD;
            depthAttachment.loadOp = nri::LoadOp::LOAD;
            if (sceneDepthDSV) renderingDesc.depth = depthAttachment;
            beginRendering(renderingDesc);
            // Skybox changes the entire binding layout. Restore all built-in
            // receiver state before transparent or custom material draws.
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS,m_PipelineLayout);
            m_DescriptorPool.Bind(Commands().GetNative());
            Commands().SetRootDescriptor(0,m_SceneConstantBuffer.GetCBV());
            Commands().SetRootDescriptor(1,m_LightBuffer.GetCBV());
            Commands().SetRootDescriptor(3,m_CascadedShadows.GetConstantsCBV());
            Commands().SetRootDescriptor(4,m_LocalShadows.GetConstantsCBV());

            // 4b. Render Transparent & Additive Queue (Sorted Back-to-Front)
            if (!transparentQueue.empty()) {
                std::sort(transparentQueue.begin(), transparentQueue.end(), [](const DrawItem& a, const DrawItem& b) {
                    return a.RenderQueue == b.RenderQueue ? a.DistanceSq > b.DistanceSq : a.RenderQueue < b.RenderQueue;
                });
                for (auto& item : transparentQueue) {
                    RenderDrawItem(item);
                    if (drawFailed) { endRendering(); return fail(); }
                }
            }

            endRendering();
            return {};
        }, [&](RenderGraph& graph) {
            if (copyRefraction) graph.AddPass("RefractionCopy", {
                {sceneColor,Layout::CopySource}, {sceneDepth,Layout::CopySource},
                {opaqueColor,Layout::CopyDest,Access::Write}, {opaqueDepth,Layout::CopyDest,Access::Write}});
            auto transparentUses = attachments(Load::Load);
            if(localAtlas)transparentUses.push_back({localShadows,Layout::ShaderResource});
            if (shadowAtlas) transparentUses.push_back({shadows,Layout::ShaderResource});
            if (copyRefraction) {
                transparentUses.push_back({opaqueColor,Layout::ShaderResource});
                transparentUses.push_back({opaqueDepth,Layout::ShaderResource});
            }
            graph.AddPass("Transparent", std::move(transparentUses));
        });

        addPass("ParticlesAndResolve", [&](const RenderPassContext&) -> RenderPassResult {
            if (copyParticleDepth) {
                if (!beginPass("ParticleDepthCopy")) return fail();
                Commands().CopyTexture(*m_ParticleSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);
            }
            if (!beginPass("ParticlesAndPhysics")) return fail();
            beginRendering(renderingDesc);
            // Particle simulation remains external to rendering; only owned batches
            // and the declared depth snapshot are consumed here.
            if (settings.EnableParticles && m_ParticleRenderer && !world.Particles.empty()) {
                m_ParticleRenderer->ResetMetrics();
                RHITexture* particleDepth = copyParticleDepth ? m_ParticleSceneDepth : nullptr;
                // MSAA uses depth testing with soft intersections disabled until a compatible depth resolve exists.
                m_ParticleRenderer->Begin(Commands(), camera, sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
                m_ParticleRenderer->SetSceneDepth(particleDepth, width, height);
                for (const auto& prepared : preparedParticles) {
                    const auto& batch=prepared.Batch;
                    const auto& effective=prepared.Material;
                    const auto& texture=prepared.Texture;
                    const auto& normal=prepared.Normal;
                    auto config = batch->Config;
                    config.BlendMode = effective.BlendMode; config.LightingEnabled = effective.LightingEnabled;
                    config.WrapLighting = effective.WrapLighting; config.ScatteringFactor = effective.ScatteringFactor;
                    config.NormalMapStrength = effective.NormalMapStrength;
                    if (!m_ParticleRenderer->Render(Commands(), batch->Pool, config, texture.get(), normal.get(), config.SoftnessDistance,
                        batch->Position, batch->Rotation, batch->Scale, &effective)) {
                        m_ParticleRenderer->End(); endRendering();
                        return RenderPassResult::Failed("particle upload or recording failed");
                    }
                }
                m_ParticleRenderer->End();
                m_LastDrawCallCount += m_ParticleRenderer->GetDrawCallCount();
            }

            // 6.5. Jolt Physics Debug Wireframe Pass (Colliders, wheel suspension raycasts, bounds)
            if (m_PhysicsDebugRenderer && ((settings.ShowPhysicsDebug && physicsScene) || !world.DebugLines.empty())) {
                PhysicsScene* activePhysics = physicsScene;
                {
                    m_PhysicsDebugRenderer->Begin(camera.GetPosition(),
                                                 camera.GetViewProjectionMatrixZO(),
                                                 sceneTargetFormat,
                                                 m_CurrentDepthFormat,
                                                 activeSampleCount,
                                                 true,
                                                 settings.PhysicsDepthBias,
                                                 settings.PhysicsAlphaMultiplier);

                    if (settings.ShowPhysicsDebug && activePhysics) activePhysics->DrawDebug(*m_PhysicsDebugRenderer, settings.PhysicsDrawSettings);
                    for (const auto& line : world.DebugLines) m_PhysicsDebugRenderer->DrawLineSegment(line.From, line.To, line.Color);


                    if (!m_PhysicsDebugRenderer->End(Commands())) {
                        endRendering(); return RenderPassResult::Failed("physics debug upload or recording failed");
                    }
                    m_LastDrawCallCount += m_PhysicsDebugRenderer->GetDrawCallCount();
                }
            }

            // 7. End Scene Rendering Pass
            endRendering();

            // 7. Resolve Hardware MSAA if enabled
            if (useMSAA && m_MSAAColorTarget && m_MSAAColorTarget->IsValid()) {
                nri::Texture* resolveDstTexture = colorTarget;
                if (useToneMap) {
                    resolveDstTexture = m_IntermediateHDRTarget->GetNative();
                } else if (useFXAA) {
                    resolveDstTexture = m_IntermediateFXAATarget->GetNative();
                }

                if (!beginPass("Resolve")) return fail();
                Commands().ResolveTexture(resolveDstTexture, m_MSAAColorTarget->GetNative(), nri::ResolveOp::AVERAGE);
            }

            return {};
        }, [&](RenderGraph& graph) {
            if (copyParticleDepth) graph.AddPass("ParticleDepthCopy", {
                {sceneDepth,Layout::CopySource}, {particleDepthImage,Layout::CopyDest,Access::Write}});
            auto overlayUses = attachments(Load::Load);
            if (copyParticleDepth) overlayUses.push_back({particleDepthImage,Layout::ShaderResource});
            graph.AddPass("ParticlesAndPhysics", std::move(overlayUses));
            if (useMSAA) graph.AddPass("Resolve", {
                {sceneColor,Layout::ResolveSource}, {useToneMap ? hdr : fxaa,Layout::ResolveDest,Access::Write}});
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (settings.EnableParticles && m_ParticleRenderer) {
                m_ParticleRenderer->BeginPreparation();
                for (const auto& batch : world.Particles) {
                    if (!batch) return RenderPassResult::Failed("null particle snapshot");
                    auto effective=batch->MaterialResolved?batch->Material:ParticleMaterialAdapter::Resolve(batch->Config,batch->Resources);
                    auto texture=effective.UseTexture?m_MaterialTextures.Get(effective.TexturePath,true):nullptr;
                    auto normal=effective.UseNormalMap?m_MaterialTextures.Get(effective.NormalMapPath,false):nullptr;
                    if (!m_ParticleRenderer->PrepareInputs(graph,"ParticlesAndPhysics",batch->Pool,
                        texture?texture->GetSRV():nullptr,normal?normal->GetSRV():nullptr,
                        copyParticleDepth?m_ParticleSceneDepth->GetSRV():nullptr)) return fail();
                    preparedParticles.push_back({batch,std::move(effective),std::move(texture),std::move(normal)});
                }
            }
            if (m_PhysicsDebugRenderer && ((settings.ShowPhysicsDebug && physicsScene) || !world.DebugLines.empty())) {
                if (!graph.DeclareRead("ParticlesAndPhysics",m_PhysicsDebugRenderer->GetConstantBuffer().GetCBV()) ||
                    !graph.DeclareBuffer("ParticlesAndPhysics",m_PhysicsDebugRenderer->GetVertexBuffer().AcquireLease(),RHIAccess::VertexBuffer)) return fail();
            }
            return {};
        });

        addPass("DDGIUpdate", [&](const RenderPassContext&) -> RenderPassResult {
            if (useDDGI) {
                if (!beginPass("DDGICopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);
                if (!beginPass("DDGIUpdate")) return fail();
                RHIDDGIConstants ddgiConstants{};
                ddgiConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height);
                ddgiConstants.Params = glm::vec4(giFrame.EnergyMultiplier, 1.0f, 0.0f, 0.0f);
                ddgiConstants.GridDimensions = glm::vec4(glm::vec3(giFrame.GridDimensions), 0.0f);
                ddgiConstants.GridOrigin = glm::vec4(giFrame.GridOrigin, 0.0f);
                ddgiConstants.GridSpacing = glm::vec4(giFrame.GridSpacing, 0.0f);
                if (!passUniform().UpdateData(&ddgiConstants, sizeof(RHIDDGIConstants))) return fail();
                auto* ddgiTarget = viewTargets->GetDDGI();
                if (!m_DDGIDescriptorPool.UpdateTexture(m_DDGITextureSet, 0, 0, m_OpaqueSceneDepth->GetSRV()) ||
                    !m_DDGIDescriptorPool.UpdateTexture(m_DDGITextureSet, 0, 1, m_OpaqueSceneColor->GetSRV()) ||
                    !m_DDGIDescriptorPool.UpdateSampler(m_DDGITextureSet, 1, 0, m_DDGISampler.GetNative()) ||
                    !m_DDGIDescriptorPool.UpdateStorageTexture(m_DDGIStorageSet, 0, 0, ddgiTarget->GetUAV())) return fail();
                Commands().SetPipeline(m_DDGIPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_DDGIPipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                m_DDGIDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_DDGITextureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, m_DDGIStorageSet, nri::BindPoint::COMPUTE)) return fail();
                Commands().Dispatch((ddgiTarget->GetWidth() + 7) / 8, (ddgiTarget->GetHeight() + 7) / 8, 1); ++dispatchCount;
            }

            return {};
        }, [&](RenderGraph& graph) {
            if (useDDGI) {
                graph.AddPass("DDGICopy", {
                    {sceneColor,Layout::CopySource}, {ddgiInput,Layout::CopyDest,Access::Write},
                    {sceneDepth,Layout::CopySource}, {ddgiDepth,Layout::CopyDest,Access::Write}});
                graph.AddPass("DDGIUpdate", {
                    {ddgiInput,Layout::ShaderResource}, {ddgiDepth,Layout::ShaderResource},
                    {ddgiOutput,Layout::ShaderResourceStorage,Access::Write}});
            }
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useDDGI && !uniform("DDGIUpdate",sizeof(RHIDDGIConstants))) return fail();
            return {};
        });

        addPass("GTAOPass", [&](const RenderPassContext&) -> RenderPassResult {
            // 7.5. Half-resolution-style screen-space GTAO sampling. The shared
            // fullscreen pass samples a 2x depth footprint and writes the AO
            // modulation back into the scene color before tone mapping/FXAA.
            if (useGTAO) {
                const Passes::PassRecordOps ops{
                    beginPass, passUniform,
                    [&](uint32_t index, nri::DescriptorSet* set, nri::BindPoint bindPoint) {
                        return bindDescriptorSet(index, set, bindPoint);
                    }, beginRendering, endRendering};
                if (!Passes::GTAOPass::Record(*this,
                    {world, camera, settings, viewTargets, projZO, sceneTargetTexture,
                     sceneDepthTexture, sceneTargetRTV, sceneTargetFormat, width, height, dispatchCount}, ops))
                    return fail();
            }

            return {};
        }, [&](RenderGraph& graph) {
            if (useGTAO) {
                Passes::GTAOPass::Declare(graph, {sceneColor, gtaoInput, sceneDepth, gtaoDepth,
                    sceneNormal, gtaoStorage});
            }
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useGTAO && (!uniform("GTAOCompute",sizeof(RHIGTAOConstants)) ||
                !uniform("GTAOApply",sizeof(RHIGTAOConstants)))) return fail();
            return {};
        });

        addPass("ScreenSpaceReflectionPass", [&](const RenderPassContext&) -> RenderPassResult {
            // 7.6. Screen-Space Reflections with probe fallback.
            if (useSSR) {
                const Passes::PassRecordOps ops{
                    beginPass, passUniform,
                    [&](uint32_t index, nri::DescriptorSet* set, nri::BindPoint bindPoint) {
                        return bindDescriptorSet(index, set, bindPoint);
                    }, beginRendering, endRendering};
                if (!Passes::ScreenSpaceReflectionPass::Record(*this,
                    {camera, settings, viewTargets, projZO, sceneTargetTexture, sceneDepthTexture,
                     sceneTargetRTV, sceneTargetFormat, width, height, dispatchCount}, ops))
                    return fail();
            }

            return {};
        }, [&](RenderGraph& graph) {
            if (useSSR) {
                Passes::ScreenSpaceReflectionPass::Declare(graph, {sceneColor, ssrInput, sceneDepth,
                    ssrDepth, sceneNormal, ssrStorage, hizResources});
            }
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useSSR) {
                if (!uniform("SSRCompute",sizeof(RHISSRConstants)) || !uniform("SSRApply",sizeof(RHISSRConstants))) return fail();
                for (uint32_t i=0; i<hizResources.size(); ++i) if (!uniform("HiZ"+std::to_string(i),sizeof(RHIHiZConstants))) return fail();
                auto probe = m_ActiveReflectionProbePrefilter && m_ActiveReflectionProbePrefilter->IsValid()
                    ? m_ActiveReflectionProbePrefilter : RHIMaterial::GetDefaultPrefilterCubemap();
                if (!probe || !graph.DeclareRead("SSRCompute",probe->GetSRV())) return fail();
            }
            return {};
        });

        addPass("FroxelFogPass", [&](const RenderPassContext&) -> RenderPassResult {
            // 7.7. Volumetric fog and atmospheric media.
            if (useFog) {
                const Passes::PassRecordOps ops{
                    beginPass, passUniform,
                    [&](uint32_t index, nri::DescriptorSet* set, nri::BindPoint bindPoint) {
                        return bindDescriptorSet(index, set, bindPoint);
                    }, beginRendering, endRendering};
                if (!Passes::FroxelFogPass::Record(*this,
                    {world, camera, settings, projZO, sceneTargetTexture, sceneDepthTexture,
                     sceneTargetRTV, sceneTargetFormat, width, height, dispatchCount}, ops))
                    return fail();
            }

            return {};
        }, [&](RenderGraph& graph) {
            if (useFog) {
                Passes::FroxelFogPass::Declare(graph, {sceneColor, fogInput, sceneDepth, fogDepth, fogStorage});
            }
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useFog && (!uniform("FogCompute",sizeof(RHIVolumetricFogConstants)) ||
                !uniform("FogApply",sizeof(RHIVolumetricFogConstants)))) return fail();
            return {};
        });

        addPass("PostProcessPass", [&](const RenderPassContext&) -> RenderPassResult {
            std::vector<std::pair<uint32_t, uint64_t>> identities;
            auto collect = [&](const auto& queue) {
                for (const auto& item : queue) {
                    const auto entity = static_cast<uint32_t>(item.DiagnosticEntity);
                    identities.emplace_back(entity, item.Surface.Generation);
                    m_ViewContext.Diagnostics().AddMaterial(entity, item.Surface);
                    if (item.Surface.Fallback) {
                        m_ViewContext.Diagnostics().AddFallback(item.Surface.Diagnostic);
                        m_MaterialDiagnostics[entity] = item.Surface.Diagnostic;
                    }
                }
            };
            collect(opaqueQueue); collect(transparentQueue);
            std::sort(identities.begin(), identities.end());
            uint64_t materialGeneration = 1469598103934665603ull;
            for (const auto& [entity, generation] : identities) {
                materialGeneration ^= entity; materialGeneration *= 1099511628211ull;
                materialGeneration ^= generation; materialGeneration *= 1099511628211ull;
            }
            if (m_ViewContext.ObserveMaterialGeneration(materialGeneration))
                temporalSelection = m_ViewContext.Temporal().Begin(useTAA ? 2 : 0, {jitterX, jitterY});
            const Passes::PassRecordOps postOps{
                beginPass, passUniform,
                [&](uint32_t index, nri::DescriptorSet* set, nri::BindPoint bindPoint) {
                    return bindDescriptorSet(index, set, bindPoint);
                }, beginRendering, endRendering};
            if (!Passes::PostProcessPass::Record(*this,
                {settings, viewTargets, temporalSelection, viewProjZO, sceneTargetTexture,
                 sceneTargetRTV, colorTarget, colorRTV, sceneTargetFormat, width, height,
                 jitterX, jitterY, useBloom, useAutoExposure, useTAA, useToneMap, useFXAA,
                 historyRead, historyWrite, dispatchCount}, postOps)) return fail();

            return {};
        }, [&](RenderGraph& graph) {
            Passes::PostProcessPass::Declare(graph, {sceneColor, sceneDepth, output, hdr, fxaa,
                bloomInput, exposureInput, exposureOutput, taaHistoryRead, taaHistoryWrite,
                bloomResources, useBloom, useAutoExposure, useTAA, useToneMap, useFXAA});
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useBloom) {
                if (!uniform("BloomApply",sizeof(RHIBloomConstants))) return fail();
                for (uint32_t i=0; i<bloomResources.size(); ++i) if (!uniform("BloomDown"+std::to_string(i),sizeof(RHIBloomConstants))) return fail();
                for (int i=static_cast<int>(bloomResources.size())-2; i>=0; --i)
                    if (!uniform("BloomUp"+std::to_string(i),sizeof(RHIBloomConstants))) return fail();
            }
            if (useAutoExposure && !uniform("AutoExposure",sizeof(RHIExposureConstants))) return fail();
            if (useTAA) {
                // A fresh frame arena bounds memory and removes the preceding
                // frame's history dependencies before declaring current slots.
                if (!m_TAADescriptorPool.Init(m_TAADescriptorPool.GetDesc())) return fail();
                m_TAADescriptorSet = m_TAADescriptorPool.AllocateDescriptorSet(m_TAAPipelineLayout, 0);
                if (!m_TAADescriptorSet ||
                    !m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 3, 0, m_TAALinearSampler.GetNative()) ||
                    !m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 4, 0, m_TAAPointSampler.GetNative()) ||
                    !uniform("TAAResolve",sizeof(RHITAAConstants)) ||
                    !graph.DeclareRead("TAAResolve",m_TAADescriptorSet) ||
                    !graph.DeclareRead("TAAResolve",m_IntermediateHDRTarget ? m_IntermediateHDRTarget->GetSRV() : m_OpaqueSceneColor->GetSRV()) ||
                    !graph.DeclareRead("TAAResolve",m_OpaqueSceneDepth->GetSRV()) ||
                    !graph.DeclareRead("TAAResolve",historyRead->GetSRV())) return fail();
            }
            if (useToneMap && !uniform("ToneMapping",sizeof(RHIToneMappingConstants))) return fail();
            if (useToneMap && useAutoExposure &&
                !graph.DeclareRead("ToneMapping",viewTargets->GetExposure()->GetSRV())) return fail();
            if (useFXAA && (!uniform("FXAA",sizeof(RHIFXAAConstants)) ||
                !graph.DeclareRead("FXAA",RHIMaterial::GetDefaultWhiteTexture()->GetSRV()))) return fail();
            return {};
        });

        addPass("Export", [&](const RenderPassContext&) -> RenderPassResult {
            if (!beginPass("Export")) return fail();
            if (m_ActiveGPUTimestampSlot != static_cast<size_t>(-1)) {
                EndGPUTimestampPass();
                auto& slot = m_GPUTimestampSlots[m_ActiveGPUTimestampSlot];
                auto& core = m_Device->GetNRICore();
                core.CmdEndQuery(*Commands().GetNative(), *slot.Pool, 1);
                Commands().TransitionBuffer(slot.Readback.GetNative(), RHIAccess::None, RHIAccess::CopyDest);
                if (!Commands().IsRecording()) return fail();
                core.CmdCopyQueries(*Commands().GetNative(), *slot.Pool, 0, slot.QueryCount,
                    *slot.Readback.GetNative(), 0);
            }

            if (!graph.IsComplete()) return fail();
            return {};
        }, [&](RenderGraph& graph) {
            std::vector<Graph::Use> exports{{output,Layout::ShaderResource}};
            if (sceneDepthDSV) exports.push_back({sceneDepth,Layout::DepthStencilRead});
            graph.AddPass("Export", std::move(exports));
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (m_ActiveGPUTimestampSlot != static_cast<size_t>(-1) &&
                !graph.DeclareBuffer("Export",m_GPUTimestampSlots[m_ActiveGPUTimestampSlot].Readback.AcquireLease(),
                    RHIAccess::CopyDest,Access::Write)) return fail();
            return {};
        });

        RenderPassContext passContext;
        passContext.World = &world;
        passContext.Camera = &camera;
        passContext.Settings = &settings;
        passContext.View = &m_ViewContext;
        passContext.Graph = &graph;
        passContext.Commands = &Commands();
        passContext.Capabilities = capabilities;
        const auto frameResult = m_PassScheduler.Execute(passContext, true);
        if (!frameResult) {
            endRendering();
            if (diagnosticsPassActive) m_ViewContext.Diagnostics().EndPass(DiagnosticStatus::Failed);
            m_ViewContext.Diagnostics().AddError(m_PassScheduler.GetDiagnostic());
            AXLE_CORE_ERROR("View pass failed: {0}", m_PassScheduler.GetDiagnostic());
            return false;
        }
        if (diagnosticsPassActive) m_ViewContext.Diagnostics().EndPass();
        graph.EndRecording();
        if (!m_ViewContext.EndFrameAndSubmit()) {
            AXLE_CORE_ERROR("View command finalization or submit failed");
            return false;
        }
        m_ViewContext.Temporal().Commit(viewProjZO, useTAA);
        m_LocalShadows.OnSubmitted();
        m_LastCompletion = m_ViewContext.GetCompletion();
        m_LastRenderSucceeded = static_cast<bool>(m_LastCompletion);
        if (m_LastRenderSucceeded && m_ActiveGPUTimestampSlot != static_cast<size_t>(-1)) {
            auto& slot = m_GPUTimestampSlots[m_ActiveGPUTimestampSlot];
            slot.Completion = m_LastCompletion;
            slot.Pending = true;
        }
        m_ActiveGPUTimestampSlot = static_cast<size_t>(-1);
        m_ViewContext.Diagnostics().SetCounters(m_LastDrawCallCount, dispatchCount);
        return m_LastRenderSucceeded;
    }
}
