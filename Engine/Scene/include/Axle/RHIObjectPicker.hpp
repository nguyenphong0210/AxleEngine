#pragma once
#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/Components.hpp>

#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <limits>

namespace Axle {

    class Scene;

    // 128-byte Root Constants for GPU Object ID picking
    struct alignas(16) RHIObjectIDPushConstants {
        glm::mat4 ModelViewProjection{ 1.0f }; // 64 bytes
        uint32_t EntityID = 0xFFFFFFFF;       // 4 bytes
        uint32_t Padding[3]{};                // 12 bytes
        glm::vec4 Reserved[3]{};              // 48 bytes -> Total 128 bytes

        static constexpr uint32_t GetByteSize() { return sizeof(RHIObjectIDPushConstants); }
    };
    static_assert(sizeof(RHIObjectIDPushConstants) == 128, "RHIObjectIDPushConstants must be 128 bytes!");

    class RHIObjectPicker {
    public:
        static constexpr uint32_t InvalidEntityID = std::numeric_limits<uint32_t>::max();

        RHIObjectPicker();
        ~RHIObjectPicker();

        RHIObjectPicker(const RHIObjectPicker&) = delete;
        RHIObjectPicker& operator=(const RHIObjectPicker&) = delete;

        // Initialize shader compilation, pipeline layout, and graphics pipeline
        bool Init(RHI::RHIDevice& device = RHI::RHIDevice::Get());

        // Release GPU resources
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }

        // Resize picking targets
        void Resize(uint32_t width, uint32_t height);

        // Render entire scene entities into R32_UINT picking texture
        void RenderPickingPass(const std::shared_ptr<Scene>& scene,
                               const PerspectiveCamera& camera,
                               uint32_t width, uint32_t height);

        // Read back the entity ID at pixel coordinate (x, y)
        // Returns InvalidEntityID (0xFFFFFFFF) if background or out of bounds
        uint32_t ReadPixel(uint32_t x, uint32_t y);

        // Render picking pass and immediately query pixel at (x, y)
        uint32_t Pick(const std::shared_ptr<Scene>& scene,
                      const PerspectiveCamera& camera,
                      uint32_t width, uint32_t height,
                      uint32_t x, uint32_t y);

        RHI::RHITexture* GetPickingTexture() const { return m_PickingTexture.get(); }
        RHI::RHITexture* GetDepthTexture() const { return m_DepthTexture.get(); }
        nri::Pipeline* GetPipeline() const { return m_Pipeline; }
        const RHI::RHIPipelineLayout& GetPipelineLayout() const { return m_PipelineLayout; }
        uint32_t GetDrawCallCount() const { return m_LastDrawCallCount; }
        uint32_t GetRenderedEntityCount() const { return m_LastRenderedEntityCount; }

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreateGraphicsPipeline(RHI::RHIDevice& device);

    private:
        RHI::RHIDevice* m_Device = nullptr;
        bool m_Initialized = false;
        bool m_HasRendered = false;

        std::unique_ptr<RHI::RHITexture> m_PickingTexture;
        std::unique_ptr<RHI::RHITexture> m_DepthTexture;

        RHI::RHIPipelineLayout m_PipelineLayout;
        nri::Pipeline* m_Pipeline = nullptr;

        RHI::RHIShaderBytecode m_VertexShader;
        RHI::RHIShaderBytecode m_PixelShader;

        RHI::RHICommandBuffer m_CommandBuffer;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_LastDrawCallCount = 0;
        uint32_t m_LastRenderedEntityCount = 0;
    };

}
