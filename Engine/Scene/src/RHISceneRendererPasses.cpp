#include "RHISceneRendererPasses.hpp"
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
            world, camera, settings, physicsScene,
            viewTargets, colorTarget, colorRTV, width,
            height, useMSAA, useToneMap, useFXAA,
            activeSampleCount, sceneTargetTexture, sceneTargetRTV, sceneDepthTexture,
            sceneDepthDSV, sceneTargetFormat, activePipeline,
            projZO, viewProjZO, jitterX,
            jitterY, giUpdateScheduled, capabilities
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
            viewTargets->GetExposure() && viewTargets->GetExposure()->IsValid() && viewTargets->GetExposure()->GetUAV() != nullptr;
        const bool useDDGI = giUpdateScheduled && world.DDGIProbeGrid.has_value() && activeSampleCount == 1 && m_DDGIPipeline &&
            viewTargets->GetDDGI() && viewTargets->GetDDGI()->IsValid() && viewTargets->GetDDGI()->GetUAV() != nullptr;
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
        const auto lighting = ResolveSceneLighting(world);
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
            if (diagnosticsPassActive) {
                m_ViewContext.Diagnostics().EndPass();
                diagnosticsPassActive = false;
            }
            activePass = name;
            if (graph.BeginPass(name, Commands())) {
                activeGraphPass = name;
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
                    skyboxConstants.Reserved[0]=glm::vec4(1,static_cast<float>(authoredScale*m_Environment.SourceExposure()),0,0);
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
                m_CascadedShadows.IsActive() ? 1u : 0u,
                m_LocalShadows.IsActive() ? 1u : 0u};
            const auto lightingFrame = m_LightingService.BuildFrame(world, settings, shadowPublication);
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

            if (world.DirectionalLight.has_value()) {
                const auto& dl = *world.DirectionalLight;
                float lux = (dl.Intensity > 100.0f) ? dl.Intensity : (dl.Intensity * PhysicalIlluminance::DirectSunlight);
                lightData.SunLight.DirectionAndIlluminance = glm::vec4(dl.Direction, lux);
                lightData.SunLight.ColorAndAmbient = glm::vec4(dl.Color, lighting.AmbientIntensity);
                lightData.SunLight.AmbientColor = glm::vec4(lighting.AmbientColor,1.0f);
                lightData.SunLight.ShadowParams = glm::vec4(dl.ShadowBias, dl.ShadowFilterRadius,
                    std::clamp(dl.SolarAngularRadius, 0.0f, 0.25f), 0.0f);
                lightData.LightCounts.z = 1;
            } else {
                lightData.SunLight.DirectionAndIlluminance = glm::vec4(glm::normalize(lighting.SunDirection),0.0f);
                lightData.SunLight.ColorAndAmbient = glm::vec4(lighting.SunColor,lighting.AmbientIntensity);
                lightData.SunLight.AmbientColor = glm::vec4(lighting.AmbientColor,1.0f);
                lightData.SunLight.ShadowParams = glm::vec4(0.0012f, 2.0f, 0.00462512f, 0.0f);
                lightData.LightCounts.z = 1;
            }

            uint32_t pointIndex = 0;
            for (const auto& dl : world.AdditionalDirectionalLights) {
                if (pointIndex >= MAX_POINT_LIGHTS) break;
                float lux = dl.Intensity > 100.0f ? dl.Intensity : dl.Intensity * PhysicalIlluminance::DirectSunlight;
                auto& light = lightData.PointLights[pointIndex++];
                light.PositionAndRadius = glm::vec4(dl.Direction, 0.0f);
                light.ColorAndIntensity = glm::vec4(dl.Color, lux * 0.000035f);
                light.Params = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            }

            for (const auto& pl : world.PointLights) {
                if (pointIndex >= MAX_POINT_LIGHTS) break;
                float intensity = (pl.Intensity > 10.0f) ? pl.Intensity : (pl.Intensity * 1000.0f);
                lightData.PointLights[pointIndex].PositionAndRadius = glm::vec4(pl.Position, pl.Radius);
                lightData.PointLights[pointIndex].ColorAndIntensity = glm::vec4(pl.Color, intensity);
                lightData.PointLights[pointIndex].Params = glm::vec4(pl.Falloff, 0.05f * 0.05f, 0.0f, 0.0f);
                pointIndex++;
            }
            lightData.LightCounts.x = pointIndex;

            uint32_t spotIndex = 0;
            for (const auto& sl : world.SpotLights) {
                if (spotIndex >= MAX_SPOT_LIGHTS) break;
                float intensity = (sl.Intensity > 10.0f) ? sl.Intensity : (sl.Intensity * 500.0f);
                float cosInner = std::cos(glm::radians(sl.InnerAngle));
                float cosOuter = std::cos(glm::radians(sl.OuterAngle));
                lightData.SpotLights[spotIndex].PositionAndRange = glm::vec4(sl.Position, sl.Range);
                lightData.SpotLights[spotIndex].DirectionAndIntensity = glm::vec4(sl.Direction, intensity);
                lightData.SpotLights[spotIndex].ColorAndInnerAngle = glm::vec4(sl.Color, cosInner);
                lightData.SpotLights[spotIndex].AngleParams = glm::vec4(cosOuter, 1.0f, 0.0f, 0.0f);
                spotIndex++;
            }
            lightData.LightCounts.y = spotIndex;

            uint32_t rectIndex = 0;
            for (const auto& rl : world.RectLights) {
                if (rectIndex >= 2) break;
                float intensity = rl.TwoSided ? -std::abs(rl.Intensity) : std::abs(rl.Intensity);
                lightData.RectLights[rectIndex].PositionAndWidth = glm::vec4(rl.Position, rl.Width);
                lightData.RectLights[rectIndex].DirectionAndHeight = glm::vec4(rl.Direction, rl.Height);
                lightData.RectLights[rectIndex].UpAndRange = glm::vec4(rl.Up, rl.Range);
                lightData.RectLights[rectIndex].ColorAndIntensity = glm::vec4(rl.Color, intensity);
                rectIndex++;
            }
            lightData.ReservedHeader[1].x = static_cast<float>(rectIndex);

            bool useIBL = m_IBLAllowed && (m_Environment.Ready() || m_PreparedManualIBL) && lighting.EnvironmentLighting;
            lightData.ReservedHeader[0].y = lighting.AmbientIntensity*(lighting.EnvironmentLighting ? m_Environment.SourceExposure() : 1.0f);
            lightData.ReservedHeader[0].z = lighting.EnvironmentLighting && !useIBL ? 1.f : 0.f;
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

            return {};
        }, [&](const RenderPassContext&) -> RenderPassResult {
            RenderPassResult probeResult;
            if (!PrepareReflectionProbe(world, camera)) {
                m_ActiveReflectionProbePrefilter.reset();
                m_HasActiveReflectionProbe = false;
                probeResult = RenderPassResult::Fallback("reflection probe preparation failed; active environment fallback");
                m_ViewContext.Diagnostics().AddFallback(probeResult.Diagnostic);
            }
            if (!m_CascadedShadows.Prepare(world, camera, settings, m_MaterialTextures))
                return RenderPassResult::Failed(m_CascadedShadows.GetDiagnostic());
            if (!m_LocalShadows.Prepare(world, camera, settings, m_MaterialTextures))
                return RenderPassResult::Failed(m_LocalShadows.GetDiagnostic());
            return probeResult;
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
            if (m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()) {
                if (!graph.DeclareRead("SurfaceNormal",m_DefaultDescriptorSet) ||
                    !graph.DeclareRead("SurfaceNormal",m_SceneConstantBuffer.GetCBV())) return fail();
                for (const auto& item : opaqueQueue) if (!declareMesh("SurfaceNormal",*item.Mesh)) return fail();
            }
            return {};
        });

        addPass("ScreenSpace", [&](const RenderPassContext&) -> RenderPassResult {
            if (!m_OpaqueSceneNormal || !m_OpaqueSceneNormal->IsValid())
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
            if (m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()) {
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

        addPass("ReflectionAndGI", [&](const RenderPassContext&) -> RenderPassResult {
            if (useDDGI) {
                if (!beginPass("DDGICopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);
                if (!beginPass("DDGIUpdate")) return fail();
                RHIDDGIConstants ddgiConstants{};
                ddgiConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height);
                ddgiConstants.Params = glm::vec4(settings.DDGIMultiplier, 1.0f, 0.0f, 0.0f);
                if (world.DDGIProbeGrid) {
                    const auto& grid = *world.DDGIProbeGrid;
                    ddgiConstants.GridDimensions = glm::vec4(glm::vec3(grid.GridDimensions), 0.0f);
                    ddgiConstants.GridOrigin = glm::vec4(grid.GridOrigin, 0.0f);
                    ddgiConstants.GridSpacing = glm::vec4(grid.GridSpacing, 0.0f);
                    ddgiConstants.Params.x *= grid.EnergyMultiplier;
                }
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

            // 7.5. Half-resolution-style screen-space GTAO sampling. The shared
            // fullscreen pass samples a 2x depth footprint and writes the AO
            // modulation back into the scene color before tone mapping/FXAA.
            if (useGTAO) {
                if (!beginPass("GTAOCopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);

                if (!beginPass("GTAOCompute")) return fail();
                RHIGTAOConstants gtaoConstants{};
                gtaoConstants.InvProjection = glm::inverse(projZO);
                gtaoConstants.View = camera.GetViewMatrix();
                auto* gtaoTarget = viewTargets->GetGTAO();
                const uint32_t gtaoWidth = gtaoTarget->GetWidth();
                const uint32_t gtaoHeight = gtaoTarget->GetHeight();
                gtaoConstants.ScreenSize = glm::vec4(static_cast<float>(gtaoWidth), static_cast<float>(gtaoHeight),
                    1.0f / static_cast<float>(gtaoWidth), 1.0f / static_cast<float>(gtaoHeight));
                gtaoConstants.GTAOParams = glm::vec4(settings.GTAORadiusPixels * 0.5f, settings.EnableGTAO ? settings.GTAOStrength : 0.0f, 0.0025f, 2.0f);
                if (world.DirectionalLight && settings.EnableContactShadows) {
                    glm::vec3 sunDir = glm::normalize(world.DirectionalLight->Direction);
                    glm::vec3 sunDirView = glm::mat3(camera.GetViewMatrix()) * sunDir;
                    gtaoConstants.SunDirView = glm::vec4(sunDirView, 1.0f);
                    gtaoConstants.ContactParams = glm::vec4(settings.ContactShadowLengthPixels, 16.0f, settings.ContactShadowStrength, 0.02f);
                } else {
                    gtaoConstants.SunDirView = glm::vec4(0.0f);
                    gtaoConstants.ContactParams = glm::vec4(0.0f);
                }
                if (!passUniform().UpdateData(&gtaoConstants, sizeof(RHIGTAOConstants))) return fail();

                m_GTAODescriptorPool.UpdateTexture(m_GTAOComputeTextureSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
                nri::Descriptor* normalSRV = (m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()) ? m_OpaqueSceneNormal->GetSRV() : RHIMaterial::GetDefaultBlackTexture()->GetSRV();
                m_GTAODescriptorPool.UpdateTexture(m_GTAOComputeTextureSet, 1, 0, normalSRV);
                m_GTAODescriptorPool.UpdateSampler(m_GTAOComputeTextureSet, 2, 0, m_GTAOSampler.GetNative());
                m_GTAODescriptorPool.UpdateStorageTexture(m_GTAOComputeStorageSet, 0, 0, gtaoTarget->GetUAV());

                Commands().SetPipeline(m_GTAOComputePipeline);
                Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_GTAOComputePipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                m_GTAODescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_GTAOComputeTextureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, m_GTAOComputeStorageSet, nri::BindPoint::COMPUTE)) return fail();
                Commands().Dispatch((gtaoWidth + 7) / 8, (gtaoHeight + 7) / 8, 1); ++dispatchCount;

                if (!beginPass("GTAOApply")) return fail();
                if (!passUniform().UpdateData(&gtaoConstants, sizeof(RHIGTAOConstants))) return fail();
                m_GTAODescriptorPool.UpdateTexture(m_GTAOApplyDescriptorSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
                m_GTAODescriptorPool.UpdateTexture(m_GTAOApplyDescriptorSet, 1, 0, gtaoTarget->GetSRV());
                m_GTAODescriptorPool.UpdateTexture(m_GTAOApplyDescriptorSet, 2, 0, m_OpaqueSceneColor->GetSRV());
                m_GTAODescriptorPool.UpdateSampler(m_GTAOApplyDescriptorSet, 3, 0, m_GTAOSampler.GetNative());

                nri::Pipeline* applyPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_GTAOApplyPipelineHDR)
                    ? m_GTAOApplyPipelineHDR : m_GTAOApplyPipelineSDR;
                if (!applyPipeline) return fail();

                nri::AttachmentDesc applyColorAttachment = {};
                applyColorAttachment.descriptor = sceneTargetRTV;
                applyColorAttachment.loadOp = nri::LoadOp::CLEAR;
                applyColorAttachment.storeOp = nri::StoreOp::STORE;
                applyColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc applyRenderingDesc = {};
                applyRenderingDesc.colors = &applyColorAttachment;
                applyRenderingDesc.colorNum = 1;

                beginRendering(applyRenderingDesc);
                nri::Viewport applyVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                Commands().SetViewport(applyVp);
                nri::Rect applyScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                Commands().SetScissor(applyScissor);

                Commands().SetPipeline(applyPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_GTAOApplyPipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                m_GTAODescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_GTAOApplyDescriptorSet)) return fail();
                Commands().Draw(3, 1, 0, 0);
                endRendering();
            }

            // 7.6. Screen-Space Reflections (SSR) with Reflection Probes Fallback
            if (useSSR) {
                if (!beginPass("SSRCopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);

                // Build the conservative min-depth pyramid before SSR traversal.
                for (uint32_t level = 0; level < 8; ++level) {
                    if (!beginPass("HiZ" + std::to_string(level))) return fail();
                    auto* source = level == 0 ? m_OpaqueSceneDepth : viewTargets->GetHiZ(level - 1);
                    auto* destination = viewTargets->GetHiZ(level);
                    if (!source || !destination || !source->IsValid() || !destination->IsValid()) return fail();
                    RHIHiZConstants hizConstants{};
                    hizConstants.SourceSize = glm::vec4(static_cast<float>(source->GetWidth()), static_cast<float>(source->GetHeight()),
                        1.0f / static_cast<float>(source->GetWidth()), 1.0f / static_cast<float>(source->GetHeight()));
                    hizConstants.DestinationSize = glm::vec4(static_cast<float>(destination->GetWidth()), static_cast<float>(destination->GetHeight()),
                        1.0f / static_cast<float>(destination->GetWidth()), 1.0f / static_cast<float>(destination->GetHeight()));
                    hizConstants.Params.x = level == 0 ? 1.0f : 0.0f;
                    hizConstants.Params.y = static_cast<float>(level);
                    if (!passUniform().UpdateData(&hizConstants, sizeof(RHIHiZConstants))) return fail();
                    auto* textureSet = m_HiZTextureSets[level];
                    auto* storageSet = m_HiZStorageSets[level];
                    if (!textureSet || !storageSet ||
                        !m_HiZDescriptorPool.UpdateTexture(textureSet, 0, 0, source->GetSRV()) ||
                        !m_HiZDescriptorPool.UpdateSampler(textureSet, 1, 0, m_HiZSampler.GetNative()) ||
                        !m_HiZDescriptorPool.UpdateStorageTexture(storageSet, 0, 0, destination->GetUAV())) return fail();
                    Commands().SetPipeline(m_HiZPipeline);
                    Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_HiZPipelineLayout);
                    Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                    m_HiZDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, textureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, storageSet, nri::BindPoint::COMPUTE)) return fail();
                    Commands().Dispatch((destination->GetWidth() + 7) / 8, (destination->GetHeight() + 7) / 8, 1); ++dispatchCount;
                }

                if (!beginPass("SSRCompute")) return fail();
                RHISSRConstants ssrConstants{};
                ssrConstants.View = camera.GetViewMatrix();
                ssrConstants.InvView = glm::inverse(camera.GetViewMatrix());
                ssrConstants.Projection = projZO;
                ssrConstants.InvProjection = glm::inverse(projZO);
                ssrConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                    1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
                ssrConstants.SSRParams = glm::vec4(settings.SSRLengthPixels, 32.0f, settings.SSRStrength, 0.5f);

                if (m_HasActiveReflectionProbe) {
                    const auto& probe = m_ActiveReflectionProbe;
                    ssrConstants.ProbeBoxMin = glm::vec4(probe.Position - probe.BoxExtents * 0.5f, probe.ParallaxCorrected ? 1.0f : 0.0f);
                    ssrConstants.ProbeBoxMax = glm::vec4(probe.Position + probe.BoxExtents * 0.5f, probe.Intensity);
                    ssrConstants.ProbePosition = glm::vec4(probe.Position, 1.0f);
                } else {
                    ssrConstants.ProbeBoxMin = glm::vec4(-10.0f, -10.0f, -10.0f, 1.0f);
                    ssrConstants.ProbeBoxMax = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
                    ssrConstants.ProbePosition = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                }

                if (!passUniform().UpdateData(&ssrConstants, sizeof(RHISSRConstants))) return fail();

                for (uint32_t level = 0; level < 8; ++level) {
                    auto* hiz = viewTargets->GetHiZ(level);
                    if (!hiz || !hiz->IsValid()) return fail();
                    m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 0, level, hiz->GetSRV());
                }
                nri::Descriptor* ssrNormalSRV = (m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()) ? m_OpaqueSceneNormal->GetSRV() : RHIMaterial::GetDefaultBlackTexture()->GetSRV();
                nri::Descriptor* probeSRV = m_ActiveReflectionProbePrefilter && m_ActiveReflectionProbePrefilter->IsValid()
                    ? m_ActiveReflectionProbePrefilter->GetSRV() : RHIMaterial::GetDefaultPrefilterCubemap()->GetSRV();
                m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 1, 0, ssrNormalSRV);
                m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 2, 0, m_OpaqueSceneColor->GetSRV());
                m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 2, 1, probeSRV);
                m_SSRDescriptorPool.UpdateSampler(m_SSRComputeTextureSet, 3, 0, m_SSRSampler.GetNative());
                m_SSRDescriptorPool.UpdateStorageTexture(m_SSRComputeStorageSet, 0, 0, m_ScreenStorageTarget->GetUAV());

                Commands().SetPipeline(m_SSRComputePipeline);
                Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_SSRComputePipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                m_SSRDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_SSRComputeTextureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, m_SSRComputeStorageSet, nri::BindPoint::COMPUTE)) return fail();
                Commands().Dispatch((width + 7) / 8, (height + 7) / 8, 1); ++dispatchCount;

                if (!beginPass("SSRApply")) return fail();
                if (!passUniform().UpdateData(&ssrConstants, sizeof(RHISSRConstants))) return fail();
                m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
                m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 1, 0, m_ScreenStorageTarget->GetSRV());
                m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 2, 0, m_OpaqueSceneColor->GetSRV());
                m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 3, 0, ssrNormalSRV);
                m_SSRDescriptorPool.UpdateSampler(m_SSRApplyDescriptorSet, 4, 0, m_SSRSampler.GetNative());

                nri::Pipeline* applyPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_SSRApplyPipelineHDR)
                    ? m_SSRApplyPipelineHDR : m_SSRApplyPipelineSDR;
                if (!applyPipeline) return fail();

                nri::AttachmentDesc applyColorAttachment = {};
                applyColorAttachment.descriptor = sceneTargetRTV;
                applyColorAttachment.loadOp = nri::LoadOp::CLEAR;
                applyColorAttachment.storeOp = nri::StoreOp::STORE;
                applyColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc applyRenderingDesc = {};
                applyRenderingDesc.colors = &applyColorAttachment;
                applyRenderingDesc.colorNum = 1;

                beginRendering(applyRenderingDesc);
                nri::Viewport applyVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                Commands().SetViewport(applyVp);
                nri::Rect applyScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                Commands().SetScissor(applyScissor);

                Commands().SetPipeline(applyPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_SSRApplyPipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                m_SSRDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_SSRApplyDescriptorSet)) return fail();
                Commands().Draw(3, 1, 0, 0);
                endRendering();
            }

            // 7.7. Volumetric Fog & Atmospheric Media Pass
            if (useFog) {
                if (!beginPass("FogCopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);

                if (!beginPass("FogCompute")) return fail();
                RHIVolumetricFogConstants fogConstants{};
                fogConstants.View = camera.GetViewMatrix();
                fogConstants.InvView = glm::inverse(camera.GetViewMatrix());
                fogConstants.Projection = projZO;
                fogConstants.InvProjection = glm::inverse(projZO);
                fogConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                    1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));

                if (world.DirectionalLight) {
                    glm::vec3 sunDir = -glm::normalize(world.DirectionalLight->Direction);
                    float sunIntensity = world.DirectionalLight->Intensity;
                    fogConstants.SunDirectionIntensity = glm::vec4(sunDir, sunIntensity);
                    fogConstants.SunColor = glm::vec4(world.DirectionalLight->Color, 1.0f);
                } else {
                    fogConstants.SunDirectionIntensity = glm::vec4(0.45f, 0.85f, 0.25f, 1.0f);
                    fogConstants.SunColor = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);
                }

                if (world.VolumetricFog && world.VolumetricFog->Enabled) {
                    const auto& vf = *world.VolumetricFog;
                    fogConstants.FogParams = glm::vec4(vf.Density, vf.HeightFalloff, vf.ScatteringAsymmetry, vf.MaxDistance);
                    fogConstants.FogColorIntensity = glm::vec4(vf.Color, vf.Intensity);
                } else {
                    fogConstants.FogParams = glm::vec4(settings.FogDensity, settings.FogHeightFalloff, settings.FogScattering, settings.FogMaxDistance);
                    fogConstants.FogColorIntensity = glm::vec4(settings.FogColor, 1.0f);
                }
                fogConstants.DDGIParams = glm::vec4(settings.DDGIMultiplier, 0.0f, 0.0f, 0.0f);

                if (!passUniform().UpdateData(&fogConstants, sizeof(RHIVolumetricFogConstants))) return fail();

                m_FogDescriptorPool.UpdateTexture(m_FogComputeTextureSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
                m_FogDescriptorPool.UpdateSampler(m_FogComputeTextureSet, 1, 0, m_FogSampler.GetNative());
                m_FogDescriptorPool.UpdateStorageTexture(m_FogComputeStorageSet, 0, 0, m_ScreenStorageTarget->GetUAV());

                Commands().SetPipeline(m_FogComputePipeline);
                Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_FogComputePipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                m_FogDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_FogComputeTextureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, m_FogComputeStorageSet, nri::BindPoint::COMPUTE)) return fail();
                Commands().Dispatch((width + 7) / 8, (height + 7) / 8, 1); ++dispatchCount;

                if (!beginPass("FogApply")) return fail();
                if (!passUniform().UpdateData(&fogConstants, sizeof(RHIVolumetricFogConstants))) return fail();
                m_FogDescriptorPool.UpdateTexture(m_FogApplyDescriptorSet, 0, 0, m_OpaqueSceneColor->GetSRV());
                m_FogDescriptorPool.UpdateTexture(m_FogApplyDescriptorSet, 1, 0, m_ScreenStorageTarget->GetSRV());
                m_FogDescriptorPool.UpdateSampler(m_FogApplyDescriptorSet, 2, 0, m_FogSampler.GetNative());

                nri::Pipeline* applyPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_FogApplyPipelineHDR)
                    ? m_FogApplyPipelineHDR : m_FogApplyPipelineSDR;
                if (!applyPipeline) return fail();

                nri::AttachmentDesc applyColorAttachment = {};
                applyColorAttachment.descriptor = sceneTargetRTV;
                applyColorAttachment.loadOp = nri::LoadOp::CLEAR;
                applyColorAttachment.storeOp = nri::StoreOp::STORE;
                applyColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc applyRenderingDesc = {};
                applyRenderingDesc.colors = &applyColorAttachment;
                applyRenderingDesc.colorNum = 1;

                beginRendering(applyRenderingDesc);
                nri::Viewport applyVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                Commands().SetViewport(applyVp);
                nri::Rect applyScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                Commands().SetScissor(applyScissor);

                Commands().SetPipeline(applyPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_FogApplyPipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                m_FogDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_FogApplyDescriptorSet)) return fail();
                Commands().Draw(3, 1, 0, 0);
                endRendering();
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
            if (useGTAO) {
                graph.AddPass("GTAOCopy", {{sceneColor,Layout::CopySource}, {gtaoInput,Layout::CopyDest,Access::Write},
                    {sceneDepth,Layout::CopySource}, {gtaoDepth,Layout::CopyDest,Access::Write}});
                graph.AddPass("GTAOCompute", {
                    {gtaoDepth, Layout::ShaderResource},
                    {sceneNormal, Layout::ShaderResource},
                    {gtaoStorage, Layout::ShaderResourceStorage, Access::Write}});
                graph.AddPass("GTAOApply", {
                    {gtaoDepth, Layout::ShaderResource},
                    {gtaoStorage, Layout::ShaderResource},
                    {gtaoInput, Layout::ShaderResource},
                    {sceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
            }
            if (useSSR) {
                graph.AddPass("SSRCopy", {{sceneColor,Layout::CopySource}, {ssrInput,Layout::CopyDest,Access::Write},
                    {sceneDepth,Layout::CopySource}, {ssrDepth,Layout::CopyDest,Access::Write}});
                for (uint32_t level = 0; level < hizResources.size(); ++level) {
                    graph.AddPass("HiZ" + std::to_string(level), {
                        {level == 0 ? ssrDepth : hizResources[level - 1], Layout::ShaderResource},
                        {hizResources[level], Layout::ShaderResourceStorage, Access::Write}});
                }
                std::vector<Graph::Use> ssrComputeUses{
                    {ssrDepth, Layout::ShaderResource},
                    {sceneNormal, Layout::ShaderResource},
                    {ssrInput, Layout::ShaderResource},
                    {ssrStorage, Layout::ShaderResourceStorage, Access::Write}};
                for (const auto& hiz : hizResources) ssrComputeUses.push_back({hiz, Layout::ShaderResource});
                graph.AddPass("SSRCompute", std::move(ssrComputeUses));
                graph.AddPass("SSRApply", {
                    {sceneNormal, Layout::ShaderResource},
                    {ssrDepth, Layout::ShaderResource},
                    {ssrStorage, Layout::ShaderResource},
                    {ssrInput, Layout::ShaderResource},
                    {sceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
            }
            if (useFog) {
                graph.AddPass("FogCopy", {{sceneColor,Layout::CopySource}, {fogInput,Layout::CopyDest,Access::Write},
                    {sceneDepth,Layout::CopySource}, {fogDepth,Layout::CopyDest,Access::Write}});
                graph.AddPass("FogCompute", {
                    {fogDepth, Layout::ShaderResource},
                    {fogStorage, Layout::ShaderResourceStorage, Access::Write}});
                graph.AddPass("FogApply", {
                    {fogInput, Layout::ShaderResource},
                    {fogStorage, Layout::ShaderResource},
                    {sceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
            }
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useDDGI && !uniform("DDGIUpdate",sizeof(RHIDDGIConstants))) return fail();
            if (useGTAO && (!uniform("GTAOCompute",sizeof(RHIGTAOConstants)) ||
                !uniform("GTAOApply",sizeof(RHIGTAOConstants)))) return fail();
            if (useSSR) {
                if (!uniform("SSRCompute",sizeof(RHISSRConstants)) || !uniform("SSRApply",sizeof(RHISSRConstants))) return fail();
                for (uint32_t i=0; i<hizResources.size(); ++i) if (!uniform("HiZ"+std::to_string(i),sizeof(RHIHiZConstants))) return fail();
                auto probe = m_ActiveReflectionProbePrefilter && m_ActiveReflectionProbePrefilter->IsValid()
                    ? m_ActiveReflectionProbePrefilter : RHIMaterial::GetDefaultPrefilterCubemap();
                if (!probe || !graph.DeclareRead("SSRCompute",probe->GetSRV())) return fail();
            }
            if (useFog && (!uniform("FogCompute",sizeof(RHIVolumetricFogConstants)) ||
                !uniform("FogApply",sizeof(RHIVolumetricFogConstants)))) return fail();
            return {};
        });

        addPass("PostAndTemporal", [&](const RenderPassContext&) -> RenderPassResult {
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
            if (useBloom && m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid()) {
                if (!beginPass("BloomCopy")) return fail();
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                uint32_t bloomPassIndex = 0;
                RHIBloomConstants lastBloomConstants{};
                auto dispatchBloom = [&](std::string_view passName, RHITexture* source, RHITexture* destination, float mode, uint32_t level) -> bool {
                    if (!source || !destination || !source->IsValid() || !destination->IsValid()) { AXLE_CORE_ERROR("Bloom pass {0}: invalid source/destination", passName); return false; }
                    if (!beginPass(passName)) return false;
                    const float dstWidth = static_cast<float>(destination->GetWidth());
                    const float dstHeight = static_cast<float>(destination->GetHeight());
                    RHIBloomConstants bloomConstants{};
                    bloomConstants.ScreenSize = glm::vec4(dstWidth, dstHeight, 1.0f / dstWidth, 1.0f / dstHeight);
                    bloomConstants.BloomParams = glm::vec4(settings.BloomThreshold, settings.BloomIntensity, settings.BloomKnee, mode);
                    bloomConstants.BloomTint = glm::vec4(settings.BloomTint, settings.BloomRadius * std::pow(1.35f, static_cast<float>(level)));
                    if (!passUniform().UpdateData(&bloomConstants, sizeof(RHIBloomConstants))) { AXLE_CORE_ERROR("Bloom pass {0}: constant update failed", passName); return false; }
                    lastBloomConstants = bloomConstants;
                    if (bloomPassIndex >= m_BloomComputeTextureSets.size()) return false;
                    auto* textureSet = m_BloomComputeTextureSets[bloomPassIndex];
                    auto* storageSet = m_BloomComputeStorageSets[bloomPassIndex++];
                    if (!m_BloomDescriptorPool.UpdateTexture(textureSet, 0, 0, source->GetSRV()) ||
                        !m_BloomDescriptorPool.UpdateStorageTexture(storageSet, 0, 0, destination->GetUAV())) { AXLE_CORE_ERROR("Bloom pass {0}: descriptor update failed", passName); return false; }
                    if (m_BloomSampler.IsValid()) m_BloomDescriptorPool.UpdateSampler(textureSet, 1, 0, m_BloomSampler.GetNative());
                    Commands().SetPipeline(m_BloomComputePipeline);
                    Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_BloomComputePipelineLayout);
                    Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                    m_BloomDescriptorPool.Bind(Commands().GetNative());
                     if (!bindDescriptorSet(0, textureSet, nri::BindPoint::COMPUTE) ||
                         !bindDescriptorSet(1, storageSet, nri::BindPoint::COMPUTE)) return false;
                    Commands().Dispatch((destination->GetWidth() + 7) / 8, (destination->GetHeight() + 7) / 8, 1); ++dispatchCount;
                    return true;
                };
                for (uint32_t level = 0; level < 6; ++level) {
                    auto* source = level == 0 ? m_OpaqueSceneColor : viewTargets->GetBloom(level - 1);
                    if (!dispatchBloom("BloomDown" + std::to_string(level), source, viewTargets->GetBloom(level), 0.0f, level)) return fail();
                }
                for (int level = 4; level >= 0; --level) {
                    if (!dispatchBloom("BloomUp" + std::to_string(level), viewTargets->GetBloom(level + 1), viewTargets->GetBloom(level), 1.0f, static_cast<uint32_t>(level))) return fail();
                }

                if (!beginPass("BloomApply")) return fail();
                if (!passUniform().UpdateData(&lastBloomConstants, sizeof(lastBloomConstants))) return fail();
                m_BloomDescriptorPool.UpdateTexture(m_BloomApplyDescriptorSet, 0, 0, viewTargets->GetBloom(0)->GetSRV());
                m_BloomDescriptorPool.UpdateTexture(m_BloomApplyDescriptorSet, 1, 0, m_OpaqueSceneColor->GetSRV());
                if (m_BloomSampler.IsValid()) {
                    m_BloomDescriptorPool.UpdateSampler(m_BloomApplyDescriptorSet, 2, 0, m_BloomSampler.GetNative());
                }

                nri::Pipeline* bloomPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_BloomApplyPipelineHDR)
                    ? m_BloomApplyPipelineHDR : m_BloomApplyPipelineSDR;
                if (!bloomPipeline) return fail();

                nri::AttachmentDesc bloomColorAttachment = {};
                bloomColorAttachment.descriptor = sceneTargetRTV;
                bloomColorAttachment.loadOp = nri::LoadOp::CLEAR;
                bloomColorAttachment.storeOp = nri::StoreOp::STORE;
                bloomColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc bloomRenderingDesc = {};
                bloomRenderingDesc.colors = &bloomColorAttachment;
                bloomRenderingDesc.colorNum = 1;

                beginRendering(bloomRenderingDesc);
                nri::Viewport bloomVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                Commands().SetViewport(bloomVp);
                nri::Rect bloomScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                Commands().SetScissor(bloomScissor);

                Commands().SetPipeline(bloomPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_BloomApplyPipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                m_BloomDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_BloomApplyDescriptorSet)) return fail();
                Commands().Draw(3, 1, 0, 0);
                endRendering();
            }

            // 7c.5. Histogram-based eye adaptation on the linear HDR scene.
            if (useAutoExposure) {
                if (!useToneMap) {
                    if (!beginPass("AutoExposureCopy")) return fail();
                    Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                }
                if (!beginPass("AutoExposure")) return fail();
                const uint32_t exposureWidth = useToneMap && m_IntermediateHDRTarget ? m_IntermediateHDRTarget->GetWidth() : width;
                const uint32_t exposureHeight = useToneMap && m_IntermediateHDRTarget ? m_IntermediateHDRTarget->GetHeight() : height;
                RHIExposureConstants exposureConstants{};
                exposureConstants.ScreenSize = glm::vec4(static_cast<float>(exposureWidth), static_cast<float>(exposureHeight),
                    1.0f / static_cast<float>(exposureWidth), 1.0f / static_cast<float>(exposureHeight));
                exposureConstants.ExposureParams = glm::vec4(-10.0f, 6.0f, settings.AutoExposureSpeed, 1.0f / 60.0f);
                exposureConstants.ExposureLimits = glm::vec4(settings.AutoExposureMin, settings.AutoExposureMax, settings.AutoExposureKeyValue, 0.0f);
                if (!passUniform().UpdateData(&exposureConstants, sizeof(RHIExposureConstants))) return fail();
                RHITexture* exposureSource = useToneMap && m_IntermediateHDRTarget ? m_IntermediateHDRTarget : m_OpaqueSceneColor;
                auto* exposureTarget = viewTargets->GetExposure();
                if (!exposureSource || !exposureTarget || !m_AutoExposureDescriptorPool.UpdateTexture(m_AutoExposureTextureSet, 0, 0, exposureSource->GetSRV()) ||
                    !m_AutoExposureDescriptorPool.UpdateSampler(m_AutoExposureTextureSet, 1, 0, m_AutoExposureSampler.GetNative()) ||
                    !m_AutoExposureDescriptorPool.UpdateStorageTexture(m_AutoExposureStorageSet, 0, 0, exposureTarget->GetUAV())) return fail();
                Commands().SetPipeline(m_AutoExposurePipeline);
                Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_AutoExposurePipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::COMPUTE);
                m_AutoExposureDescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_AutoExposureTextureSet, nri::BindPoint::COMPUTE) ||
                     !bindDescriptorSet(1, m_AutoExposureStorageSet, nri::BindPoint::COMPUTE)) return fail();
                Commands().Dispatch((exposureWidth + 15) / 16, (exposureHeight + 15) / 16, 1); ++dispatchCount;
            }

            // 7d. Temporal Anti-Aliasing (TAA) Pass
            if (useTAA && historyRead && historyWrite) {
                if (!beginPass("TAAResolve")) return fail();
                RHITAAConstants taaConstants{};
                taaConstants.InvViewProj = glm::inverse(viewProjZO);
                taaConstants.PrevViewProj = temporalSelection.HistoryValid ? m_ViewContext.Temporal().GetPreviousViewProjection() : viewProjZO;
                taaConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                                                    1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
                taaConstants.TAAParams = glm::vec4(jitterX, jitterY, settings.TAAFeedbackMin, settings.TAAFeedbackMax);
                taaConstants.Reserved[0].x = temporalSelection.HistoryValid ? 1.0f : 0.0f;
                if (!passUniform().UpdateData(&taaConstants, sizeof(RHITAAConstants))) return fail();

                nri::Descriptor* currentSRV = m_IntermediateHDRTarget ? m_IntermediateHDRTarget->GetSRV() : m_OpaqueSceneColor->GetSRV();
                nri::Descriptor* depthSRV = m_OpaqueSceneDepth->GetSRV();
                if (!m_TAADescriptorPool.UpdateTexture(m_TAADescriptorSet, 0, 0, currentSRV) ||
                    !m_TAADescriptorPool.UpdateTexture(m_TAADescriptorSet, 1, 0, depthSRV) ||
                    !m_TAADescriptorPool.UpdateTexture(m_TAADescriptorSet, 2, 0, historyRead->GetSRV())) return fail();
                if (m_TAALinearSampler.IsValid() && m_TAAPointSampler.IsValid()) {
                    m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 3, 0, m_TAALinearSampler.GetNative());
                    m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 4, 0, m_TAAPointSampler.GetNative());
                }

                nri::Pipeline* taaPipeline = (historyWrite->GetFormat() == RHI::RHITextureFormat::RGBA16_FLOAT && m_TAAPipelineHDR)
                    ? m_TAAPipelineHDR : m_TAAPipelineSDR;
                if (!taaPipeline) return fail();

                nri::AttachmentDesc taaColorAttachment = {};
                taaColorAttachment.descriptor = historyWrite->GetRTV();
                taaColorAttachment.loadOp = nri::LoadOp::CLEAR;
                taaColorAttachment.storeOp = nri::StoreOp::STORE;
                taaColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

                nri::RenderingDesc taaRenderingDesc = {};
                taaRenderingDesc.colors = &taaColorAttachment;
                taaRenderingDesc.colorNum = 1;

                beginRendering(taaRenderingDesc);
                nri::Viewport taaVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
                Commands().SetViewport(taaVp);
                nri::Rect taaScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
                Commands().SetScissor(taaScissor);

                Commands().SetPipeline(taaPipeline);
                Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_TAAPipelineLayout);
                Commands().SetRootDescriptor(0, passUniform().GetCBV(), 0, nri::BindPoint::GRAPHICS);
                m_TAADescriptorPool.Bind(Commands().GetNative());
                 if (!bindDescriptorSet(0, m_TAADescriptorSet)) return fail();
                Commands().Draw(3, 1, 0, 0);
                endRendering();

                if (!beginPass("TAACopy")) return fail();
                Commands().CopyTexture(*sceneTargetTexture, nullptr, *historyWrite->GetNative(), nullptr);
            }

            // 8. Post-Processing Tone Mapping Pass
            if (useToneMap && m_IntermediateHDRTarget && m_IntermediateHDRTarget->IsValid()) {
                if (!beginPass("ToneMapping")) return fail();
                RHIToneMappingConstants toneConstants{};
                float effectiveExposure = settings.Exposure;
                if (settings.EnableAutoExposure) {
                    const float targetExp = std::clamp(settings.Exposure, settings.AutoExposureMin, settings.AutoExposureMax);
                    const float luminance = settings.AutoExposureKeyValue / std::max(targetExp, 0.0001f);
                    effectiveExposure = m_ViewContext.PostProcess().UpdateExposure(luminance, 1.0f / 60.0f);
                } else {
                    effectiveExposure = m_ViewContext.PostProcess().UpdateExposure(1.0f, 0.0f);
                }
                toneConstants.Params = glm::vec4(effectiveExposure, settings.Gamma, static_cast<float>(settings.ToneMappingMode), settings.WhitePoint);
                toneConstants.ColorGrading = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

                nri::Texture* toneOutTexture = colorTarget;
                nri::Descriptor* toneOutRTV = colorRTV;
                if (useFXAA) {
                    toneOutTexture = m_IntermediateFXAATarget->GetNative();
                    toneOutRTV = m_IntermediateFXAATarget->GetRTV();
                }

                if (!RenderToneMapping(Commands(),
                                  m_IntermediateHDRTarget->GetSRV(),
                                  toneOutTexture,
                                  toneOutRTV,
                                  width, height,
                                  m_CurrentColorFormat,
                                  toneConstants, &passUniform())) return fail();
            }

            // 9. Post-Processing FXAA Pass
            if (useFXAA && m_IntermediateFXAATarget && m_IntermediateFXAATarget->IsValid()) {
                if (!beginPass("FXAA")) return fail();
                RHIFXAAConstants fxaaConstants{};
                fxaaConstants.ScreenSize = glm::vec4(
                    static_cast<float>(width),
                    static_cast<float>(height),
                    1.0f / static_cast<float>(width),
                    1.0f / static_cast<float>(height)
                );
                fxaaConstants.FXAAParams = glm::vec4(settings.FXAASubpixQuality, settings.FXAAEdgeThreshold, settings.FXAAEdgeThresholdMin, 0.0f);

                if (!RenderFXAA(Commands(),
                           m_IntermediateFXAATarget->GetSRV(),
                           colorTarget,
                           colorRTV,
                           width, height,
                           m_CurrentColorFormat,
                           fxaaConstants, nullptr, &passUniform())) return fail();
            }

            return {};
        }, [&](RenderGraph& graph) {
            if (useBloom) {
                graph.AddPass("BloomCopy", {{sceneColor, Layout::CopySource}, {bloomInput, Layout::CopyDest, Access::Write}});
                for (uint32_t level = 0; level < bloomResources.size(); ++level) {
                    graph.AddPass("BloomDown" + std::to_string(level), {
                        {level == 0 ? bloomInput : bloomResources[level - 1], Layout::ShaderResource},
                        {bloomResources[level], Layout::ShaderResourceStorage, Access::Write}});
                }
                for (int level = static_cast<int>(bloomResources.size()) - 2; level >= 0; --level) {
                    graph.AddPass("BloomUp" + std::to_string(level), {
                        {bloomResources[level + 1], Layout::ShaderResource},
                        {bloomResources[level], Layout::ShaderResourceStorage, Access::Write}});
                }
                graph.AddPass("BloomApply", {
                    {bloomResources[0], Layout::ShaderResource},
                    {bloomInput, Layout::ShaderResource},
                    {sceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
            }
            if (useAutoExposure) {
                if (!useToneMap) graph.AddPass("AutoExposureCopy", {
                    {sceneColor,Layout::CopySource}, {exposureInput,Layout::CopyDest,Access::Write}});
                graph.AddPass("AutoExposure", {
                    {exposureInput, Layout::ShaderResource},
                    {exposureOutput, Layout::ShaderResourceStorage, Access::Write}});
            }
            if (useTAA) {
                const auto taaSource = useToneMap ? hdr : sceneColor;
                graph.AddPass("TAAResolve", {
                    {taaSource, Layout::ShaderResource},
                    {sceneDepth, Layout::ShaderResource},
                    {taaHistoryRead, Layout::ShaderResource},
                    {taaHistoryWrite, Layout::ColorAttachment, Access::Write, Load::Clear}});
                graph.AddPass("TAACopy", {
                    {taaHistoryWrite, Layout::CopySource},
                    {taaSource, Layout::CopyDest, Access::Write}});
            }
            if (useToneMap) graph.AddPass("ToneMapping", {
                {hdr,Layout::ShaderResource}, {fxaa,Layout::ColorAttachment,Access::Write,Load::Clear}});
            if (useFXAA) graph.AddPass("FXAA", {
                {fxaa,Layout::ShaderResource}, {output,Layout::ColorAttachment,Access::Write,Load::Clear}});
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (useBloom) {
                if (!uniform("BloomApply",sizeof(RHIBloomConstants))) return fail();
                for (uint32_t i=0; i<bloomResources.size(); ++i) if (!uniform("BloomDown"+std::to_string(i),sizeof(RHIBloomConstants))) return fail();
                for (int i=static_cast<int>(bloomResources.size())-2; i>=0; --i)
                    if (!uniform("BloomUp"+std::to_string(i),sizeof(RHIBloomConstants))) return fail();
            }
            if (useAutoExposure && !uniform("AutoExposure",sizeof(RHIExposureConstants))) return fail();
            if (useTAA) {
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
            if (useFXAA && (!uniform("FXAA",sizeof(RHIFXAAConstants)) ||
                !graph.DeclareRead("FXAA",RHIMaterial::GetDefaultWhiteTexture()->GetSRV()))) return fail();
            return {};
        });

        addPass("Export", [&](const RenderPassContext&) -> RenderPassResult {
            if (!beginPass("Export")) return fail();
            if (m_GPUTimestampPool && m_GPUTimestampReadback.IsValid()) {
                auto& core = m_Device->GetNRICore();
                core.CmdEndQuery(*Commands().GetNative(), *m_GPUTimestampPool, 1);
                Commands().TransitionBuffer(m_GPUTimestampReadback.GetNative(), RHIAccess::None, RHIAccess::CopyDest);
                if (!Commands().IsRecording()) return fail();
                core.CmdCopyQueries(*Commands().GetNative(), *m_GPUTimestampPool, 0, 2, *m_GPUTimestampReadback.GetNative(), 0);
            }

            if (!graph.IsComplete()) return fail();
            return {};
        }, [&](RenderGraph& graph) {
            std::vector<Graph::Use> exports{{output,Layout::ShaderResource}};
            if (sceneDepthDSV) exports.push_back({sceneDepth,Layout::DepthStencilRead});
            graph.AddPass("Export", std::move(exports));
        }, [&](const RenderPassContext&) -> RenderPassResult {
            if (m_GPUTimestampPool && m_GPUTimestampReadback.IsValid() &&
                !graph.DeclareBuffer("Export",m_GPUTimestampReadback.AcquireLease(),RHIAccess::CopyDest,Access::Write)) return fail();
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
        m_ViewContext.Diagnostics().SetCounters(m_LastDrawCallCount, dispatchCount);
        return m_LastRenderSucceeded;
    }
}
