#pragma once

#include <Axle/Types.hpp>
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace Axle {

    // =========================================================================
    // Vertex Format for Physics Line Wireframe Rendering (28 Bytes)
    // =========================================================================
    struct RHIDebugLineVertex {
        glm::vec3 Position = glm::vec3(0.0f); // 12 bytes
        glm::vec4 Color    = glm::vec4(1.0f); // 16 bytes -> Total 28 bytes
    };
    static_assert(sizeof(RHIDebugLineVertex) == 28, "RHIDebugLineVertex must be exactly 28 bytes!");

    // =========================================================================
    // Constant Buffer for Physics Debug Rendering (256 Bytes Hardware Aligned)
    // =========================================================================
    struct alignas(256) RHIPhysicsDebugConstants {
        glm::mat4 ViewProjection  = glm::mat4(1.0f); // 64 bytes (offset 0)
        glm::vec4 DepthBiasParams = glm::vec4(0.0001f, 1.0f, 0.0f, 0.0f); // 16 bytes (offset 64)
        glm::vec4 Reserved[11]{};                    // 176 bytes padding -> total 256 bytes (offset 80..256)
    };
    static_assert(sizeof(RHIPhysicsDebugConstants) == 256, "RHIPhysicsDebugConstants must be exactly 256 bytes (256-byte aligned)!");

    namespace Internal {
        struct JoltInitHelper {
            JoltInitHelper();
        };
    }

    // =========================================================================
    // RHIPhysicsDebugRenderer: Modern Cross-API Jolt Debug Wireframe Renderer
    // =========================================================================
    class RHIPhysicsDebugRenderer : private Internal::JoltInitHelper, public JPH::DebugRendererSimple {
    public:
        static constexpr uint32_t MAX_VERTICES_PER_BATCH = 100000; // 50,000 line segments

        RHIPhysicsDebugRenderer();
        ~RHIPhysicsDebugRenderer() override;

        RHIPhysicsDebugRenderer(const RHIPhysicsDebugRenderer&) = delete;
        RHIPhysicsDebugRenderer& operator=(const RHIPhysicsDebugRenderer&) = delete;

        // Lifecycle
        bool Init(RHI::RHIDevice& device = RHI::RHIDevice::Get());
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }
        bool IsValid() const { return m_Initialized; }

        // Frame Batching Lifecycle
        void Begin(const glm::vec3& cameraPos,
                   const glm::mat4& viewProj,
                   nri::Format colorFormat = nri::Format::RGBA8_UNORM,
                   nri::Format depthFormat = nri::Format::D32_SFLOAT,
                   uint8_t sampleCount = 1,
                   bool depthTest = true,
                   float depthBias = 0.0001f,
                   float alphaMultiplier = 1.0f);

        void Flush(RHI::RHICommandBuffer& cmd);
        void End(RHI::RHICommandBuffer& cmd);

        void Clear();

        // JPH::DebugRendererSimple Virtual Overrides
        void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
        void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) override;

        // Geometry Primitive Helpers
        void DrawLineSegment(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
        void DrawBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
        void DrawOrientedBox(const glm::mat4& transform, const glm::vec3& halfExtent, const glm::vec4& color);
        void DrawTransform(const glm::mat4& transform, float scale = 1.0f);
        void DrawCircle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec4& color, int segments = 24);
        void DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);
        void DrawRay(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);

        // Pipeline Caching & Retrieval
        nri::Pipeline* GetOrCreatePipeline(nri::Format colorFormat,
                                           nri::Format depthFormat,
                                           uint8_t sampleCount,
                                           bool depthTest);

        // Accessors & Telemetry
        uint32_t GetLineCount() const { return static_cast<uint32_t>(m_LineVertices.size() / 2); }
        uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_LineVertices.size()); }
        uint32_t GetDrawCallCount() const { return m_DrawCallCount; }
        uint32_t GetFlushedVertexCount() const { return m_FlushedVertexCount; }

        RHI::RHIPipelineLayout& GetPipelineLayout() { return m_PipelineLayout; }
        RHI::RHIBuffer& GetConstantBuffer() { return m_ConstantBuffer; }
        RHI::RHIBuffer& GetVertexBuffer() { return m_VertexBuffer; }

    private:
        bool CompileShaders(RHI::RHIDevice& device);
        bool CreatePipelineLayout(RHI::RHIDevice& device);
        bool CreateBuffers(RHI::RHIDevice& device);

    private:
        RHI::RHIDevice* m_Device = nullptr;
        bool m_Initialized = false;
        bool m_InBatch = false;

        RHI::RHIShaderBytecode m_VertexShader;
        RHI::RHIShaderBytecode m_PixelShader;
        RHI::RHIPipelineLayout m_PipelineLayout;

        // Default PSOs (DepthTest On & Off for SDR & HDR)
        nri::Pipeline* m_PipelineSDR_Depth = nullptr;
        nri::Pipeline* m_PipelineSDR_NoDepth = nullptr;
        nri::Pipeline* m_PipelineHDR_Depth = nullptr;
        nri::Pipeline* m_PipelineHDR_NoDepth = nullptr;

        // GPU Buffers
        RHI::RHIBuffer m_ConstantBuffer;
        RHI::RHIBuffer m_VertexBuffer;

        // Active Batch State
        RHIPhysicsDebugConstants m_ActiveConstants{};
        nri::Format m_ActiveColorFormat = nri::Format::RGBA8_UNORM;
        nri::Format m_ActiveDepthFormat = nri::Format::D32_SFLOAT;
        uint8_t m_ActiveSampleCount = 1;
        bool m_ActiveDepthTest = true;

        std::vector<RHIDebugLineVertex> m_LineVertices;
        uint32_t m_DrawCallCount = 0;
        uint32_t m_FlushedVertexCount = 0;
    };

}
