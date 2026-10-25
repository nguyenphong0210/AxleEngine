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

#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace Axle {

    class Scene;

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

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreateGraphicsPipeline(RHI::RHIDevice& device);

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

        RHI::RHICommandBuffer m_CommandBuffer;
        std::unique_ptr<RHI::RHIStreamer> m_Streamer;

        uint32_t m_LastDrawCallCount = 0;
        uint32_t m_LastRenderedEntityCount = 0;
        uint64_t m_LastStreamedTransformBytes = 0;

        bool m_Initialized = false;
    };

}
