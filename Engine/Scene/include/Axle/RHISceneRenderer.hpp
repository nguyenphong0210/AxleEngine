#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/RHICascadedShadows.hpp>
#include <Axle/RHIEnvironment.hpp>
#include <Axle/RHILocalShadows.hpp>

#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/RHI/RHITransform.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHILight.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/MaterialTextureCache.hpp>
#include <Axle/CustomMaterialProgram.hpp>
#include <Axle/ViewContext.hpp>

#include <memory>
#include <array>
#include <string>
#include <glm/glm.hpp>

namespace Axle {

    class Scene;
    class PhysicsScene;
    class RHIParticleRenderer;
    class RHIPhysicsDebugRenderer;
    class RHIObjectPicker;

    // 256-byte CBV aligned constants for Atmospheric Skybox pass
    struct RHISkyboxConstants {
        glm::mat4 InvViewProjection;   // 64 bytes
        glm::vec4 CameraPosition;      // 16 bytes
        glm::vec4 SunDirection;        // 16 bytes (xyz = normalized dir, w = solar elevation)
        glm::vec4 SunColor;            // 16 bytes (rgb = sun color * intensity, w = day factor)
        glm::vec4 ZenithColor;         // 16 bytes (rgb = zenith color, w = sunset factor)
        glm::vec4 HorizonColor;        // 16 bytes (rgb = horizon color, w = turbidity)
        glm::vec4 GroundColor;         // 16 bytes (rgb = ground color, w = exposure)
        glm::vec4 Params;              // 16 bytes (x = SunSize, y = SunGlow, z = ToneMappingMode, w = Gamma)
        glm::vec4 Reserved[5];         // 80 bytes padding -> Total: 256 bytes
    };

    // 256-byte CBV aligned constants for Tone Mapping post-processing pass
    struct alignas(256) RHIToneMappingConstants {
        glm::vec4 Params{ 1.0f, 2.2f, 0.0f, 4.0f };       // x: Exposure, y: Gamma, z: ToneMappingMode, w: WhitePoint
        glm::vec4 ColorGrading{ 1.0f, 1.0f, 0.0f, 0.0f }; // x: Contrast, y: Saturation, z: VignetteIntensity, w: Reserved
        glm::vec4 Reserved[14]{};                         // 224 bytes padding -> Total: 256 bytes
    };
    static_assert(sizeof(RHIToneMappingConstants) == 256, "RHIToneMappingConstants must be 256 bytes aligned!");

    // 256-byte CBV aligned constants for GTAO & Contact Shadows pass
    struct alignas(256) RHIGTAOConstants {
        glm::mat4 InvProjection{ 1.0f };                                          // 64 bytes
        glm::mat4 View{ 1.0f };                                                   // 64 bytes
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // 16 bytes: (width, height, 1/w, 1/h)
        glm::vec4 GTAOParams{ 8.0f, 1.0f, 0.0025f, 2.0f };                        // 16 bytes: (radius px, strength, depth bias, power)
        glm::vec4 SunDirView{ 0.0f, 0.0f, 0.0f, 0.0f };                           // 16 bytes: (xyz: view-space dir, w: enabled)
        glm::vec4 ContactParams{ 16.0f, 16.0f, 1.0f, 0.02f };                     // 16 bytes: (max dist px, steps, strength, thickness)
        glm::vec4 Reserved[4]{};                                                  // 64 bytes padding -> Total: 256 bytes
    };
    static_assert(sizeof(RHIGTAOConstants) == 256, "RHIGTAOConstants must be 256 bytes aligned!");

    // 256-byte CBV aligned constants for SSR & Reflection Probes pass
    struct alignas(256) RHISSRConstants {
        glm::mat4 View{ 1.0f };                                    // 64 bytes
        glm::mat4 InvView{ 1.0f };                                 // 64 bytes
        glm::mat4 Projection{ 1.0f };                              // 64 bytes
        glm::mat4 InvProjection{ 1.0f };                           // 64 bytes
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // 16 bytes
        glm::vec4 SSRParams{ 50.0f, 32.0f, 1.0f, 0.5f };           // 16 bytes: (max dist, steps, strength, thickness)
        glm::vec4 ProbeBoxMin{ -10.0f, -10.0f, -10.0f, 1.0f };     // 16 bytes: (xyz: min, w: ParallaxCorrected)
        glm::vec4 ProbeBoxMax{ 10.0f, 10.0f, 10.0f, 1.0f };        // 16 bytes: (xyz: max, w: Intensity)
        glm::vec4 ProbePosition{ 0.0f, 0.0f, 0.0f, 0.0f };         // 16 bytes: (xyz: center, w: HasProbe)
        glm::vec4 Padding[11]{};                                   // 176 bytes -> 512 bytes total
    };
    static_assert(sizeof(RHISSRConstants) == 512, "RHISSRConstants must be 512 bytes aligned!");

    // 256-byte CBV aligned constants for one Hi-Z depth reduction level.
    struct alignas(256) RHIHiZConstants {
        glm::vec4 SourceSize{ 1.0f, 1.0f, 1.0f, 1.0f }; // source width/height and inverse size
        glm::vec4 DestinationSize{ 1.0f, 1.0f, 1.0f, 1.0f }; // destination width/height and inverse size
        glm::vec4 Params{ 0.0f, 0.0f, 0.0f, 0.0f }; // x: source is hardware depth, y: mip level
        glm::vec4 Reserved[13]{};
    };
    static_assert(sizeof(RHIHiZConstants) == 256, "RHIHiZConstants must be 256 bytes aligned!");

