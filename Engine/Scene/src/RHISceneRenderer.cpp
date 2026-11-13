#include <Axle/RHISceneRenderer.hpp>
#include <Axle/Path.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/SceneLighting.hpp>
#include <Axle/RenderContracts.hpp>
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
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include "NativeForwardSource.hpp"
#include "NativeSkySource.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

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

        if (!m_Frame.Init() || !m_CascadedShadows.Init() || !m_LocalShadows.Init()) return false;

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
        constexpr uint32_t materialSets=4096+2;
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
                    m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 0, defaultSRVs);
                }
            }
        }

        RHIDescriptorPoolDesc customPool;
        customPool.MaxDescriptorSets=4096; customPool.MaxConstantBuffers=16384;
        customPool.MaxTextures=16384; customPool.MaxSamplers=1024;
        if (!m_CustomDescriptorPool.Init(customPool) || !m_CustomSampler.Init(RHISamplerDesc::Anisotropic16x())) return false;
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
                m_ToneMappingDescriptorPool.UpdateSampler(m_ToneMappingDescriptorSet, 1, 0, m_ToneMappingSampler.GetNative());
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
                m_FXAADescriptorPool.UpdateSampler(m_FXAADescriptorSet, 2, 0, m_FXAASampler.GetNative());
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
                m_GTAODescriptorPool.UpdateSampler(m_GTAOComputeTextureSet, 2, 0, m_GTAOSampler.GetNative());
                m_GTAODescriptorPool.UpdateSampler(m_GTAOApplyDescriptorSet, 3, 0, m_GTAOSampler.GetNative());
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
                m_SSRDescriptorPool.UpdateSampler(m_SSRComputeTextureSet, 3, 0, m_SSRSampler.GetNative());
                m_SSRDescriptorPool.UpdateSampler(m_SSRApplyDescriptorSet, 4, 0, m_SSRSampler.GetNative());
            }
        }

        if (!m_SSRConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISSRConstants), RHIMemoryLocation::HostUpload, "SSRConstantsBuffer"))) return false;

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
                m_FogDescriptorPool.UpdateSampler(m_FogComputeTextureSet, 1, 0, m_FogSampler.GetNative());
                m_FogDescriptorPool.UpdateSampler(m_FogApplyDescriptorSet, 2, 0, m_FogSampler.GetNative());
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
        bloomPoolDesc.MaxDescriptorSets = 16;
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
            if (m_BloomSampler.IsValid()) {
                m_BloomDescriptorPool.UpdateSampler(m_BloomComputeTextureSet, 1, 0, m_BloomSampler.GetNative());
                m_BloomDescriptorPool.UpdateSampler(m_BloomApplyDescriptorSet, 2, 0, m_BloomSampler.GetNative());
            }
        }

        if (!m_BloomConstantBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIBloomConstants), RHIMemoryLocation::HostUpload, "BloomConstantsBuffer"))) return false;

        // Initialize Temporal Anti-Aliasing (TAA) Resources
        if (!CompileTAAShaders(device) || !CreateTAAPipelineLayout(device) || !CreateTAAPipeline(device)) return false;

        if (!m_TAALinearSampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge)) ||
            !m_TAAPointSampler.Init(RHISamplerDesc::Point(RHIAddressMode::ClampToEdge))) {
            AXLE_CORE_WARN("RHISceneRenderer: Failed to initialize samplers for TAA.");
            return false;
        }

        RHIDescriptorPoolDesc taaPoolDesc;
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
                m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 3, 0, m_TAALinearSampler.GetNative());
                m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 4, 0, m_TAAPointSampler.GetNative());
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

        m_TAAPipelineSDR = nullptr;
        m_TAAPipelineHDR = nullptr;
        m_TAAPipelineLayout.Shutdown();
        m_TAAConstantBuffer.Shutdown();
        m_TAADescriptorSet = nullptr;
        m_TAADescriptorPool.Shutdown();
        m_TAALinearSampler.Shutdown();
        m_TAAPointSampler.Shutdown();
        m_TAAHistoryIndex = 0;
        m_TAAHistoryValid = false;
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
        m_SkyboxPipeline = nullptr;
        m_SkyboxPipelineHDR = nullptr;
        m_SkyboxPipelineLayout.Shutdown();
        m_SkyboxConstantBuffer.Shutdown();

        m_BoundCascadeAtlas={};
        m_CascadedShadows.Shutdown();
        m_LocalShadows.Shutdown();m_BoundLocalAtlas={};
        m_Environment.Clear();m_BoundIBL={};m_PreparedManualIBL=false;m_SkyboxDescriptorSet=nullptr;
        m_LightBuffer.Shutdown();
        m_SceneConstantBuffer.Shutdown();

        m_MaterialDraws.clear();
        m_CustomDraws.clear();
        m_CustomDescriptorPool.Shutdown();
        m_CustomSampler.Shutdown();
        m_CustomPolicySamplers.clear();
        m_CustomProgramCache.Clear();
        m_OpaqueSceneColor = nullptr;
        m_OpaqueSceneDepth = nullptr;
        m_ParticleSceneDepth = nullptr;
        m_OpaqueSceneNormal = nullptr;
        m_ScreenStorageTarget = nullptr;
        m_MaterialTextures.Clear();
        m_DefaultDescriptorSet = nullptr;
        m_IrradianceMap = nullptr;
        m_PrefilterMap = nullptr;
        m_BRDFLUT = nullptr;
        m_DescriptorPool.Shutdown();
        m_AnisoSampler.Shutdown();

        m_PipelineLayout.Shutdown();
        m_Frame = RHIFrameContext{};
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

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 1;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootConstants = &rootConst;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootDescriptors = rootDescs;
        layoutDesc.rootDescriptorNum = 5;
        layoutDesc.rootSamplers = samplers;
        layoutDesc.rootSamplerNum = 4;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
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
                m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet, 0, 6, iblSRVs);
            }
        }
    }

    std::shared_ptr<RHITexture> RHISceneRenderer::ActiveIrradiance() const {
        auto ready=m_Environment.Ready();auto texture=ready?ready->Irradiance:(m_PreparedManualIBL?m_IrradianceMap:nullptr);
        return texture&&texture->IsValid()?texture:RHIMaterial::GetDefaultIrradianceCubemap();
    }
    std::shared_ptr<RHITexture> RHISceneRenderer::ActivePrefilter() const {
        auto ready=m_Environment.Ready();auto texture=ready?ready->Prefilter:(m_PreparedManualIBL?m_PrefilterMap:nullptr);
        return texture&&texture->IsValid()?texture:RHIMaterial::GetDefaultPrefilterCubemap();
    }
    std::shared_ptr<RHITexture> RHISceneRenderer::ActiveBRDF() const {
        auto ready=m_Environment.Ready();auto texture=ready?ready->BRDF:(m_PreparedManualIBL?m_BRDFLUT:nullptr);
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
        // Range 0: t0 (HDR Texture)
        // Range 1: s0 (Linear/Bilinear Sampler)
        nri::DescriptorRangeDesc ranges[2] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 1;
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
        // Range 0: t0 (Depth) -> TEXTURE
        // Range 1: t1 (Normal) -> TEXTURE
        // Range 2: t2 (Color) -> TEXTURE
        // Range 3: s0 (Linear Sampler) -> SAMPLER
        nri::DescriptorRangeDesc compTexRanges[4] = {};
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

        compTexRanges[2].baseRegisterIndex = 2;
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

    struct DrawItem {
        std::shared_ptr<const RHIMeshView> Mesh;
        RHIObjectPushConstants PushConsts;
        Axle::SurfaceType SurfaceType = Axle::SurfaceType::Opaque;
        Axle::CullMode CullMode = Axle::CullMode::None;
        bool DepthWrite = true;
        float DistanceSq = 0.0f;
        int RenderQueue = 2000;
        std::shared_ptr<const RHIMaterial> Material;
        bool UseCustomShader = false;
        std::shared_ptr<const CustomReadyGeneration> CustomGeneration;
        PackedMaterialResultData PackedConstants;
        std::vector<TextureRequest> TextureRequests;
        entt::entity DiagnosticEntity = entt::null;
    };

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
                                       const SceneRenderSettings& settings,
                                       PhysicsScene* physicsScene,
                                       std::shared_ptr<RHI::RHIViewTargets> viewTargets)
    {
        m_LastRenderSucceeded = false; m_LastCompletion = {};
        if (!m_Initialized || !m_Device->CanSubmit() || !colorTarget || !colorRTV || width == 0 || height == 0) return;
        m_MaterialDiagnostics.clear(); m_MaterialFeedback.clear();
        if (!world.ErrorMessage.empty() || !m_Frame.Begin()) { AXLE_CORE_ERROR("View frame acquisition failed"); return; }
        auto rollback = std::unique_ptr<RHISceneRenderer, std::function<void(RHISceneRenderer*)>>(this,
            [&](auto*) { if (!m_LastRenderSucceeded) m_Frame.Abandon(); });
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
        if (!m_CascadedShadows.Prepare(world,camera,settings,m_MaterialTextures)) {
            AXLE_CORE_ERROR("Shadow preparation failed: {0}",m_CascadedShadows.GetDiagnostic()); return;
        }
        if (!m_LocalShadows.Prepare(world,camera,settings,m_MaterialTextures)) {AXLE_CORE_ERROR("Local shadow preparation failed: {0}",m_LocalShadows.GetDiagnostic());return;}
        const auto localIdentity=m_Device->GetResources().AcquireNative(m_LocalShadows.GetSRV()).GetHandle();
        if(localIdentity!=m_BoundLocalAtlas) {
            if(!m_DescriptorPool.UpdateTexture(m_DefaultDescriptorSet,0,16,m_LocalShadows.GetSRV()))return;
            for(auto& draw:m_MaterialDraws)if(!m_DescriptorPool.UpdateTexture(draw->Set,0,16,m_LocalShadows.GetSRV()))return;
            m_BoundLocalAtlas=localIdentity;
        }
        if (!m_Environment.Prepare(world.Skybox)) return;
        m_PreparedManualIBL=m_UseIBL && world.Skybox.Type==1 && world.Skybox.HDRPath.empty();
        const std::array<std::shared_ptr<RHITexture>,3> ibl={ActiveIrradiance(),ActivePrefilter(),ActiveBRDF()};
        std::array<RHIResourceHandle,3> identities;
        std::vector<nri::Descriptor*> descriptors;
        for(size_t i=0;i<3;++i){if(!ibl[i]||!ibl[i]->GetSRV())return;identities[i]=ibl[i]->GetHandle();descriptors.push_back(ibl[i]->GetSRV());}
        if(identities!=m_BoundIBL) {
            if(!m_DescriptorPool.UpdateTextureRange(m_DefaultDescriptorSet,0,6,descriptors))return;
            for(auto& draw:m_MaterialDraws)if(!m_DescriptorPool.UpdateTextureRange(draw->Set,0,6,descriptors))return;
            m_BoundIBL=identities;
        }
        const auto skyTexture=m_Environment.Ready()?m_Environment.Ready()->Radiance:RHIMaterial::GetDefaultIrradianceCubemap();
        if(!skyTexture||!m_DescriptorPool.UpdateTexture(m_SkyboxDescriptorSet,0,0,skyTexture->GetSRV()))return;
        const auto shadowIdentity=m_Device->GetResources().AcquireNative(m_CascadedShadows.GetSRV()).GetHandle();
        if (shadowIdentity!=m_BoundCascadeAtlas) {
            // Every cached set owns its last texture dependency. Refresh unused
            // sets too, so a removed mesh cannot retain a retired Ultra atlas.
            if (!m_DescriptorPool.UpdateTexture(m_DefaultDescriptorSet,0,15,m_CascadedShadows.GetSRV())) return;
            for (auto& draw:m_MaterialDraws)
                if (!m_DescriptorPool.UpdateTexture(draw->Set,0,15,m_CascadedShadows.GetSRV())) return;
            m_BoundCascadeAtlas=shadowIdentity;
        }
        m_LastDrawCallCount = 0;
        m_CustomDescriptorPool.Reset(); // The preceding RenderScene submission completed before reuse.
        m_LastRenderedEntityCount = 0;
        m_LastStreamedTransformBytes = 0;

        // 1. Stream Global Scene Constants & Forward+ Multi-Light Buffer
        const auto lighting=ResolveSceneLighting(world);
        static const float s_Halton2[8] = { 0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f };
        static const float s_Halton3[8] = { 0.333333f, 0.666667f, 0.111111f, 0.444444f, 0.777778f, 0.222222f, 0.555556f, 0.888889f };
        float jitterX = 0.0f;
        float jitterY = 0.0f;
        if (settings.EnableTAA && width > 0 && height > 0) {
            jitterX = (s_Halton2[m_FrameIndex % 8] - 0.5f) / static_cast<float>(width) * settings.TAAJitterSpread;
            jitterY = (s_Halton3[m_FrameIndex % 8] - 0.5f) / static_cast<float>(height) * settings.TAAJitterSpread;
        }

        glm::mat4 projZO = camera.GetProjectionMatrixZO();
        if (settings.EnableTAA) {
            projZO[2][0] += 2.0f * jitterX;
            projZO[2][1] += 2.0f * jitterY;
        }
        glm::mat4 viewProjZO = projZO * camera.GetViewMatrix();

        RHISceneConstants sceneConstants;
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

        m_LightBuffer.Update(lightData);

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

        if (m_SceneConstantBuffer.IsValid()) {
            if (!m_SceneConstantBuffer.UpdateData(&sceneConstants, sizeof(RHISceneConstants))) { AXLE_CORE_ERROR("View scene uniform update failed"); return; }
        }

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

        auto rejectTargets = [&] { m_Frame.Abandon(); };
        RHIViewTargetDesc targetDesc; targetDesc.Width = width; targetDesc.Height = height;
        targetDesc.ColorFormat = FromNRIFormat(m_CurrentColorFormat); targetDesc.DepthFormat = FromNRIFormat(m_CurrentDepthFormat);
        targetDesc.EnableDepth = sceneDepthTexture != nullptr; targetDesc.Samples = activeSampleCount;
        targetDesc.HDR = useToneMap; targetDesc.FXAA = useFXAA; targetDesc.OwnPrimary = false;
        targetDesc.EnableNormal = (settings.EnableGTAO || settings.EnableContactShadows || settings.EnableSSR);
        targetDesc.EnableScreenStorage = (settings.EnableGTAO || settings.EnableContactShadows || settings.EnableSSR || settings.EnableVolumetricFog || settings.EnableBloom || settings.EnableAutoExposure);
        if (settings.EnableTAA) targetDesc.HistoryCount = 2;
        if (viewTargets) {
            auto expected = targetDesc; expected.OwnPrimary = viewTargets->GetDesc().OwnPrimary;
            expected.HistoryCount = viewTargets->GetDesc().HistoryCount;
            if (!viewTargets->IsValid() || viewTargets->GetDesc() != expected ||
                (viewTargets->Get(RHIViewTarget::Color) && viewTargets->Get(RHIViewTarget::Color)->GetNative() != colorTarget) ||
                (viewTargets->Get(RHIViewTarget::Depth) && viewTargets->Get(RHIViewTarget::Depth)->GetNative() != depthTarget)) { rejectTargets(); return; }
            m_ImplicitViewTargets.reset(); m_ImplicitViewColor = {};
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
        const auto hdr = useToneMap ? graph.Import("HDR", m_IntermediateHDRTarget->GetNative()) : output;
        const auto fxaa = useFXAA ? graph.Import("FXAA", m_IntermediateFXAATarget->GetNative()) : output;
        const bool copyRefraction = activeSampleCount == 1 && sceneDepthDSV && sceneDepthTexture &&
            std::any_of(world.Meshes.begin(), world.Meshes.end(), [](const auto& mesh) {
                const auto& material = mesh.Material.RHIMaterialAsset;
                return mesh.NativeView && mesh.NativeView->IsValid() && material &&
                    material->GetSurfaceType() == SurfaceType::Transparent && material->GetDesc().RefractionEnabled;
            });
        const bool copyParticleDepth = settings.EnableParticles && m_ParticleRenderer && !world.Particles.empty() &&
            activeSampleCount == 1 && sceneDepthDSV && sceneDepthTexture;
        const auto opaqueColor = copyRefraction ? graph.Import("OpaqueColor", m_OpaqueSceneColor->GetNative()) : output;
        const auto opaqueDepth = copyRefraction ? graph.Import("OpaqueDepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto particleDepthImage = copyParticleDepth ? graph.Import("ParticleDepth", m_ParticleSceneDepth->GetNative()) : sceneDepth;
        const bool useGTAO = (settings.EnableGTAO || settings.EnableContactShadows) && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid() && m_OpaqueSceneDepth && m_OpaqueSceneDepth->IsValid() &&
            m_ScreenStorageTarget && m_ScreenStorageTarget->IsValid() && m_ScreenStorageTarget->GetUAV() != nullptr;
        const auto gtaoInput = useGTAO ? graph.Import("GTAOInput", m_OpaqueSceneColor->GetNative()) : output;
        const auto gtaoDepth = useGTAO ? graph.Import("GTAODepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto gtaoStorage = useGTAO ? graph.Import("GTAOStorage", m_ScreenStorageTarget->GetNative()) : output;

        const bool useSSR = settings.EnableSSR && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_SSRComputePipeline && m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid() &&
            m_OpaqueSceneDepth && m_OpaqueSceneDepth->IsValid() &&
            m_ScreenStorageTarget && m_ScreenStorageTarget->IsValid() && m_ScreenStorageTarget->GetUAV() != nullptr;
        const auto ssrInput = useSSR ? graph.Import("SSRInput", m_OpaqueSceneColor->GetNative()) : output;
        const auto ssrDepth = useSSR ? graph.Import("SSRDepth", m_OpaqueSceneDepth->GetNative()) : sceneDepth;
        const auto ssrStorage = useSSR ? graph.Import("SSRStorage", m_ScreenStorageTarget->GetNative()) : output;

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
        const auto bloomStorage = useBloom ? graph.Import("BloomStorage", m_ScreenStorageTarget->GetNative()) : output;

        RHITexture* history0 = viewTargets ? viewTargets->Get(RHIViewTarget::History0) : nullptr;
        RHITexture* history1 = viewTargets ? viewTargets->Get(RHIViewTarget::History1) : nullptr;
        RHITexture* historyRead = (m_TAAHistoryIndex == 0) ? history0 : history1;
        RHITexture* historyWrite = (m_TAAHistoryIndex == 0) ? history1 : history0;
        const bool useTAA = settings.EnableTAA && activeSampleCount == 1 && sceneDepthTexture && sceneDepthDSV &&
            m_TAAPipelineSDR && history0 && history0->IsValid() && history1 && history1->IsValid();
        // The first frame deliberately reads an uninitialized slot but the
        // shader bypasses it through the explicit HistoryValid flag below.
        const auto taaHistoryRead = useTAA ? graph.Import("TAAHistoryRead", historyRead->GetNative(), true) : output;
        const auto taaHistoryWrite = useTAA ? graph.Import("TAAHistoryWrite", historyWrite->GetNative()) : output;
        auto attachments = [&](Load load) {
            std::vector<Graph::Use> uses{{sceneColor, Layout::ColorAttachment, Access::Write, load}};
            if (sceneDepthDSV) uses.push_back({sceneDepth, Layout::DepthStencilWrite, Access::Write, load});
            return uses;
        };
        auto* shadowAtlas=m_CascadedShadows.GetSampledAtlas();
        const auto shadows=graph.Import("CascadeAtlas",shadowAtlas?shadowAtlas->GetNative():nullptr,!m_CascadedShadows.IsActive());
        if (m_CascadedShadows.IsActive()) graph.AddPass("CascadedShadows", {{shadows,Layout::DepthStencilWrite,Access::Write,Load::Clear}});
        auto* localAtlas=m_LocalShadows.GetSampledAtlas();
        const auto localShadows=graph.Import("LocalShadowAtlas",localAtlas?localAtlas->GetNative():nullptr,!m_LocalShadows.NeedsRender());
        if(m_LocalShadows.NeedsRender())graph.AddPass("LocalShadows",{{localShadows,Layout::DepthStencilWrite,Access::Write,Load::Clear}});
        auto opaqueUses=attachments(Load::Clear);
        if(localAtlas)opaqueUses.push_back({localShadows,Layout::ShaderResource});
        if (shadowAtlas) opaqueUses.push_back({shadows,Layout::ShaderResource});
        graph.AddPass("OpaqueAndSky", std::move(opaqueUses));
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
        if (copyParticleDepth) graph.AddPass("ParticleDepthCopy", {
            {sceneDepth,Layout::CopySource}, {particleDepthImage,Layout::CopyDest,Access::Write}});
        auto overlayUses = attachments(Load::Load);
        if (copyParticleDepth) overlayUses.push_back({particleDepthImage,Layout::ShaderResource});
        graph.AddPass("ParticlesAndPhysics", std::move(overlayUses));
        if (useMSAA) graph.AddPass("Resolve", {
            {sceneColor,Layout::ResolveSource}, {useToneMap ? hdr : fxaa,Layout::ResolveDest,Access::Write}});
        if (useGTAO) {
            graph.AddPass("GTAOCopy", {{sceneColor,Layout::CopySource}, {gtaoInput,Layout::CopyDest,Access::Write},
                {sceneDepth,Layout::CopySource}, {gtaoDepth,Layout::CopyDest,Access::Write}});
            graph.AddPass("GTAOCompute", {
                {gtaoDepth, Layout::ShaderResource},
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
            graph.AddPass("SSRCompute", {
                {ssrDepth, Layout::ShaderResource},
                {ssrInput, Layout::ShaderResource},
                {ssrStorage, Layout::ShaderResourceStorage, Access::Write}});
            graph.AddPass("SSRApply", {
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
        if (useBloom) {
            graph.AddPass("BloomCopy", {{sceneColor, Layout::CopySource}, {bloomInput, Layout::CopyDest, Access::Write}});
            graph.AddPass("BloomCompute", {
                {bloomInput, Layout::ShaderResource},
                {bloomStorage, Layout::ShaderResourceStorage, Access::Write}});
            graph.AddPass("BloomApply", {
                {bloomStorage, Layout::ShaderResource},
                {bloomInput, Layout::ShaderResource},
                {sceneColor, Layout::ColorAttachment, Access::Write, Load::Clear}});
        }
        if (useTAA) {
            graph.AddPass("TAAResolve", {
                {sceneColor, Layout::ShaderResource},
                {sceneDepth, Layout::ShaderResource},
                {taaHistoryRead, Layout::ShaderResource},
                {taaHistoryWrite, Layout::ColorAttachment, Access::Write, Load::Clear}});
            graph.AddPass("TAACopy", {
                {taaHistoryWrite, Layout::CopySource},
                {sceneColor, Layout::CopyDest, Access::Write}});
        }
        if (useToneMap) graph.AddPass("ToneMapping", {
            {hdr,Layout::ShaderResource}, {fxaa,Layout::ColorAttachment,Access::Write,Load::Clear}});
        if (useFXAA) graph.AddPass("FXAA", {
            {fxaa,Layout::ShaderResource}, {output,Layout::ColorAttachment,Access::Write,Load::Clear}});
        std::vector<Graph::Use> exports{{output,Layout::ShaderResource}};
        if (sceneDepthDSV) exports.push_back({sceneDepth,Layout::DepthStencilRead});
        graph.AddPass("Export", std::move(exports));
        auto beginPass = [&](std::string_view name) {
            if (graph.BeginPass(name, Commands())) return true;
            AXLE_CORE_ERROR("View graph recording failed: {0}", graph.GetError());
            return false;
        };
        if (!graph.Compile()) {
            AXLE_CORE_ERROR("View graph rejected: {0}", graph.GetError()); return;
        }

        if (m_CascadedShadows.IsActive() && (!beginPass("CascadedShadows") || !m_CascadedShadows.Record(Commands()))) return;
        m_LastDrawCallCount+=m_CascadedShadows.GetDrawCount();
        if(m_LocalShadows.NeedsRender()&&(!beginPass("LocalShadows")||!m_LocalShadows.Record(Commands())))return;
        m_LastDrawCallCount+=m_LocalShadows.GetDrawCount();
        if (!beginPass("OpaqueAndSky")) return;

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

        Commands().BeginRendering(renderingDesc);
        auto abandonRecording = [&] {
            Commands().EndRendering();
            m_Frame.Abandon();
            rejectTargets();
        };


        // Viewport & Scissor Setup
        nri::Viewport vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        Commands().SetViewport(vp);

        nri::Rect scissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
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
            if (m_DefaultDescriptorSet) {
                RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_DefaultDescriptorSet);
            }
        }

        auto RenderSkybox=[&]() {
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
                if (!m_SkyboxConstantBuffer.UpdateData(&skyboxConstants, sizeof(RHISkyboxConstants))) return;

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
                RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(),0,m_SkyboxDescriptorSet);
                // 5. Draw full-screen triangle on far plane (SV_VertexID unproject)
                Commands().Draw(3, 1, 0, 0);
                m_LastDrawCallCount++;
            }
        }

        };

        // 4. Iterate Scene Entities & Stream Transforms via Root Constants (Multi-Pass Forward Pipeline)
        {
            std::vector<DrawItem> opaqueQueue;
            std::vector<DrawItem> transparentQueue;
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
                    item.PushConsts.ColorPrimary = meshInst.Material.AlbedoColor;
                    item.PushConsts.ColorSecondary = meshInst.Material.SecondaryColor;
                    float defaultRoughness = (meshInst.Material.PatternMode == 1 || meshInst.Material.PatternMode == 0) ? 0.15f : 0.5f;
                    float defaultMetallic = (meshInst.Material.PatternMode == 1 || meshInst.Material.PatternMode == 0) ? 0.05f : 0.0f;
                    item.PushConsts.Params = glm::vec4(meshInst.Material.TileScale.x, static_cast<float>(meshInst.Material.PatternMode), defaultRoughness, defaultMetallic);
                    item.PushConsts.SunDirAndIntensity = sceneConstants.SunDirection;
                    glm::vec3 diff = glm::vec3(worldMat[3]) - camPos;
                    item.DistanceSq = glm::dot(diff, diff);

                    // Apply PBR Material parameters if present
                    // Entity material overrides the mesh resource material.
                    if (meshInst.Material.RHIMaterialAsset) {
                        auto mat = meshInst.Material;
                        item.Material = mat.RHIMaterialAsset;
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
                                auto matchesSchema=[&](const auto& properties) {
                                    return properties.size()==plan.PropertySchema.size() &&
                                        std::equal(properties.begin(),properties.end(),plan.PropertySchema.begin(),
                                            [](const auto& property,const auto& desc){return property.Desc==desc;});
                                };
                                mat.CustomProperties=SynchronizeCustomProperties(plan.PropertySchema,mat.CustomProperties);
                                m_MaterialFeedback[meshInst.EntityID] = {mat.CustomProperties, selected.Generation->GenerationID};
                                EngineMaterialValues engine;
                                engine.Model=worldMat; engine.ViewProjection=sceneConstants.ViewProjection;
                                engine.View=sceneConstants.View; engine.Projection=sceneConstants.Projection;
                                engine.CameraPosition=sceneConstants.CameraPosition;
                                engine.SunDirection=sceneConstants.SunDirection; engine.SunColor=sceneConstants.SunColor;
                                engine.BaseColor=mat.AlbedoColor; engine.BaseUVTransform=glm::vec4(mat.UVScale,mat.UVOffset);
                                engine.LegacySurfaceParams=glm::vec4(mat.Roughness,mat.Metallic,mat.AO,mat.UVScale.x);
                                auto resolved=ResolveMaterialProperties(plan,mat.CustomProperties);
                                if (auto* properties=std::get_if<ResolvedMaterialProperties>(&resolved)) {
                                    auto packed=PackMaterialUniforms(plan,engine,*properties);
                                    if (auto* buffers=std::get_if<PackedMaterialResultData>(&packed)) {
                                        EffectiveMaterialValues values;
                                        values.BaseMapPath=mat.AlbedoPath; values.UseBaseMap=mat.UseAlbedoMap;
                                        values.Properties=mat.CustomProperties;
                                        auto textures=ResolveMaterialTextureRequests(plan,values);
                                        if (auto* requests=std::get_if<TextureRequestResultData>(&textures)) {
                                            item.PackedConstants=std::move(*buffers);
                                            item.TextureRequests=std::move(requests->Requests);
                                            item.CustomGeneration=selected.Generation;
                                            item.UseCustomShader=true;
                                        } else diagnostic(std::get<MaterialBindingError>(textures).Message);
                                    } else diagnostic(std::get<MaterialBindingError>(packed).Message);
                                } else diagnostic(std::get<MaterialBindingError>(resolved).Message);
                            }
                        }
                    } else {
                        item.Material = meshInst.Material.RHIMaterialAsset;
                    }
                    if (item.Material) {
                        item.Material->ApplyToPushConstants(item.PushConsts);
                        item.PushConsts.Model = worldMat;
                        item.SurfaceType = item.Material->GetSurfaceType();
                        item.CullMode = item.Material->GetCullMode();
                        item.DepthWrite = item.Material->GetDepthWrite();
                        item.RenderQueue = item.Material->GetDesc().RenderQueue;
                    }

                    if (item.SurfaceType == Axle::SurfaceType::Transparent || item.SurfaceType == Axle::SurfaceType::Additive) {
                        transparentQueue.push_back(std::move(item));
                    } else {
                        opaqueQueue.push_back(std::move(item));
                    }
                }
            }

            nri::Pipeline* currentPipeline = activePipeline;

            bool refractionReady=false;
            size_t materialDrawIndex = 0;
            size_t customDrawIndex = 0;
            bool drawFailed = false;
            auto RenderDrawItem = [&](const DrawItem& item) {
                if (item.UseCustomShader && item.CustomGeneration) {
                    auto drawCustom=[&]() -> bool {
                        const auto& generation=*item.CustomGeneration;
                        auto* pipeline=GetOrCreateCustomPipeline(generation,sceneTargetFormat,m_CurrentDepthFormat,activeSampleCount,item.SurfaceType,item.CullMode,item.DepthWrite);
                        if (!pipeline || !generation.PipelineLayout || !generation.PipelineLayout->IsValid()) return false;
                        if (customDrawIndex==m_CustomDraws.size()) m_CustomDraws.push_back(std::make_unique<CustomDrawResources>());
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
                            auto texture=m_MaterialTextures.Get(request.ResolvedPath,request.Role==TextureRole::Color);
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
                                m_CustomDescriptorPool.UpdateTexture(set,static_cast<uint32_t>(rangeIndex),0,descriptor);
                            }
                        }
                        Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS,*generation.PipelineLayout);
                        m_CustomDescriptorPool.Bind(Commands().GetNative());
                        for(size_t i=0;i<resource.Sets.size();++i) RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(),static_cast<uint32_t>(i),resource.Sets[i]);
                        Commands().SetPipeline(pipeline);
                        item.Mesh->Bind(Commands()); item.Mesh->Draw(Commands());
                        m_LastDrawCallCount+=static_cast<uint32_t>(item.Mesh->GetSubmeshCount()); ++m_LastRenderedEntityCount;
                        return true;
                    };
                    bool drawn=drawCustom();
                    // Restore the complete built-in binding context, even on preparation failure.
                    Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS,m_PipelineLayout);
                    m_DescriptorPool.Bind(Commands().GetNative());
                    Commands().SetRootDescriptor(0,m_SceneConstantBuffer.GetCBV());
                    Commands().SetRootDescriptor(1,m_LightBuffer.GetCBV());
                    Commands().SetRootDescriptor(3,m_CascadedShadows.GetConstantsCBV());
        Commands().SetRootDescriptor(4,m_LocalShadows.GetConstantsCBV());
                    currentPipeline=nullptr;
                    if (drawn) return;
                    m_MaterialDiagnostics[static_cast<uint32_t>(item.DiagnosticEntity)] = "Custom draw pipeline or resource preparation failed; using built-in material.";
                }
                nri::Pipeline* needed = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && item.SurfaceType == Axle::SurfaceType::Opaque && item.CullMode == Axle::CullMode::None && item.DepthWrite && activeSampleCount == 1 && m_PipelineHDR)
                    ? m_PipelineHDR
                    : (sceneTargetFormat == nri::Format::RGBA8_UNORM && item.SurfaceType == Axle::SurfaceType::Opaque && item.CullMode == Axle::CullMode::None && item.DepthWrite && activeSampleCount == 1 && m_Pipeline)
                        ? m_Pipeline
                        : GetOrCreatePipeline(sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount, item.SurfaceType, item.CullMode, item.DepthWrite);

                if (!needed) { drawFailed = true; return; }
                if (needed != currentPipeline) {
                    Commands().SetPipeline(needed);
                    currentPipeline = needed;
                }

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
                auto& resource = *m_MaterialDraws[materialDrawIndex++];
                RHIMaterialConstants materialData;
                if (item.Material) materialData = item.Material->BuildConstants();
                std::vector<nri::Descriptor*> srvs(ForwardTextureCount);
                for (uint32_t slot = 0; slot < static_cast<uint32_t>(RHITextureSlot::Count); ++slot) {
                    auto textureSlot = static_cast<RHITextureSlot>(slot);
                    std::shared_ptr<RHITexture> texture;
                    if (item.Material && (materialData.Flags.x & (1u << slot))) {
                        if (item.Material->HasTexture(textureSlot)) texture = item.Material->GetTexture(textureSlot);
                        else {
                            const auto& desc = item.Material->GetDesc();
                            auto path = slot == 2 && desc.TextureMode == TextureMode::PackedORM
                                ? desc.ORMPath : item.Material->GetTexturePath(textureSlot);
                            texture = m_MaterialTextures.Get(path, slot == 0 || slot == 5);
                        }
                    }
                    if (!texture) {
                        materialData.Flags.x &= ~(1u << slot);
                        texture = RHIMaterial::GetFallbackTexture(textureSlot);
                    }
                    if (!texture || !texture->GetSRV()) { drawFailed = true; return; }
                    resource.Textures[slot] = texture;
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
                m_DescriptorPool.UpdateTextureRange(resource.Set, 0, 0, srvs);
                if (!resource.Constants.UpdateData(&materialData, sizeof(materialData))) { drawFailed = true; return; }
                Commands().SetRootDescriptor(2, resource.Constants.GetCBV());
                RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, resource.Set);

                Commands().SetRootConstants(0, &item.PushConsts, sizeof(item.PushConsts));
                m_LastStreamedTransformBytes += sizeof(item.PushConsts);

                item.Mesh->Bind(Commands());
                item.Mesh->Draw(Commands());

                m_LastDrawCallCount += static_cast<uint32_t>(item.Mesh->GetSubmeshCount());
                m_LastRenderedEntityCount++;
            };

            std::sort(opaqueQueue.begin(), opaqueQueue.end(), [](const DrawItem& a, const DrawItem& b) {
                return a.RenderQueue < b.RenderQueue;
            });
            // 4a. Render Opaque & Cutout Queue
            for (const auto& item : opaqueQueue) {
                RenderDrawItem(item);
                if (drawFailed) { abandonRecording(); return; }
            }

            RenderSkybox();
            Commands().EndRendering();
            if (copyRefraction) {
                if (!beginPass("RefractionCopy")) return;
                Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
                Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);
                refractionReady = true;
            }
            if (!beginPass("Transparent")) return;
            colorAttachment.loadOp = nri::LoadOp::LOAD;
            depthAttachment.loadOp = nri::LoadOp::LOAD;
            if (sceneDepthDSV) renderingDesc.depth = depthAttachment;
            Commands().BeginRendering(renderingDesc);
            // Skybox changes the entire binding layout. Restore all built-in
            // receiver state before transparent or custom material draws.
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS,m_PipelineLayout);
            m_DescriptorPool.Bind(Commands().GetNative());
            Commands().SetRootDescriptor(0,m_SceneConstantBuffer.GetCBV());
            Commands().SetRootDescriptor(1,m_LightBuffer.GetCBV());
            Commands().SetRootDescriptor(3,m_CascadedShadows.GetConstantsCBV());
        Commands().SetRootDescriptor(4,m_LocalShadows.GetConstantsCBV());
            currentPipeline=nullptr;

            // 4b. Render Transparent & Additive Queue (Sorted Back-to-Front)
            if (!transparentQueue.empty()) {
                std::sort(transparentQueue.begin(), transparentQueue.end(), [](const DrawItem& a, const DrawItem& b) {
                    return a.RenderQueue == b.RenderQueue ? a.DistanceSq > b.DistanceSq : a.RenderQueue < b.RenderQueue;
                });
                for (const auto& item : transparentQueue) {
                    RenderDrawItem(item);
                    if (drawFailed) { abandonRecording(); return; }
                }
            }
        }



        Commands().EndRendering();
        if (copyParticleDepth) {
            if (!beginPass("ParticleDepthCopy")) return;
            Commands().CopyTexture(*m_ParticleSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);
        }
        if (!beginPass("ParticlesAndPhysics")) return;
        Commands().BeginRendering(renderingDesc);
        // Particle simulation remains external to rendering; only owned batches
        // and the declared depth snapshot are consumed here.
        if (settings.EnableParticles && m_ParticleRenderer && !world.Particles.empty()) {
            m_ParticleRenderer->ResetMetrics();
            RHITexture* particleDepth = copyParticleDepth ? m_ParticleSceneDepth : nullptr;
            // MSAA uses depth testing with soft intersections disabled until a compatible depth resolve exists.
            m_ParticleRenderer->Begin(Commands(), camera, sceneTargetFormat, m_CurrentDepthFormat, activeSampleCount);
            m_ParticleRenderer->SetSceneDepth(particleDepth, width, height);
            for (const auto& batch : world.Particles) {
                auto effective=batch->MaterialResolved?batch->Material:ParticleMaterialAdapter::Resolve(batch->Config,batch->Resources);
                auto texture = effective.UseTexture ? m_MaterialTextures.Get(effective.TexturePath, true) : nullptr;
                auto normal = effective.UseNormalMap ? m_MaterialTextures.Get(effective.NormalMapPath, false) : nullptr;
                auto config = batch->Config;
                config.BlendMode = effective.BlendMode; config.LightingEnabled = effective.LightingEnabled;
                config.WrapLighting = effective.WrapLighting; config.ScatteringFactor = effective.ScatteringFactor;
                config.NormalMapStrength = effective.NormalMapStrength;
                m_ParticleRenderer->Render(Commands(), batch->Pool, config, texture.get(), normal.get(), config.SoftnessDistance,
                    batch->Position, batch->Rotation, batch->Scale, &effective);
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


                m_PhysicsDebugRenderer->End(Commands());
                m_LastDrawCallCount += m_PhysicsDebugRenderer->GetDrawCallCount();
            }
        }

        // 7. End Scene Rendering Pass
        Commands().EndRendering();

        // 7. Resolve Hardware MSAA if enabled
        if (useMSAA && m_MSAAColorTarget && m_MSAAColorTarget->IsValid()) {
            nri::Texture* resolveDstTexture = colorTarget;
            if (useToneMap) {
                resolveDstTexture = m_IntermediateHDRTarget->GetNative();
            } else if (useFXAA) {
                resolveDstTexture = m_IntermediateFXAATarget->GetNative();
            }

            if (!beginPass("Resolve")) return;
            Commands().ResolveTexture(resolveDstTexture, m_MSAAColorTarget->GetNative(), nri::ResolveOp::AVERAGE);
        }

        // 7.5. Half-resolution-style screen-space GTAO sampling. The shared
        // fullscreen pass samples a 2x depth footprint and writes the AO
        // modulation back into the scene color before tone mapping/FXAA.
        if (useGTAO) {
            if (!beginPass("GTAOCopy")) return;
            Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
            Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);

            if (!beginPass("GTAOCompute")) return;
            RHIGTAOConstants gtaoConstants{};
            gtaoConstants.InvProjection = glm::inverse(projZO);
            gtaoConstants.View = camera.GetViewMatrix();
            gtaoConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
            gtaoConstants.GTAOParams = glm::vec4(settings.GTAORadiusPixels, settings.EnableGTAO ? settings.GTAOStrength : 0.0f, 0.0025f, 2.0f);
            if (world.DirectionalLight && settings.EnableContactShadows) {
                glm::vec3 sunDir = glm::normalize(world.DirectionalLight->Direction);
                glm::vec3 sunDirView = glm::mat3(camera.GetViewMatrix()) * sunDir;
                gtaoConstants.SunDirView = glm::vec4(sunDirView, 1.0f);
                gtaoConstants.ContactParams = glm::vec4(settings.ContactShadowLengthPixels, 16.0f, settings.ContactShadowStrength, 0.02f);
            } else {
                gtaoConstants.SunDirView = glm::vec4(0.0f);
                gtaoConstants.ContactParams = glm::vec4(0.0f);
            }
            if (!m_GTAOConstantBuffer.UpdateData(&gtaoConstants, sizeof(RHIGTAOConstants))) return;

            m_GTAODescriptorPool.UpdateTexture(m_GTAOComputeTextureSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
            nri::Descriptor* normalSRV = (m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()) ? m_OpaqueSceneNormal->GetSRV() : RHIMaterial::GetDefaultBlackTexture()->GetSRV();
            m_GTAODescriptorPool.UpdateTexture(m_GTAOComputeTextureSet, 1, 0, normalSRV);
            m_GTAODescriptorPool.UpdateSampler(m_GTAOComputeTextureSet, 2, 0, m_GTAOSampler.GetNative());
            m_GTAODescriptorPool.UpdateStorageTexture(m_GTAOComputeStorageSet, 0, 0, m_ScreenStorageTarget->GetUAV());

            Commands().SetPipeline(m_GTAOComputePipeline);
            Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_GTAOComputePipelineLayout);
            Commands().SetRootDescriptor(0, m_GTAOConstantBuffer.GetCBV(), 0, nri::BindPoint::COMPUTE);
            m_GTAODescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_GTAOComputeTextureSet, nri::BindPoint::COMPUTE);
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 1, m_GTAOComputeStorageSet, nri::BindPoint::COMPUTE);
            Commands().Dispatch((width + 7) / 8, (height + 7) / 8, 1);

            if (!beginPass("GTAOApply")) return;
            m_GTAODescriptorPool.UpdateTexture(m_GTAOApplyDescriptorSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
            m_GTAODescriptorPool.UpdateTexture(m_GTAOApplyDescriptorSet, 1, 0, m_ScreenStorageTarget->GetSRV());
            m_GTAODescriptorPool.UpdateTexture(m_GTAOApplyDescriptorSet, 2, 0, m_OpaqueSceneColor->GetSRV());
            m_GTAODescriptorPool.UpdateSampler(m_GTAOApplyDescriptorSet, 3, 0, m_GTAOSampler.GetNative());

            nri::Pipeline* applyPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_GTAOApplyPipelineHDR)
                ? m_GTAOApplyPipelineHDR : m_GTAOApplyPipelineSDR;
            if (!applyPipeline) return;

            nri::AttachmentDesc applyColorAttachment = {};
            applyColorAttachment.descriptor = sceneTargetRTV;
            applyColorAttachment.loadOp = nri::LoadOp::CLEAR;
            applyColorAttachment.storeOp = nri::StoreOp::STORE;
            applyColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

            nri::RenderingDesc applyRenderingDesc = {};
            applyRenderingDesc.colors = &applyColorAttachment;
            applyRenderingDesc.colorNum = 1;

            Commands().BeginRendering(applyRenderingDesc);
            nri::Viewport applyVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
            Commands().SetViewport(applyVp);
            nri::Rect applyScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
            Commands().SetScissor(applyScissor);

            Commands().SetPipeline(applyPipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_GTAOApplyPipelineLayout);
            Commands().SetRootDescriptor(0, m_GTAOConstantBuffer.GetCBV(), 0, nri::BindPoint::GRAPHICS);
            m_GTAODescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_GTAOApplyDescriptorSet, nri::BindPoint::GRAPHICS);
            Commands().Draw(3, 1, 0, 0);
            Commands().EndRendering();
        }

        // 7.6. Screen-Space Reflections (SSR) with Reflection Probes Fallback
        if (useSSR) {
            if (!beginPass("SSRCopy")) return;
            Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
            Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);

            if (!beginPass("SSRCompute")) return;
            RHISSRConstants ssrConstants{};
            ssrConstants.View = camera.GetViewMatrix();
            ssrConstants.InvView = glm::inverse(camera.GetViewMatrix());
            ssrConstants.Projection = projZO;
            ssrConstants.InvProjection = glm::inverse(projZO);
            ssrConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
            ssrConstants.SSRParams = glm::vec4(settings.SSRLengthPixels, 32.0f, settings.SSRStrength, 0.5f);

            if (!world.ReflectionProbes.empty()) {
                const auto& probe = world.ReflectionProbes[0];
                ssrConstants.ProbeBoxMin = glm::vec4(probe.Position - probe.BoxExtents * 0.5f, probe.ParallaxCorrected ? 1.0f : 0.0f);
                ssrConstants.ProbeBoxMax = glm::vec4(probe.Position + probe.BoxExtents * 0.5f, probe.Intensity);
                ssrConstants.ProbePosition = glm::vec4(probe.Position, 1.0f);
            } else {
                ssrConstants.ProbeBoxMin = glm::vec4(-10.0f, -10.0f, -10.0f, 1.0f);
                ssrConstants.ProbeBoxMax = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
                ssrConstants.ProbePosition = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (!m_SSRConstantBuffer.UpdateData(&ssrConstants, sizeof(RHISSRConstants))) return;

            m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
            nri::Descriptor* ssrNormalSRV = (m_OpaqueSceneNormal && m_OpaqueSceneNormal->IsValid()) ? m_OpaqueSceneNormal->GetSRV() : RHIMaterial::GetDefaultBlackTexture()->GetSRV();
            m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 1, 0, ssrNormalSRV);
            m_SSRDescriptorPool.UpdateTexture(m_SSRComputeTextureSet, 2, 0, m_OpaqueSceneColor->GetSRV());
            m_SSRDescriptorPool.UpdateSampler(m_SSRComputeTextureSet, 3, 0, m_SSRSampler.GetNative());
            m_SSRDescriptorPool.UpdateStorageTexture(m_SSRComputeStorageSet, 0, 0, m_ScreenStorageTarget->GetUAV());

            Commands().SetPipeline(m_SSRComputePipeline);
            Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_SSRComputePipelineLayout);
            Commands().SetRootDescriptor(0, m_SSRConstantBuffer.GetCBV(), 0, nri::BindPoint::COMPUTE);
            m_SSRDescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_SSRComputeTextureSet, nri::BindPoint::COMPUTE);
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 1, m_SSRComputeStorageSet, nri::BindPoint::COMPUTE);
            Commands().Dispatch((width + 7) / 8, (height + 7) / 8, 1);

            if (!beginPass("SSRApply")) return;
            m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
            m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 1, 0, m_ScreenStorageTarget->GetSRV());
            m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 2, 0, m_OpaqueSceneColor->GetSRV());
            m_SSRDescriptorPool.UpdateTexture(m_SSRApplyDescriptorSet, 3, 0, ssrNormalSRV);
            m_SSRDescriptorPool.UpdateSampler(m_SSRApplyDescriptorSet, 4, 0, m_SSRSampler.GetNative());

            nri::Pipeline* applyPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_SSRApplyPipelineHDR)
                ? m_SSRApplyPipelineHDR : m_SSRApplyPipelineSDR;
            if (!applyPipeline) return;

            nri::AttachmentDesc applyColorAttachment = {};
            applyColorAttachment.descriptor = sceneTargetRTV;
            applyColorAttachment.loadOp = nri::LoadOp::CLEAR;
            applyColorAttachment.storeOp = nri::StoreOp::STORE;
            applyColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

            nri::RenderingDesc applyRenderingDesc = {};
            applyRenderingDesc.colors = &applyColorAttachment;
            applyRenderingDesc.colorNum = 1;

            Commands().BeginRendering(applyRenderingDesc);
            nri::Viewport applyVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
            Commands().SetViewport(applyVp);
            nri::Rect applyScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
            Commands().SetScissor(applyScissor);

            Commands().SetPipeline(applyPipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_SSRApplyPipelineLayout);
            Commands().SetRootDescriptor(0, m_SSRConstantBuffer.GetCBV(), 0, nri::BindPoint::GRAPHICS);
            m_SSRDescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_SSRApplyDescriptorSet, nri::BindPoint::GRAPHICS);
            Commands().Draw(3, 1, 0, 0);
            Commands().EndRendering();
        }

        // 7.7. Volumetric Fog & Atmospheric Media Pass
        if (useFog) {
            if (!beginPass("FogCopy")) return;
            Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);
            Commands().CopyTexture(*m_OpaqueSceneDepth->GetNative(), nullptr, *sceneDepthTexture, nullptr);

            if (!beginPass("FogCompute")) return;
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

            if (!m_FogConstantBuffer.UpdateData(&fogConstants, sizeof(RHIVolumetricFogConstants))) return;

            m_FogDescriptorPool.UpdateTexture(m_FogComputeTextureSet, 0, 0, m_OpaqueSceneDepth->GetSRV());
            m_FogDescriptorPool.UpdateSampler(m_FogComputeTextureSet, 1, 0, m_FogSampler.GetNative());
            m_FogDescriptorPool.UpdateStorageTexture(m_FogComputeStorageSet, 0, 0, m_ScreenStorageTarget->GetUAV());

            Commands().SetPipeline(m_FogComputePipeline);
            Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_FogComputePipelineLayout);
            Commands().SetRootDescriptor(0, m_FogConstantBuffer.GetCBV(), 0, nri::BindPoint::COMPUTE);
            m_FogDescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_FogComputeTextureSet, nri::BindPoint::COMPUTE);
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 1, m_FogComputeStorageSet, nri::BindPoint::COMPUTE);
            Commands().Dispatch((width + 7) / 8, (height + 7) / 8, 1);

            if (!beginPass("FogApply")) return;
            m_FogDescriptorPool.UpdateTexture(m_FogApplyDescriptorSet, 0, 0, m_OpaqueSceneColor->GetSRV());
            m_FogDescriptorPool.UpdateTexture(m_FogApplyDescriptorSet, 1, 0, m_ScreenStorageTarget->GetSRV());
            m_FogDescriptorPool.UpdateSampler(m_FogApplyDescriptorSet, 2, 0, m_FogSampler.GetNative());

            nri::Pipeline* applyPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_FogApplyPipelineHDR)
                ? m_FogApplyPipelineHDR : m_FogApplyPipelineSDR;
            if (!applyPipeline) return;

            nri::AttachmentDesc applyColorAttachment = {};
            applyColorAttachment.descriptor = sceneTargetRTV;
            applyColorAttachment.loadOp = nri::LoadOp::CLEAR;
            applyColorAttachment.storeOp = nri::StoreOp::STORE;
            applyColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

            nri::RenderingDesc applyRenderingDesc = {};
            applyRenderingDesc.colors = &applyColorAttachment;
            applyRenderingDesc.colorNum = 1;

            Commands().BeginRendering(applyRenderingDesc);
            nri::Viewport applyVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
            Commands().SetViewport(applyVp);
            nri::Rect applyScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
            Commands().SetScissor(applyScissor);

            Commands().SetPipeline(applyPipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_FogApplyPipelineLayout);
            Commands().SetRootDescriptor(0, m_FogConstantBuffer.GetCBV(), 0, nri::BindPoint::GRAPHICS);
            m_FogDescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_FogApplyDescriptorSet, nri::BindPoint::GRAPHICS);
            Commands().Draw(3, 1, 0, 0);
            Commands().EndRendering();
        }

        // 7c. Bloom Passes (Downsample, Filter & Apply)
        if (useBloom && m_ScreenStorageTarget && m_ScreenStorageTarget->IsValid() && m_OpaqueSceneColor && m_OpaqueSceneColor->IsValid()) {
            if (!beginPass("BloomCopy")) return;
            Commands().CopyTexture(*m_OpaqueSceneColor->GetNative(), nullptr, *sceneTargetTexture, nullptr);

            if (!beginPass("BloomCompute")) return;
            RHIBloomConstants bloomConstants{};
            bloomConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                                                  1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
            bloomConstants.BloomParams = glm::vec4(settings.BloomThreshold, settings.BloomIntensity, settings.BloomKnee, 0.0f);
            bloomConstants.BloomTint = glm::vec4(settings.BloomTint, settings.BloomRadius);
            m_BloomConstantBuffer.UpdateData(&bloomConstants, sizeof(RHIBloomConstants));

            m_BloomDescriptorPool.UpdateTexture(m_BloomComputeTextureSet, 0, 0, m_OpaqueSceneColor->GetSRV());
            m_BloomDescriptorPool.UpdateStorageTexture(m_BloomComputeStorageSet, 0, 0, m_ScreenStorageTarget->GetUAV());
            if (m_BloomSampler.IsValid()) {
                m_BloomDescriptorPool.UpdateSampler(m_BloomComputeTextureSet, 1, 0, m_BloomSampler.GetNative());
            }

            Commands().SetPipeline(m_BloomComputePipeline);
            Commands().SetPipelineLayout(nri::BindPoint::COMPUTE, m_BloomComputePipelineLayout);
            Commands().SetRootDescriptor(0, m_BloomConstantBuffer.GetCBV(), 0, nri::BindPoint::COMPUTE);
            m_BloomDescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_BloomComputeTextureSet, nri::BindPoint::COMPUTE);
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 1, m_BloomComputeStorageSet, nri::BindPoint::COMPUTE);
            Commands().Dispatch((width + 7) / 8, (height + 7) / 8, 1);

            if (!beginPass("BloomApply")) return;
            m_BloomDescriptorPool.UpdateTexture(m_BloomApplyDescriptorSet, 0, 0, m_ScreenStorageTarget->GetSRV());
            m_BloomDescriptorPool.UpdateTexture(m_BloomApplyDescriptorSet, 1, 0, m_OpaqueSceneColor->GetSRV());
            if (m_BloomSampler.IsValid()) {
                m_BloomDescriptorPool.UpdateSampler(m_BloomApplyDescriptorSet, 2, 0, m_BloomSampler.GetNative());
            }

            nri::Pipeline* bloomPipeline = (sceneTargetFormat == nri::Format::RGBA16_SFLOAT && m_BloomApplyPipelineHDR)
                ? m_BloomApplyPipelineHDR : m_BloomApplyPipelineSDR;
            if (!bloomPipeline) return;

            nri::AttachmentDesc bloomColorAttachment = {};
            bloomColorAttachment.descriptor = sceneTargetRTV;
            bloomColorAttachment.loadOp = nri::LoadOp::CLEAR;
            bloomColorAttachment.storeOp = nri::StoreOp::STORE;
            bloomColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

            nri::RenderingDesc bloomRenderingDesc = {};
            bloomRenderingDesc.colors = &bloomColorAttachment;
            bloomRenderingDesc.colorNum = 1;

            Commands().BeginRendering(bloomRenderingDesc);
            nri::Viewport bloomVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
            Commands().SetViewport(bloomVp);
            nri::Rect bloomScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
            Commands().SetScissor(bloomScissor);

            Commands().SetPipeline(bloomPipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_BloomApplyPipelineLayout);
            Commands().SetRootDescriptor(0, m_BloomConstantBuffer.GetCBV(), 0, nri::BindPoint::GRAPHICS);
            m_BloomDescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_BloomApplyDescriptorSet, nri::BindPoint::GRAPHICS);
            Commands().Draw(3, 1, 0, 0);
            Commands().EndRendering();
        }

        // 7d. Temporal Anti-Aliasing (TAA) Pass
        if (useTAA && historyRead && historyWrite) {
            if (!beginPass("TAAResolve")) return;
            RHITAAConstants taaConstants{};
            taaConstants.InvViewProj = glm::inverse(viewProjZO);
            taaConstants.PrevViewProj = m_TAAHistoryValid ? m_PrevViewProjection : viewProjZO;
            taaConstants.ScreenSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                                                1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
            taaConstants.TAAParams = glm::vec4(jitterX, jitterY, settings.TAAFeedbackMin, settings.TAAFeedbackMax);
            taaConstants.Reserved[0].x = m_TAAHistoryValid ? 1.0f : 0.0f;
            m_TAAConstantBuffer.UpdateData(&taaConstants, sizeof(RHITAAConstants));

            nri::Descriptor* currentSRV = m_IntermediateHDRTarget ? m_IntermediateHDRTarget->GetSRV() : m_OpaqueSceneColor->GetSRV();
            nri::Descriptor* depthSRV = m_OpaqueSceneDepth->GetSRV();
            m_TAADescriptorPool.UpdateTexture(m_TAADescriptorSet, 0, 0, currentSRV);
            m_TAADescriptorPool.UpdateTexture(m_TAADescriptorSet, 1, 0, depthSRV);
            m_TAADescriptorPool.UpdateTexture(m_TAADescriptorSet, 2, 0, historyRead->GetSRV());
            if (m_TAALinearSampler.IsValid() && m_TAAPointSampler.IsValid()) {
                m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 3, 0, m_TAALinearSampler.GetNative());
                m_TAADescriptorPool.UpdateSampler(m_TAADescriptorSet, 4, 0, m_TAAPointSampler.GetNative());
            }

            nri::Pipeline* taaPipeline = (historyWrite->GetFormat() == RHI::RHITextureFormat::RGBA16_FLOAT && m_TAAPipelineHDR)
                ? m_TAAPipelineHDR : m_TAAPipelineSDR;
            if (!taaPipeline) return;

            nri::AttachmentDesc taaColorAttachment = {};
            taaColorAttachment.descriptor = historyWrite->GetRTV();
            taaColorAttachment.loadOp = nri::LoadOp::CLEAR;
            taaColorAttachment.storeOp = nri::StoreOp::STORE;
            taaColorAttachment.clearValue.color.f = { 0.0f, 0.0f, 0.0f, 1.0f };

            nri::RenderingDesc taaRenderingDesc = {};
            taaRenderingDesc.colors = &taaColorAttachment;
            taaRenderingDesc.colorNum = 1;

            Commands().BeginRendering(taaRenderingDesc);
            nri::Viewport taaVp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
            Commands().SetViewport(taaVp);
            nri::Rect taaScissor = { 0, 0, static_cast<nri::Dim_t>(width), static_cast<nri::Dim_t>(height) };
            Commands().SetScissor(taaScissor);

            Commands().SetPipeline(taaPipeline);
            Commands().SetPipelineLayout(nri::BindPoint::GRAPHICS, m_TAAPipelineLayout);
            Commands().SetRootDescriptor(0, m_TAAConstantBuffer.GetCBV(), 0, nri::BindPoint::GRAPHICS);
            m_TAADescriptorPool.Bind(Commands().GetNative());
            RHIDescriptorPool::BindDescriptorSet(Commands().GetNative(), 0, m_TAADescriptorSet, nri::BindPoint::GRAPHICS);
            Commands().Draw(3, 1, 0, 0);
            Commands().EndRendering();

            if (!beginPass("TAACopy")) return;
            Commands().CopyTexture(*sceneTargetTexture, nullptr, *historyWrite->GetNative(), nullptr);
            m_TAAHistoryIndex = static_cast<uint8_t>(m_TAAHistoryIndex ^ 1u);
            m_TAAHistoryValid = true;
        }

        // 8. Post-Processing Tone Mapping Pass
        if (useToneMap && m_IntermediateHDRTarget && m_IntermediateHDRTarget->IsValid()) {
            if (!beginPass("ToneMapping")) return;
            RHIToneMappingConstants toneConstants{};
            float effectiveExposure = settings.Exposure;
            if (settings.EnableAutoExposure) {
                float dt = 0.016f;
                float targetExp = std::clamp(settings.Exposure, settings.AutoExposureMin, settings.AutoExposureMax);
                float blend = 1.0f - std::exp(-dt * std::max(settings.AutoExposureSpeed, 0.1f));
                m_AdaptedExposure = glm::mix(m_AdaptedExposure, targetExp, blend);
                effectiveExposure = m_AdaptedExposure;
            } else {
                m_AdaptedExposure = settings.Exposure;
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
                              toneConstants)) return;
        }

        // 9. Post-Processing FXAA Pass
        if (useFXAA && m_IntermediateFXAATarget && m_IntermediateFXAATarget->IsValid()) {
            if (!beginPass("FXAA")) return;
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
                       fxaaConstants)) return;
        }

        m_PrevViewProjection = viewProjZO;
        m_FrameIndex++;

        if (!beginPass("Export") || !graph.IsComplete()) return;
        if (!m_Frame.End() || !m_Frame.Submit()) { AXLE_CORE_ERROR("View command finalization or submit failed"); return; }
        m_LocalShadows.OnSubmitted();
        m_LastCompletion = m_Frame.GetCompletion();
        m_LastRenderSucceeded = static_cast<bool>(m_LastCompletion);
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
                                            const RHIToneMappingConstants& constants)
    {
        if (!m_Initialized || !inputHDR_SRV || !outputTarget || !outputRTV || width == 0 || height == 0) return false;

        // 1. Update 256-byte Tone Mapping Constants
        if (!m_ToneMappingConstantBuffer.UpdateData(&constants, sizeof(RHIToneMappingConstants)) ||
            !m_ToneMappingDescriptorPool.IsValid() || !m_ToneMappingDescriptorSet) return false;

        // 2. Bind HDR Texture SRV & Sampler in Space 1
        if (m_ToneMappingDescriptorPool.IsValid() && m_ToneMappingDescriptorSet) {
            m_ToneMappingDescriptorPool.UpdateTexture(m_ToneMappingDescriptorSet, 0, 0, inputHDR_SRV);
            if (m_ToneMappingSampler.IsValid()) {
                m_ToneMappingDescriptorPool.UpdateSampler(m_ToneMappingDescriptorSet, 1, 0, m_ToneMappingSampler.GetNative());
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

        if (m_ToneMappingConstantBuffer.IsValid() && m_ToneMappingConstantBuffer.GetCBV()) {
            cmd.SetRootDescriptor(0, m_ToneMappingConstantBuffer.GetCBV());
        }

        if (m_ToneMappingDescriptorPool.IsValid() && m_ToneMappingDescriptorSet) {
            m_ToneMappingDescriptorPool.Bind(cmd.GetNative());
            RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, m_ToneMappingDescriptorSet);
        }

        // Draw full-screen triangle via SV_VertexID
        cmd.Draw(3, 1, 0, 0);

        cmd.EndRendering();

        // Transition back to ShaderResource for presentation
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
                                      nri::Descriptor* depthSRV)
    {
        if (!m_Initialized || !inputSRV || !outputTarget || !outputRTV || width == 0 || height == 0) return false;

        // 1. Update 256-byte FXAA Constants
        if (!m_FXAAConstantBuffer.UpdateData(&constants, sizeof(RHIFXAAConstants)) ||
            !m_FXAADescriptorPool.IsValid() || !m_FXAADescriptorSet) return false;

        // 2. Bind Texture SRV & Sampler in Space 1
        if (m_FXAADescriptorPool.IsValid() && m_FXAADescriptorSet) {
            m_FXAADescriptorPool.UpdateTexture(m_FXAADescriptorSet, 0, 0, inputSRV);
            m_FXAADescriptorPool.UpdateTexture(m_FXAADescriptorSet, 1, 0,
                depthSRV ? depthSRV : RHIMaterial::GetDefaultWhiteTexture()->GetSRV());
            if (m_FXAASampler.IsValid()) {
                m_FXAADescriptorPool.UpdateSampler(m_FXAADescriptorSet, 2, 0, m_FXAASampler.GetNative());
            }
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

        if (m_FXAAConstantBuffer.IsValid() && m_FXAAConstantBuffer.GetCBV()) {
            cmd.SetRootDescriptor(0, m_FXAAConstantBuffer.GetCBV());
        }

        if (m_FXAADescriptorPool.IsValid() && m_FXAADescriptorSet) {
            m_FXAADescriptorPool.Bind(cmd.GetNative());
            RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, m_FXAADescriptorSet);
        }

        // Draw full-screen triangle via SV_VertexID
        cmd.Draw(3, 1, 0, 0);

        cmd.EndRendering();

        // Transition back to ShaderResource for presentation
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
