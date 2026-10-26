#pragma once
#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/SceneRenderer.hpp>

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

#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace Axle {

    class Scene;
    class RHIParticleRenderer;

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

    // 256-byte CBV aligned constants for FXAA 3.11 post-processing pass
    struct alignas(256) RHIFXAAConstants {
        glm::vec4 ScreenSize{ 1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f }; // (width, height, 1/w, 1/h)
        glm::vec4 FXAAParams{ 0.75f, 0.166f, 0.0833f, 0.0f };                     // (subpixQuality, edgeThreshold, edgeThresholdMin, reserved)
        glm::vec4 Reserved[14]{};                                                 // 224 bytes padding -> Total: 256 bytes
    };
    static_assert(sizeof(RHIFXAAConstants) == 256, "RHIFXAAConstants must be 256 bytes aligned!");

    class RHISceneRenderer {
    public:
        RHISceneRenderer();
        ~RHISceneRenderer();

        RHISceneRenderer(const RHISceneRenderer&) = delete;
        RHISceneRenderer& operator=(const RHISceneRenderer&) = delete;

        // Initialize shader compilation, pipeline layouts, and graphics pipelines
        bool Init(RHI::RHIDevice& device = RHI::RHIDevice::Get());

        // Release GPU pipelines, shaders, and command buffers
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }

        // Render entire 3D scene into offscreen RHITexture color & depth targets
        void RenderScene(const std::shared_ptr<Scene>& scene,
                         const PerspectiveCamera& camera,
                         RHI::RHITexture* colorTarget,
                         RHI::RHITexture* depthTarget = nullptr,
                         const SceneRenderSettings& settings = SceneRenderSettings{});

        // Render entire 3D scene into specific render targets (Headless tests / custom FBO)
        void RenderScene(const std::shared_ptr<Scene>& scene,
                         const PerspectiveCamera& camera,
                         nri::Texture* colorTarget,
                         nri::Descriptor* colorRTV,
                         nri::Texture* depthTarget,
                         nri::Descriptor* depthDSV,
                         uint32_t width, uint32_t height,
                         const SceneRenderSettings& settings = SceneRenderSettings{});

        // Performance & Telemetry metrics
        uint32_t GetDrawCallCount() const { return m_LastDrawCallCount; }
        uint32_t GetRenderedEntityCount() const { return m_LastRenderedEntityCount; }
        uint64_t GetStreamedTransformBytes() const { return m_LastStreamedTransformBytes; }

        RHI::RHIPipelineLayout& GetPipelineLayout() { return m_PipelineLayout; }
        nri::Pipeline* GetPipeline() const { return m_Pipeline; }
        nri::Pipeline* GetHDRPipeline() const { return m_PipelineHDR; }
        RHI::RHIStreamer* GetStreamer() const { return m_Streamer.get(); }
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

        nri::Pipeline* GetOrCreatePipeline(nri::Format colorFormat, nri::Format depthFormat = nri::Format::D32_SFLOAT, uint8_t sampleCount = 1);
        nri::Pipeline* GetOrCreateSkyboxPipeline(nri::Format colorFormat, nri::Format depthFormat = nri::Format::D32_SFLOAT, uint8_t sampleCount = 1);

        // Image-Based Lighting (IBL) configuration
        void SetIBLMaps(const std::shared_ptr<RHI::RHITexture>& irradiance,
                        const std::shared_ptr<RHI::RHITexture>& prefilter,
                        const std::shared_ptr<RHI::RHITexture>& brdfLUT);
        void SetUseIBL(bool useIBL) { m_UseIBL = useIBL; }
        bool IsIBLEnabled() const { return m_UseIBL; }
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
        void RenderToneMapping(RHI::RHICommandBuffer& cmd,
                               RHI::RHITexture* inputHDR,
                               RHI::RHITexture* outputTarget,
                               const RHIToneMappingConstants& constants);

        void RenderToneMapping(RHI::RHICommandBuffer& cmd,
                               RHI::RHITexture* inputHDR,
                               RHI::RHITexture* outputTarget,
                               const SceneRenderSettings& settings = SceneRenderSettings{});

        void RenderToneMapping(RHI::RHICommandBuffer& cmd,
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
        RHI::RHITexture* GetIntermediateHDRTarget() const { return m_IntermediateHDRTarget.get(); }

        // Fast Approximate Anti-Aliasing (FXAA 3.11 Quality) Post-Processing Pass
        void RenderFXAA(RHI::RHICommandBuffer& cmd,
                        RHI::RHITexture* inputTexture,
                        RHI::RHITexture* outputTarget,
                        const RHIFXAAConstants& constants);

        void RenderFXAA(RHI::RHICommandBuffer& cmd,
                        RHI::RHITexture* inputTexture,
                        RHI::RHITexture* outputTarget,
                        const SceneRenderSettings& settings = SceneRenderSettings{});

        void RenderFXAA(RHI::RHICommandBuffer& cmd,
                        nri::Descriptor* inputSRV,
                        nri::Texture* outputTarget,
                        nri::Descriptor* outputRTV,
                        uint32_t width, uint32_t height,
                        nri::Format outFormat,
                        const RHIFXAAConstants& constants);

        nri::Pipeline* GetFXAAPipeline() const { return m_FXAAPipelineSDR; }
        nri::Pipeline* GetHDRFXAAPipeline() const { return m_FXAAPipelineHDR; }
        nri::Pipeline* GetOrCreateFXAAPipeline(nri::Format colorFormat);
        RHI::RHIPipelineLayout& GetFXAAPipelineLayout() { return m_FXAAPipelineLayout; }
        RHI::RHIBuffer& GetFXAAConstantBuffer() { return m_FXAAConstantBuffer; }

        // Hardware MSAA & Intermediate AA targets
        RHI::RHITexture* GetMSAAColorTarget() const { return m_MSAAColorTarget.get(); }
        RHI::RHITexture* GetMSAADepthTarget() const { return m_MSAADepthTarget.get(); }
        RHI::RHITexture* GetIntermediateFXAATarget() const { return m_IntermediateFXAATarget.get(); }
        uint8_t GetCurrentMSAASamples() const { return m_CurrentMSAASamples; }

        // GPU Instanced Particle VFX Renderer
        RHIParticleRenderer* GetParticleRenderer() const { return m_ParticleRenderer.get(); }

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreateGraphicsPipeline(RHI::RHIDevice& device);

        bool CompileSkyboxShaders(RHI::RHIDevice& device);
        bool CreateSkyboxPipelineLayout(RHI::RHIDevice& device);
        bool CreateSkyboxPipeline(RHI::RHIDevice& device);

        bool CompileToneMappingShaders(RHI::RHIDevice& device);
        bool CreateToneMappingPipelineLayout(RHI::RHIDevice& device);
        bool CreateToneMappingPipeline(RHI::RHIDevice& device);

        bool CompileFXAAShaders(RHI::RHIDevice& device);
        bool CreateFXAAPipelineLayout(RHI::RHIDevice& device);
        bool CreateFXAAPipeline(RHI::RHIDevice& device);

    private:
        RHI::RHIDevice* m_Device = nullptr;
        RHI::RHIPipelineLayout m_PipelineLayout;
        nri::Pipeline* m_Pipeline = nullptr;
        nri::Pipeline* m_PipelineHDR = nullptr;

        nri::Format m_CurrentColorFormat = nri::Format::RGBA8_UNORM;
        nri::Format m_CurrentDepthFormat = nri::Format::D32_SFLOAT;

        RHI::RHISampler m_AnisoSampler;
        RHI::RHIDescriptorPool m_DescriptorPool;
        nri::DescriptorSet* m_DefaultDescriptorSet = nullptr;

        std::shared_ptr<RHI::RHITexture> m_IrradianceMap;
        std::shared_ptr<RHI::RHITexture> m_PrefilterMap;
        std::shared_ptr<RHI::RHITexture> m_BRDFLUT;
        bool m_UseIBL = false;

        RHI::RHIBuffer m_SceneConstantBuffer;
        RHI::RHIMultiLightBuffer m_LightBuffer;

        RHI::RHIShaderBytecode m_VertexShader;
        RHI::RHIShaderBytecode m_PixelShader;

        // Atmospheric Skybox Resources
        RHI::RHIPipelineLayout m_SkyboxPipelineLayout;
        nri::Pipeline* m_SkyboxPipeline = nullptr;
        nri::Pipeline* m_SkyboxPipelineHDR = nullptr;
        RHI::RHIBuffer m_SkyboxConstantBuffer;
        RHI::RHIShaderBytecode m_SkyboxVertexShader;
        RHI::RHIShaderBytecode m_SkyboxPixelShader;
        SkyboxComponent m_SkyboxParams;

        // Tone Mapping Post-Processing Resources
        RHI::RHIPipelineLayout m_ToneMappingPipelineLayout;
        nri::Pipeline* m_ToneMappingPipelineSDR = nullptr;
        nri::Pipeline* m_ToneMappingPipelineHDR = nullptr;
        RHI::RHIBuffer m_ToneMappingConstantBuffer;
        RHI::RHIShaderBytecode m_ToneMappingVertexShader;
        RHI::RHIShaderBytecode m_ToneMappingPixelShader;
        RHI::RHISampler m_ToneMappingSampler;
        RHI::RHIDescriptorPool m_ToneMappingDescriptorPool;
        nri::DescriptorSet* m_ToneMappingDescriptorSet = nullptr;

        // Fast Approximate Anti-Aliasing (FXAA) Resources
        RHI::RHIPipelineLayout m_FXAAPipelineLayout;
        nri::Pipeline* m_FXAAPipelineSDR = nullptr;
        nri::Pipeline* m_FXAAPipelineHDR = nullptr;
        RHI::RHIBuffer m_FXAAConstantBuffer;
        RHI::RHIShaderBytecode m_FXAAVertexShader;
        RHI::RHIShaderBytecode m_FXAAPixelShader;
        RHI::RHISampler m_FXAASampler;
        RHI::RHIDescriptorPool m_FXAADescriptorPool;
        nri::DescriptorSet* m_FXAADescriptorSet = nullptr;

        // Intermediate Floating-point HDR Target for 2-pass Scene -> ToneMap execution
        std::unique_ptr<RHI::RHITexture> m_IntermediateHDRTarget;
        std::unique_ptr<RHI::RHITexture> m_IntermediateHDRDepthTarget;

        // Intermediate FXAA Target and Hardware MSAA Targets
        std::unique_ptr<RHI::RHITexture> m_IntermediateFXAATarget;
        std::unique_ptr<RHI::RHITexture> m_MSAAColorTarget;
        std::unique_ptr<RHI::RHITexture> m_MSAADepthTarget;
        uint8_t m_CurrentMSAASamples = 1;

        std::unique_ptr<RHIParticleRenderer> m_ParticleRenderer;

        RHI::RHICommandBuffer m_CommandBuffer;
        std::unique_ptr<RHI::RHIStreamer> m_Streamer;

        uint32_t m_LastDrawCallCount = 0;
        uint32_t m_LastRenderedEntityCount = 0;
        uint64_t m_LastStreamedTransformBytes = 0;

        bool m_Initialized = false;
    };

}
