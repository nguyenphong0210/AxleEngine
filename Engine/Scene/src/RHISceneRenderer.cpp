#include <Axle/RHISceneRenderer.hpp>
#include "RHISceneRendererPasses.hpp"
#include "Passes/ReflectionCapturePass.hpp"
#include "Passes/PlanarReflectionPass.hpp"
#include <Axle/Path.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/SceneLighting.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/RHIPhysicsDebugRenderer.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RenderParticleData.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include "NativeForwardSource.hpp"
#include "NativeSkySource.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>
#include <bit>

namespace Axle {

    struct RHISceneRenderer::PendingCaptureJob {
        std::string ProbeKey;
        RenderReflectionProbe Probe{};
        ReflectionCaptureDomainStamps DomainStamps{};
        ReflectionCaptureContract::ReflectionCapturePolicy Policy{};
        SceneRenderSettings CaptureSettings{};
        RenderWorld FilteredWorld;
        uint32_t PrefilterMips = 1;
        uint64_t EstimatedCacheBytes = 0;
        uint32_t CompletedFaces = 0;
        std::shared_ptr<RHI::RHITexture> CandidateRawCubemap;
        std::shared_ptr<RHI::RHITexture> CandidateCubemap;
        bool ManualTrigger = false;
    };

    using namespace Axle::RHI;
    static constexpr uint32_t ForwardTextureCount=static_cast<uint32_t>(RHITextureSlot::Count)+7;

    static bool InitAreaLightLTC(RHIDevice& device, RHITexture& texture) {
        using namespace AreaLightLTCContract;
        RHITextureDesc desc;
        desc.Width = kTableDim;
        desc.Height = 2 * kTableDim;
        desc.Format = RHITextureFormat::RGBA32_FLOAT;
        desc.Usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDest;
        desc.DebugName = "AreaLightLTC";
        if (!texture.Init(desc)) return false;

        // Upload the same fitted values used by the CPU reference, stacked in row-major order.
        static_assert(sizeof(glm::vec4) == 4 * sizeof(float));
        static_assert(offsetof(LTCTables, LTC2) == kTableSize * sizeof(glm::vec4));
        const auto tables = BuildValidatedLTCTables();
        nri::TextureSubresourceUploadDesc data{};
        data.slices = &tables;
        data.sliceNum = 1;
        data.rowPitch = desc.Width * sizeof(glm::vec4);
        data.slicePitch = desc.Height * data.rowPitch;
        nri::TextureUploadDesc upload{};
        upload.texture = texture.GetNative();
        upload.subresources = &data;
        upload.planes = nri::PlaneBits::COLOR;
        upload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::FRAGMENT_SHADER};
        return device.UploadData(&upload, 1, nullptr, 0) == nri::Result::SUCCESS;
    }

    static std::filesystem::path FindShaderPath(const std::string& shaderFileName) {
        // 1. Try Asset Path resolution
        auto path = FileSystem::ResolveAssetPath("shaders/" + shaderFileName);
        if (std::filesystem::exists(path)) return path;

        path = FileSystem::ResolveAssetPath(shaderFileName);
        if (std::filesystem::exists(path)) return path;

        // 2. Try candidate root directories
        std::vector<std::filesystem::path> roots = {
            std::filesystem::current_path(),
            std::filesystem::current_path().parent_path(),
            std::filesystem::current_path().parent_path().parent_path(),
            std::filesystem::current_path().parent_path().parent_path().parent_path(),
            std::filesystem::path(".."),
            std::filesystem::path("../.."),
            std::filesystem::path("../../.."),
            FileSystem::GetAssetDirectory(),
            FileSystem::GetAssetDirectory().parent_path()
        };

        std::vector<std::filesystem::path> subdirs = {
            "Engine/Resources/Shaders",
            "Resources/Shaders",
            "shaders",
            FileSystem::GetAssetDirectory() / "shaders",
            "assets/shaders"
        };

        for (const auto& root : roots) {
            if (root.empty()) continue;
            for (const auto& sub : subdirs) {
                std::error_code ec;
                auto testPath = root / sub / shaderFileName;
                if (std::filesystem::exists(testPath, ec) && !std::filesystem::is_directory(testPath, ec)) {
                    return testPath;
                }
            }
        }

        return {};
    }

    RHISceneRenderer::RHISceneRenderer() = default;

    RHISceneRenderer::~RHISceneRenderer() {
        Shutdown();
    }

    bool RHISceneRenderer::Init(RHIDevice& device, bool enablePhysicsDebug) {
        if (m_Initialized) return true;
        if (!device.IsNativeReady()) {
            AXLE_CORE_ERROR("RHISceneRenderer::Init failed: RHIDevice is not initialized!");
            return false;
        }

        m_Device = &device;

        // Initialize mesh registry
        if (!RHIMeshRegistry::Get().Init()) return false;

        if (!m_ViewContext.Init() || !m_CaptureViewContext.Init() || !m_PlanarViewContext.Init() ||
            !m_CascadedShadows.Init() || !m_LocalShadows.Init()) return false;

        if (device.GetCapabilities().TimestampFrequencyHz && device.GetNRICore().CreateQueryPool) {
            nri::QueryPoolDesc timestampDesc{};
            timestampDesc.queryType = nri::QueryType::TIMESTAMP;
            timestampDesc.capacity = MaxGPUTimestampQueries;
            bool ready = true;
            for (size_t index = 0; index < m_GPUTimestampSlots.size(); ++index) {
                auto& slot = m_GPUTimestampSlots[index];
                RHIBufferDesc readbackDesc{};
                readbackDesc.Size = MaxGPUTimestampQueries * sizeof(uint64_t);
                readbackDesc.MemoryLocation = RHIMemoryLocation::HostReadback;
                readbackDesc.DebugName = "RendererGPUTimestampReadback" + std::to_string(index);
                if (ScopedRHIFault::Invoke("CreateRendererTimestampPool", [&] {
                        return device.GetNRICore().CreateQueryPool(*device.GetNRIDevice(), timestampDesc, slot.Pool);
                    }) != nri::Result::SUCCESS ||
                    !slot.Pool || !slot.Readback.Init(readbackDesc)) { ready = false; break; }
            }
            if (!ready) {
                for (auto& slot : m_GPUTimestampSlots) {
                    slot.Readback.Shutdown();
                    if (slot.Pool) device.GetNRICore().DestroyQueryPool(slot.Pool);
                    slot.Pool = nullptr;
                }
                AXLE_CORE_WARN("RHISceneRenderer: GPU timing is unavailable; scene rendering continues without timestamp queries.");
            }
        }

        // Compile Forward Shaders
        if (!CompileShaders(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile forward scene shaders!");
            return false;
        }

        // Create Pipeline Layout
        if (!CreatePipelineLayout(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to create pipeline layout!");
            return false;
        }

        // Initialize 16x Anisotropic Trilinear Sampler
        if (!m_AnisoSampler.Init(RHISamplerDesc::Anisotropic16x())) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize 16x Anisotropic Sampler, using defaults.");
            return false;
        }

        // Per-draw resource descriptors; the immutable sampler uses no heap slots.
        // Reserve the fixed PBR-default, occlusion, fog ring/fallback and sky sets in addition to 4096 draws.
        constexpr uint32_t materialSets=4096+16;
        auto materialPoolDesc = RHIDescriptorPoolDesc::Tier2Bindless(materialSets*ForwardTextureCount + 16u, materialSets);
        materialPoolDesc.MaxSamplers = 0;
        if (!m_DescriptorPool.Init(materialPoolDesc)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to initialize material descriptors!");
            return false;
        } else {
            // Allocate Default PBR Material Descriptor Set (Space 1)
            m_DefaultDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 0);
            if (!m_DefaultDescriptorSet) return false;
            {
                // Bind Fallback GPU resident textures to Range 0 (t0..t8: PBR material + IBL cubemaps + BRDF LUT)
                auto whiteTex = RHIMaterial::GetDefaultWhiteTexture();
                auto normalTex = RHIMaterial::GetDefaultFlatNormalTexture();
                auto roughTex = RHIMaterial::GetDefaultRoughnessTexture();
                auto blackTex = RHIMaterial::GetDefaultBlackTexture();
                auto irradianceTex = m_IrradianceMap ? m_IrradianceMap : RHIMaterial::GetDefaultIrradianceCubemap();
                auto prefilterTex = m_PrefilterMap ? m_PrefilterMap : RHIMaterial::GetDefaultPrefilterCubemap();
                auto brdfLUTTex = m_BRDFLUT ? m_BRDFLUT : RHIMaterial::GetDefaultBRDFLUT();

                if (!whiteTex || !normalTex || !roughTex || !blackTex || !irradianceTex || !prefilterTex || !brdfLUTTex) return false;
                {
                    std::vector<nri::Descriptor*> defaultSRVs = {
                        whiteTex->GetSRV(),       // t0: Albedo (1x1 White)
                        normalTex->GetSRV(),      // t1: Normal (1x1 Flat Normal)
                        roughTex->GetSRV(),       // t2: Roughness (1x1 0.5 Rough)
                        blackTex->GetSRV(),       // t3: Metallic (1x1 Black)
                        whiteTex->GetSRV(),       // t4: AO (1x1 White)
                        blackTex->GetSRV(),       // t5: Emissive (1x1 Black)
                        irradianceTex->GetSRV(),  // t6: Irradiance Cubemap
                        prefilterTex->GetSRV(),   // t7: Specular Prefilter Cubemap
                        brdfLUTTex->GetSRV()      // t8: BRDF Integration 2D LUT
                    };
                    for (uint32_t slot=6;slot<static_cast<uint32_t>(RHITextureSlot::Count);++slot) {
                        auto fallback = RHIMaterial::GetFallbackTexture(static_cast<RHITextureSlot>(slot));
                        if (!fallback || !fallback->IsValid()) return false;
                        defaultSRVs.push_back(fallback->GetSRV());
                    }
                    defaultSRVs.push_back(blackTex->GetSRV());
                    defaultSRVs.push_back(whiteTex->GetSRV());
                    defaultSRVs.push_back(m_CascadedShadows.GetSRV());
                    defaultSRVs.push_back(m_LocalShadows.GetSRV());
                    if (!m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 0, defaultSRVs)) return false;
                }
            }
        }

        m_ForwardOcclusionDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 1);
        m_DDGICaptureOcclusionSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 1);
        auto neutralOcclusion = RHIMaterial::GetDefaultWhiteTexture();
        auto defaultProbePrefilter = RHIMaterial::GetDefaultPrefilterCubemap();
        if (!m_ForwardOcclusionDescriptorSet || !m_DDGICaptureOcclusionSet ||
            !neutralOcclusion || !defaultProbePrefilter ||
            !InitAreaLightLTC(device, m_AreaLightLTC) ||
            !m_DescriptorPool.UpdateTexture(m_ForwardOcclusionDescriptorSet, 2, 0, m_AreaLightLTC.GetSRV()) ||
            !m_DescriptorPool.UpdateTextureRange(m_ForwardOcclusionDescriptorSet, 0, 0,
                {neutralOcclusion->GetSRV(), neutralOcclusion->GetSRV()}) ||
            !m_DescriptorPool.UpdateTextureRange(m_ForwardOcclusionDescriptorSet, 1, 0,
                {defaultProbePrefilter->GetSRV(), defaultProbePrefilter->GetSRV(),
                 defaultProbePrefilter->GetSRV(), defaultProbePrefilter->GetSRV()}) ||
            !m_DescriptorPool.UpdateTextureRange(m_ForwardOcclusionDescriptorSet, 3, 0,
                {neutralOcclusion->GetSRV(), neutralOcclusion->GetSRV()}) ||
            !m_DescriptorPool.UpdateTexture(m_DDGICaptureOcclusionSet, 2, 0, m_AreaLightLTC.GetSRV()) ||
            !m_DescriptorPool.UpdateTextureRange(m_DDGICaptureOcclusionSet, 0, 0,
                {neutralOcclusion->GetSRV(), neutralOcclusion->GetSRV()}) ||
            !m_DescriptorPool.UpdateTextureRange(m_DDGICaptureOcclusionSet, 1, 0,
                {defaultProbePrefilter->GetSRV(), defaultProbePrefilter->GetSRV(),
                 defaultProbePrefilter->GetSRV(), defaultProbePrefilter->GetSRV()}) ||
            !m_DescriptorPool.UpdateTextureRange(m_DDGICaptureOcclusionSet, 3, 0,
                {neutralOcclusion->GetSRV(), neutralOcclusion->GetSRV()})) return false;

        if (!m_ReflectionProbeBuffer.Init(RHIBufferDesc::Constant(
                sizeof(RHIReflectionProbeBufferData), RHIMemoryLocation::HostUpload, "ReflectionProbeConstantsBuffer"))) {
            return false;
        }
        {
            const RHIReflectionProbeBufferData emptyProbes{};
            if (!m_ReflectionProbeBuffer.UpdateData(&emptyProbes, sizeof(emptyProbes))) return false;
        }

        m_ForwardFogFallbackDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 2);
        auto emptyFroxel = RHIMaterial::GetDefaultEmptyFroxelVolumeTexture();
        if (!m_ForwardFogFallbackDescriptorSet || !emptyFroxel || !emptyFroxel->GetSRV() ||
            !m_DescriptorPool.UpdateTexture(m_ForwardFogFallbackDescriptorSet, 0, 0, emptyFroxel->GetSRV()) ||
            !m_DescriptorPool.UpdateTexture(m_ForwardFogFallbackDescriptorSet, 1, 0, emptyFroxel->GetSRV())) {
            return false;
        }
        m_ForwardFogSets.clear();
        for (int slot = 0; slot < 4; ++slot) {
            auto* set = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 2);
            if (!set ||
                !m_DescriptorPool.UpdateTexture(set, 0, 0, emptyFroxel->GetSRV()) ||
                !m_DescriptorPool.UpdateTexture(set, 1, 0, emptyFroxel->GetSRV())) {
                return false;
            }
            m_ForwardFogSets.push_back(set);
        }
        if (!m_DisabledFogConstantBuffer.Init(RHIBufferDesc::Constant(
                sizeof(RHIVolumetricFogConstants), RHIMemoryLocation::HostUpload, "DisabledFogConstantsBuffer"))) {
            return false;
        }
        {
            const RHIVolumetricFogConstants disabledFog{};
            if (!m_DisabledFogConstantBuffer.UpdateData(&disabledFog, sizeof(disabledFog))) return false;
        }
        if (!m_DisabledDDGIConstantBuffer.Init(RHIBufferDesc::Constant(
                sizeof(RHIDDGIConstants), RHIMemoryLocation::HostUpload, "DisabledDDGIConstantsBuffer"))) {
            return false;
        }
        {
            const RHIDDGIConstants disabledDDGI{};
            if (!m_DisabledDDGIConstantBuffer.UpdateData(&disabledDDGI, sizeof(disabledDDGI))) return false;
        }

        RHIDescriptorPoolDesc customPool;
        customPool.MaxDescriptorSets=4096; customPool.MaxConstantBuffers=16384;
        customPool.MaxTextures=16384; customPool.MaxSamplers=1024;
        if (!m_CustomDescriptorPool.Init(customPool) || !m_CustomSampler.Init(RHISamplerDesc::Anisotropic16x()) ||
            !m_CustomShadowSampler.Init(RHISamplerDesc::ShadowPCF())) return false;
        for(auto policy:{ShaderSamplerPolicy::LinearRepeat,ShaderSamplerPolicy::LinearClamp,ShaderSamplerPolicy::PointRepeat,ShaderSamplerPolicy::PointClamp})
            if(!m_CustomPolicySamplers[policy].Init(*MaterialSamplerDescription(policy)))return false;

        // Create Graphics Pipeline
        if (!CreateGraphicsPipeline(device)) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to create graphics pipeline!");
            return false;
        }

        // Initialize Scene Constants and Forward+ Multi-Light Constant Buffers
        if (!m_SceneConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISceneConstants), RHIMemoryLocation::HostUpload, "SceneConstantsBuffer"))) return false;
        if (!m_LightBuffer.Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Forward+ Multi-Light GPU Buffer!");
            return false;
        }

        // Initialize Atmospheric Skybox Pipeline & Constant Buffer
        if (!CompileSkyboxShaders(device) || !CreateSkyboxPipelineLayout(device)) return false;
        {
            if (!CreateSkyboxPipeline(device)) {
                AXLE_CORE_WARN("RHISceneRenderer: Failed to create Atmospheric Skybox graphics pipeline!");
                return false;
            }
        }
        m_SkyboxDescriptorSet=m_DescriptorPool.AllocateDescriptorSet(m_SkyboxPipelineLayout,0);
        if (!m_SkyboxDescriptorSet || !m_DescriptorPool.UpdateTexture(m_SkyboxDescriptorSet,0,0,RHIMaterial::GetDefaultIrradianceCubemap()->GetSRV())) return false;
        if (!m_SkyboxConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISkyboxConstants), RHIMemoryLocation::HostUpload, "SkyboxConstantsBuffer"))) return false;

        // Initialize Film-Grade Tone Mapping Post-Processing Pipeline & Resources
        if (!CompileToneMappingShaders(device) || !CreateToneMappingPipelineLayout(device)) return false;
        {
            if (!CreateToneMappingPipeline(device)) {
                AXLE_CORE_WARN("RHISceneRenderer: Failed to create Tone Mapping graphics pipeline!");
                return false;
            }
        }

        if (!m_ToneMappingSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Bilinear Clamp Sampler for Tone Mapping.");
            return false;
        }

        if (!m_ToneMappingDescriptorPool.Init(RHIDescriptorPoolDesc::Tier2Bindless(64, 16))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Tone Mapping Descriptor Pool!");
            return false;
        } else if (m_ToneMappingPipelineLayout.IsValid()) {
            m_ToneMappingDescriptorSet = m_ToneMappingDescriptorPool.AllocateDescriptorSet(m_ToneMappingPipelineLayout, 0);
            if (!m_ToneMappingDescriptorSet) return false;
            if (m_ToneMappingSampler.IsValid()) {
                if (!m_ToneMappingDescriptorPool.UpdateSampler(m_ToneMappingDescriptorSet, 1, 0, m_ToneMappingSampler.GetNative())) return false;
            }
        }

        if (!m_ToneMappingConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIToneMappingConstants), RHIMemoryLocation::HostUpload, "ToneMappingConstantsBuffer"))) return false;

        // Initialize Fast Approximate Anti-Aliasing (FXAA 3.11 Quality) Pipeline & Resources
        if (!CompileFXAAShaders(device) || !CreateFXAAPipelineLayout(device)) return false;
        {
            if (!CreateFXAAPipeline(device)) {
                AXLE_CORE_WARN("RHISceneRenderer: Failed to create FXAA graphics pipeline!");
                return false;
            }
        }

        if (!m_FXAASampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Bilinear Clamp Sampler for FXAA.");
            return false;
        }

        if (!m_FXAADescriptorPool.Init(RHIDescriptorPoolDesc::Tier2Bindless(64, 16))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize FXAA Descriptor Pool!");
            return false;
        } else if (m_FXAAPipelineLayout.IsValid()) {
            m_FXAADescriptorSet = m_FXAADescriptorPool.AllocateDescriptorSet(m_FXAAPipelineLayout, 0);
            if (!m_FXAADescriptorSet) return false;
            if (m_FXAASampler.IsValid()) {
                if (!m_FXAADescriptorPool.UpdateSampler(m_FXAADescriptorSet, 2, 0, m_FXAASampler.GetNative())) return false;
            }
        }

        if (!m_FXAAConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIFXAAConstants), RHIMemoryLocation::HostUpload, "FXAAConstantsBuffer"))) return false;

        // Initialize Ground Truth Ambient Occlusion (GTAO) & Contact Shadows Pipeline & Resources
        if (!CompileGTAOShaders(device) || !CreateGTAOPipelineLayout(device) || !CreateGTAOPipeline(device)) return false;

        if (!m_GTAOSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Linear Clamp Sampler for GTAO.");
            return false;
        }

        RHIDescriptorPoolDesc gtaoPoolDesc;
        gtaoPoolDesc.MaxDescriptorSets = 16;
        gtaoPoolDesc.MaxTextures = 64;
        gtaoPoolDesc.MaxSamplers = 16;
        gtaoPoolDesc.MaxStorageTextures = 16;
        gtaoPoolDesc.MaxConstantBuffers = 16;
        if (!m_GTAODescriptorPool.Init(gtaoPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize GTAO Descriptor Pool!");
            return false;
        } else {
            m_GTAOComputeTextureSet = m_GTAODescriptorPool.AllocateDescriptorSet(m_GTAOComputePipelineLayout, 0);
            m_GTAOComputeStorageSet = m_GTAODescriptorPool.AllocateDescriptorSet(m_GTAOComputePipelineLayout, 1);
            m_GTAOUpsampleTextureSet = m_GTAODescriptorPool.AllocateDescriptorSet(m_GTAOUpsamplePipelineLayout, 0);
            m_GTAOUpsampleStorageSet = m_GTAODescriptorPool.AllocateDescriptorSet(m_GTAOUpsamplePipelineLayout, 1);
            m_GTAOApplyDescriptorSet = m_GTAODescriptorPool.AllocateDescriptorSet(m_GTAOApplyPipelineLayout, 0);
            if (!m_GTAOComputeTextureSet || !m_GTAOComputeStorageSet ||
                !m_GTAOUpsampleTextureSet || !m_GTAOUpsampleStorageSet || !m_GTAOApplyDescriptorSet) return false;
            if (m_GTAOSampler.IsValid()) {
                if (!m_GTAODescriptorPool.UpdateSampler(m_GTAOComputeTextureSet, 2, 0, m_GTAOSampler.GetNative()) ||
                    !m_GTAODescriptorPool.UpdateSampler(m_GTAOUpsampleTextureSet, 3, 0, m_GTAOSampler.GetNative()) ||
                    !m_GTAODescriptorPool.UpdateSampler(m_GTAOApplyDescriptorSet, 3, 0, m_GTAOSampler.GetNative())) return false;
            }
        }

        if (!m_GTAOConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIGTAOConstants), RHIMemoryLocation::HostUpload, "GTAOConstantsBuffer"))) return false;

        // Initialize Screen Space Reflections (SSR) & Reflection Probes Pipeline & Resources
        if (!CompileSSRShaders(device) || !CreateSSRPipelineLayout(device) || !CreateSSRPipeline(device)) return false;

        if (!m_SSRSampler.Init(RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Trilinear Clamp Sampler for SSR.");
            return false;
        }

        RHIDescriptorPoolDesc ssrPoolDesc;
        ssrPoolDesc.MaxDescriptorSets = 28;
        ssrPoolDesc.MaxTextures = 80;
        ssrPoolDesc.MaxSamplers = 24;
        ssrPoolDesc.MaxStorageTextures = 24;
        ssrPoolDesc.MaxConstantBuffers = 16;
        if (!m_SSRDescriptorPool.Init(ssrPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize SSR Descriptor Pool!");
            return false;
        } else {
            m_SSRComputeTextureSet = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRComputePipelineLayout, 0);
            m_SSRComputeStorageSet = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRComputePipelineLayout, 1);
            m_SSRApplyDescriptorSet = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRApplyPipelineLayout, 0);
            if (!m_SSRComputeTextureSet || !m_SSRComputeStorageSet || !m_SSRApplyDescriptorSet) return false;
            for (uint32_t level = 0; level < m_SSRDownsampleTextureSets.size(); ++level) {
                m_SSRDownsampleTextureSets[level] = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRDownsamplePipelineLayout, 0);
                m_SSRDownsampleStorageSets[level] = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRDownsamplePipelineLayout, 1);
                if (!m_SSRDownsampleTextureSets[level] || !m_SSRDownsampleStorageSets[level]) return false;
            }
            if (m_SSRSampler.IsValid()) {
                if (!m_SSRDescriptorPool.UpdateSampler(m_SSRComputeTextureSet, 3, 0, m_SSRSampler.GetNative()) ||
                    !m_SSRDescriptorPool.UpdateSampler(m_SSRApplyDescriptorSet, 9, 0, m_SSRSampler.GetNative())) return false;
                for (uint32_t level = 0; level < m_SSRDownsampleTextureSets.size(); ++level) {
                    if (!m_SSRDescriptorPool.UpdateSampler(m_SSRDownsampleTextureSets[level], 1, 0, m_SSRSampler.GetNative())) return false;
                }
            }
        }

        if (!m_SSRConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISSRConstants), RHIMemoryLocation::HostUpload, "SSRConstantsBuffer"))) return false;

        // Initialize Planar Reflection Unified Resolve Pipeline & Resources (M15-U5-46)
        if (!CompilePlanarShaders(device) || !CreatePlanarPipelineLayout(device) || !CreatePlanarPipeline(device)) {
            return false;
        }
        if (!m_PlanarSampler.Init(RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Clamp Sampler for Planar Reflection.");
            return false;
        }
        RHIDescriptorPoolDesc planarPoolDesc;
        planarPoolDesc.MaxDescriptorSets = 4;
        planarPoolDesc.MaxTextures = 16;
        planarPoolDesc.MaxSamplers = 4;
        if (!m_PlanarDescriptorPool.Init(planarPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Planar Reflection Descriptor Pool.");
            return false;
        }
        m_PlanarApplyDescriptorSet = m_PlanarDescriptorPool.AllocateDescriptorSet(m_PlanarApplyPipelineLayout, 0);
        if (!m_PlanarApplyDescriptorSet) {
            return false;
        }
        if (!m_PlanarDescriptorPool.UpdateSampler(m_PlanarApplyDescriptorSet, 6, 0, m_PlanarSampler.GetNative())) {
            return false;
        }

        // Initialize the independently allocated Hi-Z reduction levels used by
        // SSR. Keeping each level as its own storage texture avoids backend
        // specific multi-mip UAV view restrictions.
        if (!CompileHiZShaders(device) || !CreateHiZPipelineLayout(device) || !CreateHiZPipeline(device)) return false;
        if (!m_HiZSampler.Init(RHISamplerDesc::Point(RHIAddressMode::ClampToEdge))) return false;
        RHIDescriptorPoolDesc hizPoolDesc;
        hizPoolDesc.MaxDescriptorSets = 36;
        hizPoolDesc.MaxTextures = 36;
        hizPoolDesc.MaxSamplers = 36;
        hizPoolDesc.MaxStorageTextures = 36;
        hizPoolDesc.MaxConstantBuffers = 36;
        if (!m_HiZDescriptorPool.Init(hizPoolDesc)) return false;
        for (size_t level = 0; level < m_HiZTextureSets.size(); ++level) {
            m_HiZTextureSets[level] = m_HiZDescriptorPool.AllocateDescriptorSet(m_HiZPipelineLayout, 0);
            m_HiZStorageSets[level] = m_HiZDescriptorPool.AllocateDescriptorSet(m_HiZPipelineLayout, 1);
            if (!m_HiZTextureSets[level] || !m_HiZStorageSets[level]) return false;
            if (!m_HiZDescriptorPool.UpdateSampler(m_HiZTextureSets[level], 1, 0, m_HiZSampler.GetNative())) return false;
        }
        m_HiZVisualizeTextureSet = m_HiZDescriptorPool.AllocateDescriptorSet(m_HiZVisualizePipelineLayout, 0);
        if (!m_HiZVisualizeTextureSet) return false;
        if (!m_HiZConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIHiZConstants), RHIMemoryLocation::HostUpload, "HiZConstantsBuffer"))) return false;

        if (!CompileCubemapCaptureShaders(device) || !CreateCubemapCapturePipelineLayout(device) || !CreateCubemapCapturePipeline(device)) {
            return false;
        }
        RHIDescriptorPoolDesc capturePoolDesc;
        capturePoolDesc.MaxDescriptorSets = 4;
        capturePoolDesc.MaxTextures = 4;
        capturePoolDesc.MaxStorageTextures = 4;
        capturePoolDesc.MaxConstantBuffers = 4;
        if (!m_CaptureOrientationDescriptorPool.Init(capturePoolDesc)) return false;
        m_CaptureOrientationTextureSet = m_CaptureOrientationDescriptorPool.AllocateDescriptorSet(m_CaptureOrientationPipelineLayout, 0);
        m_CaptureOrientationStorageSet = m_CaptureOrientationDescriptorPool.AllocateDescriptorSet(m_CaptureOrientationPipelineLayout, 1);
        if (!m_CaptureOrientationTextureSet || !m_CaptureOrientationStorageSet) return false;
        m_CaptureTargets = std::make_unique<Passes::ReflectionCaptureTargets>();

        if (!CompileCubemapPrefilterShaders(device) || !CreateCubemapPrefilterPipelineLayout(device) || !CreateCubemapPrefilterPipelines(device)) {
            return false;
        }
        if (!m_CapturePrefilterSampler.Init(RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge))) {
            return false;
        }
        RHIDescriptorPoolDesc prefilterPoolDesc;
        prefilterPoolDesc.MaxDescriptorSets = ReflectionCaptureContract::MaxPrefilterMipCount * 4u;
        prefilterPoolDesc.MaxTextures = ReflectionCaptureContract::MaxPrefilterMipCount * 4u;
        prefilterPoolDesc.MaxSamplers = ReflectionCaptureContract::MaxPrefilterMipCount * 4u;
        prefilterPoolDesc.MaxStorageTextures = ReflectionCaptureContract::MaxPrefilterMipCount * 4u;
        prefilterPoolDesc.MaxConstantBuffers = 0;
        if (!m_CapturePrefilterDescriptorPool.Init(prefilterPoolDesc)) return false;
        for (uint32_t mip = 0; mip < ReflectionCaptureContract::MaxPrefilterMipCount; ++mip) {
            m_CaptureDownsampleTextureSets[mip] = m_CapturePrefilterDescriptorPool.AllocateDescriptorSet(m_CapturePrefilterPipelineLayout, 0);
            m_CaptureDownsampleStorageSets[mip] = m_CapturePrefilterDescriptorPool.AllocateDescriptorSet(m_CapturePrefilterPipelineLayout, 1);
            m_CapturePrefilterTextureSets[mip] = m_CapturePrefilterDescriptorPool.AllocateDescriptorSet(m_CapturePrefilterPipelineLayout, 0);
            m_CapturePrefilterStorageSets[mip] = m_CapturePrefilterDescriptorPool.AllocateDescriptorSet(m_CapturePrefilterPipelineLayout, 1);
            if (!m_CaptureDownsampleTextureSets[mip] || !m_CaptureDownsampleStorageSets[mip] ||
                !m_CapturePrefilterTextureSets[mip] || !m_CapturePrefilterStorageSets[mip]) {
                return false;
            }
            if (!m_CapturePrefilterDescriptorPool.UpdateSampler(m_CaptureDownsampleTextureSets[mip], 1, 0, m_CapturePrefilterSampler.GetNative()) ||
                !m_CapturePrefilterDescriptorPool.UpdateSampler(m_CapturePrefilterTextureSets[mip], 1, 0, m_CapturePrefilterSampler.GetNative())) {
                return false;
            }
        }
        for (size_t i = 0; i < m_CapturePrefilterConstantBuffers.size(); ++i) {
            if (!m_CapturePrefilterConstantBuffers[i].Init(
                    RHIBufferDesc::Constant(sizeof(RHICubemapPrefilterConstants), RHIMemoryLocation::HostUpload,
                                            "CubemapPrefilterConstants_" + std::to_string(i)))) {
                return false;
            }
        }

        // Initialize Volumetric Fog & Atmospheric Participating Media Resources
        if (m_CascadedShadows.IsSupported() && m_LocalShadows.IsSupported()) {
            ScopedRHIOperation operation("FogShadowSampler");
            if (!m_FogShadowSampler.Init(RHISamplerDesc::ShadowPCF())) {
                AXLE_CORE_WARN("RHISceneRenderer: Froxel fog shadow sampling is unavailable");
            }
        }
        if (!CompileFogShaders(device) || !CreateFogPipelineLayout(device) || !CreateFogPipeline(device) ||
            !m_FogSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Froxel fog GPU resources are unavailable");
        }


        // Initialize Physically Based Pyramid / Kawase Bloom Resources
        if (!CompileBloomShaders(device) || !CreateBloomPipelineLayout(device) || !CreateBloomPipeline(device)) return false;

        if (!m_BloomSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Linear Clamp Sampler for Bloom.");
            return false;
        }

        RHIDescriptorPoolDesc bloomPoolDesc;
        bloomPoolDesc.MaxDescriptorSets = 40;
        bloomPoolDesc.MaxTextures = 64;
        bloomPoolDesc.MaxSamplers = 32;
        bloomPoolDesc.MaxStorageTextures = 16;
        bloomPoolDesc.MaxConstantBuffers = 16;
        if (!m_BloomDescriptorPool.Init(bloomPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Bloom Descriptor Pool!");
            return false;
        } else {
            m_BloomComputeTextureSet = m_BloomDescriptorPool.AllocateDescriptorSet(m_BloomComputePipelineLayout, 0);
            m_BloomComputeStorageSet = m_BloomDescriptorPool.AllocateDescriptorSet(m_BloomComputePipelineLayout, 1);
            m_BloomApplyDescriptorSet = m_BloomDescriptorPool.AllocateDescriptorSet(m_BloomApplyPipelineLayout, 0);
            if (!m_BloomComputeTextureSet || !m_BloomComputeStorageSet || !m_BloomApplyDescriptorSet) return false;
            for (size_t i = 0; i < m_BloomComputeTextureSets.size(); ++i) {
                m_BloomComputeTextureSets[i] = m_BloomDescriptorPool.AllocateDescriptorSet(m_BloomComputePipelineLayout, 0);
                m_BloomComputeStorageSets[i] = m_BloomDescriptorPool.AllocateDescriptorSet(m_BloomComputePipelineLayout, 1);
                if (!m_BloomComputeTextureSets[i] || !m_BloomComputeStorageSets[i]) return false;
            }
            for (size_t i = 0; i < m_BloomUpsampleDescriptorSets.size(); ++i) {
                m_BloomUpsampleDescriptorSets[i] = m_BloomDescriptorPool.AllocateDescriptorSet(m_BloomApplyPipelineLayout, 0);
                if (!m_BloomUpsampleDescriptorSets[i]) return false;
            }
            if (m_BloomSampler.IsValid()) {
                if (!m_BloomDescriptorPool.UpdateSampler(m_BloomComputeTextureSet, 1, 0, m_BloomSampler.GetNative()) ||
                    !m_BloomDescriptorPool.UpdateSampler(m_BloomApplyDescriptorSet, 2, 0, m_BloomSampler.GetNative())) return false;
                for (auto* set : m_BloomComputeTextureSets)
                    if (!m_BloomDescriptorPool.UpdateSampler(set, 1, 0, m_BloomSampler.GetNative())) return false;
                for (auto* set : m_BloomUpsampleDescriptorSets)
                    if (!m_BloomDescriptorPool.UpdateSampler(set, 2, 0, m_BloomSampler.GetNative())) return false;
            }
        }

        if (!m_BloomConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIBloomConstants), RHIMemoryLocation::HostUpload, "BloomConstantsBuffer"))) return false;

        // Initialize histogram-based auto exposure. The 256-bin R32_UINT histogram
        // captures every viewport pixel; the reduced 1x1 target feeds ToneMapping.
        if (!CompileAutoExposureShaders(device) || !CreateAutoExposurePipelineLayout(device) || !CreateAutoExposurePipeline(device)) return false;
        if (!m_AutoExposureSampler.Init(RHISamplerDesc::Point(RHIAddressMode::ClampToEdge))) return false;
        if (!m_AutoExposureHistogram.Init(RHITextureDesc::StorageTexture2D(
                AutoExposureContract::HistogramBinCount, 1, RHITextureFormat::R32_UINT, "AutoExposureHistogram"))) {
            return false;
        }
        RHIDescriptorPoolDesc exposurePoolDesc;
        exposurePoolDesc.MaxDescriptorSets = 16;
        exposurePoolDesc.MaxTextures = 16;
        exposurePoolDesc.MaxSamplers = 8;
        exposurePoolDesc.MaxStorageTextures = 16;
        exposurePoolDesc.MaxConstantBuffers = 8;
        if (!m_AutoExposureDescriptorPool.Init(exposurePoolDesc)) return false;
        m_AutoExposureHistTextureSet = m_AutoExposureDescriptorPool.AllocateDescriptorSet(m_AutoExposureHistPipelineLayout, 0);
        m_AutoExposureHistStorageSet = m_AutoExposureDescriptorPool.AllocateDescriptorSet(m_AutoExposureHistPipelineLayout, 1);
        m_AutoExposureTextureSet = m_AutoExposureDescriptorPool.AllocateDescriptorSet(m_AutoExposurePipelineLayout, 0);
        m_AutoExposureStorageSet = m_AutoExposureDescriptorPool.AllocateDescriptorSet(m_AutoExposurePipelineLayout, 1);
        if (!m_AutoExposureHistTextureSet || !m_AutoExposureHistStorageSet ||
            !m_AutoExposureTextureSet || !m_AutoExposureStorageSet) return false;
        if (!m_AutoExposureDescriptorPool.UpdateSampler(m_AutoExposureHistTextureSet, 1, 0, m_AutoExposureSampler.GetNative())) return false;
        if (!m_AutoExposureConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIExposureConstants), RHIMemoryLocation::HostUpload, "AutoExposureConstantsBuffer"))) return false;

        if (!CompileDDGIShaders(device) || !CreateDDGIPipelineLayout(device) || !CreateDDGIPipeline(device)) return false;
        if (!m_DDGISampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) return false;
        RHIDescriptorPoolDesc ddgiPoolDesc;
        ddgiPoolDesc.MaxDescriptorSets = 8; ddgiPoolDesc.MaxTextures = 16; ddgiPoolDesc.MaxSamplers = 8;
        ddgiPoolDesc.MaxStorageTextures = 8; ddgiPoolDesc.MaxConstantBuffers = 8;
        if (!m_DDGIDescriptorPool.Init(ddgiPoolDesc)) return false;
        m_DDGITextureSet = m_DDGIDescriptorPool.AllocateDescriptorSet(m_DDGIPipelineLayout, 0);
        m_DDGIStorageSet = m_DDGIDescriptorPool.AllocateDescriptorSet(m_DDGIPipelineLayout, 1);
        if (!m_DDGITextureSet || !m_DDGIStorageSet) return false;
        if (!m_DDGIDescriptorPool.UpdateSampler(m_DDGITextureSet, 1, 0, m_DDGISampler.GetNative())) return false;
        if (!m_DDGIConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIDDGIConstants), RHIMemoryLocation::HostUpload, "DDGIConstantsBuffer"))) return false;

        // Initialize Screen-Space Motion Vectors & Reactive Mask Resources
        if (!CompileVelocityShaders(device) || !CreateVelocityPipelineLayout(device) || !CreateVelocityPipeline(device)) return false;
        if (!m_DefaultVelocityMaterialBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIMaterialConstants), RHIMemoryLocation::HostUpload, "VelocityDefaultMaterial"))) return false;
        if (!m_DefaultReactiveMaterialBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIMaterialConstants), RHIMemoryLocation::HostUpload, "ReactiveDefaultMaterial"))) return false;
        {
            RHIMaterialConstants defaultVelMat{};
            defaultVelMat.Reserved[3] = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
            if (!m_DefaultVelocityMaterialBuffer.UpdateData(&defaultVelMat, sizeof(defaultVelMat))) return false;
            RHIMaterialConstants defaultReactMat{};
            defaultReactMat.Reserved[3] = glm::vec4(0.85f, 0.5f, 2.0f, 1.0f);
            if (!m_DefaultReactiveMaterialBuffer.UpdateData(&defaultReactMat, sizeof(defaultReactMat))) return false;
        }

        // Initialize Temporal Anti-Aliasing (TAA) Resources
        if (!CompileTAAShaders(device) || !CreateTAAPipelineLayout(device) || !CreateTAAPipeline(device)) return false;

        if (!m_TAALinearSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge)) ||
            !m_TAAPointSampler.Init(RHISamplerDesc::Point(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize samplers for TAA.");
            return false;
        }

        RHIDescriptorPoolDesc taaPoolDesc;
        taaPoolDesc.DebugName = "TemporalDescriptors";
        taaPoolDesc.MaxDescriptorSets = 16;
        taaPoolDesc.MaxTextures = 64;
        taaPoolDesc.MaxSamplers = 16;
        taaPoolDesc.MaxConstantBuffers = 16;
        if (!m_TAADescriptorPool.Init(taaPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize TAA Descriptor Pool!");
            return false;
        } else {
            m_TAADescriptorSet = m_TAADescriptorPool.AllocateDescriptorSet(m_TAAPipelineLayout, 0);
            if (!m_TAADescriptorSet) return false;
            if (m_TAALinearSampler.IsValid() && m_TAAPointSampler.IsValid()) {
                if (!m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 5, 0, m_TAALinearSampler.GetNative()) ||
                    !m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 6, 0, m_TAAPointSampler.GetNative())) return false;
            }
        }

        if (!m_TAAConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHITAAConstants), RHIMemoryLocation::HostUpload, "TAAConstantsBuffer"))) return false;

        // Initialize GPU Instanced Particle VFX Renderer
        m_ParticleRenderer = std::make_unique<RHIParticleRenderer>();
        if (!m_ParticleRenderer->Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize GPU Instanced Particle VFX Renderer!");
            return false;
        }

        // Initialize RHI Physics Debug Wireframe Line Renderer
        if (enablePhysicsDebug) {
            if (JPH::DebugRenderer::sInstance != nullptr) {
                AXLE_CORE_WARN("RHISceneRenderer: JPH::DebugRenderer singleton already active, skipping embedded physics debug renderer.");
            } else {
                if (!PhysicsEngine::IsInitialized()) {
                    PhysicsEngine::Init();
                }
                m_PhysicsDebugRenderer = std::make_unique<RHIPhysicsDebugRenderer>();
                if (!m_PhysicsDebugRenderer->Init(device)) {
                    AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize RHI Physics Debug Wireframe Line Renderer!");
                    return false;
                }
            }
        }

        // Initialize GPU Integer Object ID Picker
        m_ObjectPicker = std::make_unique<RHIObjectPicker>();
        if (!m_ObjectPicker->Init(device)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize GPU Integer Object ID Picker!");
            return false;
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHISceneRenderer: Successfully initialized with PushConstant transform streaming, 16x Anisotropic Filtering, Descriptor Pools, Forward+ Multi-Light Buffer, Film-Grade Tone Mapping & Anti-Aliasing (FXAA/MSAA).");
        return true;
    }

    void RHISceneRenderer::Shutdown() {
        // Partial initialization owns resources too; all shutdown members are idempotent.

        if (m_ObjectPicker) {
            m_ObjectPicker->Shutdown();
            m_ObjectPicker.reset();
        }

        if (m_PhysicsDebugRenderer) {
            m_PhysicsDebugRenderer->Shutdown();
            m_PhysicsDebugRenderer.reset();
        }

        if (m_ParticleRenderer) {
            m_ParticleRenderer->Shutdown();
            m_ParticleRenderer.reset();
        }

        m_FXAAPipelineSDR = nullptr;
        m_FXAAPipelineHDR = nullptr;
        m_FXAAPipelineLayout.Shutdown();
        m_FXAAConstantBuffer.Shutdown();
        m_FXAADescriptorSet = nullptr;
        m_FXAADescriptorPool.Shutdown();
        m_FXAASampler.Shutdown();
        m_IntermediateFXAATarget = nullptr;

        m_GTAOComputePipeline = nullptr;
        m_GTAOUpsamplePipeline = nullptr;
        m_GTAOApplyPipelineSDR = nullptr;
        m_GTAOApplyPipelineHDR = nullptr;
        m_GTAOComputePipelineLayout.Shutdown();
        m_GTAOUpsamplePipelineLayout.Shutdown();
        m_GTAOApplyPipelineLayout.Shutdown();
        m_GTAOConstantBuffer.Shutdown();
        m_GTAOComputeTextureSet = nullptr;
        m_GTAOComputeStorageSet = nullptr;
        m_GTAOUpsampleTextureSet = nullptr;
        m_GTAOUpsampleStorageSet = nullptr;
        m_GTAOApplyDescriptorSet = nullptr;
        m_GTAODescriptorPool.Shutdown();
        m_GTAOSampler.Shutdown();

        m_SSRComputePipeline = nullptr;
        m_SSRDownsamplePipeline = nullptr;
        m_SSRApplyPipelineSDR = nullptr;
        m_SSRApplyPipelineHDR = nullptr;
        m_SSRComputePipelineLayout.Shutdown();
        m_SSRDownsamplePipelineLayout.Shutdown();
        m_SSRApplyPipelineLayout.Shutdown();
        m_SSRConstantBuffer.Shutdown();
        m_SSRComputeTextureSet = nullptr;
        m_SSRComputeStorageSet = nullptr;
        m_SSRDownsampleTextureSets.fill(nullptr);
        m_SSRDownsampleStorageSets.fill(nullptr);
        m_SSRApplyDescriptorSet = nullptr;
        m_SSRDescriptorPool.Shutdown();
        m_SSRSampler.Shutdown();

        m_HiZPipeline = nullptr;
        m_HiZVisualizePipelineSDR = nullptr;
        m_HiZVisualizePipelineHDR = nullptr;
        m_HiZPipelineLayout.Shutdown();
        m_HiZVisualizePipelineLayout.Shutdown();
        m_HiZConstantBuffer.Shutdown();
        m_HiZTextureSets.fill(nullptr);
        m_HiZStorageSets.fill(nullptr);
        m_HiZVisualizeTextureSet = nullptr;
        m_HiZDescriptorPool.Shutdown();
        m_HiZSampler.Shutdown();

        if (m_PlanarLastCompletion) {
            if (m_Device && m_PlanarLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_PlanarLastCompletion);
            }
            m_PlanarLastCompletion = {};
        }
        m_PlanarRenderSucceeded = false;
        m_PlanarSession = {};
        m_PlanarScratchViewTargets.reset();
        m_LastPlanarCaptureResult = {};
        m_PlanarApplyPipelineSDR = nullptr;
        m_PlanarApplyPipelineHDR = nullptr;
        m_PlanarApplyPipelineLayout.Shutdown();
        m_PlanarApplyVertexShader = {};
        m_PlanarApplyPixelShader = {};
        m_PlanarCopyPixelShader = {};
        m_PlanarApplyDescriptorSet = nullptr;
        m_PlanarDescriptorPool.Shutdown();
        m_PlanarSampler.Shutdown();
        m_PlanarPublishScratchColor.Shutdown();
        m_PlanarPublishScratchDepth.Shutdown();
        m_PublishedPlanarCaptures.clear();
        m_PlanarStatuses.clear();
        m_RequestedManualPlanarCaptures.clear();
        m_ForcedInvalidatedPlanar.clear();
        m_FailedPlanarCaptureRevisions.clear();
        m_NextPlanarGenerationID = 1;
        m_ActivePlanarSceneInstanceID = 0;
        m_ActivePlanarSceneLifetimeEpoch = 0;
        m_PlanarCacheStats = {};
        m_ActivePlanarResolve = {};
        if (m_CaptureLastCompletion) {
            if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_CaptureLastCompletion);
            }
            m_CaptureLastCompletion = {};
        }
        m_CaptureRenderSucceeded = false;
        m_CaptureSession = {};
        m_DynamicProbeCaptures.clear();
        m_PendingDynamicCaptures.clear();
        m_DynamicProbeStatuses.clear();
        m_RequestedManualCaptures.clear();
        m_ForcedInvalidatedProbes.clear();
        m_DynamicCaptureTextures.clear();
        m_FailedDynamicCaptureRevisions.clear();
        m_LastDynamicCaptureResult = {};
        m_CaptureLRUClock = 0;
        m_DynamicCaptureEvictionCount = 0;
        m_DynamicCaptureBudgetOverrideBytes = 0;
        m_LastDynamicCaptureBudgetBytes = 64ull * 1024ull * 1024ull;
        m_HasDynamicCaptureBudgetOverride = false;
        m_ScheduledReflectionProbeKeys.clear();
        m_ActiveDynamicSceneInstanceID = 0;
        m_ActiveDynamicSceneLifetimeEpoch = 0;
        m_LastScheduledProbes = 0;
        m_LastScheduledFaces = 0;
        if (m_CaptureTargets) {
            m_CaptureTargets->Reset();
            m_CaptureTargets.reset();
        }
        m_CaptureOrientationPipeline = nullptr;
        m_CaptureOrientationPipelineLayout.Shutdown();
        m_CaptureOrientationTextureSet = nullptr;
        m_CaptureOrientationStorageSet = nullptr;
        m_CaptureOrientationDescriptorPool.Shutdown();
        m_CaptureOrientationComputeShader = {};

        m_CaptureDownsamplePipeline = nullptr;
        m_CapturePrefilterPipeline = nullptr;
        m_CapturePrefilterPipelineLayout.Shutdown();
        m_CaptureDownsampleTextureSets.fill(nullptr);
        m_CaptureDownsampleStorageSets.fill(nullptr);
        m_CapturePrefilterTextureSets.fill(nullptr);
        m_CapturePrefilterStorageSets.fill(nullptr);
        m_CapturePrefilterDescriptorPool.Shutdown();
        m_CapturePrefilterSampler.Shutdown();
        for (auto& cb : m_CapturePrefilterConstantBuffers) {
            cb.Shutdown();
        }
        m_CaptureDownsampleComputeShader = {};
        m_CapturePrefilterComputeShader = {};

        m_FogInjectPipeline = nullptr;
        m_FogReprojectPipeline = nullptr;
        m_FogIntegratePipeline = nullptr;
        m_FogApplyPipelineSDR = nullptr;
        m_FogApplyPipelineHDR = nullptr;
        m_FogInjectPipelineLayout.Shutdown();
        m_FogReprojectPipelineLayout.Shutdown();
        m_FogIntegratePipelineLayout.Shutdown();
        m_FogApplyPipelineLayout.Shutdown();
        m_FogInjectShader = {};
        m_FogReprojectShader = {};
        m_FogIntegrateShader = {};
        m_FogApplyVertexShader = {};
        m_FogApplyPixelShader = {};
        m_FogInjectTextureSet = nullptr;
        m_FogInjectStorageSet = nullptr;
        m_FogReprojectTextureSet = nullptr;
        m_FogReprojectStorageSet = nullptr;
        m_FogIntegrateTextureSet = nullptr;
        m_FogIntegrateStorageSet = nullptr;
        m_FogApplyDescriptorSet = nullptr;
        m_ForwardFogSets.clear();
        m_ForwardFogFallbackDescriptorSet = nullptr;
        m_DisabledFogConstantBuffer.Shutdown();
        m_DisabledDDGIConstantBuffer.Shutdown();
        m_FogDescriptorPool.Shutdown();
        m_FogShadowSampler.Shutdown();
        m_FogSampler.Shutdown();

        m_BloomComputePipeline = nullptr;
        m_BloomUpsamplePipelineSDR = nullptr;
        m_BloomUpsamplePipelineHDR = nullptr;
        m_BloomApplyPipelineSDR = nullptr;
        m_BloomApplyPipelineHDR = nullptr;
        m_BloomComputePipelineLayout.Shutdown();
        m_BloomApplyPipelineLayout.Shutdown();
        m_BloomConstantBuffer.Shutdown();
        m_BloomComputeTextureSet = nullptr;
        m_BloomComputeStorageSet = nullptr;
        m_BloomUpsampleDescriptorSets.fill(nullptr);
        m_BloomApplyDescriptorSet = nullptr;
        m_BloomDescriptorPool.Shutdown();
        m_BloomSampler.Shutdown();

        m_AutoExposureClearPipeline = nullptr;
        m_AutoExposureHistogramPipeline = nullptr;
        m_AutoExposurePipeline = nullptr;
        m_AutoExposureHistPipelineLayout.Shutdown();
        m_AutoExposurePipelineLayout.Shutdown();
        m_AutoExposureConstantBuffer.Shutdown();
        m_AutoExposureHistogram.Shutdown();
        m_AutoExposureHistTextureSet = nullptr;
        m_AutoExposureHistStorageSet = nullptr;
        m_AutoExposureTextureSet = nullptr;
        m_AutoExposureStorageSet = nullptr;
        m_AutoExposureDescriptorPool.Shutdown();
        m_AutoExposureSampler.Shutdown();

        m_DDGISeedPipeline = nullptr;
        m_DDGIIrradiancePipeline = nullptr;
        m_DDGIDistancePipeline = nullptr;
        m_DDGICapturePipeline = nullptr;
        m_DDGICaptureColor.Shutdown();
        m_DDGICaptureDepth.Shutdown();
        m_DDGICaptureDraws.clear();
        m_DDGIPipelineLayout.Shutdown();
        m_DDGIConstantBuffer.Shutdown();
        m_DDGITextureSet = nullptr;
        m_DDGIStorageSet = nullptr;
        m_DDGIDescriptorPool.Shutdown();
        m_DDGISampler.Shutdown();

        m_VelocityBgPipeline = nullptr;
        m_VelocityBgPipelineLayout.Shutdown();
        m_VelocityMeshPipelineLayout.Shutdown();
        m_DefaultVelocityMaterialBuffer.Shutdown();
        m_DefaultReactiveMaterialBuffer.Shutdown();
        m_TAADepthSource = nullptr;

        m_TAAPipelineSDR = nullptr;
        m_TAAPipelineHDR = nullptr;
        m_TAAPipelineLayout.Shutdown();
        m_TAAConstantBuffer.Shutdown();
        m_TAADescriptorSet = nullptr;
        m_TAADescriptorPool.Shutdown();
        m_TAALinearSampler.Shutdown();
        m_TAAPointSampler.Shutdown();
        m_MSAAColorTarget = nullptr;
        m_MSAADepthTarget = nullptr;
        m_CurrentMSAASamples = 1;

        m_ToneMappingPipelineSDR = nullptr;
        m_ToneMappingPipelineHDR = nullptr;
        m_ToneMappingPipelineLayout.Shutdown();
        m_ToneMappingConstantBuffer.Shutdown();
        m_ToneMappingDescriptorSet = nullptr;
        m_ToneMappingDescriptorPool.Shutdown();
        m_ToneMappingSampler.Shutdown();
        m_IntermediateHDRTarget = nullptr;
        m_ActiveViewTargets.reset(); m_ImplicitViewTargets.reset(); m_ImplicitViewColor = {};

        m_Pipeline = nullptr;
        m_PipelineHDR = nullptr;
        m_SurfaceNormalPixelShader = {};
        m_ReflectionPixelShader = {};
        m_SkyboxPipeline = nullptr;
        m_SkyboxPipelineHDR = nullptr;
        m_SkyboxPipelineLayout.Shutdown();
        m_SkyboxConstantBuffer.Shutdown();

        m_BoundCascadeAtlas={};
        m_CascadedShadows.Shutdown();
        m_LocalShadows.Shutdown();m_BoundLocalAtlas={};
        m_Environment.Clear();m_BoundIBL={};m_PreparedManualIBL=false;m_SkyboxDescriptorSet=nullptr;
        m_ReflectionProbeEnvironments.clear();
        m_ActiveReflectionProbePrefilter.reset();
        m_ActiveReflectionProbePrefilters.fill({});
        m_ActiveReflectionProbes.fill({});
        m_ActiveReflectionProbeStale.fill(false);
        m_ActiveReflectionProbeCount = 0;
        m_HasActiveReflectionProbe = false;
        m_ReflectionProbeBuffer.Shutdown();
        m_LightBuffer.Shutdown();
        m_SceneConstantBuffer.Shutdown();

        m_MaterialDraws.clear();
        m_CustomDraws.clear();
        m_CustomDescriptorPool.Shutdown();
        m_CustomSampler.Shutdown();
        m_CustomShadowSampler.Shutdown();
        m_CustomPolicySamplers.clear();
        m_CustomProgramCache.Clear();
        m_OpaqueSceneColor = nullptr;
        m_OpaqueSceneDepth = nullptr;
        m_ParticleSceneDepth = nullptr;
        m_OpaqueSceneNormal = nullptr;
        m_ScreenStorageTarget = nullptr;
        m_MaterialTextures.Clear();
        m_DefaultDescriptorSet = nullptr;
        m_ForwardOcclusionDescriptorSet = nullptr;
        m_DDGICaptureOcclusionSet = nullptr;
        m_AreaLightLTC.Shutdown();
        m_IrradianceMap = nullptr;
        m_PrefilterMap = nullptr;
        m_BRDFLUT = nullptr;
        m_DescriptorPool.Shutdown();
        m_AnisoSampler.Shutdown();

        m_PipelineLayout.Shutdown();
        for (auto& slot : m_GPUTimestampSlots) {
            slot.Readback.Shutdown();
            if (slot.Pool && m_Device && m_Device->GetNRICore().DestroyQueryPool)
                m_Device->GetNRICore().DestroyQueryPool(slot.Pool);
            slot.Pool = nullptr; slot.Pending = false; slot.Completion = {};
            slot.PassNames.clear(); slot.QueryCount = 0;
        }
        m_ActiveGPUTimestampSlot = static_cast<size_t>(-1);
        m_ActivePassEndQuery = UINT32_MAX;
        m_NextGPUTimestampSlot = 0;
        m_LastGPUTimingFrame = {};
        m_LastGPUTimestampMilliseconds = 0.0;
        m_PlanarViewContext.Shutdown();
        m_CaptureViewContext.Shutdown();
        m_ViewContext.Shutdown();
        m_Device = nullptr;
        m_Initialized = false;
    }

    bool RHISceneRenderer::CompileShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FindShaderPath("ForwardPBR.hlsl");
        if (shaderPath.empty()) {
            shaderPath = FindShaderPath("ForwardScene.hlsl");
        }

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
                AXLE_CORE_INFO("RHISceneRenderer: Loaded ForwardPBR shader from {0}", PathToUtf8(shaderPath));
            }
        }

        if (shaderSource.empty() || shaderSource.find("cbuffer CascadeReceiver")==std::string::npos || shaderSource.find("AXLE_FORWARD_LOCAL_SHADOWS")==std::string::npos) {
            AXLE_CORE_WARN("RHISceneRenderer: Using canonical embedded forward shader (missing/current-interface resource).");
            shaderSource = kNativeForwardSource;
            shaderPath.clear();
        }

        RHIShaderFormat format = (device.GetBackend() == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        // Compile Vertex Shader
        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = shaderPath.empty() ? "ForwardPBR.hlsl" : PathToUtf8(shaderPath);
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = format;
        vsDesc.Optimization = true;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: VS compile failed: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_VertexShader = vsRes.Bytecode;

        // Compile Pixel Shader
        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: PS compile failed: {0}", psRes.ErrorMessage);
            return false;
        }
        m_PixelShader = psRes.Bytecode;
        auto reflectionDesc = psDesc;
        reflectionDesc.EntryPoint = "PSReflectionMain";
        const auto reflectionRes = compiler.Compile(reflectionDesc);
        if (!reflectionRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: reflection PS compile failed: {0}", reflectionRes.ErrorMessage);
            return false;
        }
        m_ReflectionPixelShader = reflectionRes.Bytecode;

        auto ddgiCaptureDesc = psDesc;
        ddgiCaptureDesc.EntryPoint = "PSDDGICapture";
        const auto ddgiCaptureRes = compiler.Compile(ddgiCaptureDesc);
        if (!ddgiCaptureRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: DDGI capture PS compile failed: {0}", ddgiCaptureRes.ErrorMessage);
            return false;
        }
        m_DDGICapturePixelShader = ddgiCaptureRes.Bytecode;

        auto normalShaderPath = FindShaderPath("SurfaceNormal.hlsl");
        if (normalShaderPath.empty() || !std::filesystem::exists(normalShaderPath)) {
            AXLE_CORE_ERROR("RHISceneRenderer: SurfaceNormal.hlsl was not found");
            return false;
        }
        RHIShaderCompileDesc normalDesc;
        normalDesc.SourcePath = PathToUtf8(normalShaderPath);
        normalDesc.EntryPoint = "PSMain";
        normalDesc.Stage = RHIShaderStage::Fragment;
        normalDesc.HLSLVersion = "2021";
        normalDesc.ShaderModel = "6_0";
        normalDesc.Format = format;
        normalDesc.Optimization = true;
        {
            std::ifstream file(normalShaderPath);
            std::stringstream source;
            source << file.rdbuf();
            normalDesc.Source = source.str();
        }
        auto normalRes = compiler.Compile(normalDesc);
        if (!normalRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Surface normal PS compile failed: {0}", normalRes.ErrorMessage);
            return false;
        }
        m_SurfaceNormalPixelShader = normalRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreatePipelineLayout(RHIDevice& device) {
        // Root Constant (b0, space 0): 128-byte per-entity push constants
        nri::RootConstantDesc rootConst = {};
        rootConst.registerIndex = 0;
        rootConst.size = sizeof(RHIObjectPushConstants); // 128 bytes
        rootConst.shaderStages = nri::StageBits::ALL;

        // Root Descriptors (space 0):
        // [0] b1, space 0: 256-byte Scene Constants
        // [1] b2, space 0: 2304-byte Forward+ Multi-Light Buffer
        nri::RootDescriptorDesc rootDescs[8] = {};
        rootDescs[0].registerIndex = ConstantBufferContract::FrameConstantsRegister;
        rootDescs[0].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[0].shaderStages = nri::StageBits::ALL;

        rootDescs[1].registerIndex = ConstantBufferContract::LightConstantsRegister;
        rootDescs[1].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[1].shaderStages = nri::StageBits::ALL;

        rootDescs[2].registerIndex = ConstantBufferContract::MaterialConstantsRegister;
        rootDescs[2].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[2].shaderStages = nri::StageBits::ALL;

        rootDescs[3].registerIndex = 4; // C13 cascade receiver constants
        rootDescs[3].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[3].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        rootDescs[4].registerIndex = 5;
        rootDescs[4].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[4].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        rootDescs[5].registerIndex = 6; // Multi-probe reflection constants
        rootDescs[5].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[5].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        rootDescs[6].registerIndex = 7; // Froxel volumetric fog constants for transparent/additive surfaces
        rootDescs[6].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[6].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        rootDescs[7].registerIndex = 8; // World-space DDGI probe volume constants
        rootDescs[7].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDescs[7].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        // Space 1 includes PBR, IBL, refraction, cascades and local shadow textures.
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = ForwardTextureCount;
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::RootSamplerDesc samplers[4]{};
        auto& sampler=samplers[0];
        sampler.registerIndex = 0;
        sampler.desc = RHISamplerDesc::Anisotropic16x().ToNRIDesc();
        sampler.shaderStages = nri::StageBits::FRAGMENT_SHADER;

        samplers[1].registerIndex=1;
        samplers[1].desc=RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge).ToNRIDesc();
        samplers[1].shaderStages=nri::StageBits::FRAGMENT_SHADER;

        samplers[2].registerIndex=2;
        samplers[2].desc=RHISamplerDesc::ShadowPCF().ToNRIDesc();
        samplers[2].shaderStages=nri::StageBits::FRAGMENT_SHADER;

        samplers[3].registerIndex=3;
        samplers[3].desc=RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge).ToNRIDesc();
        samplers[3].shaderStages=nri::StageBits::FRAGMENT_SHADER;

        nri::DescriptorRangeDesc occlusionAndProbeRanges[4]{};
        occlusionAndProbeRanges[0].baseRegisterIndex = 0;
        occlusionAndProbeRanges[0].descriptorNum = 2;
        occlusionAndProbeRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        occlusionAndProbeRanges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        occlusionAndProbeRanges[1].baseRegisterIndex = 2;
        occlusionAndProbeRanges[1].descriptorNum = MaxActiveReflectionProbes;
        occlusionAndProbeRanges[1].descriptorType = nri::DescriptorType::TEXTURE;
        occlusionAndProbeRanges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        occlusionAndProbeRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        occlusionAndProbeRanges[2].baseRegisterIndex = 6; // u_AreaLightLTC
        occlusionAndProbeRanges[2].descriptorNum = 1;
        occlusionAndProbeRanges[2].descriptorType = nri::DescriptorType::TEXTURE;
        occlusionAndProbeRanges[2].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        occlusionAndProbeRanges[3].baseRegisterIndex = 7; // u_DDGIIrradiance, u_DDGIDistance
        occlusionAndProbeRanges[3].descriptorNum = 2;
        occlusionAndProbeRanges[3].descriptorType = nri::DescriptorType::TEXTURE;
        occlusionAndProbeRanges[3].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        nri::DescriptorRangeDesc forwardFogRanges[2]{};
        forwardFogRanges[0].baseRegisterIndex = 0; // u_FroxelIntegrated
        forwardFogRanges[0].descriptorNum = 1;
        forwardFogRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        forwardFogRanges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        forwardFogRanges[1].baseRegisterIndex = 1; // u_FroxelMedia
        forwardFogRanges[1].descriptorNum = 1;
        forwardFogRanges[1].descriptorType = nri::DescriptorType::TEXTURE;
        forwardFogRanges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        nri::DescriptorSetDesc descriptorSets[3]{};
        descriptorSets[0].registerSpace = 1;
        descriptorSets[0].ranges = ranges;
        descriptorSets[0].rangeNum = 1;
        descriptorSets[1].registerSpace = 4;
        descriptorSets[1].ranges = occlusionAndProbeRanges;
        descriptorSets[1].rangeNum = 4;
        descriptorSets[2].registerSpace = 5;
        descriptorSets[2].ranges = forwardFogRanges;
        descriptorSets[2].rangeNum = 2;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootDescriptors = rootDescs;
        layoutDesc.rootDescriptorNum = 8;
        layoutDesc.rootSamplers = samplers;
        layoutDesc.rootSamplerNum = 4;
        layoutDesc.descriptorSets = descriptorSets;
        layoutDesc.descriptorSetNum = 3;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_PipelineLayout.Init(layoutDesc);
    }

    void RHISceneRenderer::SetIBLMaps(const std::shared_ptr<RHI::RHITexture>& irradiance,
                                      const std::shared_ptr<RHI::RHITexture>& prefilter,
                                      const std::shared_ptr<RHI::RHITexture>& brdfLUT) {
        m_IBLAllowed = true;
        m_IrradianceMap = irradiance;
        m_PrefilterMap = prefilter;
        m_BRDFLUT = brdfLUT;
        m_UseIBL = (irradiance && prefilter && brdfLUT);
        m_BoundIBL = {};

        if (m_DefaultDescriptorSet && m_DescriptorPool.IsValid()) {
            auto irrTex = m_IrradianceMap ? m_IrradianceMap : RHIMaterial::GetDefaultIrradianceCubemap();
            auto prefTex = m_PrefilterMap ? m_PrefilterMap : RHIMaterial::GetDefaultPrefilterCubemap();
            auto lutTex = m_BRDFLUT ? m_BRDFLUT : RHIMaterial::GetDefaultBRDFLUT();

            if (irrTex && prefTex && lutTex) {
                std::vector<nri::Descriptor*> iblSRVs = {
                    irrTex->GetSRV(),
                    prefTex->GetSRV(),
                    lutTex->GetSRV()
                };
                if (!m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 6, iblSRVs)) {
                    m_UseIBL = false;
                    AXLE_CORE_ERROR("RHISceneRenderer: Failed to update IBL descriptors");
                }
            }
        }
    }

    std::shared_ptr<RHITexture> RHISceneRenderer::ActiveIrradiance() const {
        auto ready=m_Environment.Ready();auto texture=ready?ready->Irradiance:(m_PreparedManualIBL?m_IrradianceMap:nullptr);
        if (texture && IsCaptureFeedbackTexture(*texture)) texture.reset();
        return texture&&texture->IsValid()?texture:RHIMaterial::GetDefaultIrradianceCubemap();
    }
    std::shared_ptr<RHITexture> RHISceneRenderer::ActivePrefilter() const {
        auto ready=m_Environment.Ready();auto texture=ready?ready->Prefilter:(m_PreparedManualIBL?m_PrefilterMap:nullptr);
        if (texture && IsCaptureFeedbackTexture(*texture)) texture.reset();
        return texture&&texture->IsValid()?texture:RHIMaterial::GetDefaultPrefilterCubemap();
    }
    std::shared_ptr<RHITexture> RHISceneRenderer::ActiveBRDF() const {
        auto ready=m_Environment.Ready();auto texture=ready?ready->BRDF:(m_PreparedManualIBL?m_BRDFLUT:nullptr);
        if (texture && IsCaptureFeedbackTexture(*texture)) texture.reset();
        return texture&&texture->IsValid()?texture:RHIMaterial::GetDefaultBRDFLUT();
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreatePipeline(nri::Format colorFormat, nri::Format depthFormat, uint8_t sampleCount,
                                                         Axle::SurfaceType surfaceType,
                                                         Axle::CullMode cullMode,
                                                         bool depthWrite, bool reflectionTerms,
                                                         bool reverseWinding) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, reflectionTerms ? m_ReflectionPixelShader : m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        if (cullMode == Axle::CullMode::Back) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::BACK;
        } else if (cullMode == Axle::CullMode::Front) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::FRONT;
        } else {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        }
        gfxDesc.Rasterizer.FrontCounterClockwise = !reverseWinding;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = depthWrite;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        if (surfaceType == Axle::SurfaceType::Transparent) {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::AlphaBlend() };
        } else if (surfaceType == Axle::SurfaceType::Additive) {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Additive() };
        } else {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        }
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        if (reflectionTerms) {
            for (int i = 0; i < 2; ++i) {
                gfxDesc.ColorAttachments.push_back(RHIBlendAttachmentDesc::Opaque());
                gfxDesc.ColorAttachmentFormats.push_back(nri::Format::RGBA16_SFLOAT);
            }
        }
        gfxDesc.SampleCount = sampleCount;

        gfxDesc.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateSurfaceNormalPipeline(nri::Format colorFormat, nri::Format depthFormat,
                                                                    bool preLighting, CullMode cull, bool reverseWinding) {
        if (!m_SurfaceNormalPixelShader.IsValid() || !m_VertexShader.IsValid()) return nullptr;

        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_SurfaceNormalPixelShader };
        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = cull == CullMode::Back ? nri::CullMode::BACK : cull == CullMode::Front ? nri::CullMode::FRONT : nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = !reverseWinding;
        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = preLighting;
        gfxDesc.DepthStencil.DepthCompareOp = preLighting ? nri::CompareOp::LESS : nri::CompareOp::EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;
        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.SampleCount = 1;
        gfxDesc.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();
        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateCustomPipeline(const CustomReadyGeneration& generation,
        nri::Format colorFormat, nri::Format depthFormat, uint8_t samples,
        SurfaceType surface, CullMode cull, bool depthWrite, bool reflectionTerms, bool reverseWinding) {
        return CreateCustomMaterialPipeline(generation,colorFormat,depthFormat,samples,surface,cull,depthWrite,false,reflectionTerms,reverseWinding);
    }

    bool RHISceneRenderer::CreateGraphicsPipeline(RHIDevice& device) {
        m_Pipeline = GetOrCreatePipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT);
        m_PipelineHDR = GetOrCreatePipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT);
        m_DDGICapturePipeline = GetOrCreateDDGICapturePipeline();
        return m_Pipeline != nullptr && m_PipelineHDR != nullptr && m_DDGICapturePipeline != nullptr;
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateDDGICapturePipeline() {
        if (!m_VertexShader.IsValid() || !m_DDGICapturePixelShader.IsValid()) {
            return nullptr;
        }
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_DDGICapturePixelShader };
        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        // Double-sided capture marks backfaces via SV_IsFrontFace to detect inside-geometry probes
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = DepthClipContract::FrontCounterClockwise;
        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = true;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = nri::Format::D32_SFLOAT;
        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { nri::Format::RGBA16_SFLOAT };
        gfxDesc.SampleCount = 1;
        gfxDesc.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();
        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CompileSkyboxShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FindShaderPath("AtmosphericSkybox.hlsl");

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
            }
        }

        if (shaderSource.empty() || shaderSource.find("u_EnvironmentCube")==std::string::npos) {
            shaderSource=kNativeSkySource;shaderPath.clear();
        }

        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = PathToUtf8(shaderPath);
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        vsDesc.Optimization = true;

        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile AtmosphericSkybox VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_SkyboxVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile AtmosphericSkybox PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_SkyboxPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateSkyboxPipelineLayout(RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: 256-byte SkyboxConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        nri::DescriptorRangeDesc range{};range.baseRegisterIndex=0;range.descriptorNum=1;
        range.descriptorType=nri::DescriptorType::TEXTURE;range.shaderStages=nri::StageBits::FRAGMENT_SHADER;
        nri::DescriptorSetDesc set{};set.registerSpace=1;set.ranges=&range;set.rangeNum=1;
        nri::RootSamplerDesc sampler{};sampler.registerIndex=0;sampler.shaderStages=nri::StageBits::FRAGMENT_SHADER;
        sampler.desc=RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge).ToNRIDesc();
        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets=&set;layoutDesc.descriptorSetNum=1;layoutDesc.rootSamplers=&sampler;layoutDesc.rootSamplerNum=1;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_SkyboxPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateSkyboxPipeline(nri::Format colorFormat, nri::Format depthFormat, uint8_t sampleCount, bool reflectionTerms) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_SkyboxPipelineLayout;
        gfxDesc.Shaders = { m_SkyboxVertexShader, m_SkyboxPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS_EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        if (reflectionTerms) {
            auto ignored = RHIBlendAttachmentDesc::Opaque();
            ignored.ColorWriteMask = nri::ColorWriteBits::NONE;
            for (int i = 0; i < 2; ++i) {
                gfxDesc.ColorAttachments.push_back(ignored);
                gfxDesc.ColorAttachmentFormats.push_back(nri::Format::RGBA16_SFLOAT);
            }
        }
        gfxDesc.SampleCount = sampleCount;

        // Empty vertex input layout (full-screen triangle generated via SV_VertexID)
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateSkyboxPipeline(RHIDevice& device) {
        m_SkyboxPipeline = GetOrCreateSkyboxPipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT);
        m_SkyboxPipelineHDR = GetOrCreateSkyboxPipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT);
        return m_SkyboxPipeline != nullptr && m_SkyboxPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileToneMappingShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FindShaderPath("ToneMapping.hlsl");

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
            }
        }

        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate ToneMapping.hlsl!");
            return false;
        }

        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = PathToUtf8(shaderPath);
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        vsDesc.Optimization = true;

        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile ToneMapping VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_ToneMappingVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile ToneMapping PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_ToneMappingPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateToneMappingPipelineLayout(RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: 256-byte ToneMappingConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (HDR Texture), t1 (adapted exposure)
        // Range 1: s0 (Linear/Bilinear Sampler)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 2;
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[1].baseRegisterIndex = 0;
        ranges[1].descriptorNum = 1;
        ranges[1].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 2;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_ToneMappingPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateToneMappingPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_ToneMappingPipelineLayout;
        gfxDesc.Shaders = { m_ToneMappingVertexShader, m_ToneMappingPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };

        // Empty vertex input layout (full-screen triangle generated via SV_VertexID)
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateToneMappingPipeline(RHIDevice& device) {
        m_ToneMappingPipelineSDR = GetOrCreateToneMappingPipeline(nri::Format::RGBA8_UNORM);
        m_ToneMappingPipelineHDR = GetOrCreateToneMappingPipeline(nri::Format::RGBA16_SFLOAT);
        return m_ToneMappingPipelineSDR != nullptr && m_ToneMappingPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileFXAAShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FindShaderPath("FXAA.hlsl");

        if (!shaderPath.empty() && std::filesystem::exists(shaderPath)) {
            std::ifstream file(shaderPath);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderSource = ss.str();
            }
        }

        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate FXAA.hlsl!");
            return false;
        }

        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = PathToUtf8(shaderPath);
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        vsDesc.Optimization = true;

        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile FXAA VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_FXAAVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile FXAA PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_FXAAPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateFXAAPipelineLayout(RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: 256-byte RHIFXAAConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (Input Texture)
        // Range 1: t1 (Scene depth for GTAO; white fallback for FXAA)
        // Range 2: s0 (Linear Sampler)
        nri::DescriptorRangeDesc ranges[3] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 1;
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[1].baseRegisterIndex = 1;
        ranges[1].descriptorNum = 1;
        ranges[1].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        ranges[2].baseRegisterIndex = 0;
        ranges[2].descriptorNum = 1;
        ranges[2].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[2].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 3;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_FXAAPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateFXAAPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_FXAAPipelineLayout;
        gfxDesc.Shaders = { m_FXAAVertexShader, m_FXAAPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };

        // Empty vertex input layout (full-screen triangle generated via SV_VertexID)
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateFXAAPipeline(RHIDevice& device) {
        m_FXAAPipelineSDR = GetOrCreateFXAAPipeline(nri::Format::RGBA8_UNORM);
        m_FXAAPipelineHDR = GetOrCreateFXAAPipeline(nri::Format::RGBA16_SFLOAT);
        return m_FXAAPipelineSDR != nullptr && m_FXAAPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileGTAOShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("GTAO.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate GTAO.hlsl!");
            return false;
        }

        std::string shaderSource;
        std::ifstream file(shaderPath);
        if (file.is_open()) {
            std::stringstream ss; ss << file.rdbuf(); shaderSource = ss.str();
        }
        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read GTAO.hlsl!");
            return false;
        }

        RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        // 1. Compute Shader: CSMain
        RHIShaderCompileDesc csDesc;
        csDesc.Source = shaderSource;
        csDesc.SourcePath = PathToUtf8(shaderPath);
        csDesc.EntryPoint = "CSMain";
        csDesc.Stage = RHIShaderStage::Compute;
        csDesc.HLSLVersion = "2021";
        csDesc.ShaderModel = "6_0";
        csDesc.Format = format;
        csDesc.Optimization = true;

        auto csRes = compiler.Compile(csDesc);
        if (!csRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile GTAO CS: {0}", csRes.ErrorMessage);
            return false;
        }
        m_GTAOComputeShader = csRes.Bytecode;

        RHIShaderCompileDesc upsampleCsDesc = csDesc;
        upsampleCsDesc.EntryPoint = "CSUpsample";
        auto upsampleCsRes = compiler.Compile(upsampleCsDesc);
        if (!upsampleCsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile GTAO Upsample CS: {0}", upsampleCsRes.ErrorMessage);
            return false;
        }
        m_GTAOUpsampleShader = upsampleCsRes.Bytecode;

        // 2. Vertex Shader: VSMain
        RHIShaderCompileDesc vsDesc = csDesc;
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile GTAO VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_GTAOApplyVertexShader = vsRes.Bytecode;

        // 3. Pixel Shader: PSMain
        RHIShaderCompileDesc psDesc = csDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile GTAO PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_GTAOApplyPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateGTAOPipelineLayout(RHIDevice& device) {
        // 1. Compute Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 512-byte RHIGTAOConstants
        nri::RootDescriptorDesc compRootDesc = {};
        compRootDesc.registerIndex = 0;
        compRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        compRootDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        // Set 0 (space 1):
        // Range 0: t0 (Depth) -> TEXTURE
        // Range 1: t1 (Normal) -> TEXTURE
        // Range 2: s0 (Linear Sampler) -> SAMPLER
        nri::DescriptorRangeDesc compTexRanges[3] = {};
        compTexRanges[0].baseRegisterIndex = 0;
        compTexRanges[0].descriptorNum = 1;
        compTexRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        compTexRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        compTexRanges[1].baseRegisterIndex = 1;
        compTexRanges[1].descriptorNum = 1;
        compTexRanges[1].descriptorType = nri::DescriptorType::TEXTURE;
        compTexRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        compTexRanges[2].baseRegisterIndex = 0;
        compTexRanges[2].descriptorNum = 1;
        compTexRanges[2].descriptorType = nri::DescriptorType::SAMPLER;
        compTexRanges[2].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        // Set 1 (space 2):
        // Range 0: u0 (Storage Output) -> STORAGE_TEXTURE
        nri::DescriptorRangeDesc compStorageRanges[1] = {};
        compStorageRanges[0].baseRegisterIndex = 0;
        compStorageRanges[0].descriptorNum = 1;
        compStorageRanges[0].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        compStorageRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compStorageRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc compDescSets[2] = {};
        compDescSets[0].registerSpace = 1;
        compDescSets[0].ranges = compTexRanges;
        compDescSets[0].rangeNum = 3;

        compDescSets[1].registerSpace = 2;
        compDescSets[1].ranges = compStorageRanges;
        compDescSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc compLayoutDesc = {};
        compLayoutDesc.rootRegisterSpace = 0;
        compLayoutDesc.rootDescriptors = &compRootDesc;
        compLayoutDesc.rootDescriptorNum = 1;
        compLayoutDesc.descriptorSets = compDescSets;
        compLayoutDesc.descriptorSetNum = 2;
        compLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        if (!m_GTAOComputePipelineLayout.Init(compLayoutDesc)) return false;

        // 1b. Full-Resolution Bilateral Upsample Pipeline Layout:
        // Set 0 (space 1): t0 Depth, t1 Normal, t2 HalfResGTAO, s0 Sampler
        // Set 1 (space 2): u0 ResolvedGTAO
        nri::DescriptorRangeDesc upsampleTexRanges[4] = {};
        for (uint32_t idx = 0; idx < 4; ++idx) {
            upsampleTexRanges[idx].baseRegisterIndex = idx < 3 ? idx : 0;
            upsampleTexRanges[idx].descriptorNum = 1;
            upsampleTexRanges[idx].descriptorType = idx < 3 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
            upsampleTexRanges[idx].shaderStages = nri::StageBits::COMPUTE_SHADER;
            upsampleTexRanges[idx].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        }
        nri::DescriptorSetDesc upsampleDescSets[2] = {};
        upsampleDescSets[0].registerSpace = 1;
        upsampleDescSets[0].ranges = upsampleTexRanges;
        upsampleDescSets[0].rangeNum = 4;
        upsampleDescSets[1].registerSpace = 2;
        upsampleDescSets[1].ranges = compStorageRanges;
        upsampleDescSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc upsampleLayoutDesc = {};
        upsampleLayoutDesc.rootRegisterSpace = 0;
        upsampleLayoutDesc.rootDescriptors = &compRootDesc;
        upsampleLayoutDesc.rootDescriptorNum = 1;
        upsampleLayoutDesc.descriptorSets = upsampleDescSets;
        upsampleLayoutDesc.descriptorSetNum = 2;
        upsampleLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        if (!m_GTAOUpsamplePipelineLayout.Init(upsampleLayoutDesc)) return false;

        // 2. Apply Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 512-byte RHIGTAOConstants
        nri::RootDescriptorDesc applyRootDesc = {};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (Depth) -> TEXTURE
        // Range 1: t1 (Occlusion Texture) -> TEXTURE
        // Range 2: t2 (Scene Color) -> TEXTURE
        // Range 3: s0 (Sampler) -> SAMPLER
        nri::DescriptorRangeDesc applyRanges[4] = {};
        applyRanges[0].baseRegisterIndex = 0;
        applyRanges[0].descriptorNum = 1;
        applyRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        applyRanges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        applyRanges[1].baseRegisterIndex = 1;
        applyRanges[1].descriptorNum = 1;
        applyRanges[1].descriptorType = nri::DescriptorType::TEXTURE;
        applyRanges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        applyRanges[2].baseRegisterIndex = 2;
        applyRanges[2].descriptorNum = 1;
        applyRanges[2].descriptorType = nri::DescriptorType::TEXTURE;
        applyRanges[2].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        applyRanges[3].baseRegisterIndex = 0;
        applyRanges[3].descriptorNum = 1;
        applyRanges[3].descriptorType = nri::DescriptorType::SAMPLER;
        applyRanges[3].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[3].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc applyDescSet = {};
        applyDescSet.registerSpace = 1;
        applyDescSet.ranges = applyRanges;
        applyDescSet.rangeNum = 4;

        nri::PipelineLayoutDesc applyLayoutDesc = {};
        applyLayoutDesc.rootRegisterSpace = 0;
        applyLayoutDesc.rootDescriptors = &applyRootDesc;
        applyLayoutDesc.rootDescriptorNum = 1;
        applyLayoutDesc.descriptorSets = &applyDescSet;
        applyLayoutDesc.descriptorSetNum = 1;
        applyLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_GTAOApplyPipelineLayout.Init(applyLayoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateGTAOApplyPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_GTAOApplyPipelineLayout;
        gfxDesc.Shaders = { m_GTAOApplyVertexShader, m_GTAOApplyPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateGTAOPipeline(RHIDevice& device) {
        auto& cache = RHIPipelineCache::Get();
        RHIComputePipelineDesc compDesc;
        compDesc.Layout = &m_GTAOComputePipelineLayout;
        compDesc.ComputeShader = m_GTAOComputeShader;
        m_GTAOComputePipeline = cache.GetOrCreateComputePipeline(compDesc);

        RHIComputePipelineDesc upsampleDesc;
        upsampleDesc.Layout = &m_GTAOUpsamplePipelineLayout;
        upsampleDesc.ComputeShader = m_GTAOUpsampleShader;
        m_GTAOUpsamplePipeline = cache.GetOrCreateComputePipeline(upsampleDesc);

        m_GTAOApplyPipelineSDR = GetOrCreateGTAOApplyPipeline(nri::Format::RGBA8_UNORM);
        m_GTAOApplyPipelineHDR = GetOrCreateGTAOApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_GTAOComputePipeline != nullptr && m_GTAOUpsamplePipeline != nullptr &&
               m_GTAOApplyPipelineSDR != nullptr && m_GTAOApplyPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileSSRShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("SSR.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate SSR.hlsl!");
            return false;
        }

        std::string shaderSource;
        std::ifstream file(shaderPath);
        if (file.is_open()) {
            std::stringstream ss; ss << file.rdbuf(); shaderSource = ss.str();
        }
        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read SSR.hlsl!");
            return false;
        }

        RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        // 1. Compute Shader: CSMain
        RHIShaderCompileDesc csDesc;
        csDesc.Source = shaderSource;
        csDesc.SourcePath = PathToUtf8(shaderPath);
        csDesc.EntryPoint = "CSMain";
        csDesc.Stage = RHIShaderStage::Compute;
        csDesc.HLSLVersion = "2021";
        csDesc.ShaderModel = "6_0";
        csDesc.Format = format;
        csDesc.Optimization = true;

        auto csRes = compiler.Compile(csDesc);
        if (!csRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile SSR CS: {0}", csRes.ErrorMessage);
            return false;
        }
        m_SSRComputeShader = csRes.Bytecode;

        RHIShaderCompileDesc dsDesc = csDesc;
        dsDesc.EntryPoint = "DownsampleColorCS";
        auto dsRes = compiler.Compile(dsDesc);
        if (!dsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile SSR Downsample CS: {0}", dsRes.ErrorMessage);
            return false;
        }
        m_SSRDownsampleShader = dsRes.Bytecode;

        // 2. Vertex Shader: VSMain
        RHIShaderCompileDesc vsDesc = csDesc;
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile SSR VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_SSRApplyVertexShader = vsRes.Bytecode;

        // 3. Pixel Shader: PSMain
        RHIShaderCompileDesc psDesc = csDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile SSR PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_SSRApplyPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateSSRPipelineLayout(RHIDevice& device) {
        // 1. Compute Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 512-byte RHISSRConstants
        nri::RootDescriptorDesc compRootDesc = {};
        compRootDesc.registerIndex = 0;
        compRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        compRootDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        // Set 0 (space 1):
        // Range 0: t0..t7 (Hi-Z depth mips) -> TEXTURE
        // Range 1: t8 (Normal) -> TEXTURE
        // Range 2: t9 (Color) -> TEXTURE
        // Range 3: s0 (Linear Sampler) -> SAMPLER
        nri::DescriptorRangeDesc compTexRanges[4] = {};
        compTexRanges[0].baseRegisterIndex = 0;
        compTexRanges[0].descriptorNum = 8;
        compTexRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        compTexRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        compTexRanges[1].baseRegisterIndex = 8;
        compTexRanges[1].descriptorNum = 1;
        compTexRanges[1].descriptorType = nri::DescriptorType::TEXTURE;
        compTexRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        compTexRanges[2].baseRegisterIndex = 9;
        compTexRanges[2].descriptorNum = 1;
        compTexRanges[2].descriptorType = nri::DescriptorType::TEXTURE;
        compTexRanges[2].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        compTexRanges[3].baseRegisterIndex = 0;
        compTexRanges[3].descriptorNum = 1;
        compTexRanges[3].descriptorType = nri::DescriptorType::SAMPLER;
        compTexRanges[3].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[3].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        // Set 1 (space 2):
        // Range 0: u0 (Storage Output) -> STORAGE_TEXTURE
        nri::DescriptorRangeDesc compStorageRanges[1] = {};
        compStorageRanges[0].baseRegisterIndex = 0;
        compStorageRanges[0].descriptorNum = 1;
        compStorageRanges[0].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        compStorageRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compStorageRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc compDescSets[2] = {};
        compDescSets[0].registerSpace = 1;
        compDescSets[0].ranges = compTexRanges;
        compDescSets[0].rangeNum = 4;

        compDescSets[1].registerSpace = 2;
        compDescSets[1].ranges = compStorageRanges;
        compDescSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc compLayoutDesc = {};
        compLayoutDesc.rootRegisterSpace = 0;
        compLayoutDesc.rootDescriptors = &compRootDesc;
        compLayoutDesc.rootDescriptorNum = 1;
        compLayoutDesc.descriptorSets = compDescSets;
        compLayoutDesc.descriptorSetNum = 2;
        compLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        if (!m_SSRComputePipelineLayout.Init(compLayoutDesc)) return false;

        nri::RootDescriptorDesc dsRootDesc{};
        dsRootDesc.registerIndex = 0;
        dsRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        dsRootDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc dsTexRanges[2]{};
        dsTexRanges[0].baseRegisterIndex = 0;
        dsTexRanges[0].descriptorNum = 1;
        dsTexRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        dsTexRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        dsTexRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        dsTexRanges[1].baseRegisterIndex = 0;
        dsTexRanges[1].descriptorNum = 1;
        dsTexRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        dsTexRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        dsTexRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorRangeDesc dsStorageRange{};
        dsStorageRange.baseRegisterIndex = 0;
        dsStorageRange.descriptorNum = 1;
        dsStorageRange.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        dsStorageRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        dsStorageRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc dsSets[2]{};
        dsSets[0].registerSpace = 1;
        dsSets[0].ranges = dsTexRanges;
        dsSets[0].rangeNum = 2;
        dsSets[1].registerSpace = 2;
        dsSets[1].ranges = &dsStorageRange;
        dsSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc dsLayoutDesc{};
        dsLayoutDesc.rootRegisterSpace = 0;
        dsLayoutDesc.rootDescriptors = &dsRootDesc;
        dsLayoutDesc.rootDescriptorNum = 1;
        dsLayoutDesc.descriptorSets = dsSets;
        dsLayoutDesc.descriptorSetNum = 2;
        dsLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        if (!m_SSRDownsamplePipelineLayout.Init(dsLayoutDesc)) return false;

        // Forward reflection resolve inputs: Depth, SSR hit, SceneColor, EnvSpec, Resp, SSRColor0..3, Sampler.
        nri::RootDescriptorDesc applyRootDesc{};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;
        nri::DescriptorRangeDesc applyRanges[10]{};
        for (uint32_t i = 0; i < 10; ++i) {
            applyRanges[i].baseRegisterIndex = i < 9 ? i : 0;
            applyRanges[i].descriptorNum = 1;
            applyRanges[i].descriptorType = i < 9 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
            applyRanges[i].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        }

        nri::DescriptorSetDesc applyDescSet = {};
        applyDescSet.registerSpace = 1;
        applyDescSet.ranges = applyRanges;
        applyDescSet.rangeNum = 10;

        nri::PipelineLayoutDesc applyLayoutDesc = {};
        applyLayoutDesc.rootRegisterSpace = 0;
        applyLayoutDesc.rootDescriptors = &applyRootDesc;
        applyLayoutDesc.rootDescriptorNum = 1;
        applyLayoutDesc.descriptorSets = &applyDescSet;
        applyLayoutDesc.descriptorSetNum = 1;
        applyLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_SSRApplyPipelineLayout.Init(applyLayoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateSSRApplyPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_SSRApplyPipelineLayout;
        gfxDesc.Shaders = { m_SSRApplyVertexShader, m_SSRApplyPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateSSRPipeline(RHIDevice& device) {
        auto& cache = RHIPipelineCache::Get();
        RHIComputePipelineDesc compDesc;
        compDesc.Layout = &m_SSRComputePipelineLayout;
        compDesc.ComputeShader = m_SSRComputeShader;
        m_SSRComputePipeline = cache.GetOrCreateComputePipeline(compDesc);

        RHIComputePipelineDesc dsDesc;
        dsDesc.Layout = &m_SSRDownsamplePipelineLayout;
        dsDesc.ComputeShader = m_SSRDownsampleShader;
        m_SSRDownsamplePipeline = cache.GetOrCreateComputePipeline(dsDesc);

        m_SSRApplyPipelineSDR = GetOrCreateSSRApplyPipeline(nri::Format::RGBA8_UNORM);
        m_SSRApplyPipelineHDR = GetOrCreateSSRApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_SSRComputePipeline != nullptr && m_SSRDownsamplePipeline != nullptr &&
            m_SSRApplyPipelineSDR != nullptr && m_SSRApplyPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompilePlanarShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("PlanarReflection.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate PlanarReflection.hlsl!");
            return false;
        }

        std::string shaderSource;
        std::ifstream file(shaderPath);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            shaderSource = ss.str();
        }
        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read PlanarReflection.hlsl!");
            return false;
        }

        const RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan)
            ? RHIShaderFormat::SPIRV
            : RHIShaderFormat::DXIL;

        RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = PathToUtf8(shaderPath);
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = format;
        vsDesc.Optimization = true;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Planar VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_PlanarApplyVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Planar PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_PlanarApplyPixelShader = psRes.Bytecode;
        psDesc.EntryPoint = "PSCopyMain";
        const auto copyRes = compiler.Compile(psDesc);
        if (!copyRes.Success) return false;
        m_PlanarCopyPixelShader = copyRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreatePlanarPipelineLayout(RHIDevice& device) {
        (void)device;
        nri::RootDescriptorDesc applyRootDesc{};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;

        nri::DescriptorRangeDesc applyRanges[7]{};
        for (uint32_t i = 0; i < 7; ++i) {
            applyRanges[i].baseRegisterIndex = i < 6 ? i : 0;
            applyRanges[i].descriptorNum = 1;
            applyRanges[i].descriptorType = i < 6 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
            applyRanges[i].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        }

        nri::DescriptorSetDesc applyDescSet{};
        applyDescSet.registerSpace = 1;
        applyDescSet.ranges = applyRanges;
        applyDescSet.rangeNum = 7;

        nri::PipelineLayoutDesc applyLayoutDesc{};
        applyLayoutDesc.rootRegisterSpace = 0;
        applyLayoutDesc.rootDescriptors = &applyRootDesc;
        applyLayoutDesc.rootDescriptorNum = 1;
        applyLayoutDesc.descriptorSets = &applyDescSet;
        applyLayoutDesc.descriptorSetNum = 1;
        applyLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_PlanarApplyPipelineLayout.Init(applyLayoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreatePlanarApplyPipeline(nri::Format colorFormat, bool copyOnly) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PlanarApplyPipelineLayout;
        gfxDesc.Shaders = { m_PlanarApplyVertexShader, copyOnly ? m_PlanarCopyPixelShader : m_PlanarApplyPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        // Convex blend over current HDR scene (`baseLit + prevSpec`) ->
        // `baseLit + lerp(prevSpec, planarSpec, wPlanar)`.
        gfxDesc.ColorAttachments = { copyOnly ? RHIBlendAttachmentDesc::Opaque() : RHIBlendAttachmentDesc::AlphaBlend() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreatePlanarPipeline(RHIDevice& device) {
        (void)device;
        m_PlanarApplyPipelineSDR = GetOrCreatePlanarApplyPipeline(nri::Format::RGBA8_UNORM);
        m_PlanarApplyPipelineHDR = GetOrCreatePlanarApplyPipeline(nri::Format::RGBA16_SFLOAT);
        return m_PlanarApplyPipelineSDR != nullptr && m_PlanarApplyPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileHiZShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("HiZ.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath);
        std::stringstream sourceStream;
        sourceStream << file.rdbuf();
        const auto source = sourceStream.str();
        if (source.empty()) return false;

        RHIShaderCompileDesc desc;
        desc.Source = source;
        desc.SourcePath = PathToUtf8(shaderPath);
        desc.EntryPoint = "CSMain";
        desc.Stage = RHIShaderStage::Compute;
        desc.HLSLVersion = "2021";
        desc.ShaderModel = "6_0";
        desc.Format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        desc.Optimization = true;
        const auto result = RHIShaderCompiler::Get().Compile(desc);
        if (!result.Success) return false;
        m_HiZComputeShader = result.Bytecode;

        RHIShaderCompileDesc visVsDesc = desc;
        visVsDesc.EntryPoint = "VisualizeVSMain";
        visVsDesc.Stage = RHIShaderStage::Vertex;
        const auto visVsRes = RHIShaderCompiler::Get().Compile(visVsDesc);
        if (!visVsRes.Success) return false;
        m_HiZVisualizeVertexShader = visVsRes.Bytecode;

        RHIShaderCompileDesc visPsDesc = desc;
        visPsDesc.EntryPoint = "VisualizePSMain";
        visPsDesc.Stage = RHIShaderStage::Fragment;
        const auto visPsRes = RHIShaderCompiler::Get().Compile(visPsDesc);
        if (!visPsRes.Success) return false;
        m_HiZVisualizePixelShader = visPsRes.Bytecode;
        return true;
    }

    bool RHISceneRenderer::CreateHiZPipelineLayout(RHIDevice& device) {
        (void)device;
        nri::RootDescriptorDesc root{};
        root.registerIndex = 0;
        root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc textureRanges[2]{};
        textureRanges[0].baseRegisterIndex = 0;
        textureRanges[0].descriptorNum = 1;
        textureRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        textureRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        textureRanges[1].baseRegisterIndex = 0;
        textureRanges[1].descriptorNum = 1;
        textureRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        textureRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorRangeDesc storageRange{};
        storageRange.baseRegisterIndex = 0;
        storageRange.descriptorNum = 1;
        storageRange.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        storageRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        storageRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc sets[2]{};
        sets[0].registerSpace = 1;
        sets[0].ranges = textureRanges;
        sets[0].rangeNum = 2;
        sets[1].registerSpace = 2;
        sets[1].ranges = &storageRange;
        sets[1].rangeNum = 1;

        nri::PipelineLayoutDesc layout{};
        layout.rootRegisterSpace = 0;
        layout.rootDescriptors = &root;
        layout.rootDescriptorNum = 1;
        layout.descriptorSets = sets;
        layout.descriptorSetNum = 2;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        if (!m_HiZPipelineLayout.Init(layout)) return false;

        nri::RootDescriptorDesc visRoot{};
        visRoot.registerIndex = 0;
        visRoot.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        visRoot.shaderStages = nri::StageBits::ALL;

        nri::DescriptorRangeDesc visTexRange{};
        visTexRange.baseRegisterIndex = 0;
        visTexRange.descriptorNum = 1;
        visTexRange.descriptorType = nri::DescriptorType::TEXTURE;
        visTexRange.shaderStages = nri::StageBits::FRAGMENT_SHADER;
        visTexRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc visSet{};
        visSet.registerSpace = 1;
        visSet.ranges = &visTexRange;
        visSet.rangeNum = 1;

        nri::PipelineLayoutDesc visLayout{};
        visLayout.rootRegisterSpace = 0;
        visLayout.rootDescriptors = &visRoot;
        visLayout.rootDescriptorNum = 1;
        visLayout.descriptorSets = &visSet;
        visLayout.descriptorSetNum = 1;
        visLayout.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
        return m_HiZVisualizePipelineLayout.Init(visLayout);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateHiZVisualizePipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_HiZVisualizePipelineLayout;
        gfxDesc.Shaders = { m_HiZVisualizeVertexShader, m_HiZVisualizePixelShader };
        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;
        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;
        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};
        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateHiZPipeline(RHIDevice& device) {
        (void)device;
        RHIComputePipelineDesc desc;
        desc.Layout = &m_HiZPipelineLayout;
        desc.ComputeShader = m_HiZComputeShader;
        m_HiZPipeline = RHIPipelineCache::Get().GetOrCreateComputePipeline(desc);
        m_HiZVisualizePipelineSDR = GetOrCreateHiZVisualizePipeline(nri::Format::RGBA8_UNORM);
        m_HiZVisualizePipelineHDR = GetOrCreateHiZVisualizePipeline(nri::Format::RGBA16_SFLOAT);
        return m_HiZPipeline != nullptr && m_HiZVisualizePipelineSDR != nullptr && m_HiZVisualizePipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileCubemapCaptureShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("CubemapCapture.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath);
        std::stringstream sourceStream;
        sourceStream << file.rdbuf();
        const auto source = sourceStream.str();
        if (source.empty()) return false;

        RHIShaderCompileDesc desc;
        desc.Source = source;
        desc.SourcePath = PathToUtf8(shaderPath);
        desc.EntryPoint = "OrientFaceCS";
        desc.Stage = RHIShaderStage::Compute;
        desc.HLSLVersion = "2021";
        desc.ShaderModel = "6_0";
        desc.Format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        desc.Optimization = true;
        const auto result = RHIShaderCompiler::Get().Compile(desc);
        if (!result.Success) return false;
        m_CaptureOrientationComputeShader = result.Bytecode;
        return true;
    }

    bool RHISceneRenderer::CreateCubemapCapturePipelineLayout(RHIDevice& device) {
        (void)device;
        nri::RootDescriptorDesc root{};
        root.registerIndex = 0;
        root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc textureRange{};
        textureRange.baseRegisterIndex = 0;
        textureRange.descriptorNum = 1;
        textureRange.descriptorType = nri::DescriptorType::TEXTURE;
        textureRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorRangeDesc storageRange{};
        storageRange.baseRegisterIndex = 0;
        storageRange.descriptorNum = 1;
        storageRange.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        storageRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        storageRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc sets[2]{};
        sets[0].registerSpace = 1;
        sets[0].ranges = &textureRange;
        sets[0].rangeNum = 1;
        sets[1].registerSpace = 2;
        sets[1].ranges = &storageRange;
        sets[1].rangeNum = 1;

        nri::PipelineLayoutDesc layout{};
        layout.rootRegisterSpace = 0;
        layout.rootDescriptors = &root;
        layout.rootDescriptorNum = 1;
        layout.descriptorSets = sets;
        layout.descriptorSetNum = 2;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        return m_CaptureOrientationPipelineLayout.Init(layout);
    }

    bool RHISceneRenderer::CreateCubemapCapturePipeline(RHIDevice& device) {
        (void)device;
        RHIComputePipelineDesc desc;
        desc.Layout = &m_CaptureOrientationPipelineLayout;
        desc.ComputeShader = m_CaptureOrientationComputeShader;
        m_CaptureOrientationPipeline = RHIPipelineCache::Get().GetOrCreateComputePipeline(desc);
        return m_CaptureOrientationPipeline != nullptr;
    }

    bool RHISceneRenderer::CompileCubemapPrefilterShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("CubemapPrefilter.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath);
        std::stringstream sourceStream;
        sourceStream << file.rdbuf();
        const auto source = sourceStream.str();
        if (source.empty()) return false;

        RHIShaderCompileDesc downDesc;
        downDesc.Source = source;
        downDesc.SourcePath = PathToUtf8(shaderPath);
        downDesc.EntryPoint = "DownsampleCubeCS";
        downDesc.Stage = RHIShaderStage::Compute;
        downDesc.HLSLVersion = "2021";
        downDesc.ShaderModel = "6_0";
        downDesc.Format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        downDesc.Optimization = true;
        const auto downResult = RHIShaderCompiler::Get().Compile(downDesc);
        if (!downResult.Success) return false;
        m_CaptureDownsampleComputeShader = downResult.Bytecode;

        RHIShaderCompileDesc preDesc = downDesc;
        preDesc.EntryPoint = "PrefilterCubeCS";
        const auto preResult = RHIShaderCompiler::Get().Compile(preDesc);
        if (!preResult.Success) return false;
        m_CapturePrefilterComputeShader = preResult.Bytecode;
        return true;
    }

    bool RHISceneRenderer::CreateCubemapPrefilterPipelineLayout(RHIDevice& device) {
        (void)device;
        nri::RootDescriptorDesc root{};
        root.registerIndex = 0;
        root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc textureRanges[2]{};
        textureRanges[0].baseRegisterIndex = 0;
        textureRanges[0].descriptorNum = 1;
        textureRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        textureRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        textureRanges[1].baseRegisterIndex = 0;
        textureRanges[1].descriptorNum = 1;
        textureRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        textureRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorRangeDesc storageRange{};
        storageRange.baseRegisterIndex = 0;
        storageRange.descriptorNum = 1;
        storageRange.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        storageRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        storageRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc sets[2]{};
        sets[0].registerSpace = 1;
        sets[0].ranges = textureRanges;
        sets[0].rangeNum = 2;
        sets[1].registerSpace = 2;
        sets[1].ranges = &storageRange;
        sets[1].rangeNum = 1;

        nri::PipelineLayoutDesc layout{};
        layout.rootRegisterSpace = 0;
        layout.rootDescriptors = &root;
        layout.rootDescriptorNum = 1;
        layout.descriptorSets = sets;
        layout.descriptorSetNum = 2;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        return m_CapturePrefilterPipelineLayout.Init(layout);
    }

    bool RHISceneRenderer::CreateCubemapPrefilterPipelines(RHIDevice& device) {
        (void)device;
        auto& cache = RHIPipelineCache::Get();
        RHIComputePipelineDesc downDesc;
        downDesc.Layout = &m_CapturePrefilterPipelineLayout;
        downDesc.ComputeShader = m_CaptureDownsampleComputeShader;
        m_CaptureDownsamplePipeline = cache.GetOrCreateComputePipeline(downDesc);

        RHIComputePipelineDesc preDesc;
        preDesc.Layout = &m_CapturePrefilterPipelineLayout;
        preDesc.ComputeShader = m_CapturePrefilterComputeShader;
        m_CapturePrefilterPipeline = cache.GetOrCreateComputePipeline(preDesc);
        return m_CaptureDownsamplePipeline != nullptr && m_CapturePrefilterPipeline != nullptr;
    }

    bool RHISceneRenderer::PrefilterCapturedCubemap(
        RHITexture& rawCubemap,
        RHITexture& prefilteredCubemap,
        uint32_t resolution,
        uint32_t mipCount,
        std::string* outDiagnostic) {
        static_assert(MaxCapturePrefilterMips == ReflectionCaptureContract::MaxPrefilterMipCount,
            "MaxCapturePrefilterMips must match ReflectionCaptureContract::MaxPrefilterMipCount");
        if (outDiagnostic) outDiagnostic->clear();
        if (!m_Device || !m_Device->CanSubmit() ||
            !rawCubemap.IsValid() || !prefilteredCubemap.IsValid() ||
            !m_CaptureDownsamplePipeline || !m_CapturePrefilterPipeline ||
            !m_CapturePrefilterSampler.IsValid() ||
            mipCount == 0 || mipCount > ReflectionCaptureContract::MaxPrefilterMipCount ||
            rawCubemap.GetMipCount() < mipCount || prefilteredCubemap.GetMipCount() < mipCount) {
            if (outDiagnostic) *outDiagnostic = "prefilter pipeline or target cubemap prerequisites invalid";
            return false;
        }

        RHICommandBuffer commands;
        if (!commands.Init(m_Device->GetGraphicsQueue()) || !commands.Begin()) {
            if (outDiagnostic) *outDiagnostic = "failed to begin cubemap prefilter command buffer";
            return false;
        }

        commands.KeepAlive(rawCubemap.AcquireLease());
        commands.KeepAlive(prefilteredCubemap.AcquireLease());
        commands.KeepAlive(m_CapturePrefilterPipelineLayout.AcquireLease());
        commands.KeepAlive(m_CapturePrefilterSampler.AcquireLease());
        commands.KeepAlive(m_CapturePrefilterDescriptorPool.AcquireLease());

        // Step 1: Generate source cubemap mip chain in 3D cubemap space so GGX importance
        // sampling can read anti-aliased solid-angle levels across all 6 faces seamlessly.
        for (uint32_t mip = 1u; mip < mipCount; ++mip) {
            const uint32_t dstSize = std::max(1u, resolution >> mip);
            nri::Descriptor* srcMipSRV = rawCubemap.GetMipSRV(mip - 1u);
            nri::Descriptor* dstMipUAV = rawCubemap.GetMipUAV(mip);
            auto* texSet = m_CaptureDownsampleTextureSets[mip];
            auto* storageSet = m_CaptureDownsampleStorageSets[mip];
            auto& cb = m_CapturePrefilterConstantBuffers[mip];
            if (!srcMipSRV || !dstMipUAV || !texSet || !storageSet || !cb.IsValid()) {
                if (outDiagnostic) *outDiagnostic = "missing downsample per-mip views or descriptor set";
                return false;
            }

            RHICubemapPrefilterConstants constants{};
            constants.Dims = glm::uvec4(dstSize, resolution, mip, mipCount);
            constants.Params = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            if (!cb.UpdateData(&constants, sizeof(constants)) ||
                !m_CapturePrefilterDescriptorPool.UpdateTexture(texSet, 0, 0, srcMipSRV) ||
                !m_CapturePrefilterDescriptorPool.UpdateStorageTexture(storageSet, 0, 0, dstMipUAV)) {
                if (outDiagnostic) *outDiagnostic = "failed to update downsample descriptor or constants";
                return false;
            }

            RHITextureBarrier toStorage(rawCubemap.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResourceStorage);
            toStorage.AfterStage = RHIPipelineStage::ComputeShader;
            toStorage.MipOffset = static_cast<uint16_t>(mip);
            toStorage.MipNum = 1u;
            toStorage.LayerOffset = 0u;
            toStorage.LayerNum = ReflectionCaptureContract::FaceCount;
            toStorage.Planes = nri::PlaneBits::COLOR;
            RHIBarrierGroup beginGroup;
            beginGroup.AddTexture(toStorage);
            commands.PipelineBarrier(beginGroup);

            commands.KeepAlive(cb.AcquireLease());
            commands.SetPipeline(m_CaptureDownsamplePipeline);
            commands.SetPipelineLayout(nri::BindPoint::COMPUTE, m_CapturePrefilterPipelineLayout);
            commands.SetRootDescriptor(0, cb.GetCBV(), 0, nri::BindPoint::COMPUTE);
            m_CapturePrefilterDescriptorPool.Bind(commands.GetNative());
            RHIDescriptorPool::BindDescriptorSet(commands.GetNative(), 0, texSet, nri::BindPoint::COMPUTE);
            RHIDescriptorPool::BindDescriptorSet(commands.GetNative(), 1, storageSet, nri::BindPoint::COMPUTE);
            const uint32_t groups = (dstSize + 7u) / 8u;
            commands.Dispatch(groups, groups, ReflectionCaptureContract::FaceCount);

            RHITextureBarrier toSRV(rawCubemap.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
            toSRV.AfterStage = RHIPipelineStage::ComputeShader;
            toSRV.MipOffset = static_cast<uint16_t>(mip);
            toSRV.MipNum = 1u;
            toSRV.LayerOffset = 0u;
            toSRV.LayerNum = ReflectionCaptureContract::FaceCount;
            toSRV.Planes = nri::PlaneBits::COLOR;
            RHIBarrierGroup endGroup;
            endGroup.AddTexture(toSRV);
            commands.PipelineBarrier(endGroup);
            if (!commands.IsRecording()) {
                if (outDiagnostic) *outDiagnostic = "downsample command recording failed";
                return false;
            }
        }

        // Step 2: GGX importance-sampled roughness prefilter across every mip level and all 6 faces.
        {
            RHITextureBarrier prefilterWrite(prefilteredCubemap.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResourceStorage);
            prefilterWrite.AfterStage = RHIPipelineStage::ComputeShader;
            prefilterWrite.MipOffset = 0u;
            prefilterWrite.MipNum = static_cast<uint16_t>(mipCount);
            prefilterWrite.LayerOffset = 0u;
            prefilterWrite.LayerNum = ReflectionCaptureContract::FaceCount;
            prefilterWrite.Planes = nri::PlaneBits::COLOR;
            RHIBarrierGroup writeGroup;
            writeGroup.AddTexture(prefilterWrite);
            commands.PipelineBarrier(writeGroup);
        }

        nri::Descriptor* rawCubeSRV = rawCubemap.GetSRV();
        if (!rawCubeSRV) {
            if (outDiagnostic) *outDiagnostic = "source cubemap SRV missing";
            return false;
        }
        for (uint32_t mip = 0u; mip < mipCount; ++mip) {
            const uint32_t dstSize = std::max(1u, resolution >> mip);
            const float roughness = ReflectionCaptureContract::ComputePrefilterRoughness(mip, mipCount);
            nri::Descriptor* dstMipUAV = prefilteredCubemap.GetMipUAV(mip);
            auto* texSet = m_CapturePrefilterTextureSets[mip];
            auto* storageSet = m_CapturePrefilterStorageSets[mip];
            auto& cb = m_CapturePrefilterConstantBuffers[ReflectionCaptureContract::MaxPrefilterMipCount + mip];
            if (!dstMipUAV || !texSet || !storageSet || !cb.IsValid()) {
                if (outDiagnostic) *outDiagnostic = "missing prefilter per-mip UAV or descriptor set";
                return false;
            }

            RHICubemapPrefilterConstants constants{};
            constants.Dims = glm::uvec4(dstSize, resolution, mip, mipCount);
            constants.Params = glm::vec4(
                roughness,
                static_cast<float>(ReflectionCaptureContract::DefaultPrefilterSampleCount),
                0.0f,
                0.0f);
            if (!cb.UpdateData(&constants, sizeof(constants)) ||
                !m_CapturePrefilterDescriptorPool.UpdateTexture(texSet, 0, 0, rawCubeSRV) ||
                !m_CapturePrefilterDescriptorPool.UpdateStorageTexture(storageSet, 0, 0, dstMipUAV)) {
                if (outDiagnostic) *outDiagnostic = "failed to update prefilter descriptor or constants";
                return false;
            }

            commands.KeepAlive(cb.AcquireLease());
            commands.SetPipeline(m_CapturePrefilterPipeline);
            commands.SetPipelineLayout(nri::BindPoint::COMPUTE, m_CapturePrefilterPipelineLayout);
            commands.SetRootDescriptor(0, cb.GetCBV(), 0, nri::BindPoint::COMPUTE);
            m_CapturePrefilterDescriptorPool.Bind(commands.GetNative());
            RHIDescriptorPool::BindDescriptorSet(commands.GetNative(), 0, texSet, nri::BindPoint::COMPUTE);
            RHIDescriptorPool::BindDescriptorSet(commands.GetNative(), 1, storageSet, nri::BindPoint::COMPUTE);
            const uint32_t groups = (dstSize + 7u) / 8u;
            commands.Dispatch(groups, groups, ReflectionCaptureContract::FaceCount);
            if (!commands.IsRecording()) {
                if (outDiagnostic) *outDiagnostic = "prefilter dispatch recording failed";
                return false;
            }
        }

        {
            RHITextureBarrier prefilterRead(prefilteredCubemap.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
            prefilterRead.MipOffset = 0u;
            prefilterRead.MipNum = static_cast<uint16_t>(mipCount);
            prefilterRead.LayerOffset = 0u;
            prefilterRead.LayerNum = ReflectionCaptureContract::FaceCount;
            prefilterRead.Planes = nri::PlaneBits::COLOR;
            RHIBarrierGroup readGroup;
            readGroup.AddTexture(prefilterRead);
            commands.PipelineBarrier(readGroup);
        }

        if (!commands.End() || !commands.Submit()) {
            if (outDiagnostic) *outDiagnostic = "cubemap prefilter command submit failed";
            return false;
        }
        m_CaptureLastCompletion = m_Device->GetLastSubmittedToken();
        return static_cast<bool>(m_CaptureLastCompletion);
    }

    bool RHISceneRenderer::CompileFogShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("VolumetricFog.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate VolumetricFog.hlsl!");
            return false;
        }

        std::string shaderSource;
        std::ifstream file(shaderPath);
        if (file.is_open()) {
            std::stringstream ss; ss << file.rdbuf(); shaderSource = ss.str();
        }
        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read VolumetricFog.hlsl!");
            return false;
        }

        RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        auto compileEntry = [&](const char* entryPoint, RHIShaderStage stage, RHI::RHIShaderBytecode& outBytecode) -> bool {
            RHIShaderCompileDesc desc;
            desc.Source = shaderSource;
            desc.SourcePath = PathToUtf8(shaderPath);
            desc.EntryPoint = entryPoint;
            desc.Stage = stage;
            desc.HLSLVersion = "2021";
            desc.ShaderModel = "6_0";
            desc.Format = format;
            desc.Optimization = true;
            if (!m_FogShadowSampler.IsValid())
                desc.Macros.push_back({"AXLE_FOG_SHADOWS_DISABLED", "1"});
            auto res = compiler.Compile(desc);
            if (!res.Success) {
                AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile VolumetricFog {0}: {1}", entryPoint, res.ErrorMessage);
                return false;
            }
            outBytecode = res.Bytecode;
            return true;
        };

        if (!compileEntry("CSInjectMedia", RHIShaderStage::Compute, m_FogInjectShader) ||
            !compileEntry("CSIntegrate", RHIShaderStage::Compute, m_FogIntegrateShader) ||
            !compileEntry("VSMain", RHIShaderStage::Vertex, m_FogApplyVertexShader) ||
            !compileEntry("PSMain", RHIShaderStage::Fragment, m_FogApplyPixelShader)) return false;
        compileEntry("CSReproject", RHIShaderStage::Compute, m_FogReprojectShader);
        return true;
    }

    bool RHISceneRenderer::CreateFogPipelineLayout(RHIDevice& device) {
        auto makeRange = [](uint32_t baseRegister, nri::DescriptorType type, nri::StageBits stages) {
            nri::DescriptorRangeDesc range = {};
            range.baseRegisterIndex = baseRegister;
            range.descriptorNum = 1;
            range.descriptorType = type;
            range.shaderStages = stages;
            range.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
            return range;
        };

        // Shared root descriptor b0, space 0.
        nri::RootDescriptorDesc compRootDesc = {};
        compRootDesc.registerIndex = 0;
        compRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        compRootDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        // 1. Inject Media Layout:
        // Root Descriptors (space 0): b0 (FogConstants), b2 (MultiLightBuffer), b4 (CascadeReceiver), b5 (LocalShadowReceiver)
        auto makeRootCBV = [](uint32_t registerIndex) {
            nri::RootDescriptorDesc desc = {};
            desc.registerIndex = registerIndex;
            desc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
            desc.shaderStages = nri::StageBits::COMPUTE_SHADER;
            return desc;
        };
        nri::RootDescriptorDesc injectRoots[] = {
            makeRootCBV(0),
            makeRootCBV(ConstantBufferContract::LightConstantsRegister),
            makeRootCBV(4),
            makeRootCBV(5),
        };
        // Set 0 (space 1): t4 (CascadeDepth), t5 (LocalShadowDepth), s1 (ShadowSampler)
        nri::DescriptorRangeDesc injectTexRanges[] = {
            makeRange(4, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER),
            makeRange(5, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER),
            makeRange(1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER),
        };
        // Set 1 (space 2): u0 (FroxelScattering UAV)
        nri::DescriptorRangeDesc injectStorageRange = makeRange(0, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER);
        nri::DescriptorSetDesc injectDescSets[2] = {};
        injectDescSets[0].registerSpace = 1;
        injectDescSets[0].ranges = injectTexRanges;
        injectDescSets[0].rangeNum = std::size(injectTexRanges);
        injectDescSets[1].registerSpace = 2;
        injectDescSets[1].ranges = &injectStorageRange;
        injectDescSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc injectLayoutDesc = {};
        const bool injectShadows = m_FogShadowSampler.IsValid();
        injectLayoutDesc.rootRegisterSpace = 0;
        injectLayoutDesc.rootDescriptors = injectRoots;
        injectLayoutDesc.rootDescriptorNum = injectShadows ? std::size(injectRoots) : 2;
        injectLayoutDesc.descriptorSets = injectShadows ? injectDescSets : &injectDescSets[1];
        injectLayoutDesc.descriptorSetNum = injectShadows ? std::size(injectDescSets) : 1;
        injectLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        if (!m_FogInjectPipelineLayout.Init(injectLayoutDesc)) return false;

        // 2. Integrate Layout:
        // Set 0 (space 1): t2 (FroxelScattering SRV)
        // Set 1 (space 2): u0 (FroxelIntegrated UAV)
        nri::DescriptorRangeDesc integrateTexRange = makeRange(2, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER);
        nri::DescriptorRangeDesc integrateStorageRange = makeRange(0, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER);
        nri::DescriptorSetDesc integrateDescSets[2] = {};
        integrateDescSets[0].registerSpace = 1;
        integrateDescSets[0].ranges = &integrateTexRange;
        integrateDescSets[0].rangeNum = 1;
        integrateDescSets[1].registerSpace = 2;
        integrateDescSets[1].ranges = &integrateStorageRange;
        integrateDescSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc integrateLayoutDesc = {};
        integrateLayoutDesc.rootRegisterSpace = 0;
        integrateLayoutDesc.rootDescriptors = &compRootDesc;
        integrateLayoutDesc.rootDescriptorNum = 1;
        integrateLayoutDesc.descriptorSets = integrateDescSets;
        integrateLayoutDesc.descriptorSetNum = 2;
        integrateLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        if (!m_FogIntegratePipelineLayout.Init(integrateLayoutDesc)) return false;

        // Reproject: current scattering and previous history (space 1), next history (space 2).
        nri::DescriptorRangeDesc reprojectTexRanges[] = {
            integrateTexRange,
            makeRange(3, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER),
            makeRange(0, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER),
        };
        nri::DescriptorSetDesc reprojectSets[] = {integrateDescSets[0], integrateDescSets[1]};
        reprojectSets[0].ranges = reprojectTexRanges;
        reprojectSets[0].rangeNum = std::size(reprojectTexRanges);
        auto reprojectLayout = integrateLayoutDesc;
        reprojectLayout.descriptorSets = reprojectSets;
        if (m_FogReprojectShader.IsValid()) {
            ScopedRHIOperation operation("FogReprojectLayout");
            m_FogReprojectPipelineLayout.Init(reprojectLayout);
        }

        // 3. Apply Pipeline Layout:
        // Root descriptor b0, space 0.
        nri::RootDescriptorDesc applyRootDesc = {};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;

        // Set 0 (space 1):
        // Range 0: t0 (Scene Color)
        // Range 1: t1 (Scene Depth)
        // Range 2: t2 (Froxel Integrated 3D Volume)
        // Range 3: t3 (Froxel Scattering 3D Volume)
        // Range 4: s0 (Linear Sampler)
        nri::DescriptorRangeDesc applyRanges[] = {
            makeRange(0, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER),
            makeRange(1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER),
            makeRange(2, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER),
            makeRange(3, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER),
            makeRange(0, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER),
        };

        nri::DescriptorSetDesc applyDescSet = {};
        applyDescSet.registerSpace = 1;
        applyDescSet.ranges = applyRanges;
        applyDescSet.rangeNum = std::size(applyRanges);

        nri::PipelineLayoutDesc applyLayoutDesc = {};
        applyLayoutDesc.rootRegisterSpace = 0;
        applyLayoutDesc.rootDescriptors = &applyRootDesc;
        applyLayoutDesc.rootDescriptorNum = 1;
        applyLayoutDesc.descriptorSets = &applyDescSet;
        applyLayoutDesc.descriptorSetNum = 1;
        applyLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_FogApplyPipelineLayout.Init(applyLayoutDesc);
    }

    bool RHISceneRenderer::EnsureFogDescriptorSets() {
        if (!m_FogSampler.IsValid()) {
            return false;
        }
        const bool injectShadows = m_FogShadowSampler.IsValid();
        auto& resources = RHIDevice::Get().GetResources();
        const bool hasAllSets = (!injectShadows || m_FogInjectTextureSet) && m_FogInjectStorageSet &&
                                (!m_FogReprojectPipeline || (m_FogReprojectTextureSet && m_FogReprojectStorageSet)) &&
                                m_FogIntegrateTextureSet && m_FogIntegrateStorageSet && m_FogApplyDescriptorSet;
        if (hasAllSets && m_FogDescriptorPool.IsValid()) {
            bool anyInUse = false;
            for (nri::DescriptorSet* set : { m_FogInjectTextureSet, m_FogInjectStorageSet,
                                             m_FogReprojectTextureSet, m_FogReprojectStorageSet,
                                             m_FogIntegrateTextureSet, m_FogIntegrateStorageSet, m_FogApplyDescriptorSet }) {
                if (!set) continue;
                auto lease = resources.AcquireNative(set);
                if (!lease.IsLive() || resources.IsInUse(lease)) {
                    anyInUse = true;
                    break;
                }
            }
            if (!anyInUse) {
                return true;
            }
        }

        RHIDescriptorPoolDesc fogPoolDesc;
        fogPoolDesc.DebugName = "FogDescriptorPool";
        fogPoolDesc.MaxDescriptorSets = 16;
        fogPoolDesc.MaxTextures = 64;
        fogPoolDesc.MaxSamplers = 16;
        fogPoolDesc.MaxStorageTextures = 16;
        fogPoolDesc.MaxConstantBuffers = 16;
        RHIDescriptorPool candidate;
        if (!candidate.Init(fogPoolDesc)) return false;
        auto* injectTexture = injectShadows ? candidate.AllocateDescriptorSet(m_FogInjectPipelineLayout, 0) : nullptr;
        auto* injectStorage = candidate.AllocateDescriptorSet(m_FogInjectPipelineLayout, injectShadows ? 1 : 0);
        auto* integrateInput = candidate.AllocateDescriptorSet(m_FogIntegratePipelineLayout, 0);
        auto* integrateOutput = candidate.AllocateDescriptorSet(m_FogIntegratePipelineLayout, 1);
        auto* apply = candidate.AllocateDescriptorSet(m_FogApplyPipelineLayout, 0);
        if (!injectStorage || !integrateInput || !integrateOutput || !apply ||
            (injectShadows && (!injectTexture ||
                !candidate.UpdateSampler(injectTexture, 2, 0, m_FogShadowSampler.GetNative()))) ||
            !candidate.UpdateSampler(apply, 4, 0, m_FogSampler.GetNative())) {
            return false;
        }
        nri::DescriptorSet* reprojectInput = nullptr;
        nri::DescriptorSet* reprojectOutput = nullptr;
        if (m_FogReprojectPipeline) {
            reprojectInput = candidate.AllocateDescriptorSet(m_FogReprojectPipelineLayout, 0);
            reprojectOutput = candidate.AllocateDescriptorSet(m_FogReprojectPipelineLayout, 1);
            if (!reprojectInput || !reprojectOutput ||
                !candidate.UpdateSampler(reprojectInput, 2, 0, m_FogSampler.GetNative())) {
                reprojectInput = nullptr;
                reprojectOutput = nullptr;
            }
        }
        m_FogDescriptorPool = std::move(candidate);
        m_FogInjectTextureSet = injectTexture;
        m_FogInjectStorageSet = injectStorage;
        m_FogReprojectTextureSet = reprojectInput;
        m_FogReprojectStorageSet = reprojectOutput;
        m_FogIntegrateTextureSet = integrateInput;
        m_FogIntegrateStorageSet = integrateOutput;
        m_FogApplyDescriptorSet = apply;
        return true;
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateFogApplyPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_FogApplyPipelineLayout;
        gfxDesc.Shaders = { m_FogApplyVertexShader, m_FogApplyPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateFogPipeline(RHIDevice& device) {
        auto& cache = RHIPipelineCache::Get();

        RHIComputePipelineDesc injectDesc;
        injectDesc.Layout = &m_FogInjectPipelineLayout;
        injectDesc.ComputeShader = m_FogInjectShader;
        m_FogInjectPipeline = cache.GetOrCreateComputePipeline(injectDesc);

        if (m_FogReprojectPipelineLayout.IsValid()) {
            RHIComputePipelineDesc reprojectDesc;
            reprojectDesc.Layout = &m_FogReprojectPipelineLayout;
            reprojectDesc.ComputeShader = m_FogReprojectShader;
            m_FogReprojectPipeline = cache.GetOrCreateComputePipeline(reprojectDesc);
        }

        RHIComputePipelineDesc integrateDesc;
        integrateDesc.Layout = &m_FogIntegratePipelineLayout;
        integrateDesc.ComputeShader = m_FogIntegrateShader;
        m_FogIntegratePipeline = cache.GetOrCreateComputePipeline(integrateDesc);

        m_FogApplyPipelineSDR = GetOrCreateFogApplyPipeline(nri::Format::RGBA8_UNORM);
        m_FogApplyPipelineHDR = GetOrCreateFogApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_FogInjectPipeline != nullptr &&
               m_FogIntegratePipeline != nullptr &&
               m_FogApplyPipelineSDR != nullptr &&
               m_FogApplyPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileBloomShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("Bloom.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate Bloom.hlsl!");
            return false;
        }

        std::string shaderSource;
        std::ifstream file(shaderPath);
        if (file.is_open()) {
            std::stringstream ss; ss << file.rdbuf(); shaderSource = ss.str();
        }
        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read Bloom.hlsl!");
            return false;
        }

        RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        RHIShaderCompileDesc csDesc;
        csDesc.SourcePath = shaderPath.string();
        csDesc.Source = shaderSource;
        csDesc.EntryPoint = "CSMain";
        csDesc.Stage = RHIShaderStage::Compute;
        csDesc.HLSLVersion = "2021";
        csDesc.ShaderModel = "6_0";
        csDesc.Format = format;
        csDesc.Optimization = true;
        auto csRes = compiler.Compile(csDesc);
        if (!csRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Bloom CS: {0}", csRes.ErrorMessage);
            return false;
        }
        m_BloomComputeShader = csRes.Bytecode;

        RHIShaderCompileDesc vsDesc = csDesc;
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Bloom VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_BloomApplyVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc upsampleDesc = csDesc;
        upsampleDesc.EntryPoint = "PSUpsample";
        upsampleDesc.Stage = RHIShaderStage::Fragment;
        auto upsampleRes = compiler.Compile(upsampleDesc);
        if (!upsampleRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Bloom Upsample PS: {0}", upsampleRes.ErrorMessage);
            return false;
        }
        m_BloomUpsamplePixelShader = upsampleRes.Bytecode;

        RHIShaderCompileDesc psDesc = csDesc;
        psDesc.EntryPoint = "PSApply";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Bloom PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_BloomApplyPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateBloomPipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc compRootDesc = {};
        compRootDesc.registerIndex = 0;
        compRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        compRootDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        nri::DescriptorRangeDesc compTexRanges[2] = {};
        compTexRanges[0].baseRegisterIndex = 0;
        compTexRanges[0].descriptorNum = 1;
        compTexRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        compTexRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        compTexRanges[1].baseRegisterIndex = 0;
        compTexRanges[1].descriptorNum = 1;
        compTexRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        compTexRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compTexRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorRangeDesc compStorageRanges[1] = {};
        compStorageRanges[0].baseRegisterIndex = 0;
        compStorageRanges[0].descriptorNum = 1;
        compStorageRanges[0].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        compStorageRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        compStorageRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc compDescSets[2] = {};
        compDescSets[0].registerSpace = 1;
        compDescSets[0].ranges = compTexRanges;
        compDescSets[0].rangeNum = 2;

        compDescSets[1].registerSpace = 2;
        compDescSets[1].ranges = compStorageRanges;
        compDescSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc compLayoutDesc = {};
        compLayoutDesc.rootRegisterSpace = 0;
        compLayoutDesc.rootDescriptors = &compRootDesc;
        compLayoutDesc.rootDescriptorNum = 1;
        compLayoutDesc.descriptorSets = compDescSets;
        compLayoutDesc.descriptorSetNum = 2;
        compLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        if (!m_BloomComputePipelineLayout.Init(compLayoutDesc)) return false;

        nri::RootDescriptorDesc applyRootDesc = {};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;

        nri::DescriptorRangeDesc applyRanges[3] = {};
        applyRanges[0].baseRegisterIndex = 0;
        applyRanges[0].descriptorNum = 1;
        applyRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        applyRanges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        applyRanges[1].baseRegisterIndex = 1;
        applyRanges[1].descriptorNum = 1;
        applyRanges[1].descriptorType = nri::DescriptorType::TEXTURE;
        applyRanges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        applyRanges[2].baseRegisterIndex = 0;
        applyRanges[2].descriptorNum = 1;
        applyRanges[2].descriptorType = nri::DescriptorType::SAMPLER;
        applyRanges[2].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc applyDescSet = {};
        applyDescSet.registerSpace = 1;
        applyDescSet.ranges = applyRanges;
        applyDescSet.rangeNum = 3;

        nri::PipelineLayoutDesc applyLayoutDesc = {};
        applyLayoutDesc.rootRegisterSpace = 0;
        applyLayoutDesc.rootDescriptors = &applyRootDesc;
        applyLayoutDesc.rootDescriptorNum = 1;
        applyLayoutDesc.descriptorSets = &applyDescSet;
        applyLayoutDesc.descriptorSetNum = 1;
        applyLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_BloomApplyPipelineLayout.Init(applyLayoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateBloomApplyPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_BloomApplyPipelineLayout;
        gfxDesc.Shaders = { m_BloomApplyVertexShader, m_BloomApplyPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateBloomPipeline(RHIDevice& device) {
        auto& cache = RHIPipelineCache::Get();
        RHIComputePipelineDesc compDesc;
        compDesc.Layout = &m_BloomComputePipelineLayout;
        compDesc.ComputeShader = m_BloomComputeShader;
        m_BloomComputePipeline = cache.GetOrCreateComputePipeline(compDesc);

        auto createUpsample = [&](nri::Format colorFormat) -> nri::Pipeline* {
            RHIGraphicsPipelineDesc gfxDesc;
            gfxDesc.Layout = &m_BloomApplyPipelineLayout;
            gfxDesc.Shaders = { m_BloomApplyVertexShader, m_BloomUpsamplePixelShader };
            gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
            gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
            gfxDesc.Rasterizer.FrontCounterClockwise = false;
            gfxDesc.DepthStencil.DepthTestEnable = false;
            gfxDesc.DepthStencil.DepthWriteEnable = false;
            gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;
            RHIBlendAttachmentDesc additiveOneOne;
            additiveOneOne.BlendEnabled = true;
            additiveOneOne.SrcColor = nri::BlendFactor::ONE;
            additiveOneOne.DstColor = nri::BlendFactor::ONE;
            additiveOneOne.ColorOp = nri::BlendOp::ADD;
            additiveOneOne.SrcAlpha = nri::BlendFactor::ONE;
            additiveOneOne.DstAlpha = nri::BlendFactor::ONE;
            additiveOneOne.AlphaOp = nri::BlendOp::ADD;
            gfxDesc.ColorAttachments = { additiveOneOne };
            gfxDesc.ColorAttachmentFormats = { colorFormat };
            gfxDesc.VertexInput = RHIVertexInputDesc{};
            return cache.GetOrCreateGraphicsPipeline(gfxDesc);
        };

        m_BloomUpsamplePipelineSDR = createUpsample(nri::Format::RGBA8_UNORM);
        m_BloomUpsamplePipelineHDR = createUpsample(nri::Format::RGBA16_SFLOAT);
        m_BloomApplyPipelineSDR = GetOrCreateBloomApplyPipeline(nri::Format::RGBA8_UNORM);
        m_BloomApplyPipelineHDR = GetOrCreateBloomApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_BloomComputePipeline != nullptr &&
               m_BloomUpsamplePipelineSDR != nullptr && m_BloomUpsamplePipelineHDR != nullptr &&
               m_BloomApplyPipelineSDR != nullptr && m_BloomApplyPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileAutoExposureShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("AutoExposure.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath);
        std::stringstream sourceStream; sourceStream << file.rdbuf();
        const auto source = sourceStream.str();
        if (source.empty()) return false;
        const RHIShaderFormat format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan
            ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        auto compileEntry = [&](const char* entry, RHIShaderBytecode& outBytecode) -> bool {
            RHIShaderCompileDesc desc;
            desc.SourcePath = shaderPath.string();
            desc.Source = source;
            desc.EntryPoint = entry;
            desc.Stage = RHIShaderStage::Compute;
            desc.HLSLVersion = "2021";
            desc.ShaderModel = "6_0";
            desc.Format = format;
            desc.Optimization = true;
            const auto result = RHIShaderCompiler::Get().Compile(desc);
            if (!result.Success) {
                AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile AutoExposure CS '{0}': {1}", entry, result.ErrorMessage);
                return false;
            }
            outBytecode = result.Bytecode;
            return true;
        };
        return compileEntry("CSClearHistogram", m_AutoExposureClearShader) &&
               compileEntry("CSBuildHistogram", m_AutoExposureHistogramShader) &&
               compileEntry("CSReduceExposure", m_AutoExposureComputeShader);
    }

    bool RHISceneRenderer::CreateAutoExposurePipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc root = {};
        root.registerIndex = 0;
        root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;

        // Clear / Build Histogram Layout: space1 (t0 HDRTexture, s0 LinearSampler), space2 (u1 HistogramUAV)
        nri::DescriptorRangeDesc histTexRanges[2] = {};
        histTexRanges[0].baseRegisterIndex = 0; histTexRanges[0].descriptorNum = 1;
        histTexRanges[0].descriptorType = nri::DescriptorType::TEXTURE;
        histTexRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        histTexRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        histTexRanges[1].baseRegisterIndex = 0; histTexRanges[1].descriptorNum = 1;
        histTexRanges[1].descriptorType = nri::DescriptorType::SAMPLER;
        histTexRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        histTexRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorRangeDesc histStorage = {};
        histStorage.baseRegisterIndex = 1; histStorage.descriptorNum = 1;
        histStorage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        histStorage.shaderStages = nri::StageBits::COMPUTE_SHADER;
        histStorage.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorSetDesc histSets[2] = {};
        histSets[0].registerSpace = 1; histSets[0].ranges = histTexRanges; histSets[0].rangeNum = 2;
        histSets[1].registerSpace = 2; histSets[1].ranges = &histStorage; histSets[1].rangeNum = 1;
        nri::PipelineLayoutDesc histLayout = {};
        histLayout.rootRegisterSpace = 0; histLayout.rootDescriptors = &root; histLayout.rootDescriptorNum = 1;
        histLayout.descriptorSets = histSets; histLayout.descriptorSetNum = 2;
        histLayout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        if (!m_AutoExposureHistPipelineLayout.Init(histLayout)) return false;

        // Global Reduction Layout: space1 (t1 HistogramSRV), space2 (u0 ExposureOutput UAV)
        nri::DescriptorRangeDesc reduceTexRange = {};
        reduceTexRange.baseRegisterIndex = 1; reduceTexRange.descriptorNum = 1;
        reduceTexRange.descriptorType = nri::DescriptorType::TEXTURE;
        reduceTexRange.shaderStages = nri::StageBits::COMPUTE_SHADER;
        reduceTexRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorRangeDesc reduceStorage = {};
        reduceStorage.baseRegisterIndex = 0; reduceStorage.descriptorNum = 1;
        reduceStorage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        reduceStorage.shaderStages = nri::StageBits::COMPUTE_SHADER;
        reduceStorage.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorSetDesc reduceSets[2] = {};
        reduceSets[0].registerSpace = 1; reduceSets[0].ranges = &reduceTexRange; reduceSets[0].rangeNum = 1;
        reduceSets[1].registerSpace = 2; reduceSets[1].ranges = &reduceStorage; reduceSets[1].rangeNum = 1;
        nri::PipelineLayoutDesc reduceLayout = {};
        reduceLayout.rootRegisterSpace = 0; reduceLayout.rootDescriptors = &root; reduceLayout.rootDescriptorNum = 1;
        reduceLayout.descriptorSets = reduceSets; reduceLayout.descriptorSetNum = 2;
        reduceLayout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        return m_AutoExposurePipelineLayout.Init(reduceLayout);
    }

    bool RHISceneRenderer::CreateAutoExposurePipeline(RHIDevice& device) {
        auto createCS = [](const RHIPipelineLayout& layout, const RHIShaderBytecode& shader) {
            RHIComputePipelineDesc desc;
            desc.Layout = &layout;
            desc.ComputeShader = shader;
            return RHIPipelineCache::Get().GetOrCreateComputePipeline(desc);
        };
        m_AutoExposureClearPipeline = createCS(m_AutoExposureHistPipelineLayout, m_AutoExposureClearShader);
        m_AutoExposureHistogramPipeline = createCS(m_AutoExposureHistPipelineLayout, m_AutoExposureHistogramShader);
        m_AutoExposurePipeline = createCS(m_AutoExposurePipelineLayout, m_AutoExposureComputeShader);
        return m_AutoExposureClearPipeline != nullptr &&
               m_AutoExposureHistogramPipeline != nullptr &&
               m_AutoExposurePipeline != nullptr;
    }

    bool RHISceneRenderer::CompileDDGIShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("DDGI.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath);
        if (!file.is_open()) return false;
        std::stringstream stream;
        stream << file.rdbuf();
        const std::string source = stream.str();
        const RHIShaderFormat format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan
            ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        auto compileEntry = [&](const char* entry, RHIShaderBytecode& outBytecode) -> bool {
            RHIShaderCompileDesc desc;
            desc.SourcePath = shaderPath.string();
            desc.Source = source;
            desc.EntryPoint = entry;
            desc.Stage = RHIShaderStage::Compute;
            desc.HLSLVersion = "2021";
            desc.ShaderModel = "6_0";
            desc.Format = format;
            desc.Optimization = true;
            const auto result = RHIShaderCompiler::Get().Compile(desc);
            if (!result.Success) {
                AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile DDGI CS '{0}': {1}", entry, result.ErrorMessage);
                return false;
            }
            outBytecode = result.Bytecode;
            return true;
        };
        return compileEntry("CSSeedClear", m_DDGISeedShader) &&
               compileEntry("CSUpdateIrradiance", m_DDGIUpdateIrradianceShader) &&
               compileEntry("CSUpdateDistanceMoments", m_DDGIUpdateDistanceShader);
    }

    bool RHISceneRenderer::CreateDDGIPipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc root = {};
        root.registerIndex = 0; root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER; root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0; ranges[0].descriptorNum = 2; ranges[0].descriptorType = nri::DescriptorType::TEXTURE; ranges[0].shaderStages = nri::StageBits::COMPUTE_SHADER; ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        ranges[1].baseRegisterIndex = 0; ranges[1].descriptorNum = 1; ranges[1].descriptorType = nri::DescriptorType::SAMPLER; ranges[1].shaderStages = nri::StageBits::COMPUTE_SHADER; ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorRangeDesc storage = {};
        storage.baseRegisterIndex = 0; storage.descriptorNum = 2; storage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE; storage.shaderStages = nri::StageBits::COMPUTE_SHADER; storage.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorSetDesc sets[2] = {};
        sets[0].registerSpace = 1; sets[0].ranges = ranges; sets[0].rangeNum = 2;
        sets[1].registerSpace = 2; sets[1].ranges = &storage; sets[1].rangeNum = 1;
        nri::PipelineLayoutDesc layout = {}; layout.rootRegisterSpace = 0; layout.rootDescriptors = &root; layout.rootDescriptorNum = 1; layout.descriptorSets = sets; layout.descriptorSetNum = 2; layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        return m_DDGIPipelineLayout.Init(layout);
    }

    bool RHISceneRenderer::CreateDDGIPipeline(RHIDevice& device) {
        auto createCS = [&](const RHIShaderBytecode& shader) {
            RHIComputePipelineDesc desc;
            desc.Layout = &m_DDGIPipelineLayout;
            desc.ComputeShader = shader;
            return RHIPipelineCache::Get().GetOrCreateComputePipeline(desc);
        };
        m_DDGISeedPipeline = createCS(m_DDGISeedShader);
        m_DDGIIrradiancePipeline = createCS(m_DDGIUpdateIrradianceShader);
        m_DDGIDistancePipeline = createCS(m_DDGIUpdateDistanceShader);
        return m_DDGISeedPipeline != nullptr &&
               m_DDGIIrradiancePipeline != nullptr &&
               m_DDGIDistancePipeline != nullptr;
    }

    bool RHISceneRenderer::CompileVelocityShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("Velocity.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate Velocity.hlsl!");
            return false;
        }
        std::ifstream file(shaderPath);
        std::stringstream ss;
        ss << file.rdbuf();
        const auto source = ss.str();
        if (source.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read Velocity.hlsl!");
            return false;
        }
        const RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan)
            ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        auto compileEntry = [&](const char* entry, RHIShaderStage stage, RHI::RHIShaderBytecode& outBytecode) -> bool {
            RHIShaderCompileDesc desc;
            desc.SourcePath = shaderPath.string();
            desc.Source = source;
            desc.EntryPoint = entry;
            desc.Stage = stage;
            desc.HLSLVersion = "2021";
            desc.ShaderModel = "6_0";
            desc.Format = format;
            desc.Optimization = true;
            const auto res = compiler.Compile(desc);
            if (!res.Success) {
                AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile Velocity '{0}': {1}", entry, res.ErrorMessage);
                return false;
            }
            outBytecode = res.Bytecode;
            return true;
        };
        return compileEntry("VSBackground", RHIShaderStage::Vertex, m_VelocityBgVertexShader) &&
               compileEntry("PSBackground", RHIShaderStage::Fragment, m_VelocityBgPixelShader) &&
               compileEntry("VSMesh", RHIShaderStage::Vertex, m_VelocityMeshVertexShader) &&
               compileEntry("PSMesh", RHIShaderStage::Fragment, m_VelocityMeshPixelShader) &&
               compileEntry("PSReactive", RHIShaderStage::Fragment, m_ReactiveMeshPixelShader);
    }

    bool RHISceneRenderer::CreateVelocityPipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc bgRoot = {};
        bgRoot.registerIndex = 1;
        bgRoot.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        bgRoot.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        nri::PipelineLayoutDesc bgLayoutDesc = {};
        bgLayoutDesc.rootRegisterSpace = 0;
        bgLayoutDesc.rootDescriptors = &bgRoot;
        bgLayoutDesc.rootDescriptorNum = 1;
        bgLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
        if (!m_VelocityBgPipelineLayout.Init(bgLayoutDesc)) return false;

        nri::RootConstantDesc meshRootConst = {};
        meshRootConst.registerIndex = 0;
        meshRootConst.size = sizeof(RHIVelocityObjectConstants);
        meshRootConst.shaderStages = nri::StageBits::VERTEX_SHADER;

        nri::RootDescriptorDesc meshRootCBVs[2] = {};
        meshRootCBVs[0].registerIndex = 1;
        meshRootCBVs[0].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        meshRootCBVs[0].shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
        meshRootCBVs[1].registerIndex = 3;
        meshRootCBVs[1].descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        meshRootCBVs[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;

        nri::RootSamplerDesc meshSampler = {};
        meshSampler.registerIndex = 0;
        meshSampler.desc = RHISamplerDesc::Anisotropic16x().ToNRIDesc();
        meshSampler.shaderStages = nri::StageBits::FRAGMENT_SHADER;

        nri::DescriptorRangeDesc meshRange = {};
        meshRange.baseRegisterIndex = 0;
        meshRange.descriptorNum = ForwardTextureCount;
        meshRange.descriptorType = nri::DescriptorType::TEXTURE;
        meshRange.shaderStages = nri::StageBits::FRAGMENT_SHADER;
        meshRange.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc meshSet = {};
        meshSet.registerSpace = 1;
        meshSet.ranges = &meshRange;
        meshSet.rangeNum = 1;

        nri::PipelineLayoutDesc meshLayoutDesc = {};
        meshLayoutDesc.rootRegisterSpace = 0;
        meshLayoutDesc.rootConstants = &meshRootConst;
        meshLayoutDesc.rootConstantNum = 1;
        meshLayoutDesc.rootDescriptors = meshRootCBVs;
        meshLayoutDesc.rootDescriptorNum = 2;
        meshLayoutDesc.rootSamplers = &meshSampler;
        meshLayoutDesc.rootSamplerNum = 1;
        meshLayoutDesc.descriptorSets = &meshSet;
        meshLayoutDesc.descriptorSetNum = 1;
        meshLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
        return m_VelocityMeshPipelineLayout.Init(meshLayoutDesc);
    }

    bool RHISceneRenderer::CreateVelocityPipeline(RHIDevice& device) {
        RHIGraphicsPipelineDesc bgDesc;
        bgDesc.Layout = &m_VelocityBgPipelineLayout;
        bgDesc.Shaders = { m_VelocityBgVertexShader, m_VelocityBgPixelShader };
        bgDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        bgDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        bgDesc.Rasterizer.FrontCounterClockwise = false;
        bgDesc.DepthStencil.DepthTestEnable = false;
        bgDesc.DepthStencil.DepthWriteEnable = false;
        bgDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;
        bgDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        bgDesc.ColorAttachmentFormats = { nri::Format::RG16_SFLOAT };
        bgDesc.VertexInput = RHIVertexInputDesc{};
        m_VelocityBgPipeline = RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(bgDesc);
        return m_VelocityBgPipeline != nullptr &&
               GetOrCreateVelocityMeshPipeline(nri::Format::D32_SFLOAT, Axle::CullMode::None, false) != nullptr &&
               GetOrCreateReactiveMeshPipeline(nri::Format::D32_SFLOAT, Axle::CullMode::None, false) != nullptr;
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateVelocityMeshPipeline(nri::Format depthFormat, Axle::CullMode cull, bool reverseWinding) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_VelocityMeshPipelineLayout;
        gfxDesc.Shaders = { m_VelocityMeshVertexShader, m_VelocityMeshPixelShader };
        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = cull == CullMode::Back ? nri::CullMode::BACK : (cull == CullMode::Front ? nri::CullMode::FRONT : nri::CullMode::NONE);
        gfxDesc.Rasterizer.FrontCounterClockwise = !reverseWinding;
        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS_EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;
        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { nri::Format::RG16_SFLOAT };
        gfxDesc.SampleCount = 1;
        gfxDesc.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();
        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateReactiveMeshPipeline(nri::Format depthFormat, Axle::CullMode cull, bool reverseWinding) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_VelocityMeshPipelineLayout;
        gfxDesc.Shaders = { m_VelocityMeshVertexShader, m_ReactiveMeshPixelShader };
        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = cull == CullMode::Back ? nri::CullMode::BACK : (cull == CullMode::Front ? nri::CullMode::FRONT : nri::CullMode::NONE);
        gfxDesc.Rasterizer.FrontCounterClockwise = !reverseWinding;
        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS_EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;
        RHIBlendAttachmentDesc maxBlend{};
        maxBlend.BlendEnabled = true;
        maxBlend.SrcColor = nri::BlendFactor::ONE;
        maxBlend.DstColor = nri::BlendFactor::ONE;
        maxBlend.ColorOp = nri::BlendOp::MAX;
        maxBlend.SrcAlpha = nri::BlendFactor::ONE;
        maxBlend.DstAlpha = nri::BlendFactor::ONE;
        maxBlend.AlphaOp = nri::BlendOp::MAX;
        gfxDesc.ColorAttachments = { maxBlend };
        gfxDesc.ColorAttachmentFormats = { nri::Format::R8_UNORM };
        gfxDesc.SampleCount = 1;
        gfxDesc.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();
        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CompileTAAShaders(RHIDevice& device) {
        auto& compiler = RHIShaderCompiler::Get();
        auto shaderPath = FindShaderPath("TAA.hlsl");
        if (shaderPath.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to locate TAA.hlsl!");
            return false;
        }

        std::string shaderSource;
        std::ifstream file(shaderPath);
        if (file.is_open()) {
            std::stringstream ss; ss << file.rdbuf(); shaderSource = ss.str();
        }
        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to read TAA.hlsl!");
            return false;
        }

        RHIShaderFormat format = (device.GetCapabilities().ActiveBackend == RHIType::Vulkan) ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;

        RHIShaderCompileDesc vsDesc;
        vsDesc.SourcePath = shaderPath.string();
        vsDesc.Source = shaderSource;
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = format;
        vsDesc.Optimization = true;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile TAA VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_TAAVertexShader = vsRes.Bytecode;

        RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile TAA PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_TAAPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateTAAPipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        nri::DescriptorRangeDesc ranges[7] = {};
        for (uint32_t t = 0; t < 5; ++t) {
            ranges[t].baseRegisterIndex = t;
            ranges[t].descriptorNum = 1;
            ranges[t].descriptorType = nri::DescriptorType::TEXTURE;
            ranges[t].shaderStages = nri::StageBits::FRAGMENT_SHADER;
            ranges[t].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        }

        ranges[5].baseRegisterIndex = 0;
        ranges[5].descriptorNum = 1;
        ranges[5].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[5].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[5].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[6].baseRegisterIndex = 1;
        ranges[6].descriptorNum = 1;
        ranges[6].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[6].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[6].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 7;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_TAAPipelineLayout.Init(layoutDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateTAAPipeline(nri::Format colorFormat) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_TAAPipelineLayout;
        gfxDesc.Shaders = { m_TAAVertexShader, m_TAAPixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = false;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::ALWAYS;

        gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.VertexInput = RHIVertexInputDesc{};

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHISceneRenderer::CreateTAAPipeline(RHIDevice& device) {
        m_TAAPipelineSDR = GetOrCreateTAAPipeline(nri::Format::RGBA8_UNORM);
        m_TAAPipelineHDR = GetOrCreateTAAPipeline(nri::Format::RGBA16_SFLOAT);
        return m_TAAPipelineSDR != nullptr && m_TAAPipelineHDR != nullptr;
    }

    void RHISceneRenderer::SetRenderTargetFormat(RHI::RHITextureFormat colorFormat, RHI::RHITextureFormat depthFormat) {
        m_CurrentColorFormat = RHI::ToNRIFormat(colorFormat);
        m_CurrentDepthFormat = RHI::ToNRIFormat(depthFormat);
    }

    RHI::RHITextureFormat RHISceneRenderer::GetRenderTargetColorFormat() const {
        switch (m_CurrentColorFormat) {
            case nri::Format::RGBA16_SFLOAT: return RHI::RHITextureFormat::RGBA16_FLOAT;
            case nri::Format::RGBA32_SFLOAT: return RHI::RHITextureFormat::RGBA32_FLOAT;
            default:                         return RHI::RHITextureFormat::RGBA8_UNORM;
        }
    }

    RHI::RHITextureFormat RHISceneRenderer::GetRenderTargetDepthFormat() const {
        switch (m_CurrentDepthFormat) {
            case nri::Format::D24_UNORM_S8_UINT: return RHI::RHITextureFormat::D24_UNORM_S8_UINT;
            default:                             return RHI::RHITextureFormat::D32_FLOAT;
        }
    }

    std::vector<ReflectionSampleBounds> RHISceneRenderer::CollectReflectionSampleBounds(const RenderWorld& world) {
        std::vector<ReflectionSampleBounds> bounds;
        if (world.ReflectionProbes.empty()) return bounds;
        bounds.reserve(world.Meshes.size());
        for (const auto& mesh : world.Meshes) {
            glm::vec3 wMin(mesh.WorldTransform[3]), wMax(wMin);
            const auto view = mesh.NativeView ? mesh.NativeView : mesh.NativeMesh ? mesh.NativeMesh->CaptureView() : nullptr;
            if (view && view->IsValid()) {
                const auto bMin = view->GetBoundingMin(), bMax = view->GetBoundingMax();
                wMin = glm::vec3(std::numeric_limits<float>::max());
                wMax = glm::vec3(std::numeric_limits<float>::lowest());
                for (int corner = 0; corner < 8; ++corner) {
                    const glm::vec3 local((corner & 1) ? bMax.x : bMin.x,
                        (corner & 2) ? bMax.y : bMin.y, (corner & 4) ? bMax.z : bMin.z);
                    const auto point = glm::vec3(mesh.WorldTransform * glm::vec4(local, 1.0f));
                    if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
                        wMin = glm::min(wMin, point);
                        wMax = glm::max(wMax, point);
                    }
                }
            }
            if (std::isfinite(wMin.x) && std::isfinite(wMin.y) && std::isfinite(wMin.z) &&
                std::isfinite(wMax.x) && std::isfinite(wMax.y) && std::isfinite(wMax.z) &&
                glm::all(glm::lessThanEqual(wMin, wMax))) bounds.push_back({wMin, wMax});
        }
        return bounds;
    }

    RHIReflectionProbeBufferData RHISceneRenderer::BuildReflectionProbeBufferData(
        const ReflectionFrame&,
        bool useIBL,
        bool useSSR,
        float envIntensity,
        const glm::vec3& skyAmbientColor,
        bool diffuseOnly,
        float skyRotationRad) const {
        RHIReflectionProbeBufferData out{};
        out.Header = glm::uvec4(
            m_ActiveReflectionProbeCount,
            useIBL ? 1u : 0u,
            diffuseOnly ? 1u : 0u,
            useSSR ? 1u : 0u);
        out.Params = glm::vec4(
            0.0f,
            std::isfinite(envIntensity) ? std::max(envIntensity, 0.0f) : 1.0f,
            std::isfinite(skyRotationRad) ? skyRotationRad : 0.0f,
            0.0f);
        out.SkyAmbient = glm::vec4(
            std::isfinite(skyAmbientColor.r) ? std::max(skyAmbientColor.r, 0.0f) : 0.0f,
            std::isfinite(skyAmbientColor.g) ? std::max(skyAmbientColor.g, 0.0f) : 0.0f,
            std::isfinite(skyAmbientColor.b) ? std::max(skyAmbientColor.b, 0.0f) : 0.0f,
            1.0f);

        for (uint32_t i = 0; i < m_ActiveReflectionProbeCount && i < MaxActiveReflectionProbes; ++i) {
            const auto& tex = m_ActiveReflectionProbePrefilters[i];
            if (!tex || !tex->IsValid()) continue;
            const ReflectionProbeBasis basis = ReflectionService::BuildProbeBasis(m_ActiveReflectionProbes[i]);
            auto& gpu = out.Probes[i];
            gpu.PositionAndShape = glm::vec4(basis.Position, basis.Shape == 1 ? 1.0f : 0.0f);
            gpu.HalfExtentsAndRadius = glm::vec4(basis.HalfExtents, basis.Radius);
            gpu.Params = glm::vec4(
                basis.Intensity,
                basis.ParallaxCorrected ? 1.0f : 0.0f,
                basis.BlendDistance,
                m_ActiveReflectionProbeStale[i] ? 1.0f : 0.0f);
            gpu.AxisX = glm::vec4(basis.AxisX, 0.0f);
            gpu.AxisY = glm::vec4(basis.AxisY, 0.0f);
            gpu.AxisZ = glm::vec4(basis.AxisZ, 0.0f);
            const uint32_t mips = std::max(1u, tex->GetMipCount());
            gpu.PrefilterInfo = glm::vec4(static_cast<float>(mips - 1u), 1.0f, 0.0f, 0.0f);
        }
        return out;
    }

    ReflectionPublication RHISceneRenderer::PrepareReflectionProbe(const ReflectionFrame& reflectionFrame) {
        m_ActiveReflectionProbePrefilter.reset();
        m_ActiveReflectionProbePrefilters.fill({});
        m_ActiveReflectionProbes.fill({});
        m_ActiveReflectionProbeStale.fill(false);
        m_ActiveReflectionProbeCount = 0;
        m_HasActiveReflectionProbe = false;
        if (m_CaptureSession.Active) {
            return {{}, "dynamic_probe_suppressed_during_capture"};
        }

        std::vector<ReflectionProbeCandidate> ranked = reflectionFrame.ActiveProbes;
        ranked.insert(ranked.end(), reflectionFrame.AdditionalCandidates.begin(), reflectionFrame.AdditionalCandidates.end());
        std::vector<RenderReflectionProbe> candidates;
        if (!reflectionFrame.ActiveProbes.empty()) {
            candidates.reserve(reflectionFrame.ActiveProbes.size());
            for (const auto& c : reflectionFrame.ActiveProbes) {
                candidates.push_back(c.Probe);
            }
            for (const auto& c : reflectionFrame.AdditionalCandidates) candidates.push_back(c.Probe);
        } else if (reflectionFrame.SelectedProbe) {
            candidates.push_back(*reflectionFrame.SelectedProbe);
        }
        if (candidates.empty()) return {};

        ReflectionPublication publication;
        publication.ProbePublications.resize(candidates.size());
        const uint64_t currentEpoch = m_Device ? m_Device->GetEpoch() : 0;
        bool primarySet = false;
        uint32_t readyCount = 0;
        size_t attemptedCount = 0;

        for (size_t i = 0; i < candidates.size() && readyCount < MaxActiveReflectionProbes; ++i) {
            ++attemptedCount;
            const auto& cand = candidates[i];
            auto& entryPub = publication.ProbePublications[i];
            std::shared_ptr<RHITexture> resolvedTexture;

            if (cand.DynamicCapture && cand.CubemapPath.empty()) {
                const std::string probeKey = ResolveDynamicProbeKey(cand);
                std::shared_ptr<DynamicReflectionProbeGeneration> dynamicProbe;
                if (const auto probeIt = m_DynamicProbeCaptures.find(probeKey);
                    probeIt != m_DynamicProbeCaptures.end() &&
                    probeIt->second &&
                    probeIt->second->IsValidForDevice(currentEpoch)) {
                    dynamicProbe = probeIt->second;
                }

                if (const auto statusIt = m_DynamicProbeStatuses.find(probeKey);
                    statusIt != m_DynamicProbeStatuses.end()) {
                    entryPub.CapturePending = statusIt->second.Pending;
                    entryPub.CaptureStale = statusIt->second.Stale;
                    entryPub.CaptureFailed = statusIt->second.Failed;
                    entryPub.CaptureCompletedFaces = statusIt->second.CompletedFaces;
                    entryPub.CaptureTotalFaces = statusIt->second.TotalFaces;
                    entryPub.Diagnostic = statusIt->second.LastDiagnostic;
                    if (dynamicProbe) {
                        statusIt->second.LastUsedSequence = dynamicProbe->LastUsedSequence;
                    }
                } else {
                    entryPub.CaptureTotalFaces = ReflectionCaptureContract::FaceCount;
                    entryPub.CapturePending = (cand.CaptureMode == ReflectionProbeCaptureMode::Automatic);
                }

                if (dynamicProbe && dynamicProbe->Cubemap && dynamicProbe->Cubemap->IsValid() &&
                    !IsCaptureFeedbackTexture(*dynamicProbe->Cubemap)) {
                    resolvedTexture = dynamicProbe->Cubemap;
                    entryPub.Ready = true;
                    entryPub.PrefilterGeneration = resolvedTexture->GetHandle();
                    entryPub.ActiveGenerationID = dynamicProbe->GenerationID;
                    if (!entryPub.CapturePending && !entryPub.CaptureStale && !entryPub.CaptureFailed) {
                        entryPub.Diagnostic.clear();
                    }
                } else if (entryPub.Diagnostic.empty()) {
                    entryPub.Diagnostic = m_LastDynamicCaptureResult.Diagnostic.empty()
                        ? "dynamic probe cubemap not captured"
                        : m_LastDynamicCaptureResult.Diagnostic;
                }
            } else if (!cand.CubemapPath.empty()) {
                const std::string path = PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(cand.CubemapPath)).lexically_normal());
                auto& environment = m_ReflectionProbeEnvironments[path];
                if (!environment) {
                    environment = std::make_unique<RHIEnvironment>();
                    EnvironmentBakeSettings settings;
                    settings.EnvironmentSize = 128;
                    settings.IrradianceSize = 16;
                    settings.PrefilterSize = 64;
                    settings.PrefilterMips = 5;
                    settings.BRDFSize = 64;
                    settings.Samples = 128;
                    if (!environment->SetBakeSettings(settings)) {
                        entryPub.Diagnostic = "probe bake settings unavailable";
                    }
                }
                if (environment) {
                    RenderSkyboxData source;
                    source.Authored = true;
                    source.Enabled = true;
                    source.EnvironmentLighting = true;
                    source.Type = 1;
                    source.HDRPath = cand.CubemapPath;
                    source.Intensity = 1.0f;
                    source.Exposure = 1.0f;
                    if (environment->Prepare(source) && environment->Ready() && environment->Ready()->IsValid() &&
                        environment->Ready()->Prefilter && environment->Ready()->Prefilter->IsValid() &&
                        !IsCaptureFeedbackTexture(*environment->Ready()->Prefilter)) {
                        resolvedTexture = environment->Ready()->Prefilter;
                        entryPub.Ready = true;
                        entryPub.PrefilterGeneration = resolvedTexture->GetHandle();
                        entryPub.Diagnostic = environment->Diagnostic();
                    } else {
                        entryPub.Diagnostic = environment->Diagnostic().empty()
                            ? "authored probe prefilter unavailable"
                            : environment->Diagnostic();
                    }
                }
            }
            if (entryPub.Ready) ++readyCount;
        }

        // Fill native slots from ready candidates before truncating. Unready
        // high-ranked probes must not hide a usable lower-ranked fallback.
        if (!ranked.empty()) {
            std::vector<RenderReflectionProbe> chosen;
            std::vector<ReflectionProbePublication> receipts;
            uint32_t missing = 0;
            for (size_t i = 0; i < attemptedCount; ++i) {
                if (!publication.ProbePublications[i].Ready) { ++missing; continue; }
                chosen.push_back(candidates[i]);
                publication.SelectedCandidates.push_back(ranked[i]);
                receipts.push_back(publication.ProbePublications[i]);
            }
            for (size_t i = 0; i < attemptedCount && chosen.size() < MaxActiveReflectionProbes; ++i) {
                if (publication.ProbePublications[i].Ready) continue;
                chosen.push_back(candidates[i]);
                publication.SelectedCandidates.push_back(ranked[i]);
                receipts.push_back(publication.ProbePublications[i]);
                --missing;
            }
            publication.RejectedProbeCount = missing;
            candidates = std::move(chosen);
            publication.ProbePublications = std::move(receipts);
        }
        for (size_t i = candidates.size(); i > 0; --i) {
            if (!publication.ProbePublications[i - 1].Ready) continue;
            const auto it = m_DynamicProbeCaptures.find(ResolveDynamicProbeKey(candidates[i - 1]));
            if (it != m_DynamicProbeCaptures.end() && it->second)
                it->second->LastUsedSequence = ++m_CaptureLRUClock;
        }

        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto& cand = candidates[i];
            const auto& entryPub = publication.ProbePublications[i];
            if (!entryPub.Ready) continue;

            std::shared_ptr<RHITexture> resolvedTexture;
            if (cand.DynamicCapture && cand.CubemapPath.empty()) {
                const auto probeIt = m_DynamicProbeCaptures.find(ResolveDynamicProbeKey(cand));
                if (probeIt != m_DynamicProbeCaptures.end() && probeIt->second &&
                    probeIt->second->IsValidForDevice(currentEpoch)) {
                    resolvedTexture = probeIt->second->Cubemap;
                }
            } else if (!cand.CubemapPath.empty()) {
                const std::string path = PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(cand.CubemapPath)).lexically_normal());
                if (const auto envIt = m_ReflectionProbeEnvironments.find(path);
                    envIt != m_ReflectionProbeEnvironments.end() && envIt->second && envIt->second->Ready()) {
                    resolvedTexture = envIt->second->Ready()->Prefilter;
                }
            }

            if (resolvedTexture && m_ActiveReflectionProbeCount < MaxActiveReflectionProbes) {
                const uint32_t slot = m_ActiveReflectionProbeCount++;
                m_ActiveReflectionProbePrefilters[slot] = resolvedTexture;
                m_ActiveReflectionProbes[slot] = cand;
                m_ActiveReflectionProbeStale[slot] = entryPub.CaptureStale;
                if (!primarySet) {
                    primarySet = true;
                    m_ActiveReflectionProbePrefilter = resolvedTexture;
                    m_ActiveReflectionProbe = cand;
                    m_HasActiveReflectionProbe = true;
                    publication.PrefilterGeneration = entryPub.PrefilterGeneration;
                    publication.ActiveGenerationID = entryPub.ActiveGenerationID;
                    publication.CapturePending = entryPub.CapturePending;
                    publication.CaptureStale = entryPub.CaptureStale;
                    publication.CaptureFailed = entryPub.CaptureFailed;
                    publication.CaptureCompletedFaces = entryPub.CaptureCompletedFaces;
                    publication.CaptureTotalFaces = entryPub.CaptureTotalFaces;
                    publication.Diagnostic = entryPub.Diagnostic;
                }
            }
        }

        if (!primarySet && !publication.ProbePublications.empty()) {
            const auto& fallbackPub = publication.ProbePublications[0];
            publication.CapturePending = fallbackPub.CapturePending;
            publication.CaptureStale = fallbackPub.CaptureStale;
            publication.CaptureFailed = fallbackPub.CaptureFailed;
            publication.CaptureCompletedFaces = fallbackPub.CaptureCompletedFaces;
            publication.CaptureTotalFaces = fallbackPub.CaptureTotalFaces;
            publication.ActiveGenerationID = fallbackPub.ActiveGenerationID;
            publication.Diagnostic = fallbackPub.Diagnostic;
        }
        return publication;
    }

    bool RHISceneRenderer::IsCaptureFeedbackTexture(const RHITexture& texture) const {
        if (m_PlanarSession.Active &&
            m_PlanarSession.TargetColor &&
            m_PlanarSession.TargetColor->GetNative() == texture.GetNative()) {
            return true;
        }
        if (!m_CaptureSession.Active) return false;
        if (m_CaptureSession.CandidateCubemap && m_CaptureSession.CandidateCubemap->GetNative() == texture.GetNative()) return true;
        for (const auto& handle : m_DynamicCaptureTextures)
            if (handle == texture.GetHandle()) return true;
        return false;
    }

    ReflectionCaptureDomainStamps RHISceneRenderer::ComputeDynamicCaptureDomainStamps(
        const RenderWorld& world,
        const RenderReflectionProbe& probe,
        const SceneRenderSettings& settings) const {
        auto mixBytes = [](uint64_t& hash, const void* data, size_t size) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };
        auto mixVal = [&](uint64_t& hash, const auto&... values) {
            (mixBytes(hash, &values, sizeof(values)), ...);
        };
        auto mixText = [&](uint64_t& hash, const std::string& value) {
            mixVal(hash, value.size());
            for (char c : value) {
                mixVal(hash, c);
            }
        };
        auto mixFileTime = [&](uint64_t& hash, const std::string& path) {
            mixText(hash, path);
            if (path.empty()) return;
            std::error_code error;
            const auto time = std::filesystem::last_write_time(FileSystem::ResolveAssetPath(PathFromUtf8(path)), error);
            mixVal(hash, error.value());
            if (!error) {
                mixVal(hash, time.time_since_epoch().count());
            }
        };

        constexpr uint64_t kFNVOffset = 1469598103934665603ull;
        ReflectionCaptureDomainStamps stamps{};
        stamps.TransformStamp = kFNVOffset;
        stamps.GeometryStamp = kFNVOffset;
        stamps.MaterialStamp = kFNVOffset;
        stamps.LightingStamp = kFNVOffset;
        stamps.AssetStamp = kFNVOffset;
        stamps.SettingsStamp = kFNVOffset;
        stamps.SceneRevision = world.SceneRevision;
        stamps.SceneInstanceID = world.SceneInstanceID;
        stamps.SceneLifetimeEpoch = world.SceneLifetimeEpoch;
        stamps.DeviceEpoch = m_Device ? m_Device->GetEpoch() : 0ull;

        // Domain 1: Probe transform & spatial bounds (M15-U5-21.1)
        mixVal(stamps.TransformStamp,
            probe.EntityID,
            static_cast<uint64_t>(probe.StableUUID),
            probe.Position,
            probe.BoxOffset,
            probe.BoxExtents,
            probe.Radius,
            probe.Shape,
            probe.WorldTransform,
            probe.ParallaxCorrected);

        // Domain 7: Capture settings (probe resolution/planes/mode/shadows - viewport resolution excluded per M15-U5-26.2)
        mixVal(stamps.SettingsStamp,
            probe.CaptureResolution,
            probe.CaptureNearPlane,
            probe.CaptureFarPlane,
            probe.CaptureShadows,
            static_cast<uint8_t>(probe.CaptureMode),
            probe.Intensity);

        // Domain 6 (Asset base): Device epoch & manual IBL texture handles (M15-U5-21.5)
        mixVal(stamps.AssetStamp, stamps.DeviceEpoch);
        for (const auto& texture : {m_IrradianceMap, m_PrefilterMap, m_BRDFLUT}) {
            const auto handle = texture ? texture->GetHandle() : RHIResourceHandle{};
            mixVal(stamps.AssetStamp, handle.Epoch, handle.Slot, handle.Generation);
        }

        // Domain 5: Lighting, shadow state, directional/local lights & skybox (M15-U5-21.4)
        mixVal(stamps.LightingStamp,
            m_UseIBL,
            m_IBLAllowed,
            settings.EnableShadows,
            settings.EnableSkybox,
            settings.LocalShadowResolution,
            settings.MaxShadowedPointLights,
            settings.MaxShadowedSpotLights,
            settings.LocalShadowDistance,
            settings.ShadowCascadeCount,
            settings.EnableStudioLightLobes);

        const auto hashDirectional = [&](const RenderDirectionalLight& light) {
            mixVal(stamps.LightingStamp,
                light.Direction, light.Color, light.Intensity, light.AmbientIntensity, light.SolarAngularRadius,
                light.CastsShadows, light.ShadowQuality, light.ShadowBias, light.ShadowSlopeBias,
                light.ShadowNormalBias, light.ShadowFilterRadius, light.MaxShadowDistance, light.CascadeSplitLambda);
        };
        mixVal(stamps.LightingStamp, world.DirectionalLight.has_value());
        if (world.DirectionalLight) {
            hashDirectional(*world.DirectionalLight);
        }
        mixVal(stamps.LightingStamp, world.AdditionalDirectionalLights.size());
        for (const auto& light : world.AdditionalDirectionalLights) {
            hashDirectional(light);
        }
        const auto hashShadow = [&](const LocalShadowSettings& s) {
            mixVal(stamps.LightingStamp, s.CastShadows, s.Bias, s.SlopeBias, s.NormalBias, s.FilterRadius, s.NearPlane);
        };
        mixVal(stamps.LightingStamp, world.PointLights.size(), world.SpotLights.size(), world.RectLights.size());
        for (const auto& light : world.PointLights) {
            mixVal(stamps.LightingStamp, light.EntityID, light.Position, light.Color, light.Intensity, light.Radius, light.Falloff);
            hashShadow(light.Shadows);
        }
        for (const auto& light : world.SpotLights) {
            mixVal(stamps.LightingStamp, light.EntityID, light.Position, light.Direction, light.Color, light.Intensity, light.Range, light.InnerAngle, light.OuterAngle);
            hashShadow(light.Shadows);
        }
        for (const auto& light : world.RectLights) {
            mixVal(stamps.LightingStamp, light.EntityID, light.Position, light.Direction, light.Up, light.Color, light.Intensity, light.Width, light.Height, light.Range, light.TwoSided, light.Shape, light.ShadowPolicy);
        }

        const auto& sky = world.Skybox;
        mixVal(stamps.LightingStamp,
            sky.Authored, sky.Enabled, sky.EnvironmentLighting, sky.SyncWithSun, sky.CustomAzimuth, sky.CustomElevation,
            sky.HemisphereAmbient, sky.SunSize, sky.SunGlow, sky.Turbidity, sky.Type, sky.SunDirection, sky.SunColor,
            sky.ZenithColor, sky.HorizonColor, sky.GroundColor, sky.Intensity, sky.Exposure, sky.EnvironmentRotation);
        if (!sky.HDRPath.empty()) {
            mixText(stamps.LightingStamp, sky.HDRPath);
        }
        mixFileTime(stamps.AssetStamp, sky.HDRPath);
        if (!sky.Authored) {
            mixVal(stamps.LightingStamp,
                m_SkyboxParams.Enabled, m_SkyboxParams.Type, m_SkyboxParams.Intensity, m_SkyboxParams.Exposure,
                m_SkyboxParams.ZenithColor, m_SkyboxParams.HorizonColor, m_SkyboxParams.GroundColor, m_SkyboxParams.SunSize,
                m_SkyboxParams.SunGlow, m_SkyboxParams.Turbidity, m_SkyboxParams.EnvironmentRotation);
        }

        // Domains 3, 4, 6: Geometry, materials/custom properties & asset generation/timestamps (M15-U5-21.3, 21.5)
        mixVal(stamps.GeometryStamp, world.Meshes.size(), world.Skins.size());
        mixVal(stamps.MaterialStamp, world.Meshes.size());
        for (const auto& mesh : world.Meshes) {
            const int builtinVal = mesh.BuiltinId ? static_cast<int>(*mesh.BuiltinId) : -1;
            mixVal(stamps.GeometryStamp,
                mesh.EntityID,
                static_cast<uint64_t>(mesh.StableUUID),
                mesh.WorldTransform,
                mesh.CastsShadows,
                mesh.MeshSourceUUID,
                builtinVal);
            mixText(stamps.GeometryStamp, mesh.MeshURI);

            const auto surface = MaterialResolver::Resolve(mesh.Material);
            mixVal(stamps.MaterialStamp,
                mesh.EntityID,
                surface.Generation,
                static_cast<uint64_t>(mesh.Material.MaterialUUID),
                mesh.Material.AlbedoColor,
                mesh.Material.SecondaryColor,
                mesh.Material.TileScale,
                mesh.Material.PatternMode,
                mesh.Material.Roughness,
                mesh.Material.Metallic,
                mesh.Material.Emission,
                mesh.Material.UVScale,
                mesh.Material.UVOffset,
                mesh.Material.AO,
                mesh.Material.UseAlbedoMap,
                mesh.Material.UseCustomShader,
                mesh.Material.SheenEnabled,
                mesh.Material.SheenColor,
                mesh.Material.SheenRoughness,
                mesh.Material.SubsurfaceEnabled,
                mesh.Material.SubsurfaceColor,
                mesh.Material.SubsurfaceRadius,
                mesh.Material.AbsorptionEnabled,
                mesh.Material.AbsorptionColor,
                mesh.Material.Dispersion);
            mixText(stamps.MaterialStamp, mesh.Material.MaterialPath);
            mixText(stamps.MaterialStamp, mesh.Material.CustomShaderPath);

            for (const auto& binding : surface.Textures) {
                mixText(stamps.MaterialStamp, binding.Path);
                mixFileTime(stamps.AssetStamp, binding.Path);
            }

            const auto view = mesh.NativeView ? mesh.NativeView : mesh.NativeMesh ? mesh.NativeMesh->CaptureView() : nullptr;
            const auto handle = view ? view->GetVertexHandle() : RHIResourceHandle{};
            mixVal(stamps.AssetStamp, handle.Epoch, handle.Slot, handle.Generation);
            const auto indices = view ? view->GetIndexHandle() : RHIResourceHandle{};
            mixVal(stamps.AssetStamp, indices.Epoch, indices.Slot, indices.Generation);

            mixFileTime(stamps.AssetStamp, mesh.Material.CustomShaderPath);
            if (mesh.Material.UseCustomShader && !mesh.Material.CustomShaderPath.empty()) {
                CustomProgramKey key;
                key.ShaderPath = PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(mesh.Material.CustomShaderPath)).lexically_normal());
                key.Backend = m_Device ? m_Device->GetBackend() : RHIType::None;
                mixText(stamps.AssetStamp, m_CustomProgramCache.ComputeFingerprint(key));
            }
            for (const auto& property : mesh.Material.CustomProperties) {
                mixText(stamps.MaterialStamp, property.Desc.Name);
                mixVal(stamps.MaterialStamp, property.FloatVal, property.Vec4Val, property.IntVal, property.BoolVal);
                mixFileTime(stamps.AssetStamp, property.TexturePath);
            }
        }

        for (const auto& skin : world.Skins) {
            mixVal(stamps.GeometryStamp,
                static_cast<bool>(skin.GLMesh != nullptr),
                skin.WorldTransform,
                skin.Color,
                skin.CastsShadows,
                skin.BoneMatrices.size());
            for (const auto& bone : skin.BoneMatrices) {
                mixVal(stamps.GeometryStamp, bone);
            }
        }

        stamps.CombinedStamp = kFNVOffset;
        mixVal(stamps.CombinedStamp,
            stamps.TransformStamp,
            stamps.SceneRevision,
            stamps.SceneInstanceID,
            stamps.SceneLifetimeEpoch,
            stamps.GeometryStamp,
            stamps.MaterialStamp,
            stamps.LightingStamp,
            stamps.AssetStamp,
            stamps.SettingsStamp,
            stamps.DeviceEpoch);
        if (stamps.CombinedStamp == 0) {
            stamps.CombinedStamp = 1;
        }
        return stamps;
    }

    uint64_t RHISceneRenderer::ComputeDynamicCaptureStamp(
        const RenderWorld& world,
        const RenderReflectionProbe& probe,
        const SceneRenderSettings& settings) const {
        return ComputeDynamicCaptureDomainStamps(world, probe, settings).CombinedStamp;
    }

    std::string RHISceneRenderer::ResolveDynamicProbeKey(const RenderReflectionProbe& probe) {
        if (probe.StableUUID != 0) {
            return "dynamic:uuid:" + std::to_string(static_cast<uint64_t>(probe.StableUUID));
        }
        if (probe.EntityID != 0) {
            return "dynamic:entity:" + std::to_string(probe.EntityID);
        }
        const auto px = std::bit_cast<uint32_t>(probe.Position.x);
        const auto py = std::bit_cast<uint32_t>(probe.Position.y);
        const auto pz = std::bit_cast<uint32_t>(probe.Position.z);
        return "dynamic:anon:" + std::to_string(px) + ":" + std::to_string(py) + ":" + std::to_string(pz);
    }

    uint64_t RHISceneRenderer::EffectiveDynamicCaptureBudgetBytes(const SceneRenderSettings& settings) const {
        if (m_HasDynamicCaptureBudgetOverride) {
            return m_DynamicCaptureBudgetOverrideBytes;
        }
        return settings.ReflectionCaptureCacheBudgetBytes;
    }

    void RHISceneRenderer::SetDynamicReflectionCaptureCacheBudget(uint64_t budgetBytes) {
        m_HasDynamicCaptureBudgetOverride = true;
        m_DynamicCaptureBudgetOverrideBytes = budgetBytes;
    }

    void RHISceneRenderer::ClearDynamicCaptureBudgetOverride() {
        m_HasDynamicCaptureBudgetOverride = false;
        m_DynamicCaptureBudgetOverrideBytes = 0;
    }

    void RHISceneRenderer::RequestDynamicReflectionCapture(const std::string& probeKey) {
        m_RequestedManualCaptures[probeKey] = true;
        m_FailedDynamicCaptureRevisions.erase(probeKey);
        if (auto it = m_DynamicProbeStatuses.find(probeKey); it != m_DynamicProbeStatuses.end()) {
            it->second.ManualRefreshRequired = false;
            it->second.Failed = false;
            it->second.Pending = true;
            it->second.State = DynamicCaptureLifecycleState::Pending;
            it->second.LastDiagnostic = "DYNAMIC_PROBE_PENDING: manual capture requested";
        }
    }

    void RHISceneRenderer::RequestDynamicReflectionCapture(const RenderReflectionProbe& probe) {
        RequestDynamicReflectionCapture(ResolveDynamicProbeKey(probe));
    }

    void RHISceneRenderer::InvalidateDynamicReflectionProbe(const std::string& probeKey, const std::string& reason) {
        m_ForcedInvalidatedProbes[probeKey] = reason.empty() ? std::string("manual_invalidation") : reason;
        m_FailedDynamicCaptureRevisions.erase(probeKey);
        m_PendingDynamicCaptures.erase(probeKey);
        if (auto it = m_DynamicProbeStatuses.find(probeKey); it != m_DynamicProbeStatuses.end()) {
            it->second.Stale = true;
            it->second.Pending = false;
            it->second.CompletedFaces = 0;
            if (it->second.HasGeneration) {
                it->second.State = DynamicCaptureLifecycleState::Stale;
            }
            it->second.LastDiagnostic = reason;
        }
    }

    void RHISceneRenderer::InvalidateDynamicReflectionProbe(const RenderReflectionProbe& probe, const std::string& reason) {
        InvalidateDynamicReflectionProbe(ResolveDynamicProbeKey(probe), reason);
    }

    std::shared_ptr<const DynamicReflectionProbeGeneration> RHISceneRenderer::GetDynamicReflectionProbe(
        const std::string& probeKey) const {
        const auto it = m_DynamicProbeCaptures.find(probeKey);
        if (it == m_DynamicProbeCaptures.end() || !it->second) {
            return nullptr;
        }
        const uint64_t epoch = m_Device ? m_Device->GetEpoch() : 0;
        if (!it->second->IsValidForDevice(epoch)) {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<const DynamicReflectionProbeGeneration> RHISceneRenderer::GetDynamicReflectionProbe(
        const RenderReflectionProbe& probe) const {
        return GetDynamicReflectionProbe(ResolveDynamicProbeKey(probe));
    }

    std::optional<DynamicReflectionProbeStatus> RHISceneRenderer::GetDynamicReflectionProbeStatus(
        const std::string& probeKey) const {
        const auto it = m_DynamicProbeStatuses.find(probeKey);
        if (it == m_DynamicProbeStatuses.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<DynamicReflectionProbeStatus> RHISceneRenderer::GetDynamicReflectionProbeStatus(
        const RenderReflectionProbe& probe) const {
        return GetDynamicReflectionProbeStatus(ResolveDynamicProbeKey(probe));
    }

    DynamicReflectionCaptureCacheStats RHISceneRenderer::GetDynamicReflectionCaptureCacheStats() const {
        DynamicReflectionCaptureCacheStats stats;
        stats.BudgetBytes = m_HasDynamicCaptureBudgetOverride
            ? m_DynamicCaptureBudgetOverrideBytes : m_LastDynamicCaptureBudgetBytes;
        stats.EvictionCount = m_DynamicCaptureEvictionCount;
        stats.LRUClock = m_CaptureLRUClock;
        stats.LastScheduledProbes = m_LastScheduledProbes;
        stats.LastScheduledFaces = m_LastScheduledFaces;

        const uint64_t epoch = m_Device ? m_Device->GetEpoch() : 0;
        for (const auto& [key, gen] : m_DynamicProbeCaptures) {
            if (gen && gen->IsValidForDevice(epoch)) {
                ++stats.CachedProbeCount;
                stats.UsedBytes += gen->CachedBytes;
            }
        }
        for (const auto& [key, job] : m_PendingDynamicCaptures) {
            (void)key;
            if (!job) continue;
            ++stats.PendingJobCount;
            stats.UsedBytes += job->EstimatedCacheBytes;
        }
        for (const auto& [key, status] : m_DynamicProbeStatuses) {
            (void)key;
            if (status.Stale) ++stats.StaleProbeCount;
            if (status.Failed) ++stats.FailedProbeCount;
        }
        return stats;
    }

    void RHISceneRenderer::ClearDynamicReflectionProbes() {
        if (m_PlanarLastCompletion) {
            if (m_Device && m_PlanarLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_PlanarLastCompletion);
            }
            m_PlanarLastCompletion = {};
        }
        m_PlanarRenderSucceeded = false;
        m_PlanarSession = {};
        m_PlanarScratchViewTargets.reset();
        m_LastPlanarCaptureResult = {};
        ClearPlanarReflectionCaptures();
        if (m_CaptureLastCompletion) {
            if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_CaptureLastCompletion);
            }
            m_CaptureLastCompletion = {};
        }
        m_DynamicProbeCaptures.clear();
        m_PendingDynamicCaptures.clear();
        m_DynamicProbeStatuses.clear();
        m_RequestedManualCaptures.clear();
        m_ForcedInvalidatedProbes.clear();
        m_FailedDynamicCaptureRevisions.clear();
        m_ActiveReflectionProbePrefilter.reset();
        m_ActiveReflectionProbePrefilters.fill({});
        m_ActiveReflectionProbes.fill({});
        m_ActiveReflectionProbeStale.fill(false);
        m_ActiveReflectionProbeCount = 0;
        m_HasActiveReflectionProbe = false;
        m_ActiveDynamicSceneInstanceID = 0;
        m_ActiveDynamicSceneLifetimeEpoch = 0;
        m_LastScheduledProbes = 0;
        m_LastScheduledFaces = 0;
        m_LastDynamicCaptureResult = {};
    }

    void RHISceneRenderer::EvictDynamicProbeEntry(const std::string& probeKey, const std::string& reason) {
        const uint64_t currentEpoch = m_Device ? m_Device->GetEpoch() : 0;
        if (const auto capIt = m_DynamicProbeCaptures.find(probeKey); capIt != m_DynamicProbeCaptures.end()) {
            const auto& evicted = capIt->second;
            if (evicted) {
                if (m_LastCompletion && m_Device && m_LastCompletion.Epoch == currentEpoch) {
                    m_Device->WaitForCompletion(m_LastCompletion);
                    m_LastCompletion = {};
                }
                if (evicted->Completion && m_Device && evicted->Completion.Epoch == currentEpoch) {
                    m_Device->WaitForCompletion(evicted->Completion);
                }
                if (evicted->Cubemap) {
                    const auto evictedHandle = evicted->Cubemap->GetHandle();
                    if (m_ActiveReflectionProbePrefilter && m_ActiveReflectionProbePrefilter->GetHandle() == evictedHandle) {
                        m_ActiveReflectionProbePrefilter.reset();
                        m_HasActiveReflectionProbe = false;
                    }
                    uint32_t writeSlot = 0;
                    for (uint32_t slot = 0; slot < m_ActiveReflectionProbeCount && slot < MaxActiveReflectionProbes; ++slot) {
                        if (m_ActiveReflectionProbePrefilters[slot] &&
                            m_ActiveReflectionProbePrefilters[slot]->GetHandle() == evictedHandle) {
                            continue;
                        }
                        if (writeSlot != slot) {
                            m_ActiveReflectionProbePrefilters[writeSlot] = std::move(m_ActiveReflectionProbePrefilters[slot]);
                            m_ActiveReflectionProbes[writeSlot] = m_ActiveReflectionProbes[slot];
                            m_ActiveReflectionProbeStale[writeSlot] = m_ActiveReflectionProbeStale[slot];
                        }
                        ++writeSlot;
                    }
                    for (uint32_t slot = writeSlot; slot < MaxActiveReflectionProbes; ++slot) {
                        m_ActiveReflectionProbePrefilters[slot].reset();
                        m_ActiveReflectionProbes[slot] = {};
                        m_ActiveReflectionProbeStale[slot] = false;
                    }
                    m_ActiveReflectionProbeCount = writeSlot;
                    if (m_ActiveReflectionProbeCount > 0) {
                        m_ActiveReflectionProbePrefilter = m_ActiveReflectionProbePrefilters[0];
                        m_ActiveReflectionProbe = m_ActiveReflectionProbes[0];
                        m_HasActiveReflectionProbe = true;
                    }
                }
            }
            m_DynamicProbeCaptures.erase(capIt);
            ++m_DynamicCaptureEvictionCount;
        }
        m_PendingDynamicCaptures.erase(probeKey);
        if (auto statusIt = m_DynamicProbeStatuses.find(probeKey); statusIt != m_DynamicProbeStatuses.end()) {
            statusIt->second.HasGeneration = false;
            statusIt->second.Pending = false;
            statusIt->second.Stale = false;
            statusIt->second.CachedBytes = 0;
            statusIt->second.ActiveGenerationID = 0;
            statusIt->second.ActiveInputStamp = 0;
            statusIt->second.CompletedFaces = 0;
            if (!statusIt->second.Failed) {
                statusIt->second.State = DynamicCaptureLifecycleState::Idle;
                statusIt->second.LastDiagnostic = reason;
            }
        }
    }

    bool RHISceneRenderer::SynchronizeDynamicCaptureCacheState(const RenderWorld& world) {
        const uint64_t currentEpoch = m_Device ? m_Device->GetEpoch() : 0;
        if (m_CaptureLastCompletion) {
            if (m_Device && currentEpoch != 0 && m_CaptureLastCompletion.Epoch == currentEpoch) {
                if (!m_Device->WaitForCompletion(m_CaptureLastCompletion)) return false;
            }
            m_CaptureLastCompletion = {};
        }

        const bool sceneReplaced =
            (world.SceneInstanceID != 0 && m_ActiveDynamicSceneInstanceID != 0 &&
             (world.SceneInstanceID != m_ActiveDynamicSceneInstanceID ||
              (world.SceneLifetimeEpoch != 0 && m_ActiveDynamicSceneLifetimeEpoch != 0 &&
               world.SceneLifetimeEpoch != m_ActiveDynamicSceneLifetimeEpoch)));

        std::vector<std::pair<std::string, std::string>> evictions;
        for (const auto& [key, gen] : m_DynamicProbeCaptures) {
            if (!gen || !gen->IsValidForDevice(currentEpoch)) {
                evictions.emplace_back(key, "DYNAMIC_PROBE_EVICTED: device epoch reset");
                continue;
            }
            const bool entrySceneMismatch =
                (world.SceneInstanceID != 0 && gen->SceneInstanceID != 0 &&
                 (gen->SceneInstanceID != world.SceneInstanceID ||
                  (world.SceneLifetimeEpoch != 0 && gen->SceneLifetimeEpoch != 0 &&
                   gen->SceneLifetimeEpoch != world.SceneLifetimeEpoch)));
            if (sceneReplaced || entrySceneMismatch) {
                evictions.emplace_back(key, "DYNAMIC_PROBE_EVICTED: retired scene replaced");
            }
        }
        for (const auto& [key, reason] : evictions) {
            EvictDynamicProbeEntry(key, reason);
        }

        std::vector<std::string> staleJobs;
        for (const auto& [key, job] : m_PendingDynamicCaptures) {
            if (!job) {
                staleJobs.push_back(key);
                continue;
            }
            const bool badDevice = (currentEpoch != 0 && job->DomainStamps.DeviceEpoch != currentEpoch);
            const bool badScene =
                sceneReplaced ||
                (world.SceneInstanceID != 0 && job->DomainStamps.SceneInstanceID != 0 &&
                 (job->DomainStamps.SceneInstanceID != world.SceneInstanceID ||
                  (world.SceneLifetimeEpoch != 0 && job->DomainStamps.SceneLifetimeEpoch != 0 &&
                   job->DomainStamps.SceneLifetimeEpoch != world.SceneLifetimeEpoch)));
            if (badDevice || badScene) {
                staleJobs.push_back(key);
            }
        }
        for (const auto& key : staleJobs) {
            m_PendingDynamicCaptures.erase(key);
        }

        if (sceneReplaced) {
            m_RequestedManualCaptures.clear();
            m_ForcedInvalidatedProbes.clear();
            m_FailedDynamicCaptureRevisions.clear();
            std::erase_if(m_DynamicProbeStatuses, [](const auto& item) {
                return !item.second.HasGeneration;
            });
        }

        if (world.SceneInstanceID != 0) {
            m_ActiveDynamicSceneInstanceID = world.SceneInstanceID;
            m_ActiveDynamicSceneLifetimeEpoch = world.SceneLifetimeEpoch;
        }
        return true;
    }

    bool RHISceneRenderer::EnforceDynamicCaptureCacheBudget(
        const std::string& incomingKey,
        uint64_t incomingBytes,
        uint64_t budgetBytes,
        std::string* outReason) {
        std::vector<ReflectionCaptureContract::CaptureCacheSlotInfo> slots;
        slots.reserve(m_DynamicProbeCaptures.size() + m_PendingDynamicCaptures.size());
        for (const auto& [key, gen] : m_DynamicProbeCaptures) {
            if (!gen) continue;
            const bool isViewPinned = std::find(m_ScheduledReflectionProbeKeys.begin(),
                                                m_ScheduledReflectionProbeKeys.end(),
                                                key) != m_ScheduledReflectionProbeKeys.end();
            slots.push_back(ReflectionCaptureContract::CaptureCacheSlotInfo{
                key,
                gen->CachedBytes,
                gen->LastUsedSequence,
                key == incomingKey || isViewPinned || m_PendingDynamicCaptures.contains(key)
            });
        }
        for (const auto& [key, job] : m_PendingDynamicCaptures) {
            if (key == incomingKey || !job) continue;
            slots.push_back(ReflectionCaptureContract::CaptureCacheSlotInfo{
                "pending:" + key,
                job->EstimatedCacheBytes,
                std::numeric_limits<uint64_t>::max(),
                true
            });
        }

        std::vector<std::string> evictKeys;
        if (!ReflectionCaptureContract::ComputeLRUCacheEvictionPlan(slots, budgetBytes, incomingBytes, &evictKeys, outReason)) {
            return false;
        }
        for (const auto& key : evictKeys) {
            EvictDynamicProbeEntry(key, "DYNAMIC_PROBE_EVICTED: LRU cache budget");
        }
        return true;
    }

    bool RHISceneRenderer::CaptureSingleProbeFace(
        const RenderWorld& filteredWorld,
        const RenderReflectionProbe& probe,
        const ReflectionCaptureContract::ReflectionCapturePolicy& policy,
        const SceneRenderSettings& captureSettings,
        const std::shared_ptr<RHI::RHITexture>& candidateRawCubemap,
        uint32_t faceIndex,
        std::string* outDiagnostic) {
        if (!m_CaptureTargets || !m_CaptureTargets->FaceColor || !m_CaptureTargets->FaceDepth || !m_CaptureTargets->OrientedColor) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_ALLOCATION: capture scratch targets unavailable";
            return false;
        }
        m_CurrentColorFormat = nri::Format::RGBA16_SFLOAT;
        m_CurrentDepthFormat = nri::Format::D32_SFLOAT;
        m_CurrentMSAASamples = 1;

        m_CaptureSession.Active = true;
        m_CaptureSession.FaceIndex = faceIndex;
        m_CaptureSession.Resolution = policy.Resolution;
        m_CaptureSession.FlipHorizontal = ReflectionCaptureContract::GetCubeFaceBasis(faceIndex).FlipHorizontalForCubemapCopy;
        m_CaptureSession.IncludeTransparent = policy.IncludeTransparent;
        m_CaptureSession.IncludeParticles = policy.IncludeParticles;
        m_CaptureSession.IncludeEditorOverlays = policy.IncludeEditorOverlays;
        m_CaptureSession.CandidateCubemap = candidateRawCubemap;
        m_CaptureSession.SourceFace = m_CaptureTargets->FaceColor.get();
        m_CaptureSession.OrientedFace = m_CaptureTargets->OrientedColor.get();

        const PerspectiveCamera faceCamera = ReflectionCaptureContract::BuildCubeFaceCamera(
            probe.Position, faceIndex, policy.NearPlane, policy.FarPlane);

        RenderWorldView(
            filteredWorld,
            faceCamera,
            m_CaptureTargets->FaceColor->GetNative(),
            m_CaptureTargets->FaceColor->GetRTV(),
            m_CaptureTargets->FaceDepth->GetNative(),
            m_CaptureTargets->FaceDepth->GetDSV(),
            policy.Resolution,
            policy.Resolution,
            captureSettings,
            nullptr,
            m_CaptureTargets->ViewTargets);

        m_CaptureSession = {};
        if (!m_CaptureRenderSucceeded || !m_CaptureLastCompletion) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_FACE_FAILED: face " + std::to_string(faceIndex) + " capture failed";
            return false;
        }
        return true;
    }

    struct CaptureViewStateGuard {
        RHISceneRenderer& Renderer;
        RHISceneRenderer::PlanarCaptureSessionState SavedPlanarSession;
        RHIEnvironment SavedEnvironment;
        bool SavedPreparedManualIBL;
        nri::Format SavedColorFormat;
        nri::Format SavedDepthFormat;
        uint8_t SavedMSAASamples;
        std::weak_ptr<RHI::RHIViewTargets> SavedActiveViewTargets;
        RHI::RHITexture* SavedMSAAColor;
        RHI::RHITexture* SavedMSAADepth;
        RHI::RHITexture* SavedHDR;
        RHI::RHITexture* SavedFXAA;
        RHI::RHITexture* SavedOpaqueColor;
        RHI::RHITexture* SavedOpaqueDepth;
        RHI::RHITexture* SavedParticleDepth;
        RHI::RHITexture* SavedOpaqueNormal;
        RHI::RHITexture* SavedScreenStorage;
        LightingFrame SavedLightingFrame;
        ReflectionFrame SavedReflectionFrame;
        GIFrame SavedGIFrame;
        std::shared_ptr<RHI::RHITexture> SavedActiveProbe;
        RenderReflectionProbe SavedProbeDesc;
        bool SavedHasActiveProbe;
        std::array<std::shared_ptr<RHI::RHITexture>, MaxActiveReflectionProbes> SavedActiveProbes;
        std::array<RenderReflectionProbe, MaxActiveReflectionProbes> SavedProbeDescs;
        std::array<bool, MaxActiveReflectionProbes> SavedProbeStaleFlags;
        uint32_t SavedActiveProbeCount;
        std::unordered_map<uint32_t, std::string> SavedMaterialDiagnostics;
        std::unordered_map<uint32_t, RHISceneRenderer::MaterialFeedback> SavedMaterialFeedback;
        uint32_t SavedDrawCalls;
        uint32_t SavedEntityCount;
        uint64_t SavedStreamedBytes;
        bool SavedRenderSucceeded;
        RHICompletionToken SavedCompletion;

        explicit CaptureViewStateGuard(RHISceneRenderer& r)
            : Renderer(r)
            , SavedPlanarSession(r.m_PlanarSession)
            , SavedEnvironment(r.m_Environment)
            , SavedPreparedManualIBL(r.m_PreparedManualIBL)
            , SavedColorFormat(r.m_CurrentColorFormat)
            , SavedDepthFormat(r.m_CurrentDepthFormat)
            , SavedMSAASamples(r.m_CurrentMSAASamples)
            , SavedActiveViewTargets(r.m_ActiveViewTargets)
            , SavedMSAAColor(r.m_MSAAColorTarget)
            , SavedMSAADepth(r.m_MSAADepthTarget)
            , SavedHDR(r.m_IntermediateHDRTarget)
            , SavedFXAA(r.m_IntermediateFXAATarget)
            , SavedOpaqueColor(r.m_OpaqueSceneColor)
            , SavedOpaqueDepth(r.m_OpaqueSceneDepth)
            , SavedParticleDepth(r.m_ParticleSceneDepth)
            , SavedOpaqueNormal(r.m_OpaqueSceneNormal)
            , SavedScreenStorage(r.m_ScreenStorageTarget)
            , SavedLightingFrame(r.m_LastLightingFrame)
            , SavedReflectionFrame(r.m_LastReflectionFrame)
            , SavedGIFrame(r.m_LastGIFrame)
            , SavedActiveProbe(r.m_ActiveReflectionProbePrefilter)
            , SavedProbeDesc(r.m_ActiveReflectionProbe)
            , SavedHasActiveProbe(r.m_HasActiveReflectionProbe)
            , SavedActiveProbes(r.m_ActiveReflectionProbePrefilters)
            , SavedProbeDescs(r.m_ActiveReflectionProbes)
            , SavedProbeStaleFlags(r.m_ActiveReflectionProbeStale)
            , SavedActiveProbeCount(r.m_ActiveReflectionProbeCount)
            , SavedMaterialDiagnostics(r.m_MaterialDiagnostics)
            , SavedMaterialFeedback(r.m_MaterialFeedback)
            , SavedDrawCalls(r.m_LastDrawCallCount)
            , SavedEntityCount(r.m_LastRenderedEntityCount)
            , SavedStreamedBytes(r.m_LastStreamedTransformBytes)
            , SavedRenderSucceeded(r.m_LastRenderSucceeded)
            , SavedCompletion(r.m_LastCompletion) {}

        ~CaptureViewStateGuard() {
            Renderer.m_CaptureSession = {};
            Renderer.m_PlanarSession = SavedPlanarSession;
            Renderer.m_Environment = std::move(SavedEnvironment);
            Renderer.m_PreparedManualIBL = SavedPreparedManualIBL;
            Renderer.m_BoundIBL = {}; // Descriptor inputs were changed by capture preparation.
            Renderer.m_CurrentColorFormat = SavedColorFormat;
            Renderer.m_CurrentDepthFormat = SavedDepthFormat;
            Renderer.m_CurrentMSAASamples = SavedMSAASamples;
            Renderer.m_ActiveViewTargets = SavedActiveViewTargets;
            Renderer.m_MSAAColorTarget = SavedMSAAColor;
            Renderer.m_MSAADepthTarget = SavedMSAADepth;
            Renderer.m_IntermediateHDRTarget = SavedHDR;
            Renderer.m_IntermediateFXAATarget = SavedFXAA;
            Renderer.m_OpaqueSceneColor = SavedOpaqueColor;
            Renderer.m_OpaqueSceneDepth = SavedOpaqueDepth;
            Renderer.m_ParticleSceneDepth = SavedParticleDepth;
            Renderer.m_OpaqueSceneNormal = SavedOpaqueNormal;
            Renderer.m_ScreenStorageTarget = SavedScreenStorage;
            Renderer.m_LastLightingFrame = SavedLightingFrame;
            Renderer.m_LastReflectionFrame = SavedReflectionFrame;
            Renderer.m_LastGIFrame = SavedGIFrame;
            Renderer.m_ActiveReflectionProbePrefilter = SavedActiveProbe;
            Renderer.m_ActiveReflectionProbe = SavedProbeDesc;
            Renderer.m_HasActiveReflectionProbe = SavedHasActiveProbe;
            Renderer.m_ActiveReflectionProbePrefilters = SavedActiveProbes;
            Renderer.m_ActiveReflectionProbes = SavedProbeDescs;
            Renderer.m_ActiveReflectionProbeStale = SavedProbeStaleFlags;
            Renderer.m_ActiveReflectionProbeCount = SavedActiveProbeCount;
            Renderer.m_MaterialDiagnostics = SavedMaterialDiagnostics;
            Renderer.m_MaterialFeedback = SavedMaterialFeedback;
            Renderer.m_LastDrawCallCount = SavedDrawCalls;
            Renderer.m_LastRenderedEntityCount = SavedEntityCount;
            Renderer.m_LastStreamedTransformBytes = SavedStreamedBytes;
            Renderer.m_LastRenderSucceeded = SavedRenderSucceeded;
            Renderer.m_LastCompletion = SavedCompletion;
        }
    };

    namespace {
        ReflectionCaptureContract::ReflectionCapturePolicy BuildDefaultCapturePolicy(const RenderReflectionProbe& probe) {
            ReflectionCaptureContract::ReflectionCapturePolicy policy;
            policy.Resolution = probe.CaptureResolution;
            policy.SampleCount = 1u;
            policy.ColorFormat = nri::Format::RGBA16_SFLOAT;
            policy.DepthFormat = nri::Format::D32_SFLOAT;
            policy.IncludeOpaqueAndCutout = true;
            policy.IncludeSkybox = true;
            policy.IncludeSelectedShadows = probe.CaptureShadows;
            policy.IncludeDirectionalShadows = probe.CaptureShadows;
            policy.IncludeLocalShadows = probe.CaptureShadows;
            policy.IncludeTransparent = false;
            policy.IncludeParticles = false;
            policy.IncludeEditorOverlays = false;
            policy.AllowSSR = false;
            policy.AllowProbeSampling = false;
            policy.IsolateMainViewTAA = true;
            policy.NearPlane = probe.CaptureNearPlane;
            policy.FarPlane = probe.CaptureFarPlane;
            policy.ExcludeEntityID = probe.EntityID;
            return policy;
        }
    }

    bool RHISceneRenderer::WaitForDeviceToken(RHICompletionToken& token) {
        if (token) {
            if (m_Device && token.Epoch == m_Device->GetEpoch()) {
                if (!m_Device->WaitForCompletion(token)) return false;
            } else {
                return false;
            }
            token = {};
        }
        return true;
    }

    bool RHISceneRenderer::ValidateDynamicCaptureSetup(
        const RenderReflectionProbe& probe,
        const ReflectionCaptureContract::ReflectionCapturePolicy& policy,
        uint32_t* outPrefilterMips,
        std::string* outDiagnostic) const {
        if (!m_Initialized || !m_Device || !m_Device->CanSubmit() || !m_Device->CanCreateResources()) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_DEVICE: native render owner is unavailable";
            return false;
        }
        if (!std::isfinite(probe.Position.x) || !std::isfinite(probe.Position.y) || !std::isfinite(probe.Position.z)) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_SETTINGS: non-finite probe position";
            return false;
        }
        std::string policyReason;
        if (!ReflectionCaptureContract::ValidateCapturePolicy(policy, &policyReason)) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_SETTINGS: " + policyReason;
            return false;
        }
        const uint32_t prefilterMips = ReflectionCaptureContract::ComputePrefilterMipCount(policy.Resolution);
        ReflectionCaptureContract::ReflectionPrefilterPolicy prefilterPolicy;
        prefilterPolicy.Resolution = policy.Resolution;
        prefilterPolicy.MipCount = prefilterMips;
        prefilterPolicy.SampleCount = ReflectionCaptureContract::DefaultPrefilterSampleCount;
        prefilterPolicy.ColorFormat = policy.ColorFormat;
        std::string prefilterPolicyReason;
        if (!ReflectionCaptureContract::ValidatePrefilterPolicy(prefilterPolicy, &prefilterPolicyReason)) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_SETTINGS: " + prefilterPolicyReason;
            return false;
        }
        for (uint32_t face = 0; face < ReflectionCaptureContract::FaceCount; ++face) {
            const auto basis = ReflectionCaptureContract::GetCubeFaceBasis(face);
            std::string basisReason;
            if (!ReflectionCaptureContract::ValidateCubeFaceBasis(basis, &basisReason)) {
                if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_BASIS: " + basisReason;
                return false;
            }
        }
        const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
        if (!backendPolicy.Supports(BackendFeature::Compute) ||
            !backendPolicy.Supports(BackendFeature::StorageTextures) ||
            !m_CaptureOrientationPipeline ||
            !m_CaptureDownsamplePipeline ||
            !m_CapturePrefilterPipeline ||
            !m_CapturePrefilterSampler.IsValid()) {
            if (outDiagnostic) *outDiagnostic = "DYNAMIC_PROBE_UNAVAILABLE: compute capture/prefilter pipeline unavailable on active backend";
            return false;
        }
        if (outPrefilterMips) *outPrefilterMips = prefilterMips;
        return true;
    }

    DynamicReflectionCaptureResult RHISceneRenderer::RecordDynamicCaptureFailure(
        const std::string& probeKey,
        const RenderReflectionProbe& probe,
        uint64_t targetInputStamp,
        const std::string& diagnostic,
        uint32_t capturedFaces,
        bool suppressAutoRetry) {
        WaitForDeviceToken(m_CaptureLastCompletion);
        m_CaptureSession = {};
        const std::string keyCopy = probeKey;
        const std::string diagnosticCopy = diagnostic;
        const RenderReflectionProbe probeCopy = probe;
        m_PendingDynamicCaptures.erase(keyCopy);
        m_RequestedManualCaptures.erase(keyCopy);

        const uint64_t currentEpoch = m_Device ? m_Device->GetEpoch() : 0;
        const auto prevIt = m_DynamicProbeCaptures.find(keyCopy);
        const std::shared_ptr<DynamicReflectionProbeGeneration> existing =
            (prevIt != m_DynamicProbeCaptures.end() &&
             prevIt->second &&
             prevIt->second->IsValidForDevice(currentEpoch))
            ? prevIt->second
            : nullptr;
        const bool hasPrevious = bool(existing);
        const uint64_t prevGenerationID = hasPrevious ? existing->GenerationID : 0;

        m_LastDynamicCaptureResult = {};
        m_LastDynamicCaptureResult.Success = false;
        m_LastDynamicCaptureResult.CapturedFaces = capturedFaces;
        m_LastDynamicCaptureResult.PublishedGenerationID = prevGenerationID;
        m_LastDynamicCaptureResult.PreservedPreviousGeneration = hasPrevious;
        m_LastDynamicCaptureResult.ProbeKey = keyCopy;
        m_LastDynamicCaptureResult.Diagnostic = diagnosticCopy;

        auto& status = m_DynamicProbeStatuses[keyCopy];
        status.ProbeKey = keyCopy;
        status.EntityID = probeCopy.EntityID;
        status.StableUUID = probeCopy.StableUUID;
        status.CaptureMode = probeCopy.CaptureMode;
        status.HasGeneration = hasPrevious;
        status.Stale = hasPrevious;
        status.Pending = false;
        status.Failed = true;
        status.State = DynamicCaptureLifecycleState::Failed;
        status.ActiveGenerationID = prevGenerationID;
        status.ActiveInputStamp = hasPrevious ? existing->InputStamp : 0;
        status.TargetInputStamp = targetInputStamp;
        status.CachedBytes = hasPrevious ? existing->CachedBytes : 0;
        status.LastUsedSequence = hasPrevious ? existing->LastUsedSequence : 0;
        status.CompletedFaces = capturedFaces;
        status.TotalFaces = ReflectionCaptureContract::FaceCount;
        ++status.FailureCount;
        status.LastDiagnostic = diagnosticCopy;

        // Capacity is transient: budget edits, completed jobs and evictions can unblock
        // identical input. Only deterministic invalid capture settings suppress retries.
        if (suppressAutoRetry && diagnosticCopy.starts_with("DYNAMIC_PROBE_SETTINGS:")) {
            m_FailedDynamicCaptureRevisions[keyCopy] = targetInputStamp;
        } else {
            m_FailedDynamicCaptureRevisions.erase(keyCopy);
        }
        return m_LastDynamicCaptureResult;
    }

    DynamicReflectionCaptureResult RHISceneRenderer::PublishDynamicProbeGeneration(
        const std::string& probeKey,
        const RenderReflectionProbe& probe,
        const ReflectionCaptureDomainStamps& domainStamps,
        uint64_t inputStampValue,
        uint32_t resolution,
        uint32_t prefilterMips,
        uint64_t cachedBytes,
        const std::shared_ptr<RHITexture>& rawCubemap,
        const std::shared_ptr<RHITexture>& prefilteredCubemap) {
        // Copy fields before erasing any PendingCaptureJob that owns reference parameters.
        const std::string keyCopy = probeKey;
        const RenderReflectionProbe probeCopy = probe;
        const ReflectionCaptureDomainStamps stampsCopy = domainStamps;
        const auto rawCopy = rawCubemap;
        const auto prefilteredCopy = prefilteredCubemap;

        m_PendingDynamicCaptures.erase(keyCopy);
        m_FailedDynamicCaptureRevisions.erase(keyCopy);
        m_ForcedInvalidatedProbes.erase(keyCopy);
        m_RequestedManualCaptures.erase(keyCopy);

        auto published = std::make_shared<DynamicReflectionProbeGeneration>();
        published->ProbeKey = keyCopy;
        published->EntityID = probeCopy.EntityID;
        published->StableUUID = probeCopy.StableUUID;
        published->GenerationID = m_NextDynamicCaptureGenerationID++;
        published->SceneRevision = stampsCopy.SceneRevision;
        published->SceneInstanceID = stampsCopy.SceneInstanceID;
        published->SceneLifetimeEpoch = stampsCopy.SceneLifetimeEpoch;
        published->InputStamp = inputStampValue;
        published->DomainStamps = stampsCopy;
        published->DeviceEpoch = stampsCopy.DeviceEpoch;
        published->CachedBytes = cachedBytes;
        published->LastUsedSequence = ++m_CaptureLRUClock;
        published->Resolution = resolution;
        published->MipCount = prefilterMips;
        published->CaptureMode = probeCopy.CaptureMode;
        published->Position = probeCopy.Position;
        published->Cubemap = prefilteredCopy;
        published->RawCubemap = rawCopy;
        published->Completion = m_CaptureLastCompletion;
        m_DynamicProbeCaptures[keyCopy] = published;
        std::erase_if(m_DynamicCaptureTextures, [&](const auto& handle) { return !m_Device->GetResources().Acquire(handle).IsLive(); });
        m_DynamicCaptureTextures.push_back(prefilteredCopy->GetHandle());
        m_DynamicCaptureTextures.push_back(rawCopy->GetHandle());

        auto& status = m_DynamicProbeStatuses[keyCopy];
        status.ProbeKey = keyCopy;
        status.EntityID = probeCopy.EntityID;
        status.StableUUID = probeCopy.StableUUID;
        status.CaptureMode = probeCopy.CaptureMode;
        status.State = DynamicCaptureLifecycleState::Ready;
        status.HasGeneration = true;
        status.Stale = false;
        status.Pending = false;
        status.Failed = false;
        status.ManualRefreshRequired = false;
        status.ActiveGenerationID = published->GenerationID;
        status.ActiveInputStamp = published->InputStamp;
        status.TargetInputStamp = stampsCopy.CombinedStamp;
        status.CachedBytes = cachedBytes;
        status.LastUsedSequence = published->LastUsedSequence;
        status.CompletedFaces = ReflectionCaptureContract::FaceCount;
        status.TotalFaces = ReflectionCaptureContract::FaceCount;
        status.PendingInvalidation = {};
        status.LastDiagnostic.clear();

        m_LastDynamicCaptureResult = {};
        m_LastDynamicCaptureResult.Success = true;
        m_LastDynamicCaptureResult.CapturedFaces = ReflectionCaptureContract::FaceCount;
        m_LastDynamicCaptureResult.PublishedGenerationID = published->GenerationID;
        m_LastDynamicCaptureResult.PreservedPreviousGeneration = false;
        m_LastDynamicCaptureResult.ProbeKey = keyCopy;
        return m_LastDynamicCaptureResult;
    }

    DynamicReflectionCaptureResult RHISceneRenderer::CaptureDynamicReflectionProbe(
        const RenderWorld& world,
        const RenderReflectionProbe& probe,
        const SceneRenderSettings& settings,
        const ReflectionCaptureContract::ReflectionCapturePolicy* policyOverride) {
        // Reentrant rejection must not clear or wait on the outer session.
        if (m_CaptureInProgress) {
            DynamicReflectionCaptureResult rejected;
            rejected.Diagnostic = "DYNAMIC_PROBE_RECURSION: nested reflection capture is forbidden";
            return rejected;
        }
        m_CaptureInProgress = true;
        struct CaptureGuard { bool& Active; ~CaptureGuard() { Active = false; } } captureGuard{m_CaptureInProgress};

        const std::string probeKey = ResolveDynamicProbeKey(probe);
        auto priorMainCompletion = m_ViewContext.GetCompletion();
        if ((priorMainCompletion && m_Device && priorMainCompletion.Epoch == m_Device->GetEpoch() &&
             !WaitForDeviceToken(priorMainCompletion)) || !SynchronizeDynamicCaptureCacheState(world)) {
            return RecordDynamicCaptureFailure(probeKey, probe, 0,
                "DYNAMIC_PROBE_COMPLETION: prior work did not complete", 0, false);
        }
        m_LastDynamicCaptureBudgetBytes = settings.ReflectionCaptureCacheBudgetBytes;
        m_PendingDynamicCaptures.erase(probeKey);
        const ReflectionCaptureDomainStamps domainStamps = ComputeDynamicCaptureDomainStamps(world, probe, settings);
        const uint64_t inputStamp = domainStamps.CombinedStamp;

        auto failCapture = [&](const std::string& diagnostic, uint32_t capturedFaces = 0) {
            return RecordDynamicCaptureFailure(probeKey, probe, inputStamp, diagnostic, capturedFaces, policyOverride == nullptr);
        };

        if (!world.ErrorMessage.empty()) {
            return failCapture("DYNAMIC_PROBE_WORLD_INVALID: " + world.ErrorMessage);
        }

        const ReflectionCaptureContract::ReflectionCapturePolicy policy =
            policyOverride ? *policyOverride : BuildDefaultCapturePolicy(probe);
        uint32_t prefilterMips = 1u;
        std::string setupDiag;
        if (!ValidateDynamicCaptureSetup(probe, policy, &prefilterMips, &setupDiag)) {
            return failCapture(setupDiag);
        }

        const uint64_t estimatedCacheBytes = ReflectionCaptureContract::EstimateDynamicProbeCacheBytes(
            policy.Resolution, prefilterMips, policy.ColorFormat);
        const uint64_t effectiveBudget = EffectiveDynamicCaptureBudgetBytes(settings);
        std::string budgetReason;
        if (!EnforceDynamicCaptureCacheBudget(probeKey, estimatedCacheBytes, effectiveBudget, &budgetReason)) {
            return failCapture("DYNAMIC_PROBE_BUDGET: " + budgetReason);
        }

        // Synchronize shared host-upload buffers and scratch targets before reconfiguration.
        RHICompletionToken pendingMainCompletion = m_LastCompletion;
        if (!WaitForDeviceToken(pendingMainCompletion) || !WaitForDeviceToken(m_CaptureLastCompletion))
            return failCapture("DYNAMIC_PROBE_COMPLETION: prior work did not complete");

        if (!m_CaptureTargets || !m_CaptureTargets->Configure(policy.Resolution)) {
            return failCapture("DYNAMIC_PROBE_ALLOCATION: scratch face target allocation failed");
        }

        auto candidateRawCubemap = std::make_shared<RHITexture>();
        if (!candidateRawCubemap->Init(RHITextureDesc::CubemapStorage(
                policy.Resolution,
                prefilterMips,
                RHITextureFormat::RGBA16_FLOAT,
                "DynamicReflectionProbeRawCubemap_" + probeKey)) ||
            !candidateRawCubemap->IsValid()) {
            return failCapture("DYNAMIC_PROBE_ALLOCATION: candidate raw cubemap allocation failed");
        }

        auto candidateCubemap = std::make_shared<RHITexture>();
        if (!candidateCubemap->Init(RHITextureDesc::CubemapStorage(
                policy.Resolution,
                prefilterMips,
                RHITextureFormat::RGBA16_FLOAT,
                "DynamicReflectionProbeCubemap_" + probeKey)) ||
            !candidateCubemap->IsValid()) {
            return failCapture("DYNAMIC_PROBE_ALLOCATION: candidate prefiltered cubemap allocation failed");
        }

        const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
        const SceneRenderSettings captureSettings = backendPolicy.ResolveSettings(
            ReflectionCaptureContract::SanitizeCaptureSettings(settings, policy));
        const RenderWorld filteredWorld = ReflectionCaptureContract::FilterCaptureWorld(world, policy);

        CaptureViewStateGuard viewStateGuard(*this);

        for (uint32_t face = 0; face < ReflectionCaptureContract::FaceCount; ++face) {
            if (face > 0 && !WaitForDeviceToken(m_CaptureLastCompletion)) {
                return failCapture("DYNAMIC_PROBE_COMPLETION: prior face did not complete", face);
            }
            std::string faceDiag;
            if (!CaptureSingleProbeFace(filteredWorld, probe, policy, captureSettings, candidateRawCubemap, face, &faceDiag)) {
                return failCapture(faceDiag, face);
            }
        }

        if (!WaitForDeviceToken(m_CaptureLastCompletion)) {
            return failCapture("DYNAMIC_PROBE_COMPLETION: final face capture did not complete", ReflectionCaptureContract::FaceCount);
        }

        std::string prefilterDiag;
        if (!PrefilterCapturedCubemap(*candidateRawCubemap, *candidateCubemap, policy.Resolution, prefilterMips, &prefilterDiag)) {
            return failCapture("DYNAMIC_PROBE_PREFILTER_FAILED: " + prefilterDiag, ReflectionCaptureContract::FaceCount);
        }

        if (!EnforceDynamicCaptureCacheBudget(probeKey, estimatedCacheBytes, effectiveBudget, &budgetReason)) {
            return failCapture("DYNAMIC_PROBE_BUDGET: " + budgetReason, ReflectionCaptureContract::FaceCount);
        }

        return PublishDynamicProbeGeneration(
            probeKey, probe, domainStamps, policyOverride ? 0 : inputStamp,
            policy.Resolution, prefilterMips, estimatedCacheBytes,
            candidateRawCubemap, candidateCubemap);
    }

    void RHISceneRenderer::ScheduleDynamicReflectionCaptures(
        const RenderWorld& world,
        const glm::vec3& cameraPosition,
        const SceneRenderSettings& requestedSettings) {
        if (m_CaptureInProgress) {
            return;
        }
        m_CaptureInProgress = true;
        struct CaptureGuard { bool& Active; ~CaptureGuard() { Active = false; } } captureGuard{m_CaptureInProgress};
        m_LastScheduledProbes = 0;
        m_LastScheduledFaces = 0;
        m_LastDynamicCaptureBudgetBytes = requestedSettings.ReflectionCaptureCacheBudgetBytes;

        // Public render entry points reset their last-result receipt before reaching
        // this method. The view context retains the actual prior main submission.
        // Wait before capture touches shared uploads, descriptors or cache entries.
        auto mainCompletion = m_ViewContext.GetCompletion();
        if (mainCompletion && mainCompletion.Epoch == m_Device->GetEpoch() &&
            !WaitForDeviceToken(mainCompletion)) return;
        if (!SynchronizeDynamicCaptureCacheState(world)) return;

        // A rendered world's probe list is authoritative. Deleted probes and probes
        // switched to authored data must not leave pinned, unfinished jobs behind.
        std::unordered_set<std::string> liveKeys;
        for (const auto& probe : world.ReflectionProbes) {
            if (probe.DynamicCapture && probe.CubemapPath.empty()) liveKeys.insert(ResolveDynamicProbeKey(probe));
        }
        std::unordered_set<std::string> retiredKeys;
        for (const auto& [key, gen] : m_DynamicProbeCaptures) if (!liveKeys.contains(key)) retiredKeys.insert(key);
        for (const auto& [key, job] : m_PendingDynamicCaptures) if (!liveKeys.contains(key)) retiredKeys.insert(key);
        for (const auto& key : retiredKeys) EvictDynamicProbeEntry(key, "DYNAMIC_PROBE_EVICTED: probe removed");
        std::erase_if(m_DynamicProbeStatuses, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_RequestedManualCaptures, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_ForcedInvalidatedProbes, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_FailedDynamicCaptureRevisions, [&](const auto& item) { return !liveKeys.contains(item.first); });

        const auto samplePoints = CollectReflectionSampleBounds(world);
        const auto previewReflection = m_ReflectionService.PrepareView(
            world, cameraPosition, {}, requestedSettings.EnableSSR, world.SceneRevision, samplePoints);
        std::vector<std::string> viewPriorityKeys;
        viewPriorityKeys.reserve(previewReflection.ActiveProbes.size());
        for (const auto& active : previewReflection.ActiveProbes) {
            if (active.DynamicCapture) {
                viewPriorityKeys.push_back(ResolveDynamicProbeKey(active.Probe));
            }
        }
        m_ScheduledReflectionProbeKeys.clear();
        auto pinCandidates = previewReflection.ActiveProbes;
        pinCandidates.insert(pinCandidates.end(), previewReflection.AdditionalCandidates.begin(), previewReflection.AdditionalCandidates.end());
        for (const auto& active : pinCandidates) {
            if (active.DynamicCapture && GetDynamicReflectionProbe(active.Probe)) {
                m_ScheduledReflectionProbeKeys.push_back(ResolveDynamicProbeKey(active.Probe));
                if (m_ScheduledReflectionProbeKeys.size() == MaxActiveReflectionProbes) break;
            }
        }
        struct SelectionGuard {
            std::vector<std::string>& Keys;
            ~SelectionGuard() { Keys.clear(); }
        } selectionGuard{m_ScheduledReflectionProbeKeys};

        struct CandidateProbe {
            const RenderReflectionProbe* Probe = nullptr;
            std::string ProbeKey;
            ReflectionCaptureDomainStamps CurrentStamps{};
            uint64_t PriorityScore = 0;
            bool ManualRequested = false;
        };
        std::vector<CandidateProbe> candidates;
        candidates.reserve(world.ReflectionProbes.size());
        std::vector<ReflectionCaptureContract::ScheduledCaptureWork> workQueue;

        const uint64_t currentEpoch = m_Device ? m_Device->GetEpoch() : 0;
        const uint64_t effectiveBudget = EffectiveDynamicCaptureBudgetBytes(requestedSettings);

        for (size_t idx = 0; idx < world.ReflectionProbes.size(); ++idx) {
            const auto& probe = world.ReflectionProbes[idx];
            if (!probe.DynamicCapture || !probe.CubemapPath.empty()) {
                continue;
            }
            const std::string probeKey = ResolveDynamicProbeKey(probe);
            const ReflectionCaptureDomainStamps currentStamps = ComputeDynamicCaptureDomainStamps(world, probe, requestedSettings);

            const auto existingIt = m_DynamicProbeCaptures.find(probeKey);
            const std::shared_ptr<DynamicReflectionProbeGeneration> existing =
                (existingIt != m_DynamicProbeCaptures.end() &&
                 existingIt->second &&
                 existingIt->second->IsValidForDevice(currentEpoch))
                ? existingIt->second
                : nullptr;

            const bool forcedInvalidation = m_ForcedInvalidatedProbes.contains(probeKey);
            const bool manualRequested = m_RequestedManualCaptures.contains(probeKey);
            ReflectionInvalidation invalidation;
            if (existing) {
                invalidation = m_ReflectionService.EvaluateInvalidation(existing->DomainStamps, currentStamps, manualRequested);
                if (existing->InputStamp == 0) {
                    // A policy-override capture is not a receipt for the default policy.
                    invalidation.SettingsChanged = true;
                }
            } else {
                invalidation = ReflectionInvalidation{true, true, true, true, true, true, true, true, manualRequested};
            }

            const bool needsCapture = !existing || invalidation.Any() || forcedInvalidation;
            auto pendingIt = m_PendingDynamicCaptures.find(probeKey);
            if (pendingIt != m_PendingDynamicCaptures.end()) {
                const auto& job = pendingIt->second;
                const bool incompatible = !job ||
                    (job->Policy.Resolution != probe.CaptureResolution) ||
                    (job->DomainStamps.CombinedStamp != currentStamps.CombinedStamp);
                if (incompatible) {
                    m_PendingDynamicCaptures.erase(pendingIt);
                    pendingIt = m_PendingDynamicCaptures.end();
                }
            }
            const bool hasPendingJob = (pendingIt != m_PendingDynamicCaptures.end() && pendingIt->second != nullptr);

            auto& status = m_DynamicProbeStatuses[probeKey];
            status.ProbeKey = probeKey;
            status.EntityID = probe.EntityID;
            status.StableUUID = probe.StableUUID;
            status.CaptureMode = probe.CaptureMode;
            status.HasGeneration = bool(existing);
            status.ActiveGenerationID = existing ? existing->GenerationID : 0;
            status.ActiveInputStamp = existing ? existing->InputStamp : 0;
            status.TargetInputStamp = currentStamps.CombinedStamp;
            status.CachedBytes = existing ? existing->CachedBytes : 0;
            status.LastUsedSequence = existing ? existing->LastUsedSequence : 0;
            status.TotalFaces = ReflectionCaptureContract::FaceCount;
            status.PendingInvalidation = needsCapture ? invalidation : ReflectionInvalidation{};

            // Manual probes never auto-capture on scene edits without an explicit request or active manual job.
            if (probe.CaptureMode == ReflectionProbeCaptureMode::Manual && !manualRequested && !hasPendingJob) {
                status.Pending = false;
                if (needsCapture) {
                    status.ManualRefreshRequired = true;
                    status.Stale = bool(existing);
                    status.CompletedFaces = existing ? ReflectionCaptureContract::FaceCount : 0u;
                    if (!status.Failed) {
                        status.State = existing ? DynamicCaptureLifecycleState::Stale : DynamicCaptureLifecycleState::Idle;
                        status.LastDiagnostic = existing
                            ? (forcedInvalidation ? m_ForcedInvalidatedProbes[probeKey] : std::string("DYNAMIC_PROBE_STALE: manual probe awaiting capture"))
                            : std::string("DYNAMIC_PROBE_MANUAL_IDLE: awaiting manual capture request");
                    }
                } else {
                    status.ManualRefreshRequired = false;
                    status.Stale = false;
                    status.Failed = false;
                    status.State = DynamicCaptureLifecycleState::Ready;
                    status.CompletedFaces = ReflectionCaptureContract::FaceCount;
                    status.LastDiagnostic.clear();
                }
                continue;
            }

            if (!needsCapture && !hasPendingJob) {
                status.State = DynamicCaptureLifecycleState::Ready;
                status.Stale = false;
                status.Pending = false;
                status.Failed = false;
                status.ManualRefreshRequired = false;
                status.CompletedFaces = ReflectionCaptureContract::FaceCount;
                status.LastDiagnostic.clear();
                continue;
            }

            const auto failedIt = m_FailedDynamicCaptureRevisions.find(probeKey);
            if (failedIt != m_FailedDynamicCaptureRevisions.end() &&
                failedIt->second == currentStamps.CombinedStamp &&
                !manualRequested) {
                status.State = DynamicCaptureLifecycleState::Failed;
                status.Failed = true;
                status.Pending = false;
                status.Stale = bool(existing);
                status.CompletedFaces = existing ? ReflectionCaptureContract::FaceCount : 0u;
                continue;
            }

            uint64_t priorityScore = static_cast<uint64_t>(world.ReflectionProbes.size() - idx);
            if (std::find(viewPriorityKeys.begin(), viewPriorityKeys.end(), probeKey) !=
                viewPriorityKeys.end()) {
                priorityScore += 1000000ull;
            }
            if (hasPendingJob) priorityScore += 10000ull;
            if (manualRequested) priorityScore += 1000ull;

            status.State = DynamicCaptureLifecycleState::Pending;
            status.Pending = true;
            status.Failed = false;
            status.ManualRefreshRequired = false;
            status.Stale = bool(existing) || invalidation.Any();
            status.CompletedFaces = hasPendingJob ? pendingIt->second->CompletedFaces : 0u;
            status.LastDiagnostic = hasPendingJob
                ? ("DYNAMIC_PROBE_PENDING: captured " + std::to_string(pendingIt->second->CompletedFaces) + "/6 faces")
                : "DYNAMIC_PROBE_PENDING: queued for bounded capture";

            candidates.push_back(CandidateProbe{&probe, probeKey, currentStamps, priorityScore, manualRequested});
            workQueue.push_back(ReflectionCaptureContract::ScheduledCaptureWork{
                probeKey,
                hasPendingJob ? (ReflectionCaptureContract::FaceCount - pendingIt->second->CompletedFaces)
                              : ReflectionCaptureContract::FaceCount,
                priorityScore
            });
        }

        if (workQueue.empty()) {
            return;
        }

        const auto plan = ReflectionCaptureContract::ComputeFrameCaptureSchedule(
            workQueue,
            requestedSettings.ReflectionCaptureMaxProbesPerFrame,
            requestedSettings.ReflectionCaptureMaxFacesPerFrame);
        if (plan.empty()) {
            return;
        }

        auto findCandidate = [&](const std::string& key) -> const CandidateProbe* {
            for (const auto& c : candidates) {
                if (c.ProbeKey == key) return &c;
            }
            return nullptr;
        };

        CaptureViewStateGuard viewStateGuard(*this);

        for (const auto& step : plan) {
            const CandidateProbe* candidate = findCandidate(step.ProbeKey);
            if (!candidate) continue;
            const RenderReflectionProbe& probe = *candidate->Probe;
            const std::string& probeKey = candidate->ProbeKey;

            const auto prevIt = m_DynamicProbeCaptures.find(probeKey);
            const bool hasPrevious = (prevIt != m_DynamicProbeCaptures.end() &&
                                      prevIt->second &&
                                      prevIt->second->IsValidForDevice(currentEpoch));

            auto failScheduled = [&](const std::string& diagnostic, uint32_t capturedFaces) {
                RecordDynamicCaptureFailure(
                    probeKey, probe, candidate->CurrentStamps.CombinedStamp, diagnostic, capturedFaces, true);
            };

            if (!m_PendingDynamicCaptures.contains(probeKey)) {
                const ReflectionCaptureContract::ReflectionCapturePolicy policy = BuildDefaultCapturePolicy(probe);
                uint32_t prefilterMips = 1u;
                std::string setupDiag;
                if (!ValidateDynamicCaptureSetup(probe, policy, &prefilterMips, &setupDiag)) {
                    failScheduled(setupDiag, 0);
                    continue;
                }

                const uint64_t estimatedBytes = ReflectionCaptureContract::EstimateDynamicProbeCacheBytes(
                    policy.Resolution, prefilterMips, policy.ColorFormat);
                std::string budgetReason;
                if (!EnforceDynamicCaptureCacheBudget(probeKey, estimatedBytes, effectiveBudget, &budgetReason)) {
                    failScheduled("DYNAMIC_PROBE_BUDGET: " + budgetReason, 0);
                    continue;
                }

                RHICompletionToken pendingMainCompletion = m_LastCompletion;
                if (!WaitForDeviceToken(pendingMainCompletion) || !WaitForDeviceToken(m_CaptureLastCompletion)) {
                    failScheduled("DYNAMIC_PROBE_COMPLETION: prior work did not complete", 0);
                    continue;
                }
                if (!m_CaptureTargets || !m_CaptureTargets->Configure(policy.Resolution)) {
                    failScheduled("DYNAMIC_PROBE_ALLOCATION: scratch face target allocation failed", 0);
                    continue;
                }

                auto candidateRawCubemap = std::make_shared<RHITexture>();
                if (!candidateRawCubemap->Init(RHITextureDesc::CubemapStorage(
                        policy.Resolution,
                        prefilterMips,
                        RHITextureFormat::RGBA16_FLOAT,
                        "DynamicReflectionProbeRawCubemap_" + probeKey)) ||
                    !candidateRawCubemap->IsValid()) {
                    failScheduled("DYNAMIC_PROBE_ALLOCATION: candidate raw cubemap allocation failed", 0);
                    continue;
                }

                auto candidateCubemap = std::make_shared<RHITexture>();
                if (!candidateCubemap->Init(RHITextureDesc::CubemapStorage(
                        policy.Resolution,
                        prefilterMips,
                        RHITextureFormat::RGBA16_FLOAT,
                        "DynamicReflectionProbeCubemap_" + probeKey)) ||
                    !candidateCubemap->IsValid()) {
                    failScheduled("DYNAMIC_PROBE_ALLOCATION: candidate prefiltered cubemap allocation failed", 0);
                    continue;
                }

                const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
                auto newJob = std::make_unique<PendingCaptureJob>();
                newJob->ProbeKey = probeKey;
                newJob->Probe = probe;
                newJob->DomainStamps = candidate->CurrentStamps;
                newJob->Policy = policy;
                newJob->CaptureSettings = backendPolicy.ResolveSettings(
                    ReflectionCaptureContract::SanitizeCaptureSettings(requestedSettings, policy));
                newJob->FilteredWorld = ReflectionCaptureContract::FilterCaptureWorld(world, policy);
                newJob->PrefilterMips = prefilterMips;
                newJob->EstimatedCacheBytes = estimatedBytes;
                newJob->CompletedFaces = 0;
                newJob->CandidateRawCubemap = std::move(candidateRawCubemap);
                newJob->CandidateCubemap = std::move(candidateCubemap);
                newJob->ManualTrigger = candidate->ManualRequested;
                m_PendingDynamicCaptures.emplace(probeKey, std::move(newJob));
            } else {
                const uint32_t priorFaces = m_PendingDynamicCaptures[probeKey]->CompletedFaces;
                RHICompletionToken pendingMainCompletion = m_LastCompletion;
                if (!WaitForDeviceToken(pendingMainCompletion) || !WaitForDeviceToken(m_CaptureLastCompletion)) {
                    failScheduled("DYNAMIC_PROBE_COMPLETION: prior work did not complete", priorFaces);
                    continue;
                }
                if (!m_CaptureTargets || !m_CaptureTargets->Configure(m_PendingDynamicCaptures[probeKey]->Policy.Resolution)) {
                    failScheduled("DYNAMIC_PROBE_ALLOCATION: scratch face target allocation failed", priorFaces);
                    continue;
                }
            }

            PendingCaptureJob& job = *m_PendingDynamicCaptures.at(probeKey);
            bool aborted = false;
            for (uint32_t i = 0; i < step.FacesToRun && job.CompletedFaces < ReflectionCaptureContract::FaceCount; ++i) {
                if (i > 0 && !WaitForDeviceToken(m_CaptureLastCompletion)) {
                    failScheduled("DYNAMIC_PROBE_COMPLETION: prior face did not complete", job.CompletedFaces);
                    aborted = true;
                    break;
                }
                std::string faceDiag;
                if (!CaptureSingleProbeFace(
                        job.FilteredWorld, job.Probe, job.Policy, job.CaptureSettings,
                        job.CandidateRawCubemap, job.CompletedFaces, &faceDiag)) {
                    failScheduled(faceDiag, job.CompletedFaces);
                    aborted = true;
                    break;
                }
                ++job.CompletedFaces;
                ++m_LastScheduledFaces;
            }
            if (aborted) continue;

            if (!WaitForDeviceToken(m_CaptureLastCompletion)) {
                failScheduled("DYNAMIC_PROBE_COMPLETION: captured faces did not complete", job.CompletedFaces);
                continue;
            }

            ++m_LastScheduledProbes;
            if (job.CompletedFaces < ReflectionCaptureContract::FaceCount) {
                auto& status = m_DynamicProbeStatuses[probeKey];
                status.State = DynamicCaptureLifecycleState::Pending;
                status.Pending = true;
                status.Failed = false;
                status.HasGeneration = hasPrevious;
                status.Stale = hasPrevious;
                status.CompletedFaces = job.CompletedFaces;
                status.LastDiagnostic = "DYNAMIC_PROBE_PENDING: captured " + std::to_string(job.CompletedFaces) + "/6 faces";
                continue;
            }

            std::string budgetReason;
            if (!EnforceDynamicCaptureCacheBudget(probeKey, job.EstimatedCacheBytes, effectiveBudget, &budgetReason)) {
                failScheduled("DYNAMIC_PROBE_BUDGET: " + budgetReason, job.CompletedFaces);
                continue;
            }

            std::string prefilterDiag;
            if (!PrefilterCapturedCubemap(*job.CandidateRawCubemap, *job.CandidateCubemap, job.Policy.Resolution, job.PrefilterMips, &prefilterDiag)) {
                failScheduled("DYNAMIC_PROBE_PREFILTER_FAILED: " + prefilterDiag, job.CompletedFaces);
                continue;
            }

            PublishDynamicProbeGeneration(
                probeKey, job.Probe, job.DomainStamps, job.DomainStamps.CombinedStamp,
                job.Policy.Resolution, job.PrefilterMips, job.EstimatedCacheBytes,
                job.CandidateRawCubemap, job.CandidateCubemap);
        }
    }

    PlanarReflectionCaptureResult RHISceneRenderer::CapturePlanarReflection(
        const RenderWorld& world,
        const PerspectiveCamera& mainCamera,
        const RenderPlanarReflector& reflector,
        RHI::RHITexture* targetColor,
        RHI::RHITexture* targetDepth,
        const SceneRenderSettings& settings,
        const PlanarReflectionContract::PlanarReflectionPolicy* policyOverride) {
        PlanarReflectionCaptureResult result{};
        result.ReflectorEntityID = reflector.EntityID;
        result.ReflectorUUID = reflector.StableUUID;
        auto finish = [&](const PlanarReflectionCaptureResult& out) {
            m_LastPlanarCaptureResult = out;
            return out;
        };
        auto failCapture = [&](const std::string& diagnostic) {
            result.Success = false;
            result.Diagnostic = diagnostic;
            return finish(result);
        };

        if (!m_Initialized || !m_Device || !m_Device->CanSubmit()) {
            return failCapture("PLANAR_CAPTURE_NOT_READY: renderer device cannot submit");
        }
        if (m_PlanarSession.Active) {
            return failCapture("PLANAR_RECURSION_BLOCKED: planar capture cannot re-enter itself");
        }
        if (!world.ErrorMessage.empty()) {
            return failCapture("PLANAR_WORLD_INVALID: " + world.ErrorMessage);
        }
        if (!targetColor || !targetColor->IsValid() || !targetColor->GetRTV() ||
            !targetDepth || !targetDepth->IsValid() || !targetDepth->GetDSV()) {
            return failCapture("PLANAR_TARGET_INVALID: valid planar color RTV and depth DSV required");
        }
        const uint64_t currentEpoch = m_Device->GetEpoch();
        if (targetColor->GetHandle().Epoch != currentEpoch || targetDepth->GetHandle().Epoch != currentEpoch) {
            return failCapture("PLANAR_TARGET_EPOCH_MISMATCH: planar target generation belongs to a stale device epoch");
        }
        if (targetColor->GetWidth() == 0u || targetColor->GetHeight() == 0u ||
            targetColor->GetWidth() != targetDepth->GetWidth() ||
            targetColor->GetHeight() != targetDepth->GetHeight() ||
            targetColor->GetDesc().SampleCount != 1u || targetDepth->GetDesc().SampleCount != 1u) {
            return failCapture("PLANAR_TARGET_MISMATCH: planar color and depth must be single-sample with matching extents");
        }

        const PlanarReflectionContract::PlanarReflectionSurface surface =
            PlanarReflectionContract::BuildSurfaceFromReflector(reflector);
        PlanarReflectionContract::PlanarReflectionPolicy policy;
        if (policyOverride) {
            policy = *policyOverride;
        } else {
            policy.ResolutionScale = reflector.ResolutionScale;
            // Surface owns the authored clip bias; policy adds no duplicate bias.
            policy.IncludeSkybox = reflector.CaptureSkybox;
            policy.IncludeShadows = reflector.CaptureShadows;
            policy.ExcludeEntityID = reflector.EntityID;
        }

        const PlanarReflectionContract::MirroredCameraResult mirrored =
            PlanarReflectionContract::BuildMirroredCamera(mainCamera, surface, policy);
        if (!mirrored.Valid) {
            return failCapture(mirrored.Diagnostic.empty() ? "PLANAR_CAMERA_INVALID" : mirrored.Diagnostic);
        }

        // Synchronize prior main/capture/planar submissions before updating shared buffers or scratch targets.
        RHICompletionToken pendingMainCompletion = m_ViewContext.GetCompletion();
        if (!WaitForDeviceToken(pendingMainCompletion) || !WaitForDeviceToken(m_CaptureLastCompletion) ||
            !WaitForDeviceToken(m_PlanarLastCompletion)) {
            return failCapture("PLANAR_COMPLETION_FAILED: prior submission synchronization failed");
        }

        const uint32_t targetW = targetColor->GetWidth();
        const uint32_t targetH = targetColor->GetHeight();
        RHI::RHIViewTargetDesc scratchDesc{};
        scratchDesc.Width = targetW;
        scratchDesc.Height = targetH;
        scratchDesc.ColorFormat = targetColor->GetFormat();
        scratchDesc.DepthFormat = targetDepth->GetFormat();
        scratchDesc.EnableDepth = true;
        scratchDesc.Samples = 1u;
        scratchDesc.OwnPrimary = false;
        scratchDesc.EnablePlanarReflection = false;
        if (!m_PlanarScratchViewTargets) {
            m_PlanarScratchViewTargets = std::make_shared<RHI::RHIViewTargets>();
        }
        if (!m_PlanarScratchViewTargets->IsValid() ||
            m_PlanarScratchViewTargets->GetDesc() != scratchDesc ||
            !m_PlanarScratchViewTargets->Get(RHI::RHIViewTarget::Color) ||
            m_PlanarScratchViewTargets->Get(RHI::RHIViewTarget::Color)->GetNative() != targetColor->GetNative() ||
            !m_PlanarScratchViewTargets->Get(RHI::RHIViewTarget::Depth) ||
            m_PlanarScratchViewTargets->Get(RHI::RHIViewTarget::Depth)->GetNative() != targetDepth->GetNative()) {
            if (!m_PlanarScratchViewTargets->Configure(scratchDesc, targetColor, targetDepth)) {
                return failCapture("PLANAR_SCRATCH_TARGETS_FAILED: unable to configure planar capture view targets");
            }
        }

        const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
        const SceneRenderSettings captureSettings = backendPolicy.ResolveSettings(
            PlanarReflectionContract::SanitizeCaptureSettings(settings, policy));
        const RenderWorld filteredWorld = PlanarReflectionContract::FilterCaptureWorld(world, reflector, policy);

        CaptureViewStateGuard viewStateGuard(*this);
        m_CurrentColorFormat = RHI::ToNRIFormat(targetColor->GetFormat());
        m_CurrentDepthFormat = RHI::ToNRIFormat(targetDepth->GetFormat());
        m_CurrentMSAASamples = 1u;

        m_PlanarSession = {};
        m_PlanarSession.Active = true;
        m_PlanarSession.ReverseWinding = mirrored.ReverseWinding;
        m_PlanarSession.IncludeTransparent = policy.IncludeTransparent;
        m_PlanarSession.IncludeParticles = policy.IncludeParticles;
        m_PlanarSession.IncludeEditorOverlays = policy.IncludeEditorOverlays;
        m_PlanarSession.ExcludeEntityID = reflector.EntityID;
        m_PlanarSession.TargetColor = targetColor;
        m_PlanarSession.TargetDepth = targetDepth;
        m_PlanarSession.WorldPlane = mirrored.WorldPlane;

        // Explicit captures may target a currently published view texture.
        // Once recording can modify its pixels, old metadata no longer owns them.
        std::erase_if(m_PublishedPlanarCaptures, [&](const auto& item) {
            if (item.second.PublishedColorHandle != targetColor->GetHandle()) return false;
            if (auto st = m_PlanarStatuses.find(item.first); st != m_PlanarStatuses.end()) {
                st->second = {};
                st->second.LastDiagnostic = "PLANAR_EVICTED: explicit capture replaced published pixels";
            }
            return true;
        });
        if (m_ActivePlanarResolve.PublishedColorHandle == targetColor->GetHandle()) m_ActivePlanarResolve = {};

        RenderWorldView(
            filteredWorld,
            mirrored.Camera,
            targetColor->GetNative(),
            targetColor->GetRTV(),
            targetDepth->GetNative(),
            targetDepth->GetDSV(),
            targetW,
            targetH,
            captureSettings,
            nullptr,
            m_PlanarScratchViewTargets);

        m_PlanarSession = {};
        if (!m_PlanarRenderSucceeded || !m_PlanarLastCompletion || !WaitForDeviceToken(m_PlanarLastCompletion)) {
            return failCapture("PLANAR_CAPTURE_RENDER_FAILED: planar scene render or completion failed");
        }

        result.Success = true;
        result.ObliqueClippingApplied = mirrored.ObliqueClippingApplied;
        result.ReverseWindingApplied = mirrored.ReverseWinding;
        result.TargetWidth = targetW;
        result.TargetHeight = targetH;
        result.WorldPlane = mirrored.WorldPlane;
        result.ViewPlane = mirrored.ViewPlane;
        return finish(result);
    }

    PlanarReflectionCaptureResult RHISceneRenderer::CapturePlanarReflectionToTargets(
        const RenderWorld& world,
        const PerspectiveCamera& mainCamera,
        const RenderPlanarReflector& reflector,
        RHI::RHIViewTargets& viewTargets,
        const SceneRenderSettings& settings,
        const PlanarReflectionContract::PlanarReflectionPolicy* policyOverride) {
        if (!viewTargets.Validate() || !viewTargets.GetDesc().EnablePlanarReflection) {
            PlanarReflectionCaptureResult fail{};
            fail.ReflectorEntityID = reflector.EntityID;
            fail.ReflectorUUID = reflector.StableUUID;
            fail.Diagnostic = "PLANAR_TARGETS_DISABLED: RHIViewTargets does not enable valid planar targets";
            m_LastPlanarCaptureResult = fail;
            return fail;
        }
        return CapturePlanarReflection(
            world,
            mainCamera,
            reflector,
            viewTargets.GetPlanarColor(),
            viewTargets.GetPlanarDepth(),
            settings,
            policyOverride);
    }

    std::string RHISceneRenderer::ResolvePlanarReflectorKey(const RenderPlanarReflector& reflector) {
        if (reflector.StableUUID != 0) {
            return "planar:uuid:" + std::to_string(static_cast<uint64_t>(reflector.StableUUID));
        }
        if (reflector.EntityID != 0) {
            return "planar:entity:" + std::to_string(reflector.EntityID);
        }
        const auto px = std::bit_cast<uint32_t>(reflector.Position.x);
        const auto py = std::bit_cast<uint32_t>(reflector.Position.y);
        const auto pz = std::bit_cast<uint32_t>(reflector.Position.z);
        return "planar:anon:" + std::to_string(px) + ":" + std::to_string(py) + ":" + std::to_string(pz);
    }

    std::string RHISceneRenderer::ResolvePlanarReflectorKey(uint64_t entityID, UUID stableUUID) {
        RenderPlanarReflector key{};
        key.EntityID = entityID;
        key.StableUUID = stableUUID;
        return ResolvePlanarReflectorKey(key);
    }

    void RHISceneRenderer::RequestPlanarReflectionCapture(const std::string& reflectorKey) {
        m_RequestedManualPlanarCaptures[reflectorKey] = true;
        m_FailedPlanarCaptureRevisions.erase(reflectorKey);
        if (auto it = m_PlanarStatuses.find(reflectorKey); it != m_PlanarStatuses.end()) {
            it->second.ManualRefreshRequired = false;
            it->second.Failed = false;
            it->second.LastDiagnostic = "PLANAR_CAPTURE_REQUESTED";
        }
    }

    void RHISceneRenderer::RequestPlanarReflectionCapture(uint64_t entityID, UUID stableUUID) {
        RequestPlanarReflectionCapture(ResolvePlanarReflectorKey(entityID, stableUUID));
    }

    void RHISceneRenderer::RequestPlanarReflectionCapture(const RenderPlanarReflector& reflector) {
        RequestPlanarReflectionCapture(ResolvePlanarReflectorKey(reflector));
    }

    void RHISceneRenderer::InvalidatePlanarReflection(const std::string& reflectorKey, const std::string& reason) {
        const std::string diag = reason.empty() ? std::string("manual_invalidation") : reason;
        m_ForcedInvalidatedPlanar[reflectorKey] = diag;
        m_FailedPlanarCaptureRevisions.erase(reflectorKey);
        if (auto it = m_PlanarStatuses.find(reflectorKey); it != m_PlanarStatuses.end()) {
            it->second.Stale = true;
            if (it->second.HasCapture) {
                it->second.State = PlanarCaptureLifecycleState::Stale;
            }
            it->second.LastDiagnostic = diag;
        }
    }

    void RHISceneRenderer::InvalidatePlanarReflection(uint64_t entityID, UUID stableUUID, const std::string& reason) {
        InvalidatePlanarReflection(ResolvePlanarReflectorKey(entityID, stableUUID), reason);
    }

    void RHISceneRenderer::InvalidatePlanarReflection(const RenderPlanarReflector& reflector, const std::string& reason) {
        InvalidatePlanarReflection(ResolvePlanarReflectorKey(reflector), reason);
    }

    std::optional<PlanarReflectionCaptureStatus> RHISceneRenderer::GetPlanarReflectionStatus(
        const std::string& reflectorKey) const {
        const auto it = m_PlanarStatuses.find(reflectorKey);
        if (it == m_PlanarStatuses.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<PlanarReflectionCaptureStatus> RHISceneRenderer::GetPlanarReflectionStatus(
        uint64_t entityID, UUID stableUUID) const {
        return GetPlanarReflectionStatus(ResolvePlanarReflectorKey(entityID, stableUUID));
    }

    std::optional<PlanarReflectionCaptureStatus> RHISceneRenderer::GetPlanarReflectionStatus(
        const RenderPlanarReflector& reflector) const {
        return GetPlanarReflectionStatus(ResolvePlanarReflectorKey(reflector));
    }

    PlanarReflectionCacheStats RHISceneRenderer::GetPlanarReflectionCacheStats() const {
        PlanarReflectionCacheStats stats = m_PlanarCacheStats;
        stats.ActiveEntries = static_cast<uint32_t>(m_PublishedPlanarCaptures.size());
        stats.StaleEntries = 0;
        stats.FailedEntries = 0;
        for (const auto& [key, status] : m_PlanarStatuses) {
            (void)key;
            if (status.Failed) {
                ++stats.FailedEntries;
            } else if (status.Stale) {
                ++stats.StaleEntries;
            }
        }
        return stats;
    }

    void RHISceneRenderer::ClearPlanarReflectionCaptures() {
        if (m_PlanarLastCompletion) {
            if (m_Device && m_PlanarLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_PlanarLastCompletion);
            }
            m_PlanarLastCompletion = {};
        }
        m_PlanarPublishScratchColor.Shutdown();
        m_PlanarPublishScratchDepth.Shutdown();
        m_PublishedPlanarCaptures.clear();
        m_PlanarStatuses.clear();
        m_RequestedManualPlanarCaptures.clear();
        m_ForcedInvalidatedPlanar.clear();
        m_FailedPlanarCaptureRevisions.clear();
        m_NextPlanarGenerationID = 1;
        m_ActivePlanarSceneInstanceID = 0;
        m_ActivePlanarSceneLifetimeEpoch = 0;
        m_PlanarCacheStats = {};
        m_ActivePlanarResolve = {};
    }

    PlanarReflectionDomainStamps RHISceneRenderer::ComputePlanarCaptureDomainStamps(
        const RenderWorld& world,
        const PerspectiveCamera& mainCamera,
        const RenderPlanarReflector& reflector,
        uint32_t targetWidth,
        uint32_t targetHeight,
        const SceneRenderSettings& settings) const {
        auto mixBytes = [](uint64_t& hash, const void* data, size_t size) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };
        auto mixVal = [&](uint64_t& hash, const auto&... values) {
            (mixBytes(hash, &values, sizeof(values)), ...);
        };
        auto mixText = [&](uint64_t& hash, const std::string& value) {
            mixVal(hash, value.size());
            for (char c : value) {
                mixVal(hash, c);
            }
        };
        auto mixFileTime = [&](uint64_t& hash, const std::string& path) {
            mixText(hash, path);
            if (path.empty()) return;
            std::error_code error;
            const auto time = std::filesystem::last_write_time(FileSystem::ResolveAssetPath(PathFromUtf8(path)), error);
            mixVal(hash, error.value());
            if (!error) {
                mixVal(hash, time.time_since_epoch().count());
            }
        };

        constexpr uint64_t kFNVOffset = 1469598103934665603ull;
        PlanarReflectionContract::PlanarReflectionDomainStamps stamps{};
        stamps.PlaneStamp = kFNVOffset;
        stamps.CameraStamp = kFNVOffset;
        stamps.GeometryStamp = kFNVOffset;
        stamps.MaterialStamp = kFNVOffset;
        stamps.LightingStamp = kFNVOffset;
        stamps.AssetStamp = kFNVOffset;
        stamps.SettingsStamp = kFNVOffset;
        stamps.SceneRevision = world.SceneRevision;
        stamps.SceneInstanceID = world.SceneInstanceID;
        stamps.SceneLifetimeEpoch = world.SceneLifetimeEpoch;
        stamps.DeviceEpoch = m_Device ? m_Device->GetEpoch() : 0ull;
        stamps.TargetWidth = targetWidth;
        stamps.TargetHeight = targetHeight;

        mixVal(stamps.PlaneStamp,
            reflector.EntityID,
            static_cast<uint64_t>(reflector.StableUUID),
            reflector.Enabled,
            reflector.WorldTransform,
            reflector.Position,
            reflector.Normal,
            reflector.Tangent,
            reflector.Bitangent,
            reflector.HalfExtents,
            reflector.ClipPlaneOffset,
            reflector.ClipToExtents);

        const glm::mat4 camView = mainCamera.GetViewMatrix();
        const glm::mat4 camProj = mainCamera.GetProjectionMatrixZO();
        mixVal(stamps.CameraStamp,
            mainCamera.GetPosition(),
            mainCamera.GetFOV(),
            mainCamera.GetAspectRatio(),
            mainCamera.GetNearClip(),
            mainCamera.GetFarClip(),
            camView,
            camProj);

        mixVal(stamps.SettingsStamp,
            reflector.ResolutionScale,
            reflector.CaptureNearPlane,
            reflector.CaptureFarPlane,
            reflector.CaptureShadows,
            reflector.CaptureSkybox,
            static_cast<uint8_t>(reflector.CaptureMode),
            reflector.EdgeFadeDistance,
            reflector.MaxRoughness,
            reflector.Intensity,
            settings.EnableShadows,
            settings.EnableSkybox,
            settings.EnableStudioLightLobes);

        mixVal(stamps.AssetStamp, stamps.DeviceEpoch);
        for (const auto& texture : {m_IrradianceMap, m_PrefilterMap, m_BRDFLUT}) {
            const auto handle = texture ? texture->GetHandle() : RHIResourceHandle{};
            mixVal(stamps.AssetStamp, handle.Epoch, handle.Slot, handle.Generation);
        }

        mixVal(stamps.LightingStamp,
            m_UseIBL,
            m_IBLAllowed,
            settings.EnableShadows,
            settings.EnableSkybox,
            settings.LocalShadowResolution,
            settings.MaxShadowedPointLights,
            settings.MaxShadowedSpotLights,
            settings.LocalShadowDistance,
            settings.ShadowCascadeCount,
            settings.EnableStudioLightLobes);
        const auto hashDirectional = [&](const RenderDirectionalLight& light) {
            mixVal(stamps.LightingStamp,
                light.Direction, light.Color, light.Intensity, light.AmbientIntensity, light.SolarAngularRadius,
                light.CastsShadows, light.ShadowQuality, light.ShadowBias, light.ShadowSlopeBias,
                light.ShadowNormalBias, light.ShadowFilterRadius, light.MaxShadowDistance, light.CascadeSplitLambda);
        };
        mixVal(stamps.LightingStamp, world.DirectionalLight.has_value());
        if (world.DirectionalLight) {
            hashDirectional(*world.DirectionalLight);
        }
        mixVal(stamps.LightingStamp, world.AdditionalDirectionalLights.size());
        for (const auto& light : world.AdditionalDirectionalLights) {
            hashDirectional(light);
        }
        const auto hashShadow = [&](const LocalShadowSettings& s) {
            mixVal(stamps.LightingStamp, s.CastShadows, s.Bias, s.SlopeBias, s.NormalBias, s.FilterRadius, s.NearPlane);
        };
        mixVal(stamps.LightingStamp, world.PointLights.size(), world.SpotLights.size(), world.RectLights.size());
        for (const auto& light : world.PointLights) {
            mixVal(stamps.LightingStamp, light.EntityID, light.Position, light.Color, light.Intensity, light.Radius, light.Falloff);
            hashShadow(light.Shadows);
        }
        for (const auto& light : world.SpotLights) {
            mixVal(stamps.LightingStamp, light.EntityID, light.Position, light.Direction, light.Color, light.Intensity, light.Range, light.InnerAngle, light.OuterAngle);
            hashShadow(light.Shadows);
        }
        for (const auto& light : world.RectLights) {
            mixVal(stamps.LightingStamp, light.EntityID, light.Position, light.Direction, light.Up, light.Color, light.Intensity, light.Width, light.Height, light.Range, light.TwoSided, light.Shape, light.ShadowPolicy);
        }
        const auto& sky = world.Skybox;
        mixVal(stamps.LightingStamp,
            sky.Authored, sky.Enabled, sky.EnvironmentLighting, sky.SyncWithSun, sky.CustomAzimuth, sky.CustomElevation,
            sky.HemisphereAmbient, sky.SunSize, sky.SunGlow, sky.Turbidity, sky.Type, sky.SunDirection, sky.SunColor,
            sky.ZenithColor, sky.HorizonColor, sky.GroundColor, sky.Intensity, sky.Exposure, sky.EnvironmentRotation);
        if (!sky.HDRPath.empty()) {
            mixText(stamps.LightingStamp, sky.HDRPath);
        }
        mixFileTime(stamps.AssetStamp, sky.HDRPath);
        if (!sky.Authored) {
            mixVal(stamps.LightingStamp,
                m_SkyboxParams.Enabled, m_SkyboxParams.Type, m_SkyboxParams.Intensity, m_SkyboxParams.Exposure,
                m_SkyboxParams.ZenithColor, m_SkyboxParams.HorizonColor, m_SkyboxParams.GroundColor, m_SkyboxParams.SunSize,
                m_SkyboxParams.SunGlow, m_SkyboxParams.Turbidity, m_SkyboxParams.EnvironmentRotation);
        }

        for (const auto& mesh : world.Meshes) {
            if ((reflector.EntityID != 0 && mesh.EntityID == reflector.EntityID) ||
                (reflector.StableUUID != UUID(0) && mesh.StableUUID == reflector.StableUUID)) {
                continue;
            }
            const int builtinVal = mesh.BuiltinId ? static_cast<int>(*mesh.BuiltinId) : -1;
            mixVal(stamps.GeometryStamp,
                mesh.EntityID,
                static_cast<uint64_t>(mesh.StableUUID),
                mesh.WorldTransform,
                mesh.CastsShadows,
                mesh.MeshSourceUUID,
                builtinVal);
            mixText(stamps.GeometryStamp, mesh.MeshURI);

            const auto surface = MaterialResolver::Resolve(mesh.Material);
            mixVal(stamps.MaterialStamp,
                mesh.EntityID,
                surface.Generation,
                static_cast<uint64_t>(mesh.Material.MaterialUUID),
                mesh.Material.AlbedoColor,
                mesh.Material.SecondaryColor,
                mesh.Material.TileScale,
                mesh.Material.PatternMode,
                mesh.Material.Roughness,
                mesh.Material.Metallic,
                mesh.Material.Emission,
                mesh.Material.UVScale,
                mesh.Material.UVOffset,
                mesh.Material.AO,
                mesh.Material.UseAlbedoMap,
                mesh.Material.UseCustomShader);
            mixText(stamps.MaterialStamp, mesh.Material.MaterialPath);
            mixText(stamps.MaterialStamp, mesh.Material.CustomShaderPath);

            for (const auto& binding : surface.Textures) {
                mixText(stamps.MaterialStamp, binding.Path);
                mixFileTime(stamps.AssetStamp, binding.Path);
            }

            const auto view = mesh.NativeView ? mesh.NativeView : mesh.NativeMesh ? mesh.NativeMesh->CaptureView() : nullptr;
            const auto handle = view ? view->GetVertexHandle() : RHIResourceHandle{};
            mixVal(stamps.AssetStamp, handle.Epoch, handle.Slot, handle.Generation);
            const auto indices = view ? view->GetIndexHandle() : RHIResourceHandle{};
            mixVal(stamps.AssetStamp, indices.Epoch, indices.Slot, indices.Generation);

            mixFileTime(stamps.AssetStamp, mesh.Material.CustomShaderPath);
            if (mesh.Material.UseCustomShader && !mesh.Material.CustomShaderPath.empty()) {
                CustomProgramKey key;
                key.ShaderPath = PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(mesh.Material.CustomShaderPath)).lexically_normal());
                key.Backend = m_Device ? m_Device->GetBackend() : RHIType::None;
                mixText(stamps.AssetStamp, m_CustomProgramCache.ComputeFingerprint(key));
            }
            for (const auto& property : mesh.Material.CustomProperties) {
                mixText(stamps.MaterialStamp, property.Desc.Name);
                mixVal(stamps.MaterialStamp, property.FloatVal, property.Vec4Val, property.IntVal, property.BoolVal);
                mixFileTime(stamps.AssetStamp, property.TexturePath);
            }
        }

        for (const auto& skin : world.Skins) {
            mixVal(stamps.GeometryStamp,
                static_cast<bool>(skin.GLMesh != nullptr),
                skin.WorldTransform,
                skin.Color,
                skin.CastsShadows,
                skin.BoneMatrices.size());
            for (const auto& bone : skin.BoneMatrices) {
                mixVal(stamps.GeometryStamp, bone);
            }
        }

        stamps.CombinedStamp = kFNVOffset;
        mixVal(stamps.CombinedStamp,
            stamps.PlaneStamp,
            stamps.CameraStamp,
            stamps.SceneRevision,
            stamps.SceneInstanceID,
            stamps.SceneLifetimeEpoch,
            stamps.GeometryStamp,
            stamps.MaterialStamp,
            stamps.LightingStamp,
            stamps.AssetStamp,
            stamps.SettingsStamp,
            stamps.DeviceEpoch,
            stamps.TargetWidth,
            stamps.TargetHeight);
        if (stamps.CombinedStamp == 0) {
            stamps.CombinedStamp = 1;
        }
        return stamps;
    }

    bool RHISceneRenderer::SynchronizePlanarCaptureCacheState(const RenderWorld& world) {
        const uint64_t currentEpoch = m_Device ? m_Device->GetEpoch() : 0;
        if (m_PlanarLastCompletion) {
            if (m_Device && currentEpoch != 0 && m_PlanarLastCompletion.Epoch == currentEpoch) {
                if (!m_Device->WaitForCompletion(m_PlanarLastCompletion)) return false;
            }
            m_PlanarLastCompletion = {};
        }

        const bool sceneReplaced =
            (world.SceneInstanceID != 0 && m_ActivePlanarSceneInstanceID != 0 &&
             (world.SceneInstanceID != m_ActivePlanarSceneInstanceID ||
              (world.SceneLifetimeEpoch != 0 && m_ActivePlanarSceneLifetimeEpoch != 0 &&
               world.SceneLifetimeEpoch != m_ActivePlanarSceneLifetimeEpoch)));

        std::vector<std::pair<std::string, std::string>> evictions;
        for (const auto& [key, entry] : m_PublishedPlanarCaptures) {
            if (currentEpoch != 0 && entry.DeviceEpoch != currentEpoch) {
                evictions.emplace_back(key, "PLANAR_EVICTED: device epoch reset");
                continue;
            }
            const bool entrySceneMismatch =
                (world.SceneInstanceID != 0 && entry.SceneInstanceID != 0 &&
                 (entry.SceneInstanceID != world.SceneInstanceID ||
                  (world.SceneLifetimeEpoch != 0 && entry.SceneLifetimeEpoch != 0 &&
                   entry.SceneLifetimeEpoch != world.SceneLifetimeEpoch)));
            if (sceneReplaced || entrySceneMismatch) {
                evictions.emplace_back(key, "PLANAR_EVICTED: retired scene replaced");
            }
        }
        for (const auto& [key, reason] : evictions) {
            m_PublishedPlanarCaptures.erase(key);
            if (auto st = m_PlanarStatuses.find(key); st != m_PlanarStatuses.end()) {
                st->second.HasCapture = false;
                st->second.Stale = false;
                st->second.PreservedPreviousGeneration = false;
                st->second.ActiveGenerationID = 0;
                st->second.ActiveInputStamp = 0;
                st->second.State = PlanarCaptureLifecycleState::Idle;
                st->second.LastDiagnostic = reason;
            }
        }

        if (sceneReplaced) {
            m_PlanarStatuses.clear();
            m_RequestedManualPlanarCaptures.clear();
            m_ForcedInvalidatedPlanar.clear();
            m_FailedPlanarCaptureRevisions.clear();
        }
        if (world.SceneInstanceID != 0) {
            m_ActivePlanarSceneInstanceID = world.SceneInstanceID;
            m_ActivePlanarSceneLifetimeEpoch = world.SceneLifetimeEpoch;
        }
        return true;
    }

    bool RHISceneRenderer::PublishPlanarScratchCopy(RHITexture& srcColor, RHITexture& dstColor, std::string* outDiagnostic) {
        auto fail = [&](const char* reason) { if (outDiagnostic) *outDiagnostic = reason; return false; };
        if (!m_Device || !m_Device->CanSubmit() || !srcColor.IsValid() || !dstColor.IsValid())
            return fail("PLANAR_PUBLISH_COPY_INVALID: invalid scratch/target");
        if (ScopedRHIFault::Invoke("PlanarPublishCopy", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS)
            return fail("PLANAR_PUBLISH_COPY_FAULT: publication blocked");

        // GPU work only touches a fresh candidate. Submission/wait failures must
        // never mutate the last-good published image or its generation receipt.
        RHITexture candidate;
        auto desc = RHITextureDesc::RenderTarget2D(dstColor.GetWidth(), dstColor.GetHeight(),
            dstColor.GetFormat(), "PlanarPublicationCandidate");
        if (!candidate.Init(desc)) return fail("PLANAR_PUBLISH_ALLOC_FAILED");
        RHICommandBuffer cmd;
        if (!cmd.Init() || !cmd.Begin()) return fail("PLANAR_PUBLISH_COPY_FAILED");
        RHIDescriptorPool copyPool;
        RHIBuffer copyConstants;
        if (srcColor.GetWidth() == candidate.GetWidth() && srcColor.GetHeight() == candidate.GetHeight()) {
            cmd.CopyTexture(*candidate.GetNative(), nullptr, *srcColor.GetNative(), nullptr);
            cmd.TransitionTexture(candidate.GetNative(), RHIResourceLayout::CopyDest, RHIResourceLayout::ShaderResource);
        } else {
            // The view setting is a resolution cap; each reflector can capture
            // at a lower resolution without reallocating the main view generation.
            auto* pipeline = GetOrCreatePlanarApplyPipeline(ToNRIFormat(candidate.GetFormat()), true);
            RHIDescriptorPoolDesc poolDesc;
            poolDesc.MaxDescriptorSets = 1; poolDesc.MaxTextures = 6; poolDesc.MaxSamplers = 1;
            if (!pipeline || !copyPool.Init(poolDesc) ||
                !copyConstants.Init(RHIBufferDesc::Constant(sizeof(RHIPlanarReflectionConstants), RHIMemoryLocation::HostUpload)))
                return fail("PLANAR_RESAMPLE_SETUP_FAILED");
            auto* set = copyPool.AllocateDescriptorSet(m_PlanarApplyPipelineLayout, 0);
            if (!set) return fail("PLANAR_RESAMPLE_DESCRIPTOR_FAILED");
            for (uint32_t slot = 0; slot < 6; ++slot)
                if (!copyPool.UpdateTexture(set, slot, 0, srcColor.GetSRV())) return fail("PLANAR_RESAMPLE_DESCRIPTOR_FAILED");
            if (!copyPool.UpdateSampler(set, 6, 0, m_PlanarSampler.GetNative())) return fail("PLANAR_RESAMPLE_SAMPLER_FAILED");
            cmd.TransitionTexture(srcColor.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
            cmd.TransitionTexture(candidate.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
            nri::AttachmentDesc attachment{}; attachment.descriptor = candidate.GetRTV();
            attachment.loadOp = nri::LoadOp::CLEAR; attachment.storeOp = nri::StoreOp::STORE;
            nri::RenderingDesc rendering{}; rendering.colors = &attachment; rendering.colorNum = 1;
            cmd.BeginRendering(rendering);
            cmd.SetViewport({0, 0, float(candidate.GetWidth()), float(candidate.GetHeight()), 0, 1});
            cmd.SetScissor({0, 0, static_cast<nri::Dim_t>(candidate.GetWidth()), static_cast<nri::Dim_t>(candidate.GetHeight())});
            cmd.SetPipeline(pipeline);
            cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PlanarApplyPipelineLayout);
            cmd.SetRootDescriptor(0, copyConstants.GetCBV());
            copyPool.Bind(cmd.GetNative());
            cmd.SetDescriptorSet(nri::BindPoint::GRAPHICS, 0, set);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRendering();
            cmd.TransitionTexture(candidate.GetNative(), RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);
        }
        if (!cmd.End() || !cmd.Submit() || !m_Device->WaitIdle())
            return fail("PLANAR_PUBLISH_COPY_SUBMIT_FAILED");
        if (ScopedRHIFault::Invoke("PlanarPublishAfterSubmit", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS)
            return fail("PLANAR_PUBLISH_AFTER_SUBMIT_FAILED");
        const auto retiredHandle = dstColor.GetHandle();
        dstColor = std::move(candidate);
        // Metadata is not a cache of pixels. Retire every receipt for the
        // overwritten slot, so A -> B -> A cannot reuse B's pixels as A.
        std::erase_if(m_PublishedPlanarCaptures, [&](const auto& value) {
            if (value.second.PublishedColorHandle != retiredHandle) return false;
            if (auto st = m_PlanarStatuses.find(value.first); st != m_PlanarStatuses.end()) {
                st->second.HasCapture = false; st->second.ActiveGenerationID = 0;
                st->second.State = PlanarCaptureLifecycleState::Idle;
                st->second.LastDiagnostic = "PLANAR_EVICTED: published view slot replaced";
            }
            return true;
        });
        return true;
    }

    void RHISceneRenderer::SchedulePlanarReflectionCaptures(
        const RenderWorld& world,
        const PerspectiveCamera& mainCamera,
        const SceneRenderSettings& requestedSettings,
        RHI::RHIViewTargets* viewTargets) {
        m_ActivePlanarResolve = {};
        if (!m_Initialized || !m_Device || !m_Device->CanSubmit()) {
            return;
        }
        auto mainCompletion = m_ViewContext.GetCompletion();
        if (mainCompletion && mainCompletion.Epoch == m_Device->GetEpoch() && !WaitForDeviceToken(mainCompletion)) {
            return;
        }
        if (!SynchronizePlanarCaptureCacheState(world)) {
            return;
        }

        std::unordered_set<std::string> liveKeys;
        const RenderPlanarReflector* primaryReflector = nullptr;
        float bestDistanceSq = std::numeric_limits<float>::max();
        bool primaryValid = false;
        for (const auto& reflector : world.PlanarReflectors) {
            if (!reflector.Enabled) {
                continue;
            }
            liveKeys.insert(ResolvePlanarReflectorKey(reflector));
            const glm::vec3 d = reflector.Position - mainCamera.GetPosition();
            const float distSq = glm::dot(d, d);
            const auto surface = PlanarReflectionContract::BuildSurfaceFromReflector(reflector);
            const bool valid = PlanarReflectionContract::BuildMirroredCamera(mainCamera, surface).Valid;
            // An invalid or back-facing nearby surface must not hide a valid mirror.
            // Keep an invalid candidate only when there is no usable surface, for diagnostics.
            if (!primaryReflector || (valid && !primaryValid) || (valid == primaryValid && distSq < bestDistanceSq)) {
                primaryReflector = &reflector;
                primaryValid = valid;
                bestDistanceSq = distSq;
            }
        }
        std::erase_if(m_PublishedPlanarCaptures, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_PlanarStatuses, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_RequestedManualPlanarCaptures, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_ForcedInvalidatedPlanar, [&](const auto& item) { return !liveKeys.contains(item.first); });
        std::erase_if(m_FailedPlanarCaptureRevisions, [&](const auto& item) { return !liveKeys.contains(item.first); });

        if (!primaryReflector || !requestedSettings.EnablePlanarReflections ||
            !viewTargets || !viewTargets->IsValid() || !viewTargets->GetDesc().EnablePlanarReflection) {
            return;
        }
        RHITexture* dstColor = viewTargets->GetPlanarColor();
        RHITexture* dstDepth = viewTargets->GetPlanarDepth();
        if (!dstColor || !dstColor->IsValid() || !dstDepth || !dstDepth->IsValid()) {
            return;
        }

        const uint64_t currentEpoch = m_Device->GetEpoch();
        const float componentScale = std::isfinite(primaryReflector->ResolutionScale)
            ? std::clamp(primaryReflector->ResolutionScale, 0.25f, 1.0f) : 0.5f;
        const float captureScale = std::min(viewTargets->GetDesc().PlanarResolutionScale, componentScale);
        const uint32_t targetW = RHIViewTargets::ComputePlanarTargetDimension(viewTargets->GetDesc().Width, captureScale);
        const uint32_t targetH = RHIViewTargets::ComputePlanarTargetDimension(viewTargets->GetDesc().Height, captureScale);
        const std::string key = ResolvePlanarReflectorKey(*primaryReflector);
        const PlanarReflectionContract::PlanarReflectionDomainStamps currentStamps =
            ComputePlanarCaptureDomainStamps(world, mainCamera, *primaryReflector, targetW, targetH, requestedSettings);

        const auto existingIt = m_PublishedPlanarCaptures.find(key);
        const bool hasPrevious =
            (existingIt != m_PublishedPlanarCaptures.end() &&
             existingIt->second.DeviceEpoch == currentEpoch &&
             existingIt->second.PublishedColorHandle == dstColor->GetHandle());

        const bool forcedInvalidation = m_ForcedInvalidatedPlanar.contains(key);
        const bool manualRequested = m_RequestedManualPlanarCaptures.contains(key);
        PlanarReflectionContract::PlanarReflectionInvalidation invalidation;
        if (hasPrevious) {
            invalidation = PlanarReflectionContract::EvaluateInvalidation(
                existingIt->second.DomainStamps, currentStamps, manualRequested);
        } else {
            invalidation = PlanarReflectionContract::PlanarReflectionInvalidation{
                true, true, true, true, true, true, true, true, true, true, manualRequested};
        }
        const bool needsCapture = !hasPrevious || invalidation.AnyChange() || forcedInvalidation;

        auto& status = m_PlanarStatuses[key];
        status.ReflectorKey = key;
        status.EntityID = primaryReflector->EntityID;
        status.StableUUID = primaryReflector->StableUUID;
        status.CaptureMode = primaryReflector->CaptureMode;
        status.HasCapture = hasPrevious;
        status.ActiveGenerationID = hasPrevious ? existingIt->second.GenerationID : 0;
        status.ActiveInputStamp = hasPrevious ? existingIt->second.DomainStamps.CombinedStamp : 0;
        status.TargetInputStamp = currentStamps.CombinedStamp;
        status.TargetWidth = targetW;
        status.TargetHeight = targetH;
        status.PendingInvalidation = needsCapture ? invalidation : PlanarReflectionContract::PlanarReflectionInvalidation{};
        if (hasPrevious) {
            status.ObliqueClippingApplied = existingIt->second.ObliqueClippingApplied;
            status.ReverseWindingApplied = existingIt->second.ReverseWindingApplied;
        }

        auto activateExistingResolve = [&] {
            if (hasPrevious) {
                m_ActivePlanarResolve.ValidForResolve = true;
                m_ActivePlanarResolve.ReflectorKey = key;
                m_ActivePlanarResolve.Reflector = *primaryReflector; // Resolve-only controls remain live even for stale/manual captures.
                m_ActivePlanarResolve.ReflectedViewProj = existingIt->second.ReflectedViewProj;
                m_ActivePlanarResolve.PublishedColorHandle = existingIt->second.PublishedColorHandle;
            }
        };

        if (primaryReflector->CaptureMode == PlanarReflectionCaptureMode::Manual && !manualRequested) {
            status.ManualRefreshRequired = needsCapture;
            status.Stale = hasPrevious && needsCapture;
            if (needsCapture) {
                if (!status.Failed) {
                    status.State = hasPrevious ? PlanarCaptureLifecycleState::Stale : PlanarCaptureLifecycleState::Idle;
                    status.LastDiagnostic = hasPrevious
                        ? (forcedInvalidation ? m_ForcedInvalidatedPlanar[key] : std::string("PLANAR_STALE: manual reflector awaiting capture"))
                        : std::string("PLANAR_MANUAL_IDLE: awaiting manual capture request");
                }
            } else {
                status.State = PlanarCaptureLifecycleState::Ready;
                status.Failed = false;
                status.PreservedPreviousGeneration = false;
                status.LastDiagnostic.clear();
                ++m_PlanarCacheStats.CacheHits;
            }
            activateExistingResolve();
            return;
        }

        if (!needsCapture) {
            status.State = PlanarCaptureLifecycleState::Ready;
            status.Stale = false;
            status.Failed = false;
            status.ManualRefreshRequired = false;
            status.PreservedPreviousGeneration = false;
            status.LastDiagnostic.clear();
            ++m_PlanarCacheStats.CacheHits;
            activateExistingResolve();
            return;
        }

        if (const auto failedIt = m_FailedPlanarCaptureRevisions.find(key);
            failedIt != m_FailedPlanarCaptureRevisions.end() &&
            failedIt->second == currentStamps.CombinedStamp &&
            !manualRequested) {
            status.State = PlanarCaptureLifecycleState::Failed;
            status.Failed = true;
            status.Stale = hasPrevious;
            status.PreservedPreviousGeneration = hasPrevious;
            activateExistingResolve();
            return;
        }

        auto failScheduled = [&](const std::string& diagnostic, bool retryable = true) {
            m_RequestedManualPlanarCaptures.erase(key);
            if (retryable) m_FailedPlanarCaptureRevisions.erase(key);
            else m_FailedPlanarCaptureRevisions[key] = currentStamps.CombinedStamp;
            ++m_PlanarCacheStats.FailedCaptures;
            status.State = PlanarCaptureLifecycleState::Failed;
            status.Failed = true;
            status.HasCapture = hasPrevious;
            status.Stale = hasPrevious;
            status.ManualRefreshRequired = (primaryReflector->CaptureMode == PlanarReflectionCaptureMode::Manual);
            status.PreservedPreviousGeneration = hasPrevious;
            status.LastDiagnostic = diagnostic;
            activateExistingResolve();
        };

        const PlanarReflectionContract::PlanarReflectionSurface surface =
            PlanarReflectionContract::BuildSurfaceFromReflector(*primaryReflector);
        PlanarReflectionContract::PlanarReflectionPolicy policy;
        policy.ResolutionScale = primaryReflector->ResolutionScale;
        // Surface owns the authored clip bias.
        policy.IncludeSkybox = primaryReflector->CaptureSkybox;
        policy.IncludeShadows = primaryReflector->CaptureShadows;
        policy.ExcludeEntityID = primaryReflector->EntityID;

        const PlanarReflectionContract::MirroredCameraResult mirrored =
            PlanarReflectionContract::BuildMirroredCamera(mainCamera, surface, policy);
        if (!surface.Valid || !mirrored.Valid) {
            failScheduled(mirrored.Diagnostic.empty() ? "PLANAR_CAMERA_INVALID" : mirrored.Diagnostic, false);
            return;
        }

        if (!m_PlanarPublishScratchColor.IsValid() ||
            m_PlanarPublishScratchColor.GetHandle().Epoch != currentEpoch ||
            m_PlanarPublishScratchColor.GetWidth() != targetW ||
            m_PlanarPublishScratchColor.GetHeight() != targetH ||
            m_PlanarPublishScratchColor.GetFormat() != dstColor->GetFormat()) {
            RHITextureDesc scratchColorDesc = RHITextureDesc::RenderTarget2D(
                targetW, targetH, dstColor->GetFormat(), "PlanarPublishScratchColor");
            if (!m_PlanarPublishScratchColor.Init(scratchColorDesc)) {
                failScheduled("PLANAR_SCRATCH_ALLOC_FAILED: unable to allocate planar scratch color target");
                return;
            }
        }
        if (!m_PlanarPublishScratchDepth.IsValid() ||
            m_PlanarPublishScratchDepth.GetHandle().Epoch != currentEpoch ||
            m_PlanarPublishScratchDepth.GetWidth() != targetW ||
            m_PlanarPublishScratchDepth.GetHeight() != targetH ||
            m_PlanarPublishScratchDepth.GetFormat() != dstDepth->GetFormat()) {
            RHITextureDesc scratchDepthDesc = RHITextureDesc::DepthStencil2D(
                targetW, targetH, dstDepth->GetFormat(), "PlanarPublishScratchDepth");
            if (!m_PlanarPublishScratchDepth.Init(scratchDepthDesc)) {
                failScheduled("PLANAR_SCRATCH_DEPTH_ALLOC_FAILED: unable to allocate planar scratch depth target");
                return;
            }
        }

        const PlanarReflectionCaptureResult capRes = CapturePlanarReflection(
            world,
            mainCamera,
            *primaryReflector,
            &m_PlanarPublishScratchColor,
            &m_PlanarPublishScratchDepth,
            requestedSettings,
            &policy);
        if (!capRes.Success) {
            failScheduled(capRes.Diagnostic.empty() ? "PLANAR_CAPTURE_FAILED" : capRes.Diagnostic);
            return;
        }

        std::string copyDiag;
        if (!PublishPlanarScratchCopy(m_PlanarPublishScratchColor, *dstColor, &copyDiag)) {
            failScheduled(copyDiag);
            return;
        }

        m_RequestedManualPlanarCaptures.erase(key);
        m_ForcedInvalidatedPlanar.erase(key);
        m_FailedPlanarCaptureRevisions.erase(key);

        PublishedPlanarEntry entry{};
        entry.ReflectorKey = key;
        entry.EntityID = primaryReflector->EntityID;
        entry.StableUUID = primaryReflector->StableUUID;
        entry.GenerationID = m_NextPlanarGenerationID++;
        entry.SceneInstanceID = world.SceneInstanceID;
        entry.SceneLifetimeEpoch = world.SceneLifetimeEpoch;
        entry.DeviceEpoch = currentEpoch;
        entry.PublishedColorHandle = dstColor->GetHandle();
        entry.TargetWidth = targetW;
        entry.TargetHeight = targetH;
        entry.DomainStamps = currentStamps;
        entry.Reflector = *primaryReflector;
        entry.MirroredCamera = mirrored.Camera;
        entry.ReflectedViewProj = mirrored.Camera.GetProjectionMatrixZO() * mirrored.Camera.GetViewMatrix();
        entry.ObliqueClippingApplied = capRes.ObliqueClippingApplied;
        entry.ReverseWindingApplied = capRes.ReverseWindingApplied;
        m_PublishedPlanarCaptures[key] = entry;

        if (hasPrevious && invalidation.AnyChange()) {
            ++m_PlanarCacheStats.InvalidatedCaptures;
        }
        ++m_PlanarCacheStats.SuccessfulCaptures;
        m_PlanarCacheStats.CurrentGenerationID = entry.GenerationID;

        status.State = PlanarCaptureLifecycleState::Ready;
        status.HasCapture = true;
        status.Stale = false;
        status.Failed = false;
        status.ManualRefreshRequired = false;
        status.PreservedPreviousGeneration = false;
        status.ObliqueClippingApplied = entry.ObliqueClippingApplied;
        status.ReverseWindingApplied = entry.ReverseWindingApplied;
        status.ActiveGenerationID = entry.GenerationID;
        status.ActiveInputStamp = currentStamps.CombinedStamp;
        status.PendingInvalidation = {};
        status.LastDiagnostic.clear();

        m_ActivePlanarResolve.ValidForResolve = true;
        m_ActivePlanarResolve.ReflectorKey = key;
        m_ActivePlanarResolve.Reflector = entry.Reflector;
        m_ActivePlanarResolve.ReflectedViewProj = entry.ReflectedViewProj;
        m_ActivePlanarResolve.PublishedColorHandle = entry.PublishedColorHandle;
    }

    void RHISceneRenderer::ConsumeReadyGPUTimestamps() {
        if (!m_Device) return;
        GPUTimingFrame newest;
        bool hasNewResult = false;
        for (auto& slot : m_GPUTimestampSlots) {
            if (!slot.Pending || m_Device->GetResources().IsInUse(slot.Readback.AcquireLease())) continue;
            GPUTimingFrame result;
            result.Completion = slot.Completion;
            result.Passes.reserve(slot.PassNames.size());
            const auto frequency = m_Device->GetCapabilities().TimestampFrequencyHz;
            auto* values = slot.Readback.IsValid() && frequency
                ? static_cast<const uint64_t*>(slot.Readback.Map()) : nullptr;
            const auto duration = [frequency](uint64_t start, uint64_t end) {
                return frequency && end > start
                    ? static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency) : 0.0;
            };
            if (values && slot.QueryCount >= 2) {
                result.Milliseconds = duration(values[0], values[1]);
                result.Validity = result.Milliseconds > 0.0 ? DiagnosticValidity::Valid : DiagnosticValidity::Unknown;
                for (size_t index = 0; index < slot.PassNames.size(); ++index) {
                    const uint32_t query = 2 + static_cast<uint32_t>(index) * 2;
                    const double milliseconds = query + 1 < slot.QueryCount
                        ? duration(values[query], values[query + 1]) : 0.0;
                    result.Passes.push_back({slot.PassNames[index], milliseconds,
                        milliseconds > 0.0 ? DiagnosticValidity::Valid : DiagnosticValidity::Unknown});
                }
                slot.Readback.Unmap();
            } else {
                result.Validity = DiagnosticValidity::Invalid;
                for (const auto& name : slot.PassNames)
                    result.Passes.push_back({name, 0.0, DiagnosticValidity::Invalid});
            }
            if (!hasNewResult || result.Completion.Value > newest.Completion.Value) {
                newest = std::move(result);
                hasNewResult = true;
            }
            slot.Pending = false; slot.Completion = {}; slot.QueryCount = 0; slot.PassNames.clear();
        }
        if (hasNewResult) {
            m_LastGPUTimestampMilliseconds = newest.Validity == DiagnosticValidity::Valid ? newest.Milliseconds : 0.0;
            m_ViewContext.Diagnostics().SetFrameGPUTime(m_LastGPUTimestampMilliseconds, newest.Validity);
            m_LastGPUTimingFrame = std::move(newest);
        }
    }

    void RHISceneRenderer::BeginGPUTimestampFrame() {
        m_ActiveGPUTimestampSlot = static_cast<size_t>(-1);
        m_ActivePassEndQuery = UINT32_MAX;
        for (size_t attempt = 0; attempt < m_GPUTimestampSlots.size(); ++attempt) {
            const auto index = (m_NextGPUTimestampSlot + attempt) % m_GPUTimestampSlots.size();
            auto& slot = m_GPUTimestampSlots[index];
            if (slot.Pending || !slot.Pool || !slot.Readback.IsValid()) continue;
            slot.PassNames.clear(); slot.QueryCount = 2;
            m_Device->GetNRICore().ResetQueries(*slot.Pool, 0, MaxGPUTimestampQueries);
            m_Device->GetNRICore().CmdEndQuery(*Commands().GetNative(), *slot.Pool, 0);
            m_ActiveGPUTimestampSlot = index;
            m_NextGPUTimestampSlot = (index + 1) % m_GPUTimestampSlots.size();
            return;
        }
        m_ViewContext.Diagnostics().SetFrameGPUTime(0.0, DiagnosticValidity::Unknown);
    }

    void RHISceneRenderer::EndGPUTimestampPass() {
        if (m_ActiveGPUTimestampSlot == static_cast<size_t>(-1) || m_ActivePassEndQuery == UINT32_MAX) return;
        auto& slot = m_GPUTimestampSlots[m_ActiveGPUTimestampSlot];
        m_Device->GetNRICore().CmdEndQuery(*Commands().GetNative(), *slot.Pool, m_ActivePassEndQuery);
        m_ActivePassEndQuery = UINT32_MAX;
    }

    void RHISceneRenderer::BeginGPUTimestampPass(std::string_view name) {
        EndGPUTimestampPass();
        if (m_ActiveGPUTimestampSlot == static_cast<size_t>(-1)) return;
        auto& slot = m_GPUTimestampSlots[m_ActiveGPUTimestampSlot];
        if (slot.QueryCount + 2 > MaxGPUTimestampQueries) {
            slot.PassNames.emplace_back(name); // Unmeasured pass remains Unknown.
            return;
        }
        const uint32_t start = slot.QueryCount;
        slot.QueryCount += 2;
        slot.PassNames.emplace_back(name);
        m_Device->GetNRICore().CmdEndQuery(*Commands().GetNative(), *slot.Pool, start);
        m_ActivePassEndQuery = start + 1;
    }

    void RHISceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                       const PerspectiveCamera& camera,
                                       RHITexture* colorTarget,
                                       RHITexture* depthTarget,
                                       const SceneRenderSettings& settings,
                                       PhysicsScene* physicsScene,
                                       std::shared_ptr<RHI::RHIViewTargets> viewTargets)
    {
        m_LastRenderSucceeded = false; m_LastCompletion = {};
        if (!colorTarget || !colorTarget->IsValid()) return;

        m_CurrentColorFormat = RHI::ToNRIFormat(colorTarget->GetFormat());
        if (depthTarget && depthTarget->IsValid()) {
            m_CurrentDepthFormat = RHI::ToNRIFormat(depthTarget->GetFormat());
        }

        RenderScene(scene, camera,
                    colorTarget->GetNative(),
                    colorTarget->GetRTV(),
                    depthTarget ? depthTarget->GetNative() : nullptr,
                    depthTarget ? depthTarget->GetDSV() : nullptr,
                    colorTarget->GetWidth(), colorTarget->GetHeight(),
                    settings,
                    physicsScene, std::move(viewTargets));
    }

    void RHISceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene,
                                       const PerspectiveCamera& camera,
                                       nri::Texture* colorTarget,
                                       nri::Descriptor* colorRTV,
                                       nri::Texture* depthTarget,
                                       nri::Descriptor* depthDSV,
                                       uint32_t width, uint32_t height,
                                       const SceneRenderSettings& settings,
                                       PhysicsScene* physicsScene,
                                       std::shared_ptr<RHI::RHIViewTargets> viewTargets)
    {
        ConsumeReadyGPUTimestamps();
        m_LastRenderSucceeded = false; m_LastCompletion = {};
        if (!scene) return;
        SceneExtractionOptions options; options.CameraOverride = &camera;
        auto world = ExtractRenderWorld(*scene, options);
        if (!ResolveRenderWorldResources(world)) return;
        RenderWorldView(world, camera, colorTarget, colorRTV, depthTarget, depthDSV, width, height, settings, physicsScene, std::move(viewTargets));
        if (m_LastRenderSucceeded) PublishMaterialFeedback(*scene, world);
    }

    void RHISceneRenderer::PublishMaterialFeedback(Scene& scene, const RenderWorld& world) const {
        if (!scene.CanMutate()) return;
        auto& registry = scene.GetRegistry();
        for (const auto& [id, diagnostic] : m_MaterialDiagnostics) {
            const auto found = world.FindStableUUID(id);
            const auto entity = static_cast<entt::entity>(id);
            if (!found || !registry.valid(entity) || !registry.all_of<IDComponent>(entity) || registry.get<IDComponent>(entity).ID != *found) continue;
            if (auto* material = registry.try_get<MaterialComponent>(entity)) {
                material->CustomShaderDiagnostic = diagnostic;
                material->CustomShaderGeneration = 0;
                if (material->MaterialAsset) material->MaterialAsset->SetCustomShaderDiagnostic(diagnostic);
                if (const auto feedback = m_MaterialFeedback.find(id); feedback != m_MaterialFeedback.end()) {
                    material->CustomProperties = feedback->second.Properties;
                    material->CustomShaderGeneration = feedback->second.Generation;
                    if (material->MaterialAsset) material->MaterialAsset->SetCustomProperties(feedback->second.Properties);
                }
            }
        }
    }

    void RHISceneRenderer::RenderWorldView(const RenderWorld& world,
                                           const PerspectiveCamera& camera,
                                           RHITexture* colorTarget,
                                           RHITexture* depthTarget,
                                           const SceneRenderSettings& settings,
                                           PhysicsScene* physicsScene,
                                           std::shared_ptr<RHI::RHIViewTargets> viewTargets, bool cameraCut,
                                           RenderViewKind viewKind, float deltaTime)
    {
        ConsumeReadyGPUTimestamps();
        m_LastRenderSucceeded = false; m_LastCompletion = {};
        if (!colorTarget || !colorTarget->IsValid()) return;

        m_CurrentColorFormat = RHI::ToNRIFormat(colorTarget->GetFormat());
        m_CurrentDepthFormat = depthTarget ? RHI::ToNRIFormat(depthTarget->GetFormat()) : nri::Format::D32_SFLOAT;

        nri::Texture* nativeColor = colorTarget->GetNative();
        nri::Descriptor* colorRTV = colorTarget->GetRTV();
        nri::Texture* nativeDepth = depthTarget ? depthTarget->GetNative() : nullptr;
        nri::Descriptor* depthDSV = depthTarget ? depthTarget->GetDSV() : nullptr;

        RenderWorldView(world, camera,
                        nativeColor, colorRTV,
                        nativeDepth, depthDSV,
                        colorTarget->GetWidth(), colorTarget->GetHeight(),
                        settings, physicsScene, viewTargets, cameraCut, viewKind, deltaTime);
    }

    void RHISceneRenderer::RenderWorldView(const RenderWorld& world,
                                       const PerspectiveCamera& camera,
                                       nri::Texture* colorTarget,
                                       nri::Descriptor* colorRTV,
                                       nri::Texture* depthTarget,
                                       nri::Descriptor* depthDSV,
                                       uint32_t width, uint32_t height,
                                       const SceneRenderSettings& requestedSettings,
                                       PhysicsScene* physicsScene,
                                       std::shared_ptr<RHI::RHIViewTargets> viewTargets, bool cameraCut,
                                       RenderViewKind viewKind, float deltaTime)
    {
        if (m_PlanarSession.Active) {
            m_PlanarRenderSucceeded = false;
            m_PlanarLastCompletion = {};
        } else if (m_CaptureSession.Active) {
            m_CaptureRenderSucceeded = false;
            m_CaptureLastCompletion = {};
        } else {
            ConsumeReadyGPUTimestamps();
            m_LastRenderSucceeded = false;
            m_LastCompletion = {};
            if (m_CaptureLastCompletion) {
                if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                    if (!m_Device->WaitForCompletion(m_CaptureLastCompletion)) return;
                }
                m_CaptureLastCompletion = {};
            }
            if (m_PlanarLastCompletion) {
                if (m_Device && m_PlanarLastCompletion.Epoch == m_Device->GetEpoch()) {
                    if (!m_Device->WaitForCompletion(m_PlanarLastCompletion)) return;
                }
                m_PlanarLastCompletion = {};
            }
        }
        if (!m_Initialized || !m_Device->CanSubmit() || !colorTarget || !colorRTV || width == 0 || height == 0) return;
        const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
        SceneRenderSettings settings = backendPolicy.ResolveSettings(requestedSettings);
        const bool isSubCapture = m_CaptureSession.Active || m_PlanarSession.Active;
        const auto* fog = world.VolumetricFog ? &*world.VolumetricFog : nullptr;
        bool enableFroxelTargets = false;
        uint8_t froxelQuality = RHIViewTargets::DefaultFroxelQualityTier;
        FroxelFogContract::ResolveViewTargetOptions(
            settings, isSubCapture, depthTarget != nullptr, enableFroxelTargets, froxelQuality, fog);
        const bool linearFog = enableFroxelTargets && FroxelFogContract::RequiresLinearColor(settings, fog);
        const bool linearReflections = (settings.EnableSSR || settings.EnablePlanarReflections) && settings.MSAASamples == 1;
        if ((linearReflections || linearFog) && !settings.EnableToneMapping) {
            // Compose scene radiance before the legacy ACES/gamma conversion.
            settings.EnableToneMapping = true;
            settings.ToneMappingMode = 0;
            settings.Exposure = 1.0f;
            settings.Gamma = 2.2f;
        }
        m_MaterialDiagnostics.clear(); m_MaterialFeedback.clear();

        bool useMSAA = (settings.MSAASamples > 1);
        uint8_t activeSampleCount = useMSAA ? settings.MSAASamples : 1;
        bool useToneMap = settings.EnableToneMapping;
        bool useFXAA = settings.EnableFXAA && !(settings.EnableTAA && activeSampleCount == 1);
        nri::Texture* sceneDepthTexture = depthTarget;

        RHIViewTargetDesc targetDesc;
        targetDesc.Width = width;
        targetDesc.Height = height;
        targetDesc.ColorFormat = FromNRIFormat(m_CurrentColorFormat);
        targetDesc.DepthFormat = FromNRIFormat(m_CurrentDepthFormat);
        targetDesc.EnableDepth = sceneDepthTexture != nullptr;
        targetDesc.Samples = activeSampleCount;
        targetDesc.HDR = useToneMap;
        targetDesc.FXAA = useFXAA;
        targetDesc.OwnPrimary = false;
        targetDesc.EnableNormal = (settings.EnableGTAO || settings.EnableContactShadows ||
            settings.EnableSSR || settings.EnablePlanarReflections);
        targetDesc.EnableScreenStorage = (settings.EnableGTAO || settings.EnableContactShadows ||
            settings.EnableSSR || settings.EnableBloom || settings.EnableAutoExposure);
        targetDesc.EnableHiZ = settings.EnableSSR || settings.HiZVisualizationLevel >= 0;
        targetDesc.EnableReflectionTerms = (settings.EnableSSR || settings.EnablePlanarReflections) && settings.MSAASamples == 1;
        const bool enablePlanarTargets = settings.EnablePlanarReflections && !isSubCapture;
        targetDesc.EnablePlanarReflection = enablePlanarTargets;
        targetDesc.PlanarResolutionScale = enablePlanarTargets
            ? std::clamp(settings.PlanarReflectionResolutionScale,
                         RHI::RHIViewTargets::MinPlanarResolutionScale,
                         RHI::RHIViewTargets::MaxPlanarResolutionScale)
            : 0.5f;
        targetDesc.EnableBloom = settings.EnableBloom;
        targetDesc.EnableGTAO = settings.EnableGTAO || settings.EnableContactShadows;
        targetDesc.EnableAutoExposure = settings.EnableAutoExposure;
        const auto* ddgiGrid = world.DDGIProbeGrid ? &*world.DDGIProbeGrid : nullptr;
        std::array<uint32_t, 3> ddgiDims{0u, 0u, 0u};
        DDGIContract::ResolveViewTargetOptions(settings, isSubCapture, ddgiGrid, targetDesc.EnableDDGI, ddgiDims);
        targetDesc.DDGIGridX = ddgiDims[0];
        targetDesc.DDGIGridY = ddgiDims[1];
        targetDesc.DDGIGridZ = ddgiDims[2];
        targetDesc.EnableVolumetricFog = enableFroxelTargets;
        targetDesc.FroxelQuality = froxelQuality;
        if (settings.EnableTAA) {
            targetDesc.HistoryCount = 2;
            targetDesc.EnableVelocity = (activeSampleCount == 1);
            targetDesc.EnableReactive = (activeSampleCount == 1);
        }

        if (!world.ErrorMessage.empty()) {
            AXLE_CORE_ERROR("View frame acquisition failed");
            return;
        }
        if (viewTargets) {
            auto expected = targetDesc;
            expected.OwnPrimary = viewTargets->GetDesc().OwnPrimary;
            expected.HistoryCount = viewTargets->GetDesc().HistoryCount;
            expected.EnableVelocity = viewTargets->GetDesc().EnableVelocity;
            expected.EnableReactive = viewTargets->GetDesc().EnableReactive;
            if (!viewTargets->IsValid() || viewTargets->GetDesc() != expected ||
                (viewTargets->Get(RHIViewTarget::Color) && viewTargets->Get(RHIViewTarget::Color)->GetNative() != colorTarget) ||
                (viewTargets->Get(RHIViewTarget::Depth) && viewTargets->Get(RHIViewTarget::Depth)->GetNative() != depthTarget)) {
                return;
            }
            if (!isSubCapture) {
                m_ImplicitViewTargets.reset();
                m_ImplicitViewColor = {};
            }
        } else {
            const auto identity = m_Device->GetResources().AcquireNative(colorTarget).GetHandle();
            if (!m_ImplicitViewTargets || !m_ImplicitViewTargets->IsValid() ||
                m_ImplicitViewTargets->GetDesc() != targetDesc || m_ImplicitViewColor != identity) {
                auto candidate = std::make_shared<RHIViewTargets>();
                if (!candidate->Configure(targetDesc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog)) {
                    return;
                }
                m_ImplicitViewTargets = std::move(candidate);
                m_ImplicitViewColor = identity;
            }
            viewTargets = m_ImplicitViewTargets;
        }

        if (!isSubCapture) {
            ScheduleDynamicReflectionCaptures(world, camera.GetPosition(), requestedSettings);
            SchedulePlanarReflectionCaptures(world, camera, requestedSettings, viewTargets.get());
        }

        ViewContext& viewCtx = ActiveViewContext();
        ViewContextInput viewInput;
        viewInput.Kind = viewKind;
        viewInput.Width = width;
        viewInput.Height = height;
        viewInput.Camera = camera;
        viewInput.Settings = settings;
        viewInput.Targets = viewTargets;
        viewInput.FeatureGeneration = HashViewFeaturePolicy(settings) ^
            (static_cast<uint64_t>(m_CurrentColorFormat) * 0x9e3779b97f4a7c15ull) ^
            (static_cast<uint64_t>(m_CurrentDepthFormat) * 0xbf58476d1ce4e5b9ull);
        viewInput.SceneRevision = world.ViewSceneRevision.value_or(world.SceneRevision);
        viewInput.SceneInstanceID = world.SceneInstanceID;
        viewInput.CameraCut = cameraCut;
        viewInput.DeltaTime = (std::isfinite(deltaTime) && deltaTime > 0.0f) ? deltaTime : 0.0f;
        // Effective material identity is published by the live pass schedule.
        viewInput.MaterialGeneration = viewCtx.GetInput().MaterialGeneration;
        viewCtx.Observe(viewInput, m_Device->GetEpoch());
        if (!viewCtx.BeginFrame()) { AXLE_CORE_ERROR("View frame acquisition failed"); return; }
        if (!isSubCapture) {
            ConsumeReadyGPUTimestamps();
        }
        for (const auto& fallback : backendPolicy.CollectFallbacks(requestedSettings))
            viewCtx.Diagnostics().AddFallback(fallback);
        if (settings.EnableVolumetricFog && !isSubCapture) {
            if (!targetDesc.EnableVolumetricFog) {
                if (!targetDesc.EnableDepth) viewCtx.Diagnostics().AddFallback("froxel_targets_disabled:missing_depth");
                else if (activeSampleCount != 1) viewCtx.Diagnostics().AddFallback("froxel_targets_disabled:multisampled_depth");
                else viewCtx.Diagnostics().AddFallback("froxel_targets_disabled:invalid_depth_range");
            } else if (targetDesc.FroxelQuality != static_cast<uint8_t>(settings.FogQuality)) {
                viewCtx.Diagnostics().AddFallback("froxel_quality_fallback:high");
            }
        }
        const auto reflectionSamplePoints = isSubCapture
            ? std::vector<ReflectionSampleBounds>{}
            : CollectReflectionSampleBounds(world);
        const auto reflectionFrame = m_ReflectionService.PrepareView(
            world, camera.GetPosition(), {}, settings.EnableSSR, world.SceneRevision, reflectionSamplePoints);
        m_LastReflectionFrame=reflectionFrame;
        const auto giFrame = m_GIService.Prepare(
            world, settings, backendPolicy.Supports(BackendFeature::Compute), world.SceneRevision);
        m_LastGIFrame=giFrame;
        if (giFrame.Status != GIFrameStatus::Disabled && giFrame.Fallback != GIFallback::None && !giFrame.Diagnostic.empty())
            viewCtx.Diagnostics().AddFallback(giFrame.Diagnostic);
        auto rollback = std::unique_ptr<RHISceneRenderer, std::function<void(RHISceneRenderer*)>>(this,
            [&](auto*) {
                const bool succeeded = m_PlanarSession.Active
                    ? m_PlanarRenderSucceeded
                    : (m_CaptureSession.Active ? m_CaptureRenderSucceeded : m_LastRenderSucceeded);
                if (!succeeded) {
                    viewCtx.AbandonFrame();
                }
            });
        if (!isSubCapture) {
            BeginGPUTimestampFrame();
        } else {
            m_ActiveGPUTimestampSlot = static_cast<size_t>(-1);
            m_ActivePassEndQuery = UINT32_MAX;
        }
        // The extracted CPU snapshot may be released immediately after this
        // call. Record executable owners in the actual submission pin chain.
        if (!world.DebugLines.empty()) {
            std::vector<std::shared_ptr<void>> owners;
            for (const auto& line : world.DebugLines) if (line.OwnerLease) owners.push_back(line.OwnerLease);
            if (!owners.empty()) {
                auto lease = m_Device->GetResources().Publish(RHIResourceKind::Memory, "Scene extension owners", {},
                    [owners = std::move(owners)] {});
                if (!lease || !Commands().KeepAlive(lease)) return;
            }
        }
        m_LastDrawCallCount = 0;
        if (m_CustomDescriptorPool.GetAllocatedSetCount()) m_CustomDescriptorPool.Reset();
        m_LastRenderedEntityCount = 0;
        m_LastStreamedTransformBytes = 0;

        // Determine whether intermediate targets are used for MSAA, Tone Mapping & FXAA
        nri::Texture* sceneTargetTexture = colorTarget;
        nri::Descriptor* sceneTargetRTV = colorRTV;
        nri::Descriptor* sceneDepthDSV = depthDSV;
        nri::Format sceneTargetFormat = m_CurrentColorFormat;

        auto rejectTargets = [&] { viewCtx.AbandonFrame(); };
        m_ActiveViewTargets = viewTargets;
        m_MSAAColorTarget = viewTargets->Get(RHIViewTarget::MSAAColor); m_MSAADepthTarget = viewTargets->Get(RHIViewTarget::MSAADepth);
        m_IntermediateHDRTarget = viewTargets->Get(RHIViewTarget::HDR); m_IntermediateFXAATarget = viewTargets->Get(RHIViewTarget::FXAA);
        m_OpaqueSceneColor = viewTargets->Get(RHIViewTarget::OpaqueColor); m_OpaqueSceneDepth = viewTargets->Get(RHIViewTarget::OpaqueDepth);
        m_ParticleSceneDepth = viewTargets->Get(RHIViewTarget::ParticleDepth);
        m_OpaqueSceneNormal = viewTargets->Get(RHIViewTarget::ScreenNormal);
        m_ScreenStorageTarget = viewTargets->Get(RHIViewTarget::ScreenStorage);
        viewCtx.ObserveTargets(viewTargets);
        viewCtx.PostProcess().BeginFrame(viewCtx.GetInput().Settings,
            m_CurrentColorFormat == nri::Format::RGBA16_SFLOAT);

        // Final target identity must reset histories before choosing jitter or
        // importing temporal slots. Frame acquisition already waited for reuse.
        static const float s_Halton2[8] = { 0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f };
        static const float s_Halton3[8] = { 0.333333f, 0.666667f, 0.111111f, 0.444444f, 0.777778f, 0.222222f, 0.555556f, 0.888889f };
        const auto overlayPolicy = TAAContract::ResolveTemporalOverlayPolicy(settings);
        float jitterX = 0.0f;
        float jitterY = 0.0f;
        if (overlayPolicy.ApplySceneJitter) {
            jitterX = (s_Halton2[viewCtx.GetFrameIndex() % 8] - 0.5f) / static_cast<float>(width) * settings.TAAJitterSpread;
            jitterY = (s_Halton3[viewCtx.GetFrameIndex() % 8] - 0.5f) / static_cast<float>(height) * settings.TAAJitterSpread;
        }
        viewCtx.SetJitter({ jitterX, jitterY });
        const glm::mat4 unjitteredProjZO = camera.GetProjectionMatrixZO();
        const glm::mat4 unjitteredViewProjZO = unjitteredProjZO * camera.GetViewMatrix();
        glm::mat4 projZO = unjitteredProjZO;
        if (overlayPolicy.ApplySceneJitter) {
            projZO[2][0] += 2.0f * jitterX;
            projZO[2][1] += 2.0f * jitterY;
        }
        const glm::mat4 viewProjZO = projZO * camera.GetViewMatrix();
        if (useMSAA) sceneTargetFormat = useToneMap ? nri::Format::RGBA16_SFLOAT : m_CurrentColorFormat;
        if (useMSAA) {
            m_CurrentMSAASamples = activeSampleCount;
            sceneTargetTexture = m_MSAAColorTarget->GetNative(); sceneTargetRTV = m_MSAAColorTarget->GetRTV();
            sceneDepthTexture = m_MSAADepthTarget ? m_MSAADepthTarget->GetNative() : nullptr; sceneDepthDSV = m_MSAADepthTarget ? m_MSAADepthTarget->GetDSV() : nullptr;
        } else if (useToneMap) {
            sceneTargetTexture = m_IntermediateHDRTarget->GetNative(); sceneTargetRTV = m_IntermediateHDRTarget->GetRTV();
            sceneTargetFormat = nri::Format::RGBA16_SFLOAT;
        } else if (useFXAA) {
            sceneTargetTexture = m_IntermediateFXAATarget->GetNative(); sceneTargetRTV = m_IntermediateFXAATarget->GetRTV();
        }
        const bool reverseCaptureWinding = m_PlanarSession.Active && m_PlanarSession.ReverseWinding;
        nri::Pipeline* activePipeline = (!reverseCaptureWinding && sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_PipelineHDR && activeSampleCount == 1)
                                            ? m_PipelineHDR
                                            : (!reverseCaptureWinding && sceneTargetFormat == nri::Format::RGBA8_UNORM && m_Pipeline && activeSampleCount == 1)
                                                  ? m_Pipeline
                                                  : GetOrCreatePipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount,
                                                                        Axle::SurfaceType::Opaque, Axle::CullMode::None, true, false,
                                                                        reverseCaptureWinding);
        if (!activePipeline) { rejectTargets(); return; }

        const auto passCapabilities = backendPolicy.GetPassCapabilities();
        RecordFramePasses({
            world, reflectionFrame, physicsScene,
            colorTarget, colorRTV, useMSAA, useToneMap, useFXAA,
            activeSampleCount, sceneTargetTexture, sceneTargetRTV, sceneDepthTexture,
            sceneDepthDSV, sceneTargetFormat, activePipeline,
            projZO, viewProjZO, unjitteredViewProjZO, giFrame, {passCapabilities.NativeRHI, passCapabilities.Compute,
                passCapabilities.StorageTextures, passCapabilities.Timestamps, passCapabilities.RayTracing}
        });
    }

    bool RHISceneRenderer::RenderToneMapping(RHI::RHICommandBuffer& cmd,
                                            RHI::RHITexture* inputHDR,
                                            RHI::RHITexture* outputTarget,
                                            const RHIToneMappingConstants& constants)
    {
        if (!inputHDR || !inputHDR->IsValid() || !outputTarget || !outputTarget->IsValid()) return false;

        return RenderToneMapping(cmd,
                          inputHDR->GetSRV(),
                          outputTarget->GetNative(),
                          outputTarget->GetRTV(),
                          outputTarget->GetWidth(),
                          outputTarget->GetHeight(),
                          RHI::ToNRIFormat(outputTarget->GetFormat()),
                          constants);
    }

    bool RHISceneRenderer::RenderToneMapping(RHI::RHICommandBuffer& cmd,
                                            RHI::RHITexture* inputHDR,
                                            RHI::RHITexture* outputTarget,
                                            const SceneRenderSettings& settings)
    {
        RHIToneMappingConstants constants{};
        constants.Params = glm::vec4(settings.Exposure, settings.Gamma, static_cast<float>(settings.ToneMappingMode), settings.WhitePoint);
        constants.ColorGrading = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        return RenderToneMapping(cmd, inputHDR, outputTarget, constants);
    }

    bool RHISceneRenderer::RenderToneMapping(RHI::RHICommandBuffer& cmd,
                                            nri::Descriptor* inputHDR_SRV,
                                            nri::Texture* outputTarget,
                                            nri::Descriptor* outputRTV,
                                            uint32_t width, uint32_t height,
                                            nri::Format outFormat,
                                            const RHIToneMappingConstants& constants, RHI::RHIBuffer* frameConstants,
                                            nri::Descriptor* exposureSRV)
    {
        if (!m_Initialized || !inputHDR_SRV || !outputTarget || !outputRTV || width == 0 || height == 0) return false;

        auto& constantBuffer = frameConstants ? *frameConstants : m_ToneMappingConstantBuffer;
        // 1. Update 256-byte Tone Mapping Constants
        if (!constantBuffer.UpdateData(&constants, sizeof(RHIToneMappingConstants)) ||
            !m_ToneMappingDescriptorPool.IsValid() || !m_ToneMappingDescriptorSet) return false;

        // 2. Bind HDR Texture SRV & Sampler in Space 1
        if (m_ToneMappingDescriptorPool.IsValid() && m_ToneMappingDescriptorSet) {
            if (!m_ToneMappingDescriptorPool.UpdateTexture(m_ToneMappingDescriptorSet, 0, 0, inputHDR_SRV) ||
                !m_ToneMappingDescriptorPool.UpdateTexture(m_ToneMappingDescriptorSet, 0, 1, exposureSRV ? exposureSRV : inputHDR_SRV)) return false;
            if (m_ToneMappingSampler.IsValid()) {
                if (!m_ToneMappingDescriptorPool.UpdateSampler(m_ToneMappingDescriptorSet, 1, 0, m_ToneMappingSampler.GetNative())) return false;
            }
        }

        nri::Pipeline* activePipeline = (outFormat == nri::Format::RGBA16_SFLOAT && m_ToneMappingPipelineHDR)
                                            ? m_ToneMappingPipelineHDR
                                            : (outFormat == nri::Format::RGBA8_UNORM && m_ToneMappingPipelineSDR)
                                                  ? m_ToneMappingPipelineSDR
                                                  : GetOrCreateToneMappingPipeline(outFormat);
        if (!activePipeline) return false;

        // 3. Transition Output Texture for Color Rendering
        cmd.TransitionTexture(outputTarget, RHIResourceLayout::ShaderResource, RHIResourceLayout::ColorAttachment);

        // 4. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = outputRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;

        cmd.BeginRendering(renderingDesc);

        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        cmd.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        cmd.SetScissor(scissor);

        cmd.SetPipeline(activePipeline);
        cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_ToneMappingPipelineLayout);

        if (constantBuffer.IsValid() && constantBuffer.GetCBV()) {
            cmd.SetRootDescriptor(0, constantBuffer.GetCBV());
        }

        if (m_ToneMappingDescriptorPool.IsValid() && m_ToneMappingDescriptorSet) {
            m_ToneMappingDescriptorPool.Bind(cmd.GetNative());
            RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, m_ToneMappingDescriptorSet);
        }

        // Draw full-screen triangle via SV_VertexID
        cmd.Draw(3, 1, 0, 0);

        cmd.EndRendering();

        // Transition back to ShaderResource for presentation
        if (!m_Device->GetResources().HasAccessValidator(cmd.GetNative()))
            cmd.TransitionTexture(outputTarget, RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);

        m_LastDrawCallCount++;
        return true;
    }

    bool RHISceneRenderer::RenderFXAA(RHI::RHICommandBuffer& cmd,
                                      RHI::RHITexture* inputTexture,
                                      RHI::RHITexture* outputTarget,
                                      const RHIFXAAConstants& constants)
    {
        if (!inputTexture || !inputTexture->IsValid() || !outputTarget || !outputTarget->IsValid()) return false;

        return RenderFXAA(cmd,
                   inputTexture->GetSRV(),
                   outputTarget->GetNative(),
                   outputTarget->GetRTV(),
                   outputTarget->GetWidth(),
                   outputTarget->GetHeight(),
                   RHI::ToNRIFormat(outputTarget->GetFormat()),
                   constants);
    }

    bool RHISceneRenderer::RenderFXAA(RHI::RHICommandBuffer& cmd,
                                      RHI::RHITexture* inputTexture,
                                      RHI::RHITexture* outputTarget,
                                      const SceneRenderSettings& settings)
    {
        if (!outputTarget || !outputTarget->IsValid()) return false;
        RHIFXAAConstants constants{};
        constants.ScreenSize = glm::vec4(
            static_cast<float>(outputTarget->GetWidth()),
            static_cast<float>(outputTarget->GetHeight()),
            1.0f / static_cast<float>(outputTarget->GetWidth()),
            1.0f / static_cast<float>(outputTarget->GetHeight())
        );
        constants.FXAAParams = glm::vec4(settings.FXAASubpixQuality, settings.FXAAEdgeThreshold, settings.FXAAEdgeThresholdMin, 0.0f);
        return RenderFXAA(cmd, inputTexture, outputTarget, constants);
    }

    bool RHISceneRenderer::RenderFXAA(RHI::RHICommandBuffer& cmd,
                                      nri::Descriptor* inputSRV,
                                      nri::Texture* outputTarget,
                                      nri::Descriptor* outputRTV,
                                      uint32_t width, uint32_t height,
                                      nri::Format outFormat,
                                      const RHIFXAAConstants& constants,
                                      nri::Descriptor* depthSRV, RHI::RHIBuffer* frameConstants)
    {
        if (!m_Initialized || !inputSRV || !outputTarget || !outputRTV || width == 0 || height == 0) return false;

        auto& constantBuffer = frameConstants ? *frameConstants : m_FXAAConstantBuffer;
        // 1. Update 256-byte FXAA Constants
        if (!constantBuffer.UpdateData(&constants, sizeof(RHIFXAAConstants)) ||
            !m_FXAADescriptorPool.IsValid() || !m_FXAADescriptorSet) return false;

        // 2. Bind Texture SRV & Sampler in Space 1
        if (m_FXAADescriptorPool.IsValid() && m_FXAADescriptorSet) {
            if (!m_FXAADescriptorPool.UpdateTexture(m_FXAADescriptorSet, 0, 0, inputSRV) ||
                !m_FXAADescriptorPool.UpdateTexture(m_FXAADescriptorSet, 1, 0,
                    depthSRV ? depthSRV : RHIMaterial::GetDefaultWhiteTexture()->GetSRV())) return false;
            if (m_FXAASampler.IsValid() &&
                !m_FXAADescriptorPool.UpdateSampler(m_FXAADescriptorSet, 2, 0, m_FXAASampler.GetNative())) return false;
        }

        nri::Pipeline* activePipeline = (outFormat == nri::Format::RGBA16_SFLOAT && m_FXAAPipelineHDR)
                                            ? m_FXAAPipelineHDR
                                            : (outFormat == nri::Format::RGBA8_UNORM && m_FXAAPipelineSDR)
                                                  ? m_FXAAPipelineSDR
                                                  : GetOrCreateFXAAPipeline(outFormat);
        if (!activePipeline) return false;

        // 3. Transition Output Texture for Color Rendering
        cmd.TransitionTexture(outputTarget, RHIResourceLayout::ShaderResource, RHIResourceLayout::ColorAttachment);

        // 4. Begin Rendering Pass
        nri::AttachmentDesc colorAttachment = {};
        colorAttachment.descriptor = outputRTV;
        colorAttachment.loadOp = nri::LoadOp::CLEAR;
        colorAttachment.storeOp = nri::StoreOp::STORE;

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;

        cmd.BeginRendering(renderingDesc);

        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        cmd.SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
        cmd.SetScissor(scissor);

        cmd.SetPipeline(activePipeline);
        cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_FXAAPipelineLayout);

        if (constantBuffer.IsValid() && constantBuffer.GetCBV()) {
            cmd.SetRootDescriptor(0, constantBuffer.GetCBV());
        }

        if (m_FXAADescriptorPool.IsValid() && m_FXAADescriptorSet) {
            m_FXAADescriptorPool.Bind(cmd.GetNative());
            RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, m_FXAADescriptorSet);
        }

        // Draw full-screen triangle via SV_VertexID
        cmd.Draw(3, 1, 0, 0);

        cmd.EndRendering();

        // Transition back to ShaderResource for presentation
        if (!m_Device->GetResources().HasAccessValidator(cmd.GetNative()))
            cmd.TransitionTexture(outputTarget, RHIResourceLayout::ColorAttachment, RHIResourceLayout::ShaderResource);

        m_LastDrawCallCount++;
        return true;
    }

    uint32_t RHISceneRenderer::PickEntity(const std::shared_ptr<Scene>& scene, const PerspectiveCamera& camera,
                                          uint32_t width, uint32_t height, uint32_t x, uint32_t y) {
        if (!m_ObjectPicker || !m_ObjectPicker->IsInitialized()) {
            return RHIObjectPicker::InvalidEntityID;
        }
        return m_ObjectPicker->Pick(scene, camera, width, height, x, y);
    }

}