    // 256-byte CBV aligned constants for Volumetric Fog pass
    struct alignas(256) RHIVolumetricFogConstants {
        glm::mat4 View{ 1.0f };                                    // 64 bytes
        glm::mat4 InvView{ 1.0f };                                 // 64 bytes
        glm::mat4 Projection{ 1.0f };                              // 64 bytes
        glm::mat4 InvProjection{ 1.0f };                           // 64 bytes
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // 16 bytes
        glm::vec4 SunDirectionIntensity{ 0.45f, 0.85f, 0.25f, 1.0f };              // 16 bytes
        glm::vec4 SunColor{ 1.0f, 0.98f, 0.92f, 1.0f };                            // 16 bytes
        glm::vec4 FogParams{ 0.02f, 0.1f, 0.7f, 100.0f };                          // 16 bytes
        glm::vec4 FogColorIntensity{ 0.8f, 0.85f, 0.9f, 1.0f };                    // 16 bytes
        glm::vec4 DDGIParams{ 1.0f, 0.0f, 0.0f, 0.0f };                            // 16 bytes
        glm::vec4 Padding[10]{};                                                   // 160 bytes -> 512 bytes total
    };
    static_assert(sizeof(RHIVolumetricFogConstants) == 512, "RHIVolumetricFogConstants must be 512 bytes aligned!");

    // 256-byte CBV aligned constants for FXAA 3.11 post-processing pass
    struct alignas(256) RHIFXAAConstants {
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // (width, height, 1/w, 1/h)
        glm::vec4 FXAAParams{ 0.75f, 0.166f, 0.0833f, 0.0f };                     // (subpixQuality, edgeThreshold, edgeThresholdMin, reserved)
        glm::vec4 Reserved[14]{};                                                 // 224 bytes padding -> Total: 256 bytes
    };
    static_assert(sizeof(RHIFXAAConstants) == 256, "RHIFXAAConstants must be 256 bytes aligned!");

    // 256-byte CBV aligned constants for Bloom Kawase downsample/upsample pass
    struct alignas(256) RHIBloomConstants {
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // (width, height, 1/w, 1/h)
        glm::vec4 BloomParams{ 1.0f, 0.8f, 0.5f, 0.0f };                           // (threshold, intensity, knee, mode)
        glm::vec4 BloomTint{ 1.0f, 1.0f, 1.0f, 1.5f };                            // (rgb: tint, w: radius)
        glm::vec4 Reserved[13]{};                                                 // 208 bytes padding -> Total: 256 bytes
    };
    static_assert(sizeof(RHIBloomConstants) == 256, "RHIBloomConstants must be 256 bytes aligned!");

