#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIDevice.hpp>

#include <NRI.h>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#undef min
#undef max
#endif

namespace Axle::RHI {

    class RHIBuffer;
    struct RHIStreamedBuffer;

    class RHICommandBuffer {
    public:
        RHICommandBuffer() = default;
        ~RHICommandBuffer();

        RHICommandBuffer(const RHICommandBuffer&) = delete;
        RHICommandBuffer& operator=(const RHICommandBuffer&) = delete;

        RHICommandBuffer(RHICommandBuffer&& other) noexcept;
        RHICommandBuffer& operator=(RHICommandBuffer&& other) noexcept;

        // Initialize with an existing command allocator (e.g. from RHISwapChain or external frame pool)
        bool Init(nri::CommandAllocator* allocator);

        // Initialize and allocate an internal dedicated command allocator for this command buffer
        bool Init(nri::Queue* queue = nullptr);

        // Shutdown and release NRI resources
        void Shutdown();

        // Begin command recording (single-use command list)
        bool Begin(nri::DescriptorPool* descriptorPool = nullptr);

        // End command recording
        bool End();

        // Synchronization & Barriers
        void PipelineBarrier(const RHIBarrierGroup& barrierGroup);
        void TransitionTexture(nri::Texture* texture, RHIResourceLayout before, RHIResourceLayout after);
        bool TransitionTexture(RHIResourceStateTracker& tracker, nri::Texture* texture, RHIResourceLayout targetLayout);
        void TransitionBuffer(nri::Buffer* buffer, RHIAccess beforeAccess, RHIAccess afterAccess,
                              RHIPipelineStage beforeStage = RHIPipelineStage::AllCommands,
                              RHIPipelineStage afterStage = RHIPipelineStage::AllCommands);

        // Staging & Dynamic Streamer
        void CopyStreamedData(class RHIStreamer& streamer, uint64_t copyBatch);

        // Pipeline & Resource Binding
        void SetPipeline(nri::Pipeline* pipeline);
        void SetPipelineLayout(nri::BindPoint bindPoint, const RHIPipelineLayout& layout);
        void SetDescriptorPool(nri::DescriptorPool* descriptorPool);
        void SetDescriptorSet(nri::BindPoint bindPoint, uint32_t setIndex, nri::DescriptorSet* descriptorSet);
        void SetRootConstants(uint32_t rootConstantIndex, const void* data, uint32_t size, nri::BindPoint bindPoint = nri::BindPoint::INHERIT);

        // Viewport & Scissor Setup
        void SetViewports(const nri::Viewport* viewports, uint32_t count);
        void SetViewport(const nri::Viewport& viewport);
        void SetScissors(const nri::Rect* rects, uint32_t count);
        void SetScissor(const nri::Rect& rect);

        // Rendering Pass
        void BeginRendering(const nri::RenderingDesc& renderingDesc);
        void EndRendering();
        void ClearAttachments(const nri::ClearAttachmentDesc* clearDescs, uint32_t clearDescNum, const nri::Rect* rects = nullptr, uint32_t rectNum = 0);

        // Draw & Dispatch
        void Draw(const nri::DrawDesc& drawDesc);
        void Draw(uint32_t vertexNum, uint32_t instanceNum = 1, uint32_t baseVertex = 0, uint32_t baseInstance = 0);
        void DrawIndexed(const nri::DrawIndexedDesc& drawIndexedDesc);
        void DrawIndexed(uint32_t indexNum, uint32_t instanceNum = 1, uint32_t baseIndex = 0, int32_t baseVertex = 0, uint32_t baseInstance = 0);
        void Dispatch(const nri::DispatchDesc& dispatchDesc);
        void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);

        // Indirect Draw & Dispatch
        void DrawIndirect(const nri::Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride = sizeof(nri::DrawDesc), const nri::Buffer* countBuffer = nullptr, uint64_t countBufferOffset = 0);
        void DrawIndirect(const RHIBuffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride = sizeof(nri::DrawDesc), const RHIBuffer* countBuffer = nullptr, uint64_t countBufferOffset = 0);
        void DrawIndirect(const RHIStreamedBuffer& streamBuffer, uint32_t drawNum, uint32_t stride = sizeof(nri::DrawDesc));

        void DrawIndexedIndirect(const nri::Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride = sizeof(nri::DrawIndexedDesc), const nri::Buffer* countBuffer = nullptr, uint64_t countBufferOffset = 0);
        void DrawIndexedIndirect(const RHIBuffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride = sizeof(nri::DrawIndexedDesc), const RHIBuffer* countBuffer = nullptr, uint64_t countBufferOffset = 0);
        void DrawIndexedIndirect(const RHIStreamedBuffer& streamBuffer, uint32_t drawNum, uint32_t stride = sizeof(nri::DrawIndexedDesc));

        void DispatchIndirect(const nri::Buffer& buffer, uint64_t offset);
        void DispatchIndirect(const RHIBuffer& buffer, uint64_t offset);
        void DispatchIndirect(const RHIStreamedBuffer& streamBuffer);

        // Submission helper (submits this command buffer to the target queue)
        bool Submit(nri::Queue* queue = nullptr, nri::Fence* waitFence = nullptr, uint64_t waitValue = 0, nri::Fence* signalFence = nullptr, uint64_t signalValue = 0);

        bool IsValid() const { return m_CommandBuffer != nullptr; }
        bool IsRecording() const { return m_IsRecording; }

        nri::CommandBuffer* GetNative() const { return m_CommandBuffer; }
        operator nri::CommandBuffer*() const { return m_CommandBuffer; }

    private:
        nri::CommandBuffer* m_CommandBuffer = nullptr;
        nri::CommandAllocator* m_CommandAllocator = nullptr;
        bool m_OwnsAllocator = false;
        bool m_IsRecording = false;
    };

}
