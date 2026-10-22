#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    RHICommandBuffer::~RHICommandBuffer() {
        Shutdown();
    }

    RHICommandBuffer::RHICommandBuffer(RHICommandBuffer&& other) noexcept
        : m_CommandBuffer(other.m_CommandBuffer)
        , m_CommandAllocator(other.m_CommandAllocator)
        , m_OwnsAllocator(other.m_OwnsAllocator)
        , m_IsRecording(other.m_IsRecording)
    {
        other.m_CommandBuffer = nullptr;
        other.m_CommandAllocator = nullptr;
        other.m_OwnsAllocator = false;
        other.m_IsRecording = false;
    }

    RHICommandBuffer& RHICommandBuffer::operator=(RHICommandBuffer&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_CommandBuffer = other.m_CommandBuffer;
            m_CommandAllocator = other.m_CommandAllocator;
            m_OwnsAllocator = other.m_OwnsAllocator;
            m_IsRecording = other.m_IsRecording;

            other.m_CommandBuffer = nullptr;
            other.m_CommandAllocator = nullptr;
            other.m_OwnsAllocator = false;
            other.m_IsRecording = false;
        }
        return *this;
    }

    bool RHICommandBuffer::Init(nri::CommandAllocator* allocator) {
        Shutdown();

        if (!allocator) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: CommandAllocator is null!");
            return false;
        }

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: RHIDevice is not initialized!");
            return false;
        }

        const auto& core = rhiDevice.GetNRICore();
        m_CommandAllocator = allocator;
        m_OwnsAllocator = false;

        nri::Result res = core.CreateCommandBuffer(*m_CommandAllocator, m_CommandBuffer);
        if (res != nri::Result::SUCCESS || !m_CommandBuffer) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: CreateCommandBuffer failed with code {0}!", static_cast<uint32_t>(res));
            m_CommandAllocator = nullptr;
            return false;
        }

        return true;
    }

    bool RHICommandBuffer::Init(nri::Queue* queue) {
        Shutdown();

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: RHIDevice is not initialized!");
            return false;
        }

        const auto& core = rhiDevice.GetNRICore();
        if (!queue) {
            queue = rhiDevice.GetGraphicsQueue();
        }
        if (!queue) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: No valid graphics queue available!");
            return false;
        }

        nri::Result res = core.CreateCommandAllocator(*queue, m_CommandAllocator);
        if (res != nri::Result::SUCCESS || !m_CommandAllocator) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: CreateCommandAllocator failed with code {0}!", static_cast<uint32_t>(res));
            return false;
        }
        m_OwnsAllocator = true;

        res = core.CreateCommandBuffer(*m_CommandAllocator, m_CommandBuffer);
        if (res != nri::Result::SUCCESS || !m_CommandBuffer) {
            AXLE_CORE_ERROR("RHICommandBuffer::Init: CreateCommandBuffer failed with code {0}!", static_cast<uint32_t>(res));
            core.DestroyCommandAllocator(m_CommandAllocator);
            m_CommandAllocator = nullptr;
            m_OwnsAllocator = false;
            return false;
        }

        return true;
    }

    void RHICommandBuffer::Shutdown() {
        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.IsInitialized()) {
            const auto& core = rhiDevice.GetNRICore();
            if (m_CommandBuffer && core.DestroyCommandBuffer) {
                core.DestroyCommandBuffer(m_CommandBuffer);
            }
            if (m_OwnsAllocator && m_CommandAllocator && core.DestroyCommandAllocator) {
                core.DestroyCommandAllocator(m_CommandAllocator);
            }
        }
        m_CommandBuffer = nullptr;
        m_CommandAllocator = nullptr;
        m_OwnsAllocator = false;
        m_IsRecording = false;
    }

    bool RHICommandBuffer::Begin(nri::DescriptorPool* descriptorPool) {
        if (!m_CommandBuffer) return false;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();

        nri::Result res = core.BeginCommandBuffer(*m_CommandBuffer, descriptorPool);
        m_IsRecording = (res == nri::Result::SUCCESS);
        if (!m_IsRecording) {
            AXLE_CORE_ERROR("RHICommandBuffer::Begin failed with code {0}!", static_cast<uint32_t>(res));
        }
        return m_IsRecording;
    }

    bool RHICommandBuffer::End() {
        if (!m_CommandBuffer || !m_IsRecording) return false;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();

        nri::Result res = core.EndCommandBuffer(*m_CommandBuffer);
        m_IsRecording = false;
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_ERROR("RHICommandBuffer::End failed with code {0}!", static_cast<uint32_t>(res));
            return false;
        }
        return true;
    }

    void RHICommandBuffer::PipelineBarrier(const RHIBarrierGroup& barrierGroup) {
        if (!m_CommandBuffer || !m_IsRecording || barrierGroup.IsEmpty()) return;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (!core.CmdBarrier) return;

        std::vector<nri::GlobalBarrierDesc> nriGlobals;
        std::vector<nri::BufferBarrierDesc> nriBuffers;
        std::vector<nri::TextureBarrierDesc> nriTextures;

        nriGlobals.reserve(barrierGroup.GetGlobalCount());
        for (const auto& g : barrierGroup.GetGlobals()) {
            nriGlobals.push_back(g.ToNRIDesc());
        }

        nriBuffers.reserve(barrierGroup.GetBufferCount());
        for (const auto& b : barrierGroup.GetBuffers()) {
            nriBuffers.push_back(b.ToNRIDesc());
        }

        nriTextures.reserve(barrierGroup.GetTextureCount());
        for (const auto& t : barrierGroup.GetTextures()) {
            nriTextures.push_back(t.ToNRIDesc());
        }

        nri::BarrierDesc desc = {};
        desc.globals = nriGlobals.empty() ? nullptr : nriGlobals.data();
        desc.globalNum = static_cast<uint32_t>(nriGlobals.size());
        desc.buffers = nriBuffers.empty() ? nullptr : nriBuffers.data();
        desc.bufferNum = static_cast<uint32_t>(nriBuffers.size());
        desc.textures = nriTextures.empty() ? nullptr : nriTextures.data();
        desc.textureNum = static_cast<uint32_t>(nriTextures.size());

        core.CmdBarrier(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::TransitionTexture(nri::Texture* texture, RHIResourceLayout before, RHIResourceLayout after) {
        if (!texture || before == after) return;
        RHIBarrierGroup group;
        group.AddTexture(texture, before, after);
        PipelineBarrier(group);
    }

    bool RHICommandBuffer::TransitionTexture(RHIResourceStateTracker& tracker, nri::Texture* texture, RHIResourceLayout targetLayout) {
        if (!texture) return false;
        RHIBarrierGroup group;
        if (tracker.TransitionTexture(texture, targetLayout, group)) {
            PipelineBarrier(group);
            return true;
        }
        return false;
    }

    void RHICommandBuffer::TransitionBuffer(nri::Buffer* buffer, RHIAccess beforeAccess, RHIAccess afterAccess,
                                            RHIPipelineStage beforeStage, RHIPipelineStage afterStage)
    {
        if (!buffer || !m_CommandBuffer || !m_IsRecording) return;
        RHIBarrierGroup group;
        group.AddBuffer(buffer, beforeAccess, afterAccess, beforeStage, afterStage);
        PipelineBarrier(group);
    }

    bool RHICommandBuffer::TransitionBuffer(RHIResourceStateTracker& tracker, nri::Buffer* buffer, RHIAccess targetAccess, RHIPipelineStage targetStage) {
        if (!buffer || !m_CommandBuffer || !m_IsRecording) return false;
        RHIBarrierGroup group;
        if (tracker.TransitionBuffer(buffer, targetAccess, targetStage, group)) {
            PipelineBarrier(group);
            return true;
        }
        return false;
    }

    void RHICommandBuffer::CopyStreamedData(RHIStreamer& streamer, uint64_t copyBatch) {
        if (!m_CommandBuffer || !m_IsRecording || copyBatch == 0) return;
        streamer.CmdCopyStreamedData(*m_CommandBuffer, copyBatch);
    }

    void RHICommandBuffer::SetPipeline(nri::Pipeline* pipeline) {
        if (!m_CommandBuffer || !m_IsRecording || !pipeline) return;
        auto& rhiDevice = RHIDevice::Get();
        rhiDevice.GetNRICore().CmdSetPipeline(*m_CommandBuffer, *pipeline);
    }

    void RHICommandBuffer::SetPipelineLayout(nri::BindPoint bindPoint, const RHIPipelineLayout& layout) {
        if (!m_CommandBuffer || !m_IsRecording || !layout.IsValid()) return;
        auto& rhiDevice = RHIDevice::Get();
        rhiDevice.GetNRICore().CmdSetPipelineLayout(*m_CommandBuffer, bindPoint, *layout.GetNative());
    }

    void RHICommandBuffer::SetDescriptorPool(nri::DescriptorPool* descriptorPool) {
        if (!m_CommandBuffer || !m_IsRecording || !descriptorPool) return;
        auto& rhiDevice = RHIDevice::Get();
        rhiDevice.GetNRICore().CmdSetDescriptorPool(*m_CommandBuffer, *descriptorPool);
    }

    void RHICommandBuffer::SetDescriptorSet(nri::BindPoint bindPoint, uint32_t setIndex, nri::DescriptorSet* descriptorSet) {
        if (!m_CommandBuffer || !m_IsRecording || !descriptorSet) return;
        nri::SetDescriptorSetDesc desc = {};
        desc.bindPoint = bindPoint;
        desc.setIndex = setIndex;
        desc.descriptorSet = descriptorSet;
        RHIDevice::Get().GetNRICore().CmdSetDescriptorSet(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::SetRootConstants(uint32_t rootConstantIndex, const void* data, uint32_t size, nri::BindPoint bindPoint) {
        if (!m_CommandBuffer || !m_IsRecording || !data || size == 0) return;
        nri::SetRootConstantsDesc desc = {};
        desc.rootConstantIndex = rootConstantIndex;
        desc.data = data;
        desc.size = size;
        desc.offset = 0;
        desc.bindPoint = bindPoint;
        RHIDevice::Get().GetNRICore().CmdSetRootConstants(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::SetVertexBuffers(uint32_t baseSlot, const nri::VertexBufferDesc* vertexBufferDescs, uint32_t count) {
        if (!m_CommandBuffer || !m_IsRecording || !vertexBufferDescs || count == 0) return;
        RHIDevice::Get().GetNRICore().CmdSetVertexBuffers(*m_CommandBuffer, baseSlot, vertexBufferDescs, count);
    }

    void RHICommandBuffer::SetVertexBuffer(uint32_t slot, const nri::Buffer* buffer, uint64_t offset, uint32_t stride) {
        if (!m_CommandBuffer || !m_IsRecording || !buffer) return;
        nri::VertexBufferDesc desc = {};
        desc.buffer = buffer;
        desc.offset = offset;
        desc.stride = stride;
        RHIDevice::Get().GetNRICore().CmdSetVertexBuffers(*m_CommandBuffer, slot, &desc, 1);
    }

    void RHICommandBuffer::SetVertexBuffer(uint32_t slot, const RHIBuffer& buffer, uint64_t offset, uint32_t stride) {
        SetVertexBuffer(slot, buffer.GetNative(), offset, stride);
    }

    void RHICommandBuffer::SetIndexBuffer(const nri::Buffer* buffer, uint64_t offset, nri::IndexType indexType) {
        if (!m_CommandBuffer || !m_IsRecording || !buffer) return;
        RHIDevice::Get().GetNRICore().CmdSetIndexBuffer(*m_CommandBuffer, *buffer, offset, indexType);
    }

    void RHICommandBuffer::SetIndexBuffer(const RHIBuffer& buffer, uint64_t offset, nri::IndexType indexType) {
        SetIndexBuffer(buffer.GetNative(), offset, indexType);
    }

    void RHICommandBuffer::SetViewports(const nri::Viewport* viewports, uint32_t count) {
        if (!m_CommandBuffer || !m_IsRecording || !viewports || count == 0) return;
        RHIDevice::Get().GetNRICore().CmdSetViewports(*m_CommandBuffer, viewports, count);
    }

    void RHICommandBuffer::SetViewport(const nri::Viewport& viewport) {
        SetViewports(&viewport, 1);
    }

    void RHICommandBuffer::SetScissors(const nri::Rect* rects, uint32_t count) {
        if (!m_CommandBuffer || !m_IsRecording || !rects || count == 0) return;
        RHIDevice::Get().GetNRICore().CmdSetScissors(*m_CommandBuffer, rects, count);
    }

    void RHICommandBuffer::SetScissor(const nri::Rect& rect) {
        SetScissors(&rect, 1);
    }

    void RHICommandBuffer::BeginRendering(const nri::RenderingDesc& renderingDesc) {
        if (!m_CommandBuffer || !m_IsRecording) return;
        RHIDevice::Get().GetNRICore().CmdBeginRendering(*m_CommandBuffer, renderingDesc);
    }

    void RHICommandBuffer::EndRendering() {
        if (!m_CommandBuffer || !m_IsRecording) return;
        RHIDevice::Get().GetNRICore().CmdEndRendering(*m_CommandBuffer);
    }

    void RHICommandBuffer::ClearAttachments(const nri::ClearAttachmentDesc* clearDescs, uint32_t clearDescNum, const nri::Rect* rects, uint32_t rectNum) {
        if (!m_CommandBuffer || !m_IsRecording || !clearDescs || clearDescNum == 0) return;
        RHIDevice::Get().GetNRICore().CmdClearAttachments(*m_CommandBuffer, clearDescs, clearDescNum, rects, rectNum);
    }

    void RHICommandBuffer::Draw(const nri::DrawDesc& drawDesc) {
        if (!m_CommandBuffer || !m_IsRecording) return;
        RHIDevice::Get().GetNRICore().CmdDraw(*m_CommandBuffer, drawDesc);
    }

    void RHICommandBuffer::Draw(uint32_t vertexNum, uint32_t instanceNum, uint32_t baseVertex, uint32_t baseInstance) {
        nri::DrawDesc desc = {};
        desc.vertexNum = vertexNum;
        desc.instanceNum = instanceNum;
        desc.baseVertex = baseVertex;
        desc.baseInstance = baseInstance;
        Draw(desc);
    }

    void RHICommandBuffer::DrawIndexed(const nri::DrawIndexedDesc& drawIndexedDesc) {
        if (!m_CommandBuffer || !m_IsRecording) return;
        RHIDevice::Get().GetNRICore().CmdDrawIndexed(*m_CommandBuffer, drawIndexedDesc);
    }

    void RHICommandBuffer::DrawIndexed(uint32_t indexNum, uint32_t instanceNum, uint32_t baseIndex, int32_t baseVertex, uint32_t baseInstance) {
        nri::DrawIndexedDesc desc = {};
        desc.indexNum = indexNum;
        desc.instanceNum = instanceNum;
        desc.baseIndex = baseIndex;
        desc.baseVertex = baseVertex;
        desc.baseInstance = baseInstance;
        DrawIndexed(desc);
    }

    void RHICommandBuffer::Dispatch(const nri::DispatchDesc& dispatchDesc) {
        if (!m_CommandBuffer || !m_IsRecording) return;
        RHIDevice::Get().GetNRICore().CmdDispatch(*m_CommandBuffer, dispatchDesc);
    }

    void RHICommandBuffer::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
        nri::DispatchDesc desc = {};
        desc.workGroupNumX = x;
        desc.workGroupNumY = y;
        desc.workGroupNumZ = z;
        Dispatch(desc);
    }

    void RHICommandBuffer::DrawIndirect(const nri::Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const nri::Buffer* countBuffer, uint64_t countBufferOffset) {
        if (!m_CommandBuffer || !m_IsRecording || drawNum == 0) return;
        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (core.CmdDrawIndirect) {
            core.CmdDrawIndirect(*m_CommandBuffer, buffer, offset, drawNum, stride, countBuffer, countBufferOffset);
        }
    }

    void RHICommandBuffer::DrawIndirect(const RHIBuffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const RHIBuffer* countBuffer, uint64_t countBufferOffset) {
        if (!buffer.GetNative()) return;
        DrawIndirect(*buffer.GetNative(), offset, drawNum, stride, countBuffer ? countBuffer->GetNative() : nullptr, countBufferOffset);
    }

    void RHICommandBuffer::DrawIndirect(const RHIStreamedBuffer& streamBuffer, uint32_t drawNum, uint32_t stride) {
        if (!streamBuffer.Buffer) return;
        DrawIndirect(*streamBuffer.Buffer, streamBuffer.Offset, drawNum, stride);
    }

    void RHICommandBuffer::DrawIndexedIndirect(const nri::Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const nri::Buffer* countBuffer, uint64_t countBufferOffset) {
        if (!m_CommandBuffer || !m_IsRecording || drawNum == 0) return;
        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (core.CmdDrawIndexedIndirect) {
            core.CmdDrawIndexedIndirect(*m_CommandBuffer, buffer, offset, drawNum, stride, countBuffer, countBufferOffset);
        }
    }

    void RHICommandBuffer::DrawIndexedIndirect(const RHIBuffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const RHIBuffer* countBuffer, uint64_t countBufferOffset) {
        if (!buffer.GetNative()) return;
        DrawIndexedIndirect(*buffer.GetNative(), offset, drawNum, stride, countBuffer ? countBuffer->GetNative() : nullptr, countBufferOffset);
    }

    void RHICommandBuffer::DrawIndexedIndirect(const RHIStreamedBuffer& streamBuffer, uint32_t drawNum, uint32_t stride) {
        if (!streamBuffer.Buffer) return;
        DrawIndexedIndirect(*streamBuffer.Buffer, streamBuffer.Offset, drawNum, stride);
    }

    void RHICommandBuffer::DispatchIndirect(const nri::Buffer& buffer, uint64_t offset) {
        if (!m_CommandBuffer || !m_IsRecording) return;
        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (core.CmdDispatchIndirect) {
            core.CmdDispatchIndirect(*m_CommandBuffer, buffer, offset);
        }
    }

    void RHICommandBuffer::DispatchIndirect(const RHIBuffer& buffer, uint64_t offset) {
        if (!buffer.GetNative()) return;
        DispatchIndirect(*buffer.GetNative(), offset);
    }

    void RHICommandBuffer::DispatchIndirect(const RHIStreamedBuffer& streamBuffer) {
        if (!streamBuffer.Buffer) return;
        DispatchIndirect(*streamBuffer.Buffer, streamBuffer.Offset);
    }

    bool RHICommandBuffer::Submit(nri::Queue* queue, nri::Fence* waitFence, uint64_t waitValue, nri::Fence* signalFence, uint64_t signalValue) {
        if (!m_CommandBuffer || m_IsRecording) return false;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (!queue) {
            queue = rhiDevice.GetGraphicsQueue();
        }
        if (!queue || !core.QueueSubmit) return false;

        nri::FenceSubmitDesc waitDesc = {};
        if (waitFence) {
            waitDesc.fence = waitFence;
            waitDesc.value = waitValue;
            waitDesc.stages = nri::StageBits::ALL;
        }

        nri::FenceSubmitDesc signalDesc = {};
        if (signalFence) {
            signalDesc.fence = signalFence;
            signalDesc.value = signalValue;
            signalDesc.stages = nri::StageBits::ALL;
        }

        const nri::CommandBuffer* const cbPtr = m_CommandBuffer;

        nri::QueueSubmitDesc submitDesc = {};
        submitDesc.commandBuffers = &cbPtr;
        submitDesc.commandBufferNum = 1;
        if (waitFence) {
            submitDesc.waitFences = &waitDesc;
            submitDesc.waitFenceNum = 1;
        }
        if (signalFence) {
            submitDesc.signalFences = &signalDesc;
            submitDesc.signalFenceNum = 1;
        }

        nri::Result res = core.QueueSubmit(*queue, submitDesc);
        return (res == nri::Result::SUCCESS);
    }

    bool RHICommandBuffer::SubmitBatch(const std::vector<RHICommandBuffer*>& commandBuffers,
                                       nri::Queue* queue,
                                       nri::Fence* waitFence, uint64_t waitValue,
                                       nri::Fence* signalFence, uint64_t signalValue) {
        if (commandBuffers.empty()) return true;

        std::vector<const nri::CommandBuffer*> nativeCmdBuffers;
        nativeCmdBuffers.reserve(commandBuffers.size());
        for (const auto* cb : commandBuffers) {
            if (cb && cb->IsValid()) {
                nativeCmdBuffers.push_back(cb->GetNative());
            }
        }
        if (nativeCmdBuffers.empty()) return true;

        return SubmitBatch(nativeCmdBuffers.data(), static_cast<uint32_t>(nativeCmdBuffers.size()),
                           queue, waitFence, waitValue, signalFence, signalValue);
    }

    bool RHICommandBuffer::SubmitBatch(const nri::CommandBuffer* const* commandBuffers, uint32_t count,
                                       nri::Queue* queue,
                                       nri::Fence* waitFence, uint64_t waitValue,
                                       nri::Fence* signalFence, uint64_t signalValue) {
        if (!commandBuffers || count == 0) return true;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (!queue) {
            queue = rhiDevice.GetGraphicsQueue();
        }
        if (!queue || !core.QueueSubmit) return false;

        nri::FenceSubmitDesc waitDesc = {};
        if (waitFence) {
            waitDesc.fence = waitFence;
            waitDesc.value = waitValue;
            waitDesc.stages = nri::StageBits::ALL;
        }

        nri::FenceSubmitDesc signalDesc = {};
        if (signalFence) {
            signalDesc.fence = signalFence;
            signalDesc.value = signalValue;
            signalDesc.stages = nri::StageBits::ALL;
        }

        nri::QueueSubmitDesc submitDesc = {};
        submitDesc.commandBuffers = commandBuffers;
        submitDesc.commandBufferNum = count;
        if (waitFence) {
            submitDesc.waitFences = &waitDesc;
            submitDesc.waitFenceNum = 1;
        }
        if (signalFence) {
            submitDesc.signalFences = &signalDesc;
            submitDesc.signalFenceNum = 1;
        }

        nri::Result res = core.QueueSubmit(*queue, submitDesc);
        return (res == nri::Result::SUCCESS);
    }

    bool RHICommandBuffer::Reset() {
        if (!m_CommandAllocator) return false;
        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) return false;

        const auto& core = rhiDevice.GetNRICore();
        if (m_OwnsAllocator && core.ResetCommandAllocator) {
            core.ResetCommandAllocator(*m_CommandAllocator);
        }
        m_IsRecording = false;
        return true;
    }

}