    struct alignas(256) RHIExposureConstants {
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f };
        glm::vec4 ExposureParams{ -10.0f, 6.0f, 3.0f, 1.0f };
        glm::vec4 ExposureLimits{ 0.1f, 10.0f, 0.18f, 0.0f };
        glm::vec4 Reserved[13]{};
    };
    static_assert(sizeof(RHIExposureConstants) == 256, "RHIExposureConstants must be 256 bytes aligned!");

    struct alignas(256) RHIDDGIConstants {
        glm::vec4 ScreenSize{ 1.0f };
        glm::vec4 AtlasSize{ 256.0f, 128.0f, 1.0f / 256.0f, 1.0f / 128.0f };
        glm::vec4 GridDimensions{ 32.0f, 16.0f, 32.0f, 0.0f };
        glm::vec4 GridOrigin{ 0.0f };
        glm::vec4 GridSpacing{ 1.0f };
        glm::vec4 Params{ 1.0f };
        glm::vec4 Reserved[10]{};
    };
    static_assert(sizeof(RHIDDGIConstants) == 256, "RHIDDGIConstants must be 256 bytes aligned!");

    // 256-byte CBV aligned constants for TAA pass
    struct alignas(256) RHITAAConstants {
        glm::mat4 InvViewProj{ 1.0f };                                            // 64 bytes
        glm::mat4 PrevViewProj{ 1.0f };                                           // 64 bytes
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // 16 bytes
        glm::vec4 TAAParams{ 0.0f, 0.0f, 0.88f, 0.96f };                          // 16 bytes (jitterX, jitterY, feedbackMin, feedbackMax)
        glm::vec4 Reserved[6]{};                                                  // 96 bytes padding -> Total: 256 bytes
    };
    static_assert(sizeof(RHITAAConstants) == 256, "RHITAAConstants must be 256 bytes aligned!");

    class RHISceneRenderer {
    public:
        RHISceneRenderer();
        ~RHISceneRenderer();

        RHISceneRenderer(const RHISceneRenderer&) = delete;
        RHISceneRenderer& operator=(const RHISceneRenderer&) = delete;

        // Initialize shader compilation, pipeline layouts, and graphics pipelines
        bool Init(RHI::RHIDevice& device = RHI::RHIDevice::Get(), bool enablePhysicsDebug = true);

        // Release GPU pipelines, shaders, and command buffers
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }

        // Render entire 3D scene into offscreen RHITexture color & depth targets
        void RenderScene(const std::shared_ptr<Scene>& scene,
                         const PerspectiveCamera& camera,
                         RHI::RHITexture* colorTarget,
                         RHI::RHITexture* depthTarget = nullptr,
                         const SceneRenderSettings& settings = SceneRenderSettings{},
                         PhysicsScene* physicsScene = nullptr,
                         std::shared_ptr<RHI::RHIViewTargets> viewTargets = {});

        // Render entire 3D scene into specific render targets (Headless tests / custom FBO)
        void RenderScene(const std::shared_ptr<Scene>& scene,
                         const PerspectiveCamera& camera,
                         nri::Texture* colorTarget,
                         nri::Descriptor* colorRTV,
                         nri::Texture* depthTarget,
                         nri::Descriptor* depthDSV,
                         uint32_t width, uint32_t height,
                         const SceneRenderSettings& settings = SceneRenderSettings{},
                         PhysicsScene* physicsScene = nullptr,
                         std::shared_ptr<RHI::RHIViewTargets> viewTargets = {});

        // Render an immutable RenderWorld snapshot into offscreen RHITexture targets
        void RenderWorldView(const RenderWorld& world,
                             const PerspectiveCamera& camera,
                             RHI::RHITexture* colorTarget,
                             RHI::RHITexture* depthTarget = nullptr,
                             const SceneRenderSettings& settings = SceneRenderSettings{},
                             PhysicsScene* physicsScene = nullptr,
                             std::shared_ptr<RHI::RHIViewTargets> viewTargets = {});

        // Render an immutable RenderWorld snapshot into specific native render targets
        void RenderWorldView(const RenderWorld& world,
                             const PerspectiveCamera& camera,
                             nri::Texture* colorTarget,
                             nri::Descriptor* colorRTV,
                             nri::Texture* depthTarget,
                             nri::Descriptor* depthDSV,
                             uint32_t width, uint32_t height,
                             const SceneRenderSettings& settings = SceneRenderSettings{},
                             PhysicsScene* physicsScene = nullptr,
                             std::shared_ptr<RHI::RHIViewTargets> viewTargets = {});

        // Performance & Telemetry metrics
        uint32_t GetDrawCallCount() const { return m_LastDrawCallCount; }
        uint32_t GetRenderedEntityCount() const { return m_LastRenderedEntityCount; }
        double GetLastGPUTimestampMilliseconds() const { return m_LastGPUTimestampMilliseconds; }
        uint64_t GetStreamedTransformBytes() const { return m_LastStreamedTransformBytes; }

        RHI::RHIPipelineLayout& GetPipelineLayout() { return m_PipelineLayout; }
        nri::Pipeline* GetPipeline() const { return m_Pipeline; }
        nri::Pipeline* GetHDRPipeline() const { return m_PipelineHDR; }
        const RHICascadedShadows& GetCascadedShadows() const { return m_CascadedShadows; }
        RHICascadedShadows& GetCascadedShadows() { return m_CascadedShadows; }
        bool WasLastRenderSuccessful() const { return m_LastRenderSucceeded; }
        RHI::RHICompletionToken GetLastCompletion() const { return m_LastCompletion; }
        const auto& GetMaterialDiagnostics() const { return m_MaterialDiagnostics; }
        // Apply reflected CPU schema/status on the scene owner after extraction
        // and rendering. Entity IDs are checked against the snapshot's UUIDs.
        void PublishMaterialFeedback(Scene& scene, const RenderWorld& world) const;
        RHI::RHISampler& GetSampler() { return m_AnisoSampler; }
        RHI::RHIDescriptorPool& GetDescriptorPool() { return m_DescriptorPool; }
        nri::DescriptorSet* GetDefaultDescriptorSet() const { return m_DefaultDescriptorSet; }
        RHI::RHIMultiLightBuffer& GetLightBuffer() { return m_LightBuffer; }
        const RHI::RHIMultiLightBuffer& GetLightBuffer() const { return m_LightBuffer; }

        // Render target format & HDR configuration
        void SetRenderTargetFormat(RHI::RHITextureFormat colorFormat, RHI::RHITextureFormat depthFormat = RHI::RHITextureFormat::D32_FLOAT);
        RHI::RHITextureFormat GetRenderTargetColorFormat() const;
        RHI::RHITextureFormat GetRenderTargetDepthFormat() const;
        bool IsHDR() const { return m_CurrentColorFormat == nri::Format::RGBA16_SFLOAT; }

        nri::Pipeline* GetOrCreatePipeline(nri::Format colorFormat, nri::Format depthFormat = nri::Format::D32_SFLOAT, uint8_t sampleCount = 1,
                                           Axle::SurfaceType surfaceType = Axle::SurfaceType::Opaque,
                                           Axle::CullMode cullMode = Axle::CullMode::None,
                                           bool depthWrite = true);
        nri::Pipeline* GetOrCreateSkyboxPipeline(nri::Format colorFormat, nri::Format depthFormat = nri::Format::D32_SFLOAT, uint8_t sampleCount = 1);

        // Image-Based Lighting (IBL) configuration
        void SetIBLMaps(const std::shared_ptr<RHI::RHITexture>& irradiance,
                        const std::shared_ptr<RHI::RHITexture>& prefilter,
                        const std::shared_ptr<RHI::RHITexture>& brdfLUT);
        void SetUseIBL(bool useIBL) { m_UseIBL = useIBL; m_IBLAllowed=useIBL; }
        bool IsIBLEnabled() const { return m_IBLAllowed && (m_UseIBL || bool(m_Environment.Ready())); }
        RHILocalShadows& GetLocalShadows() {return m_LocalShadows;}
        RHIEnvironment& GetEnvironment() { return m_Environment; }
        const RHIEnvironment& GetEnvironment() const { return m_Environment; }
        std::shared_ptr<RHI::RHITexture> GetIrradianceMap() const { return m_IrradianceMap; }
        std::shared_ptr<RHI::RHITexture> GetPrefilterMap() const { return m_PrefilterMap; }
        std::shared_ptr<RHI::RHITexture> GetBRDFLUT() const { return m_BRDFLUT; }

        // Procedural Atmospheric Skybox pass configuration
        void SetSkyboxParameters(const SkyboxComponent& skybox) { m_SkyboxParams = skybox; }
        const SkyboxComponent& GetSkyboxParameters() const { return m_SkyboxParams; }
        SkyboxComponent& GetSkyboxParameters() { return m_SkyboxParams; }
        nri::Pipeline* GetSkyboxPipeline() const { return m_SkyboxPipeline; }
        nri::Pipeline* GetHDRSkyboxPipeline() const { return m_SkyboxPipelineHDR; }
        RHI::RHIPipelineLayout& GetSkyboxPipelineLayout() { return m_SkyboxPipelineLayout; }
        RHI::RHIBuffer& GetSkyboxConstantBuffer() { return m_SkyboxConstantBuffer; }

        // Film-Grade Tone Mapping & Post-Processing Pass
        bool RenderToneMapping(RHI::RHICommandBuffer& cmd,
                               RHI::RHITexture* inputHDR,
                               RHI::RHITexture* outputTarget,
                               const RHIToneMappingConstants& constants);

        bool RenderToneMapping(RHI::RHICommandBuffer& cmd,
                               RHI::RHITexture* inputHDR,
                               RHI::RHITexture* outputTarget,
                               const SceneRenderSettings& settings = SceneRenderSettings{});

        bool RenderToneMapping(RHI::RHICommandBuffer& cmd,
                               nri::Descriptor* inputHDR_SRV,
                               nri::Texture* outputTarget,
                               nri::Descriptor* outputRTV,
                               uint32_t width, uint32_t height,
                               nri::Format outFormat,
                               const RHIToneMappingConstants& constants);

        nri::Pipeline* GetToneMappingPipeline() const { return m_ToneMappingPipelineSDR; }
        nri::Pipeline* GetHDRToneMappingPipeline() const { return m_ToneMappingPipelineHDR; }
        nri::Pipeline* GetOrCreateToneMappingPipeline(nri::Format colorFormat);
        RHI::RHIPipelineLayout& GetToneMappingPipelineLayout() { return m_ToneMappingPipelineLayout; }
        RHI::RHIBuffer& GetToneMappingConstantBuffer() { return m_ToneMappingConstantBuffer; }
        RHI::RHITexture* GetIntermediateHDRTarget() const { return GetActiveTarget(RHI::RHIViewTarget::HDR); }

        // Fast Approximate Anti-Aliasing (FXAA 3.11 Quality) Post-Processing Pass
        bool RenderFXAA(RHI::RHICommandBuffer& cmd,
                        RHI::RHITexture* inputTexture,
                        RHI::RHITexture* outputTarget,
                        const RHIFXAAConstants& constants);

        bool RenderFXAA(RHI::RHICommandBuffer& cmd,
                        RHI::RHITexture* inputTexture,
                        RHI::RHITexture* outputTarget,
                        const SceneRenderSettings& settings = SceneRenderSettings{});

        bool RenderFXAA(RHI::RHICommandBuffer& cmd,
                        nri::Descriptor* inputSRV,
                        nri::Texture* outputTarget,
                        nri::Descriptor* outputRTV,
                        uint32_t width, uint32_t height,
                        nri::Format outFormat,
                        const RHIFXAAConstants& constants,
                        nri::Descriptor* depthSRV = nullptr);

        nri::Pipeline* GetFXAAPipeline() const { return m_FXAAPipelineSDR; }
        nri::Pipeline* GetHDRFXAAPipeline() const { return m_FXAAPipelineHDR; }
        nri::Pipeline* GetOrCreateFXAAPipeline(nri::Format colorFormat);
        RHI::RHIPipelineLayout& GetFXAAPipelineLayout() { return m_FXAAPipelineLayout; }
        RHI::RHIBuffer& GetFXAAConstantBuffer() { return m_FXAAConstantBuffer; }

        // Hardware MSAA & Intermediate AA targets
        RHI::RHITexture* GetMSAAColorTarget() const { return GetActiveTarget(RHI::RHIViewTarget::MSAAColor); }
        RHI::RHITexture* GetMSAADepthTarget() const { return GetActiveTarget(RHI::RHIViewTarget::MSAADepth); }
        RHI::RHITexture* GetIntermediateFXAATarget() const { return GetActiveTarget(RHI::RHIViewTarget::FXAA); }
        RHI::RHITexture* GetOpaqueNormalTarget() const { return GetActiveTarget(RHI::RHIViewTarget::ScreenNormal); }
        RHI::RHITexture* GetScreenStorageTarget() const { return GetActiveTarget(RHI::RHIViewTarget::ScreenStorage); }
        RHI::RHITexture* GetHiZTarget(uint32_t level) const {
            auto targets = m_ActiveViewTargets.lock(); return targets ? targets->GetHiZ(level) : nullptr;
        }
        RHI::RHITexture* GetHistoryTarget(size_t slot) const { return GetActiveTarget(slot ? RHI::RHIViewTarget::History1 : RHI::RHIViewTarget::History0); }
        uint8_t GetCurrentMSAASamples() const { return m_CurrentMSAASamples; }

        // Physically Based Pyramid / Kawase Bloom Pass
        bool CompileBloomShaders(RHI::RHIDevice& device);
        bool CreateBloomPipelineLayout(RHI::RHIDevice& device);
        bool CreateBloomPipeline(RHI::RHIDevice& device);
        nri::Pipeline* GetOrCreateBloomApplyPipeline(nri::Format colorFormat);
        bool CompileAutoExposureShaders(RHI::RHIDevice& device);
        bool CreateAutoExposurePipelineLayout(RHI::RHIDevice& device);
        bool CreateAutoExposurePipeline(RHI::RHIDevice& device);
        bool CompileDDGIShaders(RHI::RHIDevice& device);
        bool CreateDDGIPipelineLayout(RHI::RHIDevice& device);
        bool CreateDDGIPipeline(RHI::RHIDevice& device);

        // Temporal Anti-Aliasing (TAA) Pass
        bool CompileTAAShaders(RHI::RHIDevice& device);
        bool CreateTAAPipelineLayout(RHI::RHIDevice& device);
        bool CreateTAAPipeline(RHI::RHIDevice& device);
        nri::Pipeline* GetOrCreateTAAPipeline(nri::Format colorFormat);

        // GPU Instanced Particle VFX Renderer
        RHIParticleRenderer* GetParticleRenderer() const { return m_ParticleRenderer.get(); }

        // RHI Physics Debug Wireframe Renderer
        RHIPhysicsDebugRenderer* GetPhysicsDebugRenderer() const { return m_PhysicsDebugRenderer.get(); }

        // GPU Integer Object ID Picker
        RHIObjectPicker* GetObjectPicker() const { return m_ObjectPicker.get(); }
        uint32_t PickEntity(const std::shared_ptr<Scene>& scene, const PerspectiveCamera& camera,
                            uint32_t width, uint32_t height, uint32_t x, uint32_t y);

        // Custom Material Program Cache
        CustomProgramCache& GetCustomProgramCache() { return m_CustomProgramCache; }
        const CustomProgramCache& GetCustomProgramCache() const { return m_CustomProgramCache; }

        struct CustomPipelineKey {
            uint64_t GenerationID = 0;
            nri::Format ColorFormat = nri::Format::RGBA8_UNORM;
            nri::Format DepthFormat = nri::Format::D32_SFLOAT;
            uint8_t SampleCount = 1;
            Axle::SurfaceType SurfaceType = Axle::SurfaceType::Opaque;
            Axle::CullMode CullMode = Axle::CullMode::None;
            bool DepthWrite = true;
            bool operator==(const CustomPipelineKey& other) const = default;
        };

        struct CustomPipelineKeyHash {
            size_t operator()(const CustomPipelineKey& k) const noexcept {
                size_t h = std::hash<uint64_t>{}(k.GenerationID);
                h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(k.ColorFormat)) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(k.DepthFormat)) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint8_t>{}(k.SampleCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(k.SurfaceType)) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(k.CullMode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<bool>{}(k.DepthWrite) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreateGraphicsPipeline(RHI::RHIDevice& device);
        nri::Pipeline* GetOrCreateSurfaceNormalPipeline(nri::Format colorFormat, nri::Format depthFormat);

        bool CompileSkyboxShaders(RHI::RHIDevice& device);
        bool CreateSkyboxPipelineLayout(RHI::RHIDevice& device);
        bool CreateSkyboxPipeline(RHI::RHIDevice& device);

        bool CompileToneMappingShaders(RHI::RHIDevice& device);
        bool CreateToneMappingPipelineLayout(RHI::RHIDevice& device);
        bool CreateToneMappingPipeline(RHI::RHIDevice& device);

        bool CompileFXAAShaders(RHI::RHIDevice& device);
        bool CreateFXAAPipelineLayout(RHI::RHIDevice& device);
        bool CreateFXAAPipeline(RHI::RHIDevice& device);

        bool CompileGTAOShaders(RHI::RHIDevice& device);
        bool CreateGTAOPipelineLayout(RHI::RHIDevice& device);
        bool CreateGTAOPipeline(RHI::RHIDevice& device);
        nri::Pipeline* GetOrCreateGTAOApplyPipeline(nri::Format colorFormat);

        bool CompileSSRShaders(RHI::RHIDevice& device);
        bool CreateSSRPipelineLayout(RHI::RHIDevice& device);
        bool CreateSSRPipeline(RHI::RHIDevice& device);
        nri::Pipeline* GetOrCreateSSRApplyPipeline(nri::Format colorFormat);

        bool CompileHiZShaders(RHI::RHIDevice& device);
        bool CreateHiZPipelineLayout(RHI::RHIDevice& device);
        bool CreateHiZPipeline(RHI::RHIDevice& device);

        bool CompileFogShaders(RHI::RHIDevice& device);
        bool CreateFogPipelineLayout(RHI::RHIDevice& device);
        bool CreateFogPipeline(RHI::RHIDevice& device);
        nri::Pipeline* GetOrCreateFogApplyPipeline(nri::Format colorFormat);

    private:
        void ConsumePreviousGPUTimestamp();
        bool PrepareReflectionProbe(const RenderWorld& world, const PerspectiveCamera& camera);
        RHI::RHIDevice* m_Device = nullptr;
        RHI::RHIPipelineLayout m_PipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_Pipeline;
        RHI::RHIResourceRef<nri::Pipeline> m_PipelineHDR;
        RHI::RHIResourceRef<nri::Pipeline> m_SurfaceNormalPipeline;
        RHI::RHIShaderBytecode m_SurfaceNormalPixelShader;

        nri::Format m_CurrentColorFormat = nri::Format::RGBA8_UNORM;
        nri::Format m_CurrentDepthFormat = nri::Format::D32_SFLOAT;

        RHI::RHISampler m_AnisoSampler;
        RHI::RHIDescriptorPool m_DescriptorPool;
        nri::DescriptorSet* m_DefaultDescriptorSet = nullptr;
        struct MaterialDrawResources {
            RHI::RHIBuffer Constants;
            nri::DescriptorSet* Set = nullptr;
            std::array<std::shared_ptr<RHI::RHITexture>, static_cast<size_t>(RHI::RHITextureSlot::Count)> Textures;
        };
        // Render() waits for queue completion before reusing these resources.
        std::vector<std::unique_ptr<MaterialDrawResources>> m_MaterialDraws;
        struct CustomDrawResources {
            std::vector<RHI::RHIBuffer> ConstantBuffers;
            std::vector<nri::DescriptorSet*> Sets;
            std::vector<std::shared_ptr<RHI::RHITexture>> Textures;
            std::shared_ptr<const CustomReadyGeneration> Generation;
        };
        std::vector<std::unique_ptr<CustomDrawResources>> m_CustomDraws;
        CustomProgramCache m_CustomProgramCache;

        RHI::RHIDescriptorPool m_CustomDescriptorPool;
        RHI::RHISampler m_CustomSampler;
        std::map<ShaderSamplerPolicy,RHI::RHISampler> m_CustomPolicySamplers;

        nri::Pipeline* GetOrCreateCustomPipeline(
            const CustomReadyGeneration& generation,
            nri::Format colorFormat,
            nri::Format depthFormat,
            uint8_t sampleCount,
            Axle::SurfaceType surfaceType,
            Axle::CullMode cullMode,
            bool depthWrite);

        MaterialTextureCache m_MaterialTextures;
        RHI::RHITexture* m_OpaqueSceneColor = nullptr;
        RHI::RHITexture* m_OpaqueSceneDepth = nullptr;
        RHI::RHITexture* m_ParticleSceneDepth = nullptr;
        RHI::RHITexture* m_OpaqueSceneNormal = nullptr;
        RHI::RHITexture* m_ScreenStorageTarget = nullptr;


        std::shared_ptr<RHI::RHITexture> m_IrradianceMap;
        std::shared_ptr<RHI::RHITexture> m_PrefilterMap;
        std::shared_ptr<RHI::RHITexture> m_BRDFLUT;
        bool m_UseIBL = false, m_IBLAllowed=true, m_PreparedManualIBL=false;
        RHIEnvironment m_Environment;
        std::array<RHI::RHIResourceHandle,3> m_BoundIBL{};
        std::shared_ptr<RHI::RHITexture> ActiveIrradiance() const;
        std::shared_ptr<RHI::RHITexture> ActivePrefilter() const;
        std::shared_ptr<RHI::RHITexture> ActiveBRDF() const;

        RHILocalShadows m_LocalShadows;
        RHI::RHIResourceHandle m_BoundLocalAtlas;
        RHICascadedShadows m_CascadedShadows;
        RHI::RHIResourceHandle m_BoundCascadeAtlas;
        RHI::RHIBuffer m_SceneConstantBuffer;
        RHI::RHIMultiLightBuffer m_LightBuffer;

        RHI::RHIShaderBytecode m_VertexShader;
        RHI::RHIShaderBytecode m_PixelShader;

        // Atmospheric Skybox Resources
        RHI::RHIPipelineLayout m_SkyboxPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_SkyboxPipeline;
        RHI::RHIResourceRef<nri::Pipeline> m_SkyboxPipelineHDR;
        RHI::RHIBuffer m_SkyboxConstantBuffer;
        nri::DescriptorSet* m_SkyboxDescriptorSet=nullptr;
        RHI::RHIShaderBytecode m_SkyboxVertexShader;
        RHI::RHIShaderBytecode m_SkyboxPixelShader;
        SkyboxComponent m_SkyboxParams;

        // Tone Mapping Post-Processing Resources
        RHI::RHIPipelineLayout m_ToneMappingPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_ToneMappingPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_ToneMappingPipelineHDR;
        RHI::RHIBuffer m_ToneMappingConstantBuffer;
        RHI::RHIShaderBytecode m_ToneMappingVertexShader;
        RHI::RHIShaderBytecode m_ToneMappingPixelShader;
        RHI::RHISampler m_ToneMappingSampler;
        RHI::RHIDescriptorPool m_ToneMappingDescriptorPool;
        nri::DescriptorSet* m_ToneMappingDescriptorSet = nullptr;

        // Fast Approximate Anti-Aliasing (FXAA) Resources
        RHI::RHIPipelineLayout m_FXAAPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_FXAAPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_FXAAPipelineHDR;
        RHI::RHIBuffer m_FXAAConstantBuffer;
        RHI::RHIShaderBytecode m_FXAAVertexShader;
        RHI::RHIShaderBytecode m_FXAAPixelShader;
        RHI::RHISampler m_FXAASampler;
        RHI::RHIDescriptorPool m_FXAADescriptorPool;
        nri::DescriptorSet* m_FXAADescriptorSet = nullptr;

        // Ground Truth Ambient Occlusion (GTAO) & Contact Shadows Resources
        RHI::RHIPipelineLayout m_GTAOComputePipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_GTAOComputePipeline;
        RHI::RHIPipelineLayout m_GTAOApplyPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_GTAOApplyPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_GTAOApplyPipelineHDR;
        RHI::RHIBuffer m_GTAOConstantBuffer;
        RHI::RHIShaderBytecode m_GTAOComputeShader;
        RHI::RHIShaderBytecode m_GTAOApplyVertexShader;
        RHI::RHIShaderBytecode m_GTAOApplyPixelShader;
        RHI::RHISampler m_GTAOSampler;
        RHI::RHIDescriptorPool m_GTAODescriptorPool;
        nri::DescriptorSet* m_GTAOComputeTextureSet = nullptr;
        nri::DescriptorSet* m_GTAOComputeStorageSet = nullptr;
        nri::DescriptorSet* m_GTAOApplyDescriptorSet = nullptr;

        // Screen-Space Reflections (SSR) & Reflection Probes Resources
        RHI::RHIPipelineLayout m_SSRComputePipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_SSRComputePipeline;
        RHI::RHIPipelineLayout m_SSRApplyPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_SSRApplyPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_SSRApplyPipelineHDR;
        RHI::RHIBuffer m_SSRConstantBuffer;
        RHI::RHIShaderBytecode m_SSRComputeShader;
        RHI::RHIShaderBytecode m_SSRApplyVertexShader;
        RHI::RHIShaderBytecode m_SSRApplyPixelShader;
        RHI::RHISampler m_SSRSampler;
        RHI::RHIDescriptorPool m_SSRDescriptorPool;
        nri::DescriptorSet* m_SSRComputeTextureSet = nullptr;
        nri::DescriptorSet* m_SSRComputeStorageSet = nullptr;
        nri::DescriptorSet* m_SSRApplyDescriptorSet = nullptr;
        std::unordered_map<std::string, std::unique_ptr<RHIEnvironment>> m_ReflectionProbeEnvironments;
        std::shared_ptr<RHI::RHITexture> m_ActiveReflectionProbePrefilter;
        RenderReflectionProbe m_ActiveReflectionProbe{};
        bool m_HasActiveReflectionProbe = false;

        // Hi-Z depth pyramid resources. The view target owns eight reduction
        // levels; this renderer owns the reduction compute pipeline and sets.
        RHI::RHIPipelineLayout m_HiZPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_HiZPipeline;
        RHI::RHIBuffer m_HiZConstantBuffer;
        RHI::RHIShaderBytecode m_HiZComputeShader;
        RHI::RHISampler m_HiZSampler;
        RHI::RHIDescriptorPool m_HiZDescriptorPool;
        std::array<nri::DescriptorSet*, 8> m_HiZTextureSets{};
        std::array<nri::DescriptorSet*, 8> m_HiZStorageSets{};

        // Volumetric Fog & Atmospheric Media Resources
        RHI::RHIPipelineLayout m_FogComputePipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_FogComputePipeline;
        RHI::RHIPipelineLayout m_FogApplyPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_FogApplyPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_FogApplyPipelineHDR;
        RHI::RHIBuffer m_FogConstantBuffer;
        RHI::RHIShaderBytecode m_FogComputeShader;
        RHI::RHIShaderBytecode m_FogApplyVertexShader;
        RHI::RHIShaderBytecode m_FogApplyPixelShader;
        RHI::RHISampler m_FogSampler;
        RHI::RHIDescriptorPool m_FogDescriptorPool;
        nri::DescriptorSet* m_FogComputeTextureSet = nullptr;
        nri::DescriptorSet* m_FogComputeStorageSet = nullptr;
        nri::DescriptorSet* m_FogApplyDescriptorSet = nullptr;

        // Physically Based Bloom Resources
        RHI::RHIPipelineLayout m_BloomComputePipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_BloomComputePipeline;
        RHI::RHIPipelineLayout m_BloomApplyPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_BloomApplyPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_BloomApplyPipelineHDR;
        RHI::RHIBuffer m_BloomConstantBuffer;
        RHI::RHIShaderBytecode m_BloomComputeShader;
        RHI::RHIShaderBytecode m_BloomApplyVertexShader;
        RHI::RHIShaderBytecode m_BloomApplyPixelShader;
        RHI::RHISampler m_BloomSampler;
        RHI::RHIDescriptorPool m_BloomDescriptorPool;
        nri::DescriptorSet* m_BloomComputeTextureSet = nullptr;
        nri::DescriptorSet* m_BloomComputeStorageSet = nullptr;
        std::array<nri::DescriptorSet*, 11> m_BloomComputeTextureSets{};
        std::array<nri::DescriptorSet*, 11> m_BloomComputeStorageSets{};
        nri::DescriptorSet* m_BloomApplyDescriptorSet = nullptr;

        // Histogram-based auto exposure resources
        RHI::RHIPipelineLayout m_AutoExposurePipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_AutoExposurePipeline;
        RHI::RHIBuffer m_AutoExposureConstantBuffer;
        RHI::RHIShaderBytecode m_AutoExposureComputeShader;
        RHI::RHISampler m_AutoExposureSampler;
        RHI::RHIDescriptorPool m_AutoExposureDescriptorPool;
        nri::DescriptorSet* m_AutoExposureTextureSet = nullptr;
        nri::DescriptorSet* m_AutoExposureStorageSet = nullptr;

        // Bounded DDGI irradiance atlas update resources
        RHI::RHIPipelineLayout m_DDGIPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_DDGIPipeline;
        RHI::RHIBuffer m_DDGIConstantBuffer;
        RHI::RHIShaderBytecode m_DDGIComputeShader;
        RHI::RHISampler m_DDGISampler;
        RHI::RHIDescriptorPool m_DDGIDescriptorPool;
        nri::DescriptorSet* m_DDGITextureSet = nullptr;
        nri::DescriptorSet* m_DDGIStorageSet = nullptr;

        // Temporal Anti-Aliasing (TAA) Resources
        RHI::RHIPipelineLayout m_TAAPipelineLayout;
        RHI::RHIResourceRef<nri::Pipeline> m_TAAPipelineSDR;
        RHI::RHIResourceRef<nri::Pipeline> m_TAAPipelineHDR;
        RHI::RHIBuffer m_TAAConstantBuffer;
        RHI::RHIShaderBytecode m_TAAVertexShader;
        RHI::RHIShaderBytecode m_TAAPixelShader;
        RHI::RHISampler m_TAALinearSampler;
        RHI::RHISampler m_TAAPointSampler;
        RHI::RHIDescriptorPool m_TAADescriptorPool;
        nri::DescriptorSet* m_TAADescriptorSet = nullptr;
        glm::mat4 m_PrevViewProjection{ 1.0f };
        uint8_t m_TAAHistoryIndex = 0;
        bool m_TAAHistoryValid = false;
        float m_AdaptedExposure = 1.0f;
        nri::QueryPool* m_GPUTimestampPool = nullptr;
        RHI::RHIBuffer m_GPUTimestampReadback;
        double m_LastGPUTimestampMilliseconds = 0.0;

        // Intermediate Floating-point HDR Target for 2-pass Scene -> ToneMap execution
        RHI::RHITexture* m_IntermediateHDRTarget = nullptr;
        std::weak_ptr<RHI::RHIViewTargets> m_ActiveViewTargets;
        std::shared_ptr<RHI::RHIViewTargets> m_ImplicitViewTargets;
        RHI::RHIResourceHandle m_ImplicitViewColor;
        RHI::RHITexture* GetActiveTarget(RHI::RHIViewTarget slot) const {
            auto targets = m_ActiveViewTargets.lock(); return targets ? targets->Get(slot) : nullptr;
        }

        // Intermediate FXAA Target and Hardware MSAA Targets
        RHI::RHITexture* m_IntermediateFXAATarget = nullptr;
        RHI::RHITexture* m_MSAAColorTarget = nullptr;
        RHI::RHITexture* m_MSAADepthTarget = nullptr;
        uint8_t m_CurrentMSAASamples = 1;

        std::unique_ptr<RHIParticleRenderer> m_ParticleRenderer;
        std::unique_ptr<RHIPhysicsDebugRenderer> m_PhysicsDebugRenderer;
        std::unique_ptr<RHIObjectPicker> m_ObjectPicker;

        ViewContext m_ViewContext;
        RHI::RHICommandBuffer& Commands() { return *m_ViewContext.Commands(); }
        bool m_LastRenderSucceeded = false;
        RHI::RHICompletionToken m_LastCompletion;
        std::unordered_map<uint32_t, std::string> m_MaterialDiagnostics;
        struct MaterialFeedback {
            std::vector<ShaderPropertyInstance> Properties;
            uint64_t Generation = 0;
        };
        std::unordered_map<uint32_t, MaterialFeedback> m_MaterialFeedback;

        uint32_t m_LastDrawCallCount = 0;
        uint32_t m_LastRenderedEntityCount = 0;
        uint64_t m_LastStreamedTransformBytes = 0;

        bool m_Initialized = false;
    };

}
