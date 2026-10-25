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
        RHI::RHIStreamer* GetStreamer() const { return m_Streamer.get(); }
        RHI::RHISampler& GetSampler() { return m_AnisoSampler; }
        RHI::RHIDescriptorPool& GetDescriptorPool() { return m_DescriptorPool; }
        nri::DescriptorSet* GetDefaultDescriptorSet() const { return m_DefaultDescriptorSet; }
        RHI::RHIMultiLightBuffer& GetLightBuffer() { return m_LightBuffer; }
        const RHI::RHIMultiLightBuffer& GetLightBuffer() const { return m_LightBuffer; }

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
        RHI::RHIPipelineLayout& GetSkyboxPipelineLayout() { return m_SkyboxPipelineLayout; }
        RHI::RHIBuffer& GetSkyboxConstantBuffer() { return m_SkyboxConstantBuffer; }

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreateGraphicsPipeline(RHI::RHIDevice& device);

        bool CompileSkyboxShaders(RHI::RHIDevice& device);
        bool CreateSkyboxPipelineLayout(RHI::RHIDevice& device);
        bool CreateSkyboxPipeline(RHI::RHIDevice& device);

    private:
        RHI::RHIDevice* m_Device = nullptr;
        RHI::RHIPipelineLayout m_PipelineLayout;
        nri::Pipeline* m_Pipeline = nullptr;

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
        RHI::RHIBuffer m_SkyboxConstantBuffer;
        RHI::RHIShaderBytecode m_SkyboxVertexShader;
        RHI::RHIShaderBytecode m_SkyboxPixelShader;
        SkyboxComponent m_SkyboxParams;

        RHI::RHICommandBuffer m_CommandBuffer;
        std::unique_ptr<RHI::RHIStreamer> m_Streamer;

        uint32_t m_LastDrawCallCount = 0;
        uint32_t m_LastRenderedEntityCount = 0;
        uint64_t m_LastStreamedTransformBytes = 0;

        bool m_Initialized = false;
    };

}
