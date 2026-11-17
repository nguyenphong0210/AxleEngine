#include <Axle/RHISceneRenderer.hpp>
#include "RHISceneRendererPasses.hpp"
#include "Passes/ReflectionCapturePass.hpp"
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

    using namespace Axle::RHI;
    static constexpr uint32_t ForwardTextureCount=static_cast<uint32_t>(RHITextureSlot::Count)+7;

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

        if (!m_ViewContext.Init() || !m_CaptureViewContext.Init() || !m_CascadedShadows.Init() || !m_LocalShadows.Init()) return false;

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
        // Reserve the fixed PBR-default and sky sets in addition to 4096 draws.
        constexpr uint32_t materialSets=4096+3;
        auto materialPoolDesc = RHIDescriptorPoolDesc::Tier2Bindless(materialSets*ForwardTextureCount,materialSets);
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
        auto neutralOcclusion = RHIMaterial::GetDefaultWhiteTexture();
        if (!m_ForwardOcclusionDescriptorSet || !neutralOcclusion ||
            !m_DescriptorPool.UpdateTextureRange(m_ForwardOcclusionDescriptorSet, 0, 0,
                {neutralOcclusion->GetSRV(), neutralOcclusion->GetSRV()})) return false;

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
            m_GTAOApplyDescriptorSet = m_GTAODescriptorPool.AllocateDescriptorSet(m_GTAOApplyPipelineLayout, 0);
            if (!m_GTAOComputeTextureSet || !m_GTAOComputeStorageSet || !m_GTAOApplyDescriptorSet) return false;
            if (m_GTAOSampler.IsValid()) {
                if (!m_GTAODescriptorPool.UpdateSampler(m_GTAOComputeTextureSet, 2, 0, m_GTAOSampler.GetNative()) ||
                    !m_GTAODescriptorPool.UpdateSampler(m_GTAOApplyDescriptorSet, 3, 0, m_GTAOSampler.GetNative())) return false;
            }
        }

        if (!m_GTAOConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIGTAOConstants), RHIMemoryLocation::HostUpload, "GTAOConstantsBuffer"))) return false;

        // Initialize Screen Space Reflections (SSR) & Reflection Probes Pipeline & Resources
        if (!CompileSSRShaders(device) || !CreateSSRPipelineLayout(device) || !CreateSSRPipeline(device)) return false;

        if (!m_SSRSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Linear Clamp Sampler for SSR.");
            return false;
        }

        RHIDescriptorPoolDesc ssrPoolDesc;
        ssrPoolDesc.MaxDescriptorSets = 16;
        ssrPoolDesc.MaxTextures = 64;
        ssrPoolDesc.MaxSamplers = 16;
        ssrPoolDesc.MaxStorageTextures = 16;
        ssrPoolDesc.MaxConstantBuffers = 16;
        if (!m_SSRDescriptorPool.Init(ssrPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize SSR Descriptor Pool!");
            return false;
        } else {
            m_SSRComputeTextureSet = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRComputePipelineLayout, 0);
            m_SSRComputeStorageSet = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRComputePipelineLayout, 1);
            m_SSRApplyDescriptorSet = m_SSRDescriptorPool.AllocateDescriptorSet(m_SSRApplyPipelineLayout, 0);
            if (!m_SSRComputeTextureSet || !m_SSRComputeStorageSet || !m_SSRApplyDescriptorSet) return false;
            if (m_SSRSampler.IsValid()) {
                if (!m_SSRDescriptorPool.UpdateSampler(m_SSRComputeTextureSet, 3, 0, m_SSRSampler.GetNative()) ||
                    !m_SSRDescriptorPool.UpdateSampler(m_SSRApplyDescriptorSet, 4, 0, m_SSRSampler.GetNative())) return false;
            }
        }

        if (!m_SSRConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISSRConstants), RHIMemoryLocation::HostUpload, "SSRConstantsBuffer"))) return false;

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

        // Initialize Volumetric Fog & Atmospheric Participating Media Resources
        if (!CompileFogShaders(device) || !CreateFogPipelineLayout(device) || !CreateFogPipeline(device)) return false;

        if (!m_FogSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Linear Clamp Sampler for Fog.");
            return false;
        }

        RHIDescriptorPoolDesc fogPoolDesc;
        fogPoolDesc.MaxDescriptorSets = 16;
        fogPoolDesc.MaxTextures = 64;
        fogPoolDesc.MaxSamplers = 16;
        fogPoolDesc.MaxStorageTextures = 16;
        fogPoolDesc.MaxConstantBuffers = 16;
        if (!m_FogDescriptorPool.Init(fogPoolDesc)) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Fog Descriptor Pool!");
            return false;
        } else {
            m_FogComputeTextureSet = m_FogDescriptorPool.AllocateDescriptorSet(m_FogComputePipelineLayout, 0);
            m_FogComputeStorageSet = m_FogDescriptorPool.AllocateDescriptorSet(m_FogComputePipelineLayout, 1);
            m_FogApplyDescriptorSet = m_FogDescriptorPool.AllocateDescriptorSet(m_FogApplyPipelineLayout, 0);
            if (!m_FogComputeTextureSet || !m_FogComputeStorageSet || !m_FogApplyDescriptorSet) return false;
            if (m_FogSampler.IsValid()) {
                if (!m_FogDescriptorPool.UpdateSampler(m_FogComputeTextureSet, 1, 0, m_FogSampler.GetNative()) ||
                    !m_FogDescriptorPool.UpdateSampler(m_FogApplyDescriptorSet, 2, 0, m_FogSampler.GetNative())) return false;
            }
        }

        if (!m_FogConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIVolumetricFogConstants), RHIMemoryLocation::HostUpload, "FogConstantsBuffer"))) return false;

        // Initialize Physically Based Pyramid / Kawase Bloom Resources
        if (!CompileBloomShaders(device) || !CreateBloomPipelineLayout(device) || !CreateBloomPipeline(device)) return false;

        if (!m_BloomSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize Linear Clamp Sampler for Bloom.");
            return false;
        }

        RHIDescriptorPoolDesc bloomPoolDesc;
        bloomPoolDesc.MaxDescriptorSets = 32;
        bloomPoolDesc.MaxTextures = 64;
        bloomPoolDesc.MaxSamplers = 16;
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
            if (m_BloomSampler.IsValid()) {
                if (!m_BloomDescriptorPool.UpdateSampler(m_BloomComputeTextureSet, 1, 0, m_BloomSampler.GetNative()) ||
                    !m_BloomDescriptorPool.UpdateSampler(m_BloomApplyDescriptorSet, 2, 0, m_BloomSampler.GetNative())) return false;
                for (auto* set : m_BloomComputeTextureSets)
                    if (!m_BloomDescriptorPool.UpdateSampler(set, 1, 0, m_BloomSampler.GetNative())) return false;
            }
        }

        if (!m_BloomConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIBloomConstants), RHIMemoryLocation::HostUpload, "BloomConstantsBuffer"))) return false;

        // Initialize histogram-based auto exposure. The result is kept in a
        // one-pixel storage target and can be consumed by later post passes.
        if (!CompileAutoExposureShaders(device) || !CreateAutoExposurePipelineLayout(device) || !CreateAutoExposurePipeline(device)) return false;
        if (!m_AutoExposureSampler.Init(RHISamplerDesc::Point(RHIAddressMode::ClampToEdge))) return false;
        RHIDescriptorPoolDesc exposurePoolDesc;
        exposurePoolDesc.MaxDescriptorSets = 8;
        exposurePoolDesc.MaxTextures = 8;
        exposurePoolDesc.MaxSamplers = 8;
        exposurePoolDesc.MaxStorageTextures = 8;
        exposurePoolDesc.MaxConstantBuffers = 8;
        if (!m_AutoExposureDescriptorPool.Init(exposurePoolDesc)) return false;
        m_AutoExposureTextureSet = m_AutoExposureDescriptorPool.AllocateDescriptorSet(m_AutoExposurePipelineLayout, 0);
        m_AutoExposureStorageSet = m_AutoExposureDescriptorPool.AllocateDescriptorSet(m_AutoExposurePipelineLayout, 1);
        if (!m_AutoExposureTextureSet || !m_AutoExposureStorageSet) return false;
        if (!m_AutoExposureDescriptorPool.UpdateSampler(m_AutoExposureTextureSet, 1, 0, m_AutoExposureSampler.GetNative())) return false;
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
                if (!m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 3, 0, m_TAALinearSampler.GetNative()) ||
                    !m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 4, 0, m_TAAPointSampler.GetNative())) return false;
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
        m_GTAOApplyPipelineSDR = nullptr;
        m_GTAOApplyPipelineHDR = nullptr;
        m_GTAOComputePipelineLayout.Shutdown();
        m_GTAOApplyPipelineLayout.Shutdown();
        m_GTAOConstantBuffer.Shutdown();
        m_GTAOComputeTextureSet = nullptr;
        m_GTAOComputeStorageSet = nullptr;
        m_GTAOApplyDescriptorSet = nullptr;
        m_GTAODescriptorPool.Shutdown();
        m_GTAOSampler.Shutdown();

        m_SSRComputePipeline = nullptr;
        m_SSRApplyPipelineSDR = nullptr;
        m_SSRApplyPipelineHDR = nullptr;
        m_SSRComputePipelineLayout.Shutdown();
        m_SSRApplyPipelineLayout.Shutdown();
        m_SSRConstantBuffer.Shutdown();
        m_SSRComputeTextureSet = nullptr;
        m_SSRComputeStorageSet = nullptr;
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

        if (m_CaptureLastCompletion) {
            if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_CaptureLastCompletion);
            }
            m_CaptureLastCompletion = {};
        }
        m_CaptureRenderSucceeded = false;
        m_CaptureSession = {};
        m_DynamicProbeCaptures.clear();
        m_DynamicCaptureTextures.clear();
        m_FailedDynamicCaptureRevisions.clear();
        m_LastDynamicCaptureResult = {};
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

        m_FogComputePipeline = nullptr;
        m_FogApplyPipelineSDR = nullptr;
        m_FogApplyPipelineHDR = nullptr;
        m_FogComputePipelineLayout.Shutdown();
        m_FogApplyPipelineLayout.Shutdown();
        m_FogConstantBuffer.Shutdown();
        m_FogComputeTextureSet = nullptr;
        m_FogComputeStorageSet = nullptr;
        m_FogApplyDescriptorSet = nullptr;
        m_FogDescriptorPool.Shutdown();
        m_FogSampler.Shutdown();

        m_BloomComputePipeline = nullptr;
        m_BloomApplyPipelineSDR = nullptr;
        m_BloomApplyPipelineHDR = nullptr;
        m_BloomComputePipelineLayout.Shutdown();
        m_BloomApplyPipelineLayout.Shutdown();
        m_BloomConstantBuffer.Shutdown();
        m_BloomComputeTextureSet = nullptr;
        m_BloomComputeStorageSet = nullptr;
        m_BloomApplyDescriptorSet = nullptr;
        m_BloomDescriptorPool.Shutdown();
        m_BloomSampler.Shutdown();

        m_AutoExposurePipeline = nullptr;
        m_AutoExposurePipelineLayout.Shutdown();
        m_AutoExposureConstantBuffer.Shutdown();
        m_AutoExposureTextureSet = nullptr;
        m_AutoExposureStorageSet = nullptr;
        m_AutoExposureDescriptorPool.Shutdown();
        m_AutoExposureSampler.Shutdown();

        m_DDGIPipeline = nullptr;
        m_DDGIPipelineLayout.Shutdown();
        m_DDGIConstantBuffer.Shutdown();
        m_DDGITextureSet = nullptr;
        m_DDGIStorageSet = nullptr;
        m_DDGIDescriptorPool.Shutdown();
        m_DDGISampler.Shutdown();

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
        m_HasActiveReflectionProbe = false;
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
        nri::RootDescriptorDesc rootDescs[5] = {};
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

        nri::DescriptorRangeDesc occlusionRange{};
        occlusionRange.baseRegisterIndex = 0;
        occlusionRange.descriptorNum = 2;
        occlusionRange.descriptorType = nri::DescriptorType::TEXTURE;
        occlusionRange.shaderStages = nri::StageBits::FRAGMENT_SHADER;
        nri::DescriptorSetDesc descriptorSets[2]{};
        descriptorSets[0].registerSpace = 1;
        descriptorSets[0].ranges = ranges;
        descriptorSets[0].rangeNum = 1;
        descriptorSets[1].registerSpace = 4;
        descriptorSets[1].ranges = &occlusionRange;
        descriptorSets[1].rangeNum = 1;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootDescriptors = rootDescs;
        layoutDesc.rootDescriptorNum = 5;
        layoutDesc.rootSamplers = samplers;
        layoutDesc.rootSamplerNum = 4;
        layoutDesc.descriptorSets = descriptorSets;
        layoutDesc.descriptorSetNum = 2;
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
                                                         bool depthWrite) {
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        if (cullMode == Axle::CullMode::Back) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::BACK;
        } else if (cullMode == Axle::CullMode::Front) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::FRONT;
        } else {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        }
        gfxDesc.Rasterizer.FrontCounterClockwise = true;

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
        gfxDesc.SampleCount = sampleCount;

        gfxDesc.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();

        auto& cache = RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    nri::Pipeline* RHISceneRenderer::GetOrCreateSurfaceNormalPipeline(nri::Format colorFormat, nri::Format depthFormat,
                                                                    bool preLighting, CullMode cull) {
        if (!m_SurfaceNormalPixelShader.IsValid() || !m_VertexShader.IsValid()) return nullptr;

        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_SurfaceNormalPixelShader };
        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = cull == CullMode::Back ? nri::CullMode::BACK : cull == CullMode::Front ? nri::CullMode::FRONT : nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = true;
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
        SurfaceType surface, CullMode cull, bool depthWrite) {
        return CreateCustomMaterialPipeline(generation,colorFormat,depthFormat,samples,surface,cull,depthWrite);
    }

    bool RHISceneRenderer::CreateGraphicsPipeline(RHIDevice& device) {
        m_Pipeline = GetOrCreatePipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT);
        m_PipelineHDR = GetOrCreatePipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT);
        return m_Pipeline != nullptr && m_PipelineHDR != nullptr;
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

    nri::Pipeline* RHISceneRenderer::GetOrCreateSkyboxPipeline(nri::Format colorFormat, nri::Format depthFormat, uint8_t sampleCount) {
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
        // Root Descriptor [0] b0 space 0: 256-byte RHIGTAOConstants
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

        // 2. Apply Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 256-byte RHIGTAOConstants
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

        m_GTAOApplyPipelineSDR = GetOrCreateGTAOApplyPipeline(nri::Format::RGBA8_UNORM);
        m_GTAOApplyPipelineHDR = GetOrCreateGTAOApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_GTAOComputePipeline != nullptr && m_GTAOApplyPipelineSDR != nullptr && m_GTAOApplyPipelineHDR != nullptr;
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
        compTexRanges[2].descriptorNum = 2;
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

        // 2. Apply Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 512-byte RHISSRConstants
        nri::RootDescriptorDesc applyRootDesc = {};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (Depth) -> TEXTURE
        // Range 1: t1 (SSR Result) -> TEXTURE
        // Range 2: t2 (Scene Color) -> TEXTURE
        // Range 3: t3 (Scene Normal) -> TEXTURE
        // Range 4: s0 (Sampler) -> SAMPLER
        nri::DescriptorRangeDesc applyRanges[5] = {};
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

        applyRanges[3].baseRegisterIndex = 3;
        applyRanges[3].descriptorNum = 1;
        applyRanges[3].descriptorType = nri::DescriptorType::TEXTURE;
        applyRanges[3].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[3].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        applyRanges[4].baseRegisterIndex = 0;
        applyRanges[4].descriptorNum = 1;
        applyRanges[4].descriptorType = nri::DescriptorType::SAMPLER;
        applyRanges[4].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        applyRanges[4].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc applyDescSet = {};
        applyDescSet.registerSpace = 1;
        applyDescSet.ranges = applyRanges;
        applyDescSet.rangeNum = 5;

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

        m_SSRApplyPipelineSDR = GetOrCreateSSRApplyPipeline(nri::Format::RGBA8_UNORM);
        m_SSRApplyPipelineHDR = GetOrCreateSSRApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_SSRComputePipeline != nullptr && m_SSRApplyPipelineSDR != nullptr && m_SSRApplyPipelineHDR != nullptr;
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
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile VolumetricFog CS: {0}", csRes.ErrorMessage);
            return false;
        }
        m_FogComputeShader = csRes.Bytecode;

        // 2. Vertex Shader: VSMain
        RHIShaderCompileDesc vsDesc = csDesc;
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHIShaderStage::Vertex;
        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile VolumetricFog VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_FogApplyVertexShader = vsRes.Bytecode;

        // 3. Pixel Shader: PSMain
        RHIShaderCompileDesc psDesc = csDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHIShaderStage::Fragment;
        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile VolumetricFog PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_FogApplyPixelShader = psRes.Bytecode;

        return true;
    }

    bool RHISceneRenderer::CreateFogPipelineLayout(RHIDevice& device) {
        // 1. Compute Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 512-byte RHIVolumetricFogConstants
        nri::RootDescriptorDesc compRootDesc = {};
        compRootDesc.registerIndex = 0;
        compRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        compRootDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;

        // Set 0 (space 1):
        // Range 0: t0 (Depth) -> TEXTURE
        // Range 1: s0 (Linear Sampler) -> SAMPLER
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

        if (!m_FogComputePipelineLayout.Init(compLayoutDesc)) return false;

        // 2. Apply Pipeline Layout:
        // Root Descriptor [0] b0 space 0: 512-byte RHIVolumetricFogConstants
        nri::RootDescriptorDesc applyRootDesc = {};
        applyRootDesc.registerIndex = 0;
        applyRootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        applyRootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (Scene Color) -> TEXTURE
        // Range 1: t1 (Fog Result) -> TEXTURE
        // Range 2: s0 (Sampler) -> SAMPLER
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

        return m_FogApplyPipelineLayout.Init(applyLayoutDesc);
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
        RHIComputePipelineDesc compDesc;
        compDesc.Layout = &m_FogComputePipelineLayout;
        compDesc.ComputeShader = m_FogComputeShader;
        m_FogComputePipeline = cache.GetOrCreateComputePipeline(compDesc);

        m_FogApplyPipelineSDR = GetOrCreateFogApplyPipeline(nri::Format::RGBA8_UNORM);
        m_FogApplyPipelineHDR = GetOrCreateFogApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_FogComputePipeline != nullptr && m_FogApplyPipelineSDR != nullptr && m_FogApplyPipelineHDR != nullptr;
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

        m_BloomApplyPipelineSDR = GetOrCreateBloomApplyPipeline(nri::Format::RGBA8_UNORM);
        m_BloomApplyPipelineHDR = GetOrCreateBloomApplyPipeline(nri::Format::RGBA16_SFLOAT);

        return m_BloomComputePipeline != nullptr && m_BloomApplyPipelineSDR != nullptr && m_BloomApplyPipelineHDR != nullptr;
    }

    bool RHISceneRenderer::CompileAutoExposureShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("AutoExposure.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath);
        std::stringstream sourceStream; sourceStream << file.rdbuf();
        const auto source = sourceStream.str();
        if (source.empty()) return false;
        RHIShaderCompileDesc desc;
        desc.SourcePath = shaderPath.string(); desc.Source = source; desc.EntryPoint = "CSMain";
        desc.Stage = RHIShaderStage::Compute; desc.HLSLVersion = "2021"; desc.ShaderModel = "6_0";
        desc.Format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        desc.Optimization = true;
        auto result = RHIShaderCompiler::Get().Compile(desc);
        if (!result.Success) { AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile AutoExposure CS: {0}", result.ErrorMessage); return false; }
        m_AutoExposureComputeShader = result.Bytecode;
        return true;
    }

    bool RHISceneRenderer::CreateAutoExposurePipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc root = {};
        root.registerIndex = 0; root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::DescriptorRangeDesc textureRanges[2] = {};
        textureRanges[0].baseRegisterIndex = 0; textureRanges[0].descriptorNum = 1;
        textureRanges[0].descriptorType = nri::DescriptorType::TEXTURE; textureRanges[0].shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRanges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        textureRanges[1].baseRegisterIndex = 0; textureRanges[1].descriptorNum = 1;
        textureRanges[1].descriptorType = nri::DescriptorType::SAMPLER; textureRanges[1].shaderStages = nri::StageBits::COMPUTE_SHADER;
        textureRanges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorRangeDesc storage = {};
        storage.baseRegisterIndex = 0; storage.descriptorNum = 1; storage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        storage.shaderStages = nri::StageBits::COMPUTE_SHADER; storage.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorSetDesc sets[2] = {};
        sets[0].registerSpace = 1; sets[0].ranges = textureRanges; sets[0].rangeNum = 2;
        sets[1].registerSpace = 2; sets[1].ranges = &storage; sets[1].rangeNum = 1;
        nri::PipelineLayoutDesc layout = {};
        layout.rootRegisterSpace = 0; layout.rootDescriptors = &root; layout.rootDescriptorNum = 1;
        layout.descriptorSets = sets; layout.descriptorSetNum = 2; layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        return m_AutoExposurePipelineLayout.Init(layout);
    }

    bool RHISceneRenderer::CreateAutoExposurePipeline(RHIDevice& device) {
        RHIComputePipelineDesc desc; desc.Layout = &m_AutoExposurePipelineLayout; desc.ComputeShader = m_AutoExposureComputeShader;
        m_AutoExposurePipeline = RHIPipelineCache::Get().GetOrCreateComputePipeline(desc);
        return m_AutoExposurePipeline != nullptr;
    }

    bool RHISceneRenderer::CompileDDGIShaders(RHIDevice& device) {
        auto shaderPath = FindShaderPath("DDGI.hlsl");
        if (shaderPath.empty()) return false;
        std::ifstream file(shaderPath); std::stringstream stream; stream << file.rdbuf();
        RHIShaderCompileDesc desc; desc.SourcePath = shaderPath.string(); desc.Source = stream.str(); desc.EntryPoint = "CSMain";
        desc.Stage = RHIShaderStage::Compute; desc.HLSLVersion = "2021"; desc.ShaderModel = "6_0";
        desc.Format = device.GetCapabilities().ActiveBackend == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
        desc.Optimization = true;
        auto result = RHIShaderCompiler::Get().Compile(desc);
        if (!result.Success) { AXLE_CORE_ERROR("RHISceneRenderer: Failed to compile DDGI CS: {0}", result.ErrorMessage); return false; }
        m_DDGIComputeShader = result.Bytecode; return true;
    }

    bool RHISceneRenderer::CreateDDGIPipelineLayout(RHIDevice& device) {
        nri::RootDescriptorDesc root = {};
        root.registerIndex = 0; root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER; root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::DescriptorRangeDesc ranges[3] = {};
        ranges[0].baseRegisterIndex = 0; ranges[0].descriptorNum = 2; ranges[0].descriptorType = nri::DescriptorType::TEXTURE; ranges[0].shaderStages = nri::StageBits::COMPUTE_SHADER; ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        ranges[1].baseRegisterIndex = 0; ranges[1].descriptorNum = 1; ranges[1].descriptorType = nri::DescriptorType::SAMPLER; ranges[1].shaderStages = nri::StageBits::COMPUTE_SHADER; ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorRangeDesc storage = {}; storage.baseRegisterIndex = 0; storage.descriptorNum = 1; storage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE; storage.shaderStages = nri::StageBits::COMPUTE_SHADER; storage.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;
        nri::DescriptorSetDesc sets[2] = {};
        sets[0].registerSpace = 1; sets[0].ranges = ranges; sets[0].rangeNum = 2;
        sets[1].registerSpace = 2; sets[1].ranges = &storage; sets[1].rangeNum = 1;
        nri::PipelineLayoutDesc layout = {}; layout.rootRegisterSpace = 0; layout.rootDescriptors = &root; layout.rootDescriptorNum = 1; layout.descriptorSets = sets; layout.descriptorSetNum = 2; layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        return m_DDGIPipelineLayout.Init(layout);
    }

    bool RHISceneRenderer::CreateDDGIPipeline(RHIDevice& device) {
        RHIComputePipelineDesc desc; desc.Layout = &m_DDGIPipelineLayout; desc.ComputeShader = m_DDGIComputeShader;
        m_DDGIPipeline = RHIPipelineCache::Get().GetOrCreateComputePipeline(desc); return m_DDGIPipeline != nullptr;
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

        nri::DescriptorRangeDesc ranges[5] = {};
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

        ranges[2].baseRegisterIndex = 2;
        ranges[2].descriptorNum = 1;
        ranges[2].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[2].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[3].baseRegisterIndex = 0;
        ranges[3].descriptorNum = 1;
        ranges[3].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[3].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[3].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[4].baseRegisterIndex = 1;
        ranges[4].descriptorNum = 1;
        ranges[4].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[4].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[4].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 5;

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

    ReflectionPublication RHISceneRenderer::PrepareReflectionProbe(const ReflectionFrame& reflectionFrame) {
        m_ActiveReflectionProbePrefilter.reset();
        m_HasActiveReflectionProbe = false;
        if (m_CaptureSession.Active) {
            return {{}, "dynamic_probe_suppressed_during_capture"};
        }
        if (!reflectionFrame.SelectedProbe) return {};
        const auto* selected=&*reflectionFrame.SelectedProbe;

        if (selected->DynamicCapture && selected->CubemapPath.empty()) {
            const auto dynamicProbe = GetDynamicReflectionProbe(*selected);
            if (!dynamicProbe || !dynamicProbe->Cubemap || !dynamicProbe->Cubemap->IsValid()) {
                const std::string detail = m_LastDynamicCaptureResult.Diagnostic.empty()
                    ? "dynamic probe cubemap not captured"
                    : m_LastDynamicCaptureResult.Diagnostic;
                return {{}, detail};
            }
            m_ActiveReflectionProbePrefilter = dynamicProbe->Cubemap;
            m_ActiveReflectionProbe = *selected;
            m_HasActiveReflectionProbe = true;
            return {m_ActiveReflectionProbePrefilter->GetHandle(), ""};
        }

        const std::string path = PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(selected->CubemapPath)).lexically_normal());
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
            if (!environment->SetBakeSettings(settings)) return {{}, "probe bake settings unavailable"};
        }

        RenderSkyboxData source;
        source.Authored = true;
        source.Enabled = true;
        source.EnvironmentLighting = true;
        source.Type = 1;
        source.HDRPath = selected->CubemapPath;
        source.Intensity = 1.0f;
        source.Exposure = 1.0f;
        if (!environment->Prepare(source) || !environment->Ready() || !environment->Ready()->IsValid())
            return {{}, environment->Diagnostic()};

        m_ActiveReflectionProbePrefilter = environment->Ready()->Prefilter;
        m_ActiveReflectionProbe = *selected;
        m_HasActiveReflectionProbe = m_ActiveReflectionProbePrefilter->IsValid();
        return {m_HasActiveReflectionProbe ? m_ActiveReflectionProbePrefilter->GetHandle() : RHIResourceHandle{}, environment->Diagnostic()};
    }

    bool RHISceneRenderer::IsCaptureFeedbackTexture(const RHITexture& texture) const {
        if (!m_CaptureSession.Active) return false;
        if (m_CaptureSession.CandidateCubemap && m_CaptureSession.CandidateCubemap->GetNative() == texture.GetNative()) return true;
        for (const auto& handle : m_DynamicCaptureTextures)
            if (handle == texture.GetHandle()) return true;
        return false;
    }

    uint64_t RHISceneRenderer::ComputeDynamicCaptureStamp(const RenderWorld& world,
            const RenderReflectionProbe& probe, const SceneRenderSettings& settings) const {
        uint64_t hash = 1469598103934665603ull;
        const auto mix = [&](const auto&... values) {
            const auto one = [&](const auto& value) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
                for (size_t i = 0; i < sizeof(value); ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
            };
            (one(values), ...);
        };
        const auto text = [&](const std::string& value) {
            mix(value.size()); for (char c : value) mix(c);
        };
        const auto file = [&](const std::string& path) {
            text(path);
            if (path.empty()) return;
            std::error_code error;
            const auto time = std::filesystem::last_write_time(FileSystem::ResolveAssetPath(PathFromUtf8(path)), error);
            mix(error.value()); if (!error) mix(time.time_since_epoch().count());
        };
        mix(world.SceneRevision, probe.EntityID, probe.Position, probe.CaptureResolution,
            probe.CaptureNearPlane, probe.CaptureFarPlane, probe.CaptureShadows);
        mix(m_UseIBL, m_IBLAllowed);
        for (const auto& texture : {m_IrradianceMap, m_PrefilterMap, m_BRDFLUT}) {
            const auto handle = texture ? texture->GetHandle() : RHIResourceHandle{};
            mix(handle.Epoch, handle.Slot, handle.Generation);
        }
        mix(settings.EnableShadows, settings.EnableSkybox, settings.LocalShadowResolution,
            settings.MaxShadowedPointLights, settings.MaxShadowedSpotLights, settings.LocalShadowDistance,
            settings.ShadowCascadeCount, settings.EnableStudioLightLobes);
        const auto directional = [&](const RenderDirectionalLight& light) {
            mix(light.Direction, light.Color, light.Intensity, light.AmbientIntensity, light.SolarAngularRadius,
                light.CastsShadows, light.ShadowQuality, light.ShadowBias, light.ShadowSlopeBias,
                light.ShadowNormalBias, light.ShadowFilterRadius, light.MaxShadowDistance, light.CascadeSplitLambda);
        };
        mix(world.DirectionalLight.has_value());
        if (world.DirectionalLight) directional(*world.DirectionalLight);
        mix(world.AdditionalDirectionalLights.size());
        for (const auto& light : world.AdditionalDirectionalLights) directional(light);
        const auto shadow = [&](const LocalShadowSettings& s) { mix(s.CastShadows, s.Bias, s.SlopeBias, s.NormalBias, s.FilterRadius, s.NearPlane); };
        mix(world.PointLights.size(), world.SpotLights.size(), world.RectLights.size());
        for (const auto& light : world.PointLights) { mix(light.EntityID, light.Position, light.Color, light.Intensity, light.Radius, light.Falloff); shadow(light.Shadows); }
        for (const auto& light : world.SpotLights) { mix(light.EntityID, light.Position, light.Direction, light.Color, light.Intensity, light.Range, light.InnerAngle, light.OuterAngle); shadow(light.Shadows); }
        for (const auto& light : world.RectLights) mix(light.EntityID, light.Position, light.Direction, light.Up, light.Color, light.Intensity, light.Width, light.Height, light.Range, light.TwoSided);
        const auto& sky = world.Skybox;
        mix(sky.Authored, sky.Enabled, sky.EnvironmentLighting, sky.SyncWithSun, sky.CustomAzimuth, sky.CustomElevation,
            sky.HemisphereAmbient, sky.SunSize, sky.SunGlow, sky.Turbidity, sky.Type, sky.SunDirection, sky.SunColor,
            sky.ZenithColor, sky.HorizonColor, sky.GroundColor, sky.Intensity, sky.Exposure, sky.EnvironmentRotation);
        file(sky.HDRPath);
        if (!sky.Authored) mix(m_SkyboxParams.Enabled, m_SkyboxParams.Type, m_SkyboxParams.Intensity, m_SkyboxParams.Exposure,
            m_SkyboxParams.ZenithColor, m_SkyboxParams.HorizonColor, m_SkyboxParams.GroundColor, m_SkyboxParams.SunSize,
            m_SkyboxParams.SunGlow, m_SkyboxParams.Turbidity, m_SkyboxParams.EnvironmentRotation);
        mix(world.Meshes.size());
        for (const auto& mesh : world.Meshes) {
            const auto surface = MaterialResolver::Resolve(mesh.Material);
            mix(mesh.EntityID, mesh.WorldTransform, mesh.CastsShadows, surface.Generation, mesh.MeshSourceUUID);
            text(mesh.MeshURI);
            for (const auto& binding : surface.Textures) file(binding.Path);
            const auto view = mesh.NativeView ? mesh.NativeView : mesh.NativeMesh ? mesh.NativeMesh->CaptureView() : nullptr;
            const auto handle = view ? view->GetVertexHandle() : RHIResourceHandle{};
            mix(handle.Epoch, handle.Slot, handle.Generation);
            const auto indices = view ? view->GetIndexHandle() : RHIResourceHandle{};
            mix(indices.Epoch, indices.Slot, indices.Generation);
            mix(mesh.Material.UseCustomShader); file(mesh.Material.CustomShaderPath);
            if (mesh.Material.UseCustomShader && !mesh.Material.CustomShaderPath.empty()) {
                CustomProgramKey key;
                key.ShaderPath = PathToUtf8(FileSystem::ResolveAssetPath(PathFromUtf8(mesh.Material.CustomShaderPath)).lexically_normal());
                key.Backend = m_Device ? m_Device->GetBackend() : RHIType::None;
                text(m_CustomProgramCache.ComputeFingerprint(key));
            }
            for (const auto& property : mesh.Material.CustomProperties) {
                text(property.Desc.Name); mix(property.FloatVal, property.Vec4Val, property.IntVal, property.BoolVal); file(property.TexturePath);
            }
        }
        return hash ? hash : 1;
    }

    std::string RHISceneRenderer::ResolveDynamicProbeKey(const RenderReflectionProbe& probe) {
        if (probe.EntityID != 0) {
            return "dynamic:entity:" + std::to_string(probe.EntityID);
        }
        const auto px = std::bit_cast<uint32_t>(probe.Position.x);
        const auto py = std::bit_cast<uint32_t>(probe.Position.y);
        const auto pz = std::bit_cast<uint32_t>(probe.Position.z);
        return "dynamic:anon:" + std::to_string(px) + ":" + std::to_string(py) + ":" + std::to_string(pz);
    }

    std::shared_ptr<const DynamicReflectionProbeGeneration> RHISceneRenderer::GetDynamicReflectionProbe(
        const std::string& probeKey) const {
        const auto it = m_DynamicProbeCaptures.find(probeKey);
        if (it == m_DynamicProbeCaptures.end()) {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<const DynamicReflectionProbeGeneration> RHISceneRenderer::GetDynamicReflectionProbe(
        const RenderReflectionProbe& probe) const {
        return GetDynamicReflectionProbe(ResolveDynamicProbeKey(probe));
    }

    void RHISceneRenderer::ClearDynamicReflectionProbes() {
        if (m_CaptureLastCompletion) {
            if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                m_Device->WaitForCompletion(m_CaptureLastCompletion);
            }
            m_CaptureLastCompletion = {};
        }
        m_DynamicProbeCaptures.clear();
        m_FailedDynamicCaptureRevisions.clear();
        m_LastDynamicCaptureResult = {};
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
        const uint64_t inputStamp = ComputeDynamicCaptureStamp(world, probe, settings);
        const auto prevIt = m_DynamicProbeCaptures.find(probeKey);
        const bool hasPrevious = (prevIt != m_DynamicProbeCaptures.end() &&
                                  prevIt->second &&
                                  prevIt->second->Cubemap &&
                                  prevIt->second->Cubemap->IsValid());
        const uint64_t prevGenerationID = hasPrevious ? prevIt->second->GenerationID : 0;

        auto waitCompletion = [&](RHICompletionToken& token) {
            if (token) {
                if (m_Device && token.Epoch == m_Device->GetEpoch()) {
                    if (!m_Device->WaitForCompletion(token)) return false;
                } else {
                    return false;
                }
                token = {};
            }
            return true;
        };

        auto failCapture = [&](std::string diagnostic, uint32_t capturedFaces = 0) -> DynamicReflectionCaptureResult {
            waitCompletion(m_CaptureLastCompletion);
            m_CaptureSession = {};
            m_LastDynamicCaptureResult = {};
            m_LastDynamicCaptureResult.Success = false;
            m_LastDynamicCaptureResult.CapturedFaces = capturedFaces;
            m_LastDynamicCaptureResult.PublishedGenerationID = prevGenerationID;
            m_LastDynamicCaptureResult.PreservedPreviousGeneration = hasPrevious;
            m_LastDynamicCaptureResult.ProbeKey = probeKey;
            m_LastDynamicCaptureResult.Diagnostic = std::move(diagnostic);
            if (!policyOverride && m_LastDynamicCaptureResult.Diagnostic.starts_with("DYNAMIC_PROBE_SETTINGS:"))
                m_FailedDynamicCaptureRevisions[probeKey] = inputStamp;
            else
                m_FailedDynamicCaptureRevisions.erase(probeKey);
            return m_LastDynamicCaptureResult;
        };

        if (!m_Initialized || !m_Device || !m_Device->CanSubmit() || !m_Device->CanCreateResources()) {
            return failCapture("DYNAMIC_PROBE_DEVICE: native render owner is unavailable");
        }
        if (!world.ErrorMessage.empty()) {
            return failCapture("DYNAMIC_PROBE_WORLD_INVALID: " + world.ErrorMessage);
        }
        if (!std::isfinite(probe.Position.x) || !std::isfinite(probe.Position.y) || !std::isfinite(probe.Position.z)) {
            return failCapture("DYNAMIC_PROBE_SETTINGS: non-finite probe position");
        }

        ReflectionCaptureContract::ReflectionCapturePolicy policy;
        if (policyOverride) {
            policy = *policyOverride;
        } else {
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
        }

        std::string policyReason;
        if (!ReflectionCaptureContract::ValidateCapturePolicy(policy, &policyReason)) {
            return failCapture("DYNAMIC_PROBE_SETTINGS: " + policyReason);
        }

        for (uint32_t face = 0; face < ReflectionCaptureContract::FaceCount; ++face) {
            const auto basis = ReflectionCaptureContract::GetCubeFaceBasis(face);
            std::string basisReason;
            if (!ReflectionCaptureContract::ValidateCubeFaceBasis(basis, &basisReason)) {
                return failCapture("DYNAMIC_PROBE_BASIS: " + basisReason);
            }
        }

        const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
        if (!backendPolicy.Supports(BackendFeature::Compute) ||
            !backendPolicy.Supports(BackendFeature::StorageTextures) ||
            !m_CaptureOrientationPipeline) {
            return failCapture("DYNAMIC_PROBE_UNAVAILABLE: compute orientation pipeline unavailable on active backend");
        }

        // Synchronize shared host-upload buffers and scratch targets before reconfiguration.
        RHICompletionToken pendingMainCompletion = m_LastCompletion;
        if (!waitCompletion(pendingMainCompletion) || !waitCompletion(m_CaptureLastCompletion))
            return failCapture("DYNAMIC_PROBE_COMPLETION: prior work did not complete");

        if (!m_CaptureTargets || !m_CaptureTargets->Configure(policy.Resolution)) {
            return failCapture("DYNAMIC_PROBE_ALLOCATION: scratch face target allocation failed");
        }

        auto candidateCubemap = std::make_shared<RHITexture>();
        if (!candidateCubemap->Init(RHITextureDesc::Cubemap(
                policy.Resolution,
                1u,
                RHITextureFormat::RGBA16_FLOAT,
                "DynamicReflectionProbeCubemap_" + probeKey)) ||
            !candidateCubemap->IsValid()) {
            return failCapture("DYNAMIC_PROBE_ALLOCATION: candidate cubemap allocation failed");
        }

        const SceneRenderSettings captureSettings = backendPolicy.ResolveSettings(
            ReflectionCaptureContract::SanitizeCaptureSettings(settings, policy));
        const RenderWorld filteredWorld = ReflectionCaptureContract::FilterCaptureWorld(world, policy);

        // Preserve observable main-view and environment state. Shared GPU draw,
        // light and shadow scratch is prepared again by the next main frame.
        auto savedEnvironment = m_Environment;
        const bool savedPreparedManualIBL = m_PreparedManualIBL;
        const nri::Format savedColorFormat = m_CurrentColorFormat;
        const nri::Format savedDepthFormat = m_CurrentDepthFormat;
        const uint8_t savedMSAASamples = m_CurrentMSAASamples;
        const auto savedActiveViewTargets = m_ActiveViewTargets;
        RHITexture* const savedMSAAColor = m_MSAAColorTarget;
        RHITexture* const savedMSAADepth = m_MSAADepthTarget;
        RHITexture* const savedHDR = m_IntermediateHDRTarget;
        RHITexture* const savedFXAA = m_IntermediateFXAATarget;
        RHITexture* const savedOpaqueColor = m_OpaqueSceneColor;
        RHITexture* const savedOpaqueDepth = m_OpaqueSceneDepth;
        RHITexture* const savedParticleDepth = m_ParticleSceneDepth;
        RHITexture* const savedOpaqueNormal = m_OpaqueSceneNormal;
        RHITexture* const savedScreenStorage = m_ScreenStorageTarget;
        const LightingFrame savedLightingFrame = m_LastLightingFrame;
        const ReflectionFrame savedReflectionFrame = m_LastReflectionFrame;
        const GIFrame savedGIFrame = m_LastGIFrame;
        const std::shared_ptr<RHITexture> savedActiveProbe = m_ActiveReflectionProbePrefilter;
        const RenderReflectionProbe savedProbeDesc = m_ActiveReflectionProbe;
        const bool savedHasActiveProbe = m_HasActiveReflectionProbe;
        const auto savedMaterialDiagnostics = m_MaterialDiagnostics;
        const auto savedMaterialFeedback = m_MaterialFeedback;
        const uint32_t savedDrawCalls = m_LastDrawCallCount;
        const uint32_t savedEntityCount = m_LastRenderedEntityCount;
        const uint64_t savedStreamedBytes = m_LastStreamedTransformBytes;
        const bool savedRenderSucceeded = m_LastRenderSucceeded;
        const RHICompletionToken savedCompletion = m_LastCompletion;

        struct ScopedRestore {
            RHISceneRenderer& Renderer;
            std::function<void()> Fn;
            ~ScopedRestore() { Fn(); }
        } guard{
            *this,
            [&]() {
                m_CaptureSession = {};
                m_Environment = std::move(savedEnvironment);
                m_PreparedManualIBL = savedPreparedManualIBL;
                m_BoundIBL = {}; // Descriptor inputs were changed by capture preparation.
                m_CurrentColorFormat = savedColorFormat;
                m_CurrentDepthFormat = savedDepthFormat;
                m_CurrentMSAASamples = savedMSAASamples;
                m_ActiveViewTargets = savedActiveViewTargets;
                m_MSAAColorTarget = savedMSAAColor;
                m_MSAADepthTarget = savedMSAADepth;
                m_IntermediateHDRTarget = savedHDR;
                m_IntermediateFXAATarget = savedFXAA;
                m_OpaqueSceneColor = savedOpaqueColor;
                m_OpaqueSceneDepth = savedOpaqueDepth;
                m_ParticleSceneDepth = savedParticleDepth;
                m_OpaqueSceneNormal = savedOpaqueNormal;
                m_ScreenStorageTarget = savedScreenStorage;
                m_LastLightingFrame = savedLightingFrame;
                m_LastReflectionFrame = savedReflectionFrame;
                m_LastGIFrame = savedGIFrame;
                m_ActiveReflectionProbePrefilter = savedActiveProbe;
                m_ActiveReflectionProbe = savedProbeDesc;
                m_HasActiveReflectionProbe = savedHasActiveProbe;
                m_MaterialDiagnostics = savedMaterialDiagnostics;
                m_MaterialFeedback = savedMaterialFeedback;
                m_LastDrawCallCount = savedDrawCalls;
                m_LastRenderedEntityCount = savedEntityCount;
                m_LastStreamedTransformBytes = savedStreamedBytes;
                m_LastRenderSucceeded = savedRenderSucceeded;
                m_LastCompletion = savedCompletion;
            }
        };

        for (uint32_t face = 0; face < ReflectionCaptureContract::FaceCount; ++face) {
            if (face > 0) {
                if (!waitCompletion(m_CaptureLastCompletion))
                    return failCapture("DYNAMIC_PROBE_COMPLETION: prior face did not complete", face);
            }

            m_CurrentColorFormat = nri::Format::RGBA16_SFLOAT;
            m_CurrentDepthFormat = nri::Format::D32_SFLOAT;
            m_CurrentMSAASamples = 1;

            m_CaptureSession.Active = true;
            m_CaptureSession.FaceIndex = face;
            m_CaptureSession.Resolution = policy.Resolution;
            m_CaptureSession.FlipHorizontal = ReflectionCaptureContract::GetCubeFaceBasis(face).FlipHorizontalForCubemapCopy;
            m_CaptureSession.IncludeTransparent = policy.IncludeTransparent;
            m_CaptureSession.IncludeParticles = policy.IncludeParticles;
            m_CaptureSession.IncludeEditorOverlays = policy.IncludeEditorOverlays;
            m_CaptureSession.CandidateCubemap = candidateCubemap;
            m_CaptureSession.SourceFace = m_CaptureTargets->FaceColor.get();
            m_CaptureSession.OrientedFace = m_CaptureTargets->OrientedColor.get();

            const PerspectiveCamera faceCamera = ReflectionCaptureContract::BuildCubeFaceCamera(
                probe.Position, face, policy.NearPlane, policy.FarPlane);

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

            if (!m_CaptureRenderSucceeded || !m_CaptureLastCompletion) {
                return failCapture("DYNAMIC_PROBE_FACE_FAILED: face " + std::to_string(face) + " capture failed", face);
            }
        }

        m_FailedDynamicCaptureRevisions.erase(probeKey);
        auto published = std::make_shared<DynamicReflectionProbeGeneration>();
        published->ProbeKey = probeKey;
        published->EntityID = probe.EntityID;
        published->GenerationID = m_NextDynamicCaptureGenerationID++;
        published->SceneRevision = world.SceneRevision;
        published->InputStamp = policyOverride ? 0 : inputStamp;
        published->Resolution = policy.Resolution;
        published->Position = probe.Position;
        published->Cubemap = candidateCubemap;
        published->Completion = m_CaptureLastCompletion;
        m_DynamicProbeCaptures[probeKey] = published;
        std::erase_if(m_DynamicCaptureTextures, [&](const auto& handle) { return !m_Device->GetResources().Acquire(handle).IsLive(); });
        m_DynamicCaptureTextures.push_back(candidateCubemap->GetHandle());

        m_LastDynamicCaptureResult = {};
        m_LastDynamicCaptureResult.Success = true;
        m_LastDynamicCaptureResult.CapturedFaces = ReflectionCaptureContract::FaceCount;
        m_LastDynamicCaptureResult.PublishedGenerationID = published->GenerationID;
        m_LastDynamicCaptureResult.PreservedPreviousGeneration = false;
        m_LastDynamicCaptureResult.ProbeKey = probeKey;
        return m_LastDynamicCaptureResult;
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
                                           std::shared_ptr<RHI::RHIViewTargets> viewTargets)
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
                        settings, physicsScene, viewTargets);
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
                                       std::shared_ptr<RHI::RHIViewTargets> viewTargets)
    {
        if (!m_CaptureSession.Active) {
            ConsumeReadyGPUTimestamps();
            if (m_CaptureLastCompletion) {
                if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                    m_Device->WaitForCompletion(m_CaptureLastCompletion);
                }
                m_CaptureLastCompletion = {};
            }
            m_LastRenderSucceeded = false;
            m_LastCompletion = {};
        } else {
            m_CaptureRenderSucceeded = false;
            m_CaptureLastCompletion = {};
        }
        if (!m_Initialized || !m_Device->CanSubmit() || !colorTarget || !colorRTV || width == 0 || height == 0) return;
        const auto backendPolicy = RenderBackendPolicy::FromDevice(m_Device->GetCapabilities());
        const SceneRenderSettings settings = backendPolicy.ResolveSettings(requestedSettings);
        m_MaterialDiagnostics.clear(); m_MaterialFeedback.clear();

        if (!m_CaptureSession.Active && world.ErrorMessage.empty()) {
            const auto previewReflection = m_ReflectionService.Prepare(
                world, camera.GetPosition(), settings.EnableSSR, world.SceneRevision);
            if (previewReflection.SelectedProbe &&
                previewReflection.SelectedProbe->DynamicCapture &&
                previewReflection.SelectedProbe->CubemapPath.empty()) {
                const auto& selectedProbe = *previewReflection.SelectedProbe;
                const std::string probeKey = ResolveDynamicProbeKey(selectedProbe);
                const auto existing = GetDynamicReflectionProbe(probeKey);
                const auto inputStamp = ComputeDynamicCaptureStamp(world, selectedProbe, requestedSettings);
                const bool matchesExisting = existing && existing->Cubemap && existing->Cubemap->IsValid() &&
                    existing->InputStamp == inputStamp &&
                    existing->Resolution == selectedProbe.CaptureResolution &&
                    glm::length(existing->Position - selectedProbe.Position) <= 1e-4f;
                const auto failedIt = m_FailedDynamicCaptureRevisions.find(probeKey);
                const bool alreadyFailedRevision = (failedIt != m_FailedDynamicCaptureRevisions.end() &&
                                                    failedIt->second == inputStamp);
                if (!matchesExisting && !alreadyFailedRevision) {
                    CaptureDynamicReflectionProbe(world, selectedProbe, requestedSettings);
                    if (m_CaptureLastCompletion) {
                        if (m_Device && m_CaptureLastCompletion.Epoch == m_Device->GetEpoch()) {
                            m_Device->WaitForCompletion(m_CaptureLastCompletion);
                        }
                        m_CaptureLastCompletion = {};
                    }
                }
            }
        }

        ViewContext& viewCtx = ActiveViewContext();
        ViewContextInput viewInput;
        viewInput.Width = width;
        viewInput.Height = height;
        viewInput.Camera = camera;
        viewInput.Settings = settings;
        viewInput.Targets = viewTargets ? viewTargets : (m_CaptureSession.Active ? std::shared_ptr<RHI::RHIViewTargets>{} : m_ImplicitViewTargets);
        viewInput.FeatureGeneration = HashViewFeaturePolicy(settings);
        viewInput.SceneRevision = world.SceneRevision;
        // Effective material identity is published by the live pass schedule.
        viewInput.MaterialGeneration = viewCtx.GetInput().MaterialGeneration;
        viewCtx.Observe(viewInput, m_Device->GetEpoch());
        if (!world.ErrorMessage.empty() || !viewCtx.BeginFrame()) { AXLE_CORE_ERROR("View frame acquisition failed"); return; }
        if (!m_CaptureSession.Active) {
            ConsumeReadyGPUTimestamps();
        }
        for (const auto& fallback : backendPolicy.CollectFallbacks(requestedSettings))
            viewCtx.Diagnostics().AddFallback(fallback);
        const auto reflectionFrame = m_ReflectionService.Prepare(
            world, camera.GetPosition(), settings.EnableSSR, world.SceneRevision);
        m_LastReflectionFrame=reflectionFrame;
        const auto giFrame = m_GIService.Prepare(
            world, settings, backendPolicy.Supports(BackendFeature::Compute), world.SceneRevision);
        m_LastGIFrame=giFrame;
        if (giFrame.Status != GIFrameStatus::Disabled && giFrame.Fallback != GIFallback::None && !giFrame.Diagnostic.empty())
            viewCtx.Diagnostics().AddFallback(giFrame.Diagnostic);
        auto rollback = std::unique_ptr<RHISceneRenderer, std::function<void(RHISceneRenderer*)>>(this,
            [&](auto*) {
                const bool succeeded = m_CaptureSession.Active ? m_CaptureRenderSucceeded : m_LastRenderSucceeded;
                if (!succeeded) {
                    viewCtx.AbandonFrame();
                }
            });
        if (!m_CaptureSession.Active) {
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
        m_CustomDescriptorPool.Reset(); // The preceding RenderScene submission completed before reuse.
        m_LastRenderedEntityCount = 0;
        m_LastStreamedTransformBytes = 0;

        // Determine whether intermediate targets are used for MSAA, Tone Mapping & FXAA
        bool useMSAA = (settings.MSAASamples > 1);
        uint8_t activeSampleCount = useMSAA ? settings.MSAASamples : 1;
        bool useToneMap = settings.EnableToneMapping;
        bool useFXAA = settings.EnableFXAA;

        nri::Texture* sceneTargetTexture = colorTarget;
        nri::Descriptor* sceneTargetRTV = colorRTV;
        nri::Texture* sceneDepthTexture = depthTarget;
        nri::Descriptor* sceneDepthDSV = depthDSV;
        nri::Format sceneTargetFormat = m_CurrentColorFormat;

        auto rejectTargets = [&] { viewCtx.AbandonFrame(); };
        RHIViewTargetDesc targetDesc; targetDesc.Width = width; targetDesc.Height = height;
        targetDesc.ColorFormat = FromNRIFormat(m_CurrentColorFormat); targetDesc.DepthFormat = FromNRIFormat(m_CurrentDepthFormat);
        targetDesc.EnableDepth = sceneDepthTexture != nullptr; targetDesc.Samples = activeSampleCount;
        targetDesc.HDR = useToneMap; targetDesc.FXAA = useFXAA; targetDesc.OwnPrimary = false;
        targetDesc.EnableNormal = (settings.EnableGTAO || settings.EnableContactShadows || settings.EnableSSR);
        targetDesc.EnableScreenStorage = (settings.EnableGTAO || settings.EnableContactShadows || settings.EnableSSR || settings.EnableVolumetricFog || settings.EnableBloom || settings.EnableAutoExposure);
        targetDesc.EnableHiZ = settings.EnableSSR || settings.HiZVisualizationLevel >= 0;
        targetDesc.EnableBloom = settings.EnableBloom;
        targetDesc.EnableGTAO = settings.EnableGTAO || settings.EnableContactShadows;
        targetDesc.EnableAutoExposure = settings.EnableAutoExposure;
        targetDesc.EnableDDGI = settings.EnableDDGI;
        if (settings.EnableTAA) targetDesc.HistoryCount = 2;
        if (viewTargets) {
            auto expected = targetDesc; expected.OwnPrimary = viewTargets->GetDesc().OwnPrimary;
            expected.HistoryCount = viewTargets->GetDesc().HistoryCount;
            if (!viewTargets->IsValid() || viewTargets->GetDesc() != expected ||
                (viewTargets->Get(RHIViewTarget::Color) && viewTargets->Get(RHIViewTarget::Color)->GetNative() != colorTarget) ||
                (viewTargets->Get(RHIViewTarget::Depth) && viewTargets->Get(RHIViewTarget::Depth)->GetNative() != depthTarget)) { rejectTargets(); return; }
            if (!m_CaptureSession.Active) {
                m_ImplicitViewTargets.reset(); m_ImplicitViewColor = {};
            }
        } else {
            const auto identity = m_Device->GetResources().AcquireNative(colorTarget).GetHandle();
            if (!m_ImplicitViewTargets || !m_ImplicitViewTargets->IsValid() || m_ImplicitViewTargets->GetDesc() != targetDesc || m_ImplicitViewColor != identity) {
                auto candidate = std::make_shared<RHIViewTargets>();
                if (!candidate->Configure(targetDesc)) { rejectTargets(); return; }
                m_ImplicitViewTargets = std::move(candidate); m_ImplicitViewColor = identity;
            }
            viewTargets = m_ImplicitViewTargets;
        }
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
        float jitterX = 0.0f;
        float jitterY = 0.0f;
        if (settings.EnableTAA) {
            jitterX = (s_Halton2[viewCtx.GetFrameIndex() % 8] - 0.5f) / static_cast<float>(width) * settings.TAAJitterSpread;
            jitterY = (s_Halton3[viewCtx.GetFrameIndex() % 8] - 0.5f) / static_cast<float>(height) * settings.TAAJitterSpread;
        }
        viewCtx.SetJitter({ jitterX, jitterY });
        glm::mat4 projZO = camera.GetProjectionMatrixZO();
        if (settings.EnableTAA) {
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
        nri::Pipeline* activePipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_PipelineHDR && activeSampleCount == 1)
                                            ? m_PipelineHDR
                                            : (sceneTargetFormat == nri::Format::RGBA8_UNORM && m_Pipeline && activeSampleCount == 1)
                                                  ? m_Pipeline
                                                  : GetOrCreatePipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
        if (!activePipeline) { rejectTargets(); return; }

        const auto passCapabilities = backendPolicy.GetPassCapabilities();
        RecordFramePasses({
            world, reflectionFrame, physicsScene,
            colorTarget, colorRTV, useMSAA, useToneMap, useFXAA,
            activeSampleCount, sceneTargetTexture, sceneTargetRTV, sceneDepthTexture,
            sceneDepthDSV, sceneTargetFormat, activePipeline,
            projZO, viewProjZO, giFrame, {passCapabilities.NativeRHI, passCapabilities.Compute,
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
