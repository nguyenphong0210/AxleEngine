#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#if defined(_WIN32)
#include <d3d12.h>
#endif

namespace Axle::RHI {

    RHICommandBuffer::~RHICommandBuffer() {
        Shutdown();
    }

    RHICommandBuffer::RHICommandBuffer(RHICommandBuffer&& other) noexcept
        : m_Lease(std::move(other.m_Lease))
        , m_CommandBuffer(other.m_CommandBuffer)
        , m_CommandAllocator(other.m_CommandAllocator)
        , m_OwnsAllocator(other.m_OwnsAllocator)
        , m_IsRecording(other.m_IsRecording)
        , m_IsExecutable(other.m_IsExecutable)
        , m_NeedsRecreation(other.m_NeedsRecreation)
    {
        other.m_CommandBuffer = nullptr;
        other.m_CommandAllocator = nullptr;
        other.m_OwnsAllocator = false;
        other.m_IsRecording = false;
        other.m_IsExecutable = false;
        other.m_NeedsRecreation = false;
    }

    RHICommandBuffer& RHICommandBuffer::operator=(RHICommandBuffer&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Lease = std::move(other.m_Lease);
            m_CommandBuffer = other.m_CommandBuffer;
            m_CommandAllocator = other.m_CommandAllocator;
            m_OwnsAllocator = other.m_OwnsAllocator;
            m_IsRecording = other.m_IsRecording;
            m_IsExecutable = other.m_IsExecutable;
            m_NeedsRecreation = other.m_NeedsRecreation;

            other.m_CommandBuffer = nullptr;
            other.m_CommandAllocator = nullptr;
            other.m_OwnsAllocator = false;
            other.m_IsRecording = false;
            other.m_IsExecutable = false;
            other.m_NeedsRecreation = false;
        }
        return *this;
    }

    bool RHICommandBuffer::Init(nri::CommandAllocator* allocator) {
        auto& device = RHIDevice::Get();
        if (!allocator || !device.CanCreateResources()) return false;
        RHICommandBuffer candidate;
        candidate.m_CommandAllocator = allocator;
        const auto result = ScopedRHIFault::Invoke("CreateCommandBuffer", [&] { return device.GetNRICore().CreateCommandBuffer(*allocator, candidate.m_CommandBuffer); });
        if (result != nri::Result::SUCCESS || !candidate.m_CommandBuffer) return false;
        if (!candidate.Publish()) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHICommandBuffer::Init(nri::Queue* queue) {
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources()) return false;
        if (!queue) queue = device.GetGraphicsQueue();
        RHICommandBuffer candidate;
        const auto& core = device.GetNRICore();
        candidate.m_OwnsAllocator = true;
        auto result = ScopedRHIFault::Invoke("CreateCommandAllocator", [&] { return core.CreateCommandAllocator(*queue, candidate.m_CommandAllocator); });
        if (result != nri::Result::SUCCESS || !candidate.m_CommandAllocator) return false;
        result = ScopedRHIFault::Invoke("CreateCommandBuffer", [&] { return core.CreateCommandBuffer(*candidate.m_CommandAllocator, candidate.m_CommandBuffer); });
        if (result != nri::Result::SUCCESS || !candidate.m_CommandBuffer) return false;
        if (!candidate.Publish()) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHICommandBuffer::Publish() {
        auto& device = RHIDevice::Get();
        const auto core = device.GetNRICore();
        auto commands = m_CommandBuffer; auto allocator = m_OwnsAllocator ? m_CommandAllocator : nullptr;
        std::vector<RHIResourceLease> dependencies;
        if (!m_OwnsAllocator) {
            auto owner = device.GetResources().AcquireNative(m_CommandAllocator);
            if (owner.IsLive()) dependencies.push_back(std::move(owner));
        }
        m_Lease = device.GetResources().Publish(RHIResourceKind::Commands, "Commands", {commands, allocator},
            [core, commands, allocator] {
                core.DestroyCommandBuffer(commands);
                if (allocator) core.DestroyCommandAllocator(allocator);
            }, std::move(dependencies));
        return m_Lease.IsLive();
    }

    void RHICommandBuffer::Shutdown() {
        auto& device = RHIDevice::Get();
        if (m_Lease) m_Lease.Reset();
        else if (device.IsNativeReady()) {
            const auto core = device.GetNRICore();
            auto commands = m_CommandBuffer;
            auto allocator = m_OwnsAllocator ? m_CommandAllocator : nullptr;
            device.ReleaseNative([core, commands, allocator] {
                if (commands) core.DestroyCommandBuffer(commands);
                if (allocator) core.DestroyCommandAllocator(allocator);
            });
        }
        m_CommandBuffer = nullptr; m_CommandAllocator = nullptr;
        m_OwnsAllocator = m_IsRecording = m_IsExecutable = m_NeedsRecreation = false;
    }

    bool RHICommandBuffer::Begin(nri::DescriptorPool* descriptorPool) {
        if (!IsValid() || m_IsRecording || !RHIDevice::Get().CanSubmit()) return false;
        if (!RHIDevice::Get().GetResources().BeginRecording(m_CommandBuffer)) return false;
        m_IsExecutable = false;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();

        nri::Result res = ScopedRHIFault::Invoke("BeginCommandBuffer", [&] { return core.BeginCommandBuffer(*m_CommandBuffer, descriptorPool); });
        m_IsRecording = (res == nri::Result::SUCCESS);
        m_NeedsRecreation = true;
        if (!m_IsRecording) {
            rhiDevice.GetResources().EndRecording(m_CommandBuffer, false);
            AXLE_CORE_ERROR("RHICommandBuffer::Begin failed with code {0}!", static_cast<uint32_t>(res));
        }
        return m_IsRecording && (!descriptorPool || RetainNative(descriptorPool));
    }

    bool RHICommandBuffer::End() {
        if (!CanRecord()) return false;

        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.GetResources().HasAccessValidator(m_CommandBuffer)) {
            rhiDevice.GetResources().EndRecording(m_CommandBuffer,false);
            m_NeedsRecreation=true;
            return false;
        }
        const auto& core = rhiDevice.GetNRICore();

        nri::Result res = ScopedRHIFault::Invoke("EndCommandBuffer", [&] { return core.EndCommandBuffer(*m_CommandBuffer); });
        m_IsRecording = false;
        m_IsExecutable = rhiDevice.GetResources().EndRecording(m_CommandBuffer, res == nri::Result::SUCCESS);
        m_NeedsRecreation = !m_IsExecutable;
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_ERROR("RHICommandBuffer::End failed with code {0}!", static_cast<uint32_t>(res));
            return false;
        }
        return m_IsExecutable;
    }

    bool RHICommandBuffer::KeepAlive(const RHIResourceLease& resource) {
        return CanRecord() && RHIDevice::Get().GetResources().RecordUse(m_CommandBuffer, resource);
    }
    bool RHICommandBuffer::RetainNative(const void* native) {
        return CanRecord() && RHIDevice::Get().GetResources().RecordNativeUse(m_CommandBuffer, native);
    }
    bool RHICommandBuffer::DeclareDescriptorRead(const void* descriptor) {
        auto& resources = RHIDevice::Get().GetResources();
        auto resource = resources.AcquireNative(descriptor);
        if (!resource) return true; // explicitly borrowed native adapter
        const auto pins = resources.Pin(resource);
        if (!resources.HasAccessValidator(m_CommandBuffer))
            for (auto* texture : resources.GetPinnedTextures(pins))
                TransitionTexture(texture, RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResource);
        for (auto* buffer : resources.GetPinnedBuffers(pins))
            TransitionBuffer(buffer, RHIAccess::None, RHIAccess::ConstantBuffer, RHIPipelineStage::None, RHIPipelineStage::AllShaders);
        return CanRecord();
    }

    void RHICommandBuffer::PipelineBarrier(const RHIBarrierGroup& barrierGroup) {
        if (!CanRecord() || barrierGroup.IsEmpty()) return;

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
            if (!RetainNative(b.Buffer)) return;
            if (!rhiDevice.GetResources().BuildBufferBarrier(m_CommandBuffer, b.ToNRIDesc(), nriBuffers)) {
                rhiDevice.GetResources().EndRecording(m_CommandBuffer, false);
                rhiDevice.ObserveResult(nri::Result::INVALID_ARGUMENT, "BufferState", "unregistered resource or invalid recording");
                return;
            }
        }

        nriTextures.reserve(barrierGroup.GetTextureCount());
        for (const auto& t : barrierGroup.GetTextures()) {
            if (!RetainNative(t.Texture)) return;
            if (!rhiDevice.GetResources().BuildTextureBarriers(m_CommandBuffer, t.ToNRIDesc(), nriTextures)) {
                rhiDevice.GetResources().EndRecording(m_CommandBuffer, false);
                rhiDevice.ObserveResult(nri::Result::INVALID_ARGUMENT, "TextureState", "invalid resource range or queue ownership transfer");
                return;
            }
        }

        nri::BarrierDesc desc = {};
        desc.globals = nriGlobals.empty() ? nullptr : nriGlobals.data();
        desc.globalNum = static_cast<uint32_t>(nriGlobals.size());
        desc.buffers = nriBuffers.empty() ? nullptr : nriBuffers.data();
        desc.bufferNum = static_cast<uint32_t>(nriBuffers.size());
        desc.textures = nriTextures.empty() ? nullptr : nriTextures.data();
        desc.textureNum = static_cast<uint32_t>(nriTextures.size());

#if defined(_WIN32)
        if (rhiDevice.GetBackend() == RHIType::Direct3D12 && !core.GetDeviceDesc(*rhiDevice.GetNRIDevice()).features.enhancedBarriers) {
            auto common = desc; common.textureNum = 0; common.textures = nullptr;
            if (common.globalNum || common.bufferNum) core.CmdBarrier(*m_CommandBuffer, common);
            for (const auto& texture : nriTextures) {
                nri::BarrierDesc single{}; single.textures = &texture; single.textureNum = 1;
                core.CmdBarrier(*m_CommandBuffer, single);
                if (texture.before.layout == nri::Layout::UNDEFINED &&
                    (texture.after.layout == nri::Layout::COLOR_ATTACHMENT || texture.after.layout == nri::Layout::DEPTH_STENCIL_ATTACHMENT)) {
                    const auto& info = core.GetTextureDesc(*texture.texture);
                    D3D12_DISCARD_REGION discard{};
                    discard.FirstSubresource = texture.mipOffset + texture.layerOffset * info.mipNum;
                    if (texture.planes == nri::PlaneBits::STENCIL) discard.FirstSubresource += info.mipNum * info.layerNum;
                    discard.NumSubresources = 1;
                    auto* native = static_cast<ID3D12GraphicsCommandList*>(core.GetCommandBufferNativeObject(m_CommandBuffer));
                    native->DiscardResource(reinterpret_cast<ID3D12Resource*>(core.GetTextureNativeObject(texture.texture)), &discard);
                }
            }
            return;
        }
#endif
        if (desc.globalNum || desc.bufferNum || desc.textureNum) core.CmdBarrier(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::TransitionTexture(nri::Texture* texture, RHIResourceLayout before, RHIResourceLayout after) {
        if (!texture || !CanRecord()) return;
        RHIBarrierGroup group;
        group.AddTexture(texture, before, after);
        PipelineBarrier(group);
    }

    bool RHICommandBuffer::TransitionTexture(RHIResourceStateTracker& tracker, nri::Texture* texture, RHIResourceLayout targetLayout) {
        // The standalone CPU scratch tracker cannot publish native queue state.
        if (!texture || !CanRecord()) return false;
        TransitionTexture(texture, RHIResourceLayout::Undefined, targetLayout);
        return CanRecord();
    }

    void RHICommandBuffer::TransitionBuffer(nri::Buffer* buffer, RHIAccess beforeAccess, RHIAccess afterAccess,
                                            RHIPipelineStage beforeStage, RHIPipelineStage afterStage)
    {
        if (!buffer || !CanRecord()) return;
        RHIBarrierGroup group;
        group.AddBuffer(buffer, beforeAccess, afterAccess, beforeStage, afterStage);
        PipelineBarrier(group);
    }

    bool RHICommandBuffer::TransitionBuffer(RHIResourceStateTracker& tracker, nri::Buffer* buffer, RHIAccess access, RHIPipelineStage stage) {
        if (!buffer || !CanRecord()) return false;
        TransitionBuffer(buffer, RHIAccess::None, access, RHIPipelineStage::None, stage);
        return CanRecord();
    }

    void RHICommandBuffer::CopyStreamedData(RHIStreamer& streamer, uint64_t copyBatch) {
        if (!CanRecord() || copyBatch == 0) return;
        streamer.CmdCopyStreamedData(*m_CommandBuffer, copyBatch);
    }

    void RHICommandBuffer::TransitionRegion(nri::Texture* texture, RHIResourceLayout layout, const nri::TextureRegionDesc* region) {
        RHITextureBarrier barrier(texture, RHIResourceLayout::Undefined, layout);
        if (region) {
            barrier.MipOffset = region->mipOffset; barrier.MipNum = 1;
            barrier.LayerOffset = region->layerOffset; barrier.LayerNum = 1; barrier.Planes = region->planes;
        }
        RHIBarrierGroup group; group.AddTexture(barrier); PipelineBarrier(group);
    }
    void RHICommandBuffer::CopyBuffer(nri::Buffer& dst, uint64_t dstOffset, const nri::Buffer& src, uint64_t srcOffset, uint64_t size) {
        if (!CanRecord() || !RetainNative(&dst) || !RetainNative(&src)) return;
        TransitionBuffer(&dst, RHIAccess::None, RHIAccess::CopyDest, RHIPipelineStage::None, RHIPipelineStage::Copy);
        TransitionBuffer(const_cast<nri::Buffer*>(&src), RHIAccess::None, RHIAccess::CopySource, RHIPipelineStage::None, RHIPipelineStage::Copy);
        if (CanRecord()) RHIDevice::Get().GetNRICore().CmdCopyBuffer(*m_CommandBuffer, dst, dstOffset, src, srcOffset, size);
    }
    void RHICommandBuffer::CopyTexture(nri::Texture& dst, const nri::TextureRegionDesc* dstRegion, const nri::Texture& src, const nri::TextureRegionDesc* srcRegion) {
        if (!CanRecord() || !RetainNative(&dst) || !RetainNative(&src)) return;
        TransitionRegion(&dst, RHIResourceLayout::CopyDest, dstRegion);
        TransitionRegion(const_cast<nri::Texture*>(&src), RHIResourceLayout::CopySource, srcRegion);
        if (CanRecord()) RHIDevice::Get().GetNRICore().CmdCopyTexture(*m_CommandBuffer, dst, dstRegion, src, srcRegion);
    }
    void RHICommandBuffer::ReadbackTextureToBuffer(nri::Buffer& dst, const nri::TextureDataLayoutDesc& layout, const nri::Texture& src, const nri::TextureRegionDesc& region) {
        if (!CanRecord() || !RetainNative(&dst) || !RetainNative(&src)) return;
        TransitionBuffer(&dst, RHIAccess::None, RHIAccess::CopyDest, RHIPipelineStage::None, RHIPipelineStage::Copy);
        TransitionRegion(const_cast<nri::Texture*>(&src), RHIResourceLayout::CopySource, &region);
        if (CanRecord()) RHIDevice::Get().GetNRICore().CmdReadbackTextureToBuffer(*m_CommandBuffer, dst, layout, src, region);
    }
    void RHICommandBuffer::UploadBufferToTexture(nri::Texture& dst, const nri::TextureRegionDesc& region, const nri::Buffer& src, const nri::TextureDataLayoutDesc& layout) {
        if (!CanRecord() || !RetainNative(&dst) || !RetainNative(&src)) return;
        TransitionRegion(&dst, RHIResourceLayout::CopyDest, &region);
        TransitionBuffer(const_cast<nri::Buffer*>(&src), RHIAccess::None, RHIAccess::CopySource, RHIPipelineStage::None, RHIPipelineStage::Copy);
        if (CanRecord()) RHIDevice::Get().GetNRICore().CmdUploadBufferToTexture(*m_CommandBuffer, dst, region, src, layout);
    }

    void RHICommandBuffer::SetPipeline(nri::Pipeline* pipeline) {
        if (!CanRecord() || !pipeline) return;
        if (!RetainNative(pipeline)) return;
        auto& rhiDevice = RHIDevice::Get();
        rhiDevice.GetNRICore().CmdSetPipeline(*m_CommandBuffer, *pipeline);
    }

    void RHICommandBuffer::SetPipelineLayout(nri::BindPoint bindPoint, const RHIPipelineLayout& layout) {
        if (!CanRecord() || !layout.IsValid()) return;
        if (!KeepAlive(layout.AcquireLease())) return;
        auto& rhiDevice = RHIDevice::Get();
        rhiDevice.GetNRICore().CmdSetPipelineLayout(*m_CommandBuffer, bindPoint, *layout.GetNative());
    }

    void RHICommandBuffer::SetDescriptorPool(nri::DescriptorPool* descriptorPool) {
        if (!CanRecord() || !descriptorPool) return;
        if (!RetainNative(descriptorPool)) return;
        auto& rhiDevice = RHIDevice::Get();
        rhiDevice.GetNRICore().CmdSetDescriptorPool(*m_CommandBuffer, *descriptorPool);
    }

    void RHICommandBuffer::SetDescriptorSet(nri::BindPoint bindPoint, uint32_t setIndex, nri::DescriptorSet* descriptorSet) {
        if (!CanRecord() || !descriptorSet) return;
        if (!RHIDevice::Get().GetResources().RecordDescriptorSetUse(m_CommandBuffer,descriptorSet)) return;
        if (!DeclareDescriptorRead(descriptorSet)) return;
        nri::SetDescriptorSetDesc desc = {};
        desc.bindPoint = bindPoint;
        desc.setIndex = setIndex;
        desc.descriptorSet = descriptorSet;
        RHIDevice::Get().GetNRICore().CmdSetDescriptorSet(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::SetRootConstants(uint32_t rootConstantIndex, const void* data, uint32_t size, nri::BindPoint bindPoint) {
        if (!CanRecord() || !data || size == 0) return;
        nri::SetRootConstantsDesc desc = {};
        desc.rootConstantIndex = rootConstantIndex;
        desc.data = data;
        desc.size = size;
        desc.offset = 0;
        desc.bindPoint = bindPoint;
        RHIDevice::Get().GetNRICore().CmdSetRootConstants(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::SetRootDescriptor(uint32_t rootDescriptorIndex, nri::Descriptor* descriptor, uint32_t offset, nri::BindPoint bindPoint) {
        if (!CanRecord() || !descriptor) return;
        if (!RHIDevice::Get().GetResources().RecordDescriptorUse(m_CommandBuffer,descriptor)) return;
        if (!DeclareDescriptorRead(descriptor)) return;
        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (!core.CmdSetRootDescriptor) return;

        nri::SetRootDescriptorDesc desc = {};
        desc.rootDescriptorIndex = rootDescriptorIndex;
        desc.descriptor = descriptor;
        desc.offset = offset;
        desc.bindPoint = bindPoint;
        core.CmdSetRootDescriptor(*m_CommandBuffer, desc);
    }

    void RHICommandBuffer::SetVertexBuffers(uint32_t baseSlot, const nri::VertexBufferDesc* vertexBufferDescs, uint32_t count) {
        if (!CanRecord() || !vertexBufferDescs || count == 0) return;
        for (uint32_t i = 0; i < count; ++i) if (!RetainNative(vertexBufferDescs[i].buffer)) return;
        for (uint32_t i = 0; i < count; ++i)
            TransitionBuffer(const_cast<nri::Buffer*>(vertexBufferDescs[i].buffer), RHIAccess::None, RHIAccess::VertexBuffer, RHIPipelineStage::None, RHIPipelineStage::VertexShader);
        if (!CanRecord()) return;
        RHIDevice::Get().GetNRICore().CmdSetVertexBuffers(*m_CommandBuffer, baseSlot, vertexBufferDescs, count);
    }

    void RHICommandBuffer::SetVertexBuffer(uint32_t slot, const nri::Buffer* buffer, uint64_t offset, uint32_t stride) {
        if (!CanRecord() || !buffer) return;
        if (!RetainNative(buffer)) return;
        TransitionBuffer(const_cast<nri::Buffer*>(buffer), RHIAccess::None, RHIAccess::VertexBuffer, RHIPipelineStage::None, RHIPipelineStage::VertexShader);
        if (!CanRecord()) return;
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
        if (!CanRecord() || !buffer) return;
        if (!RetainNative(buffer)) return;
        TransitionBuffer(const_cast<nri::Buffer*>(buffer), RHIAccess::None, RHIAccess::IndexBuffer, RHIPipelineStage::None, RHIPipelineStage::IndexInput);
        if (!CanRecord()) return;
        RHIDevice::Get().GetNRICore().CmdSetIndexBuffer(*m_CommandBuffer, *buffer, offset, indexType);
    }

    void RHICommandBuffer::SetIndexBuffer(const RHIBuffer& buffer, uint64_t offset, nri::IndexType indexType) {
        SetIndexBuffer(buffer.GetNative(), offset, indexType);
    }

    void RHICommandBuffer::SetViewports(const nri::Viewport* viewports, uint32_t count) {
        if (!CanRecord() || !viewports || count == 0) return;
        RHIDevice::Get().GetNRICore().CmdSetViewports(*m_CommandBuffer, viewports, count);
    }

    void RHICommandBuffer::SetViewport(const nri::Viewport& viewport) {
        SetViewports(&viewport, 1);
    }

    void RHICommandBuffer::SetScissors(const nri::Rect* rects, uint32_t count) {
        if (!CanRecord() || !rects || count == 0) return;
        RHIDevice::Get().GetNRICore().CmdSetScissors(*m_CommandBuffer, rects, count);
    }

    void RHICommandBuffer::SetScissor(const nri::Rect& rect) {
        SetScissors(&rect, 1);
    }

    void RHICommandBuffer::BeginRendering(const nri::RenderingDesc& renderingDesc) {
        if (!CanRecord()) return;
        auto retainAttachment = [&](const nri::AttachmentDesc& attachment) {
            return RetainNative(attachment.descriptor) && RetainNative(attachment.resolveDst);
        };
        if (renderingDesc.colorNum && !renderingDesc.colors) return;
        for (uint32_t i = 0; i < renderingDesc.colorNum; ++i) if (!retainAttachment(renderingDesc.colors[i])) return;
        if (!retainAttachment(renderingDesc.depth) || !retainAttachment(renderingDesc.stencil) || !RetainNative(renderingDesc.shadingRate)) return;
        auto& resources = RHIDevice::Get().GetResources();
        for (uint32_t i = 0; i < renderingDesc.colorNum; ++i) {
            if (auto* texture = resources.GetTextureForAlias(renderingDesc.colors[i].descriptor))
                TransitionTexture(texture, RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
            if (auto* texture = resources.GetTextureForAlias(renderingDesc.colors[i].resolveDst))
                TransitionTexture(texture, RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
        }
        if (auto* texture = resources.GetTextureForAlias(renderingDesc.depth.descriptor))
            TransitionTexture(texture, RHIResourceLayout::Undefined, RHIResourceLayout::DepthStencilWrite);
        if (!CanRecord()) return;
        if (!RHIDevice::Get().GetResources().SetRendering(m_CommandBuffer, true)) return;
        RHIDevice::Get().GetNRICore().CmdBeginRendering(*m_CommandBuffer, renderingDesc);
    }

    void RHICommandBuffer::EndRendering() {
        if (!CanRecord()) return;
        if (!RHIDevice::Get().GetResources().SetRendering(m_CommandBuffer, false)) return;
        RHIDevice::Get().GetNRICore().CmdEndRendering(*m_CommandBuffer);
    }

    void RHICommandBuffer::ClearAttachments(const nri::ClearAttachmentDesc* clearDescs, uint32_t clearDescNum, const nri::Rect* rects, uint32_t rectNum) {
        if (!CanRecord() || !clearDescs || clearDescNum == 0) return;
        RHIDevice::Get().GetNRICore().CmdClearAttachments(*m_CommandBuffer, clearDescs, clearDescNum, rects, rectNum);
    }

    void RHICommandBuffer::ResolveTexture(nri::Texture* dstTexture, const nri::Texture* srcTexture, nri::ResolveOp resolveOp) {
        if (!CanRecord() || !dstTexture || !srcTexture) return;
        if (!RetainNative(dstTexture) || !RetainNative(srcTexture)) return;
        TransitionTexture(dstTexture, RHIResourceLayout::Undefined, RHIResourceLayout::ResolveDest);
        TransitionTexture(const_cast<nri::Texture*>(srcTexture), RHIResourceLayout::Undefined, RHIResourceLayout::ResolveSource);
        if (!CanRecord()) return;
        RHIDevice::Get().GetNRICore().CmdResolveTexture(*m_CommandBuffer, *dstTexture, nullptr, *srcTexture, nullptr, resolveOp);
    }

    void RHICommandBuffer::Draw(const nri::DrawDesc& drawDesc) {
        if (!CanRecord()) return;
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
        if (!CanRecord()) return;
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
        if (!CanRecord()) return;
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
        if (!CanRecord() || drawNum == 0) return;
        if (!RetainNative(&buffer) || !RetainNative(countBuffer)) return;
        TransitionBuffer(const_cast<nri::Buffer*>(&buffer), RHIAccess::None, RHIAccess::ArgumentBuffer);
        if (countBuffer) TransitionBuffer(const_cast<nri::Buffer*>(countBuffer), RHIAccess::None, RHIAccess::ArgumentBuffer);
        if (!CanRecord()) return;
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
        if (!CanRecord() || drawNum == 0) return;
        if (!RetainNative(&buffer) || !RetainNative(countBuffer)) return;
        TransitionBuffer(const_cast<nri::Buffer*>(&buffer), RHIAccess::None, RHIAccess::ArgumentBuffer);
        if (countBuffer) TransitionBuffer(const_cast<nri::Buffer*>(countBuffer), RHIAccess::None, RHIAccess::ArgumentBuffer);
        if (!CanRecord()) return;
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
        if (!CanRecord()) return;
        if (!RetainNative(&buffer)) return;
        TransitionBuffer(const_cast<nri::Buffer*>(&buffer), RHIAccess::None, RHIAccess::ArgumentBuffer);
        if (!CanRecord()) return;
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
        if (!IsExecutable() || m_IsRecording) return false;
        const nri::CommandBuffer* commands = m_CommandBuffer;
        return SubmitBatch(&commands, 1, queue, waitFence, waitValue, signalFence, signalValue);
    }

    bool RHICommandBuffer::SubmitBatch(const std::vector<RHICommandBuffer*>& commandBuffers,
                                       nri::Queue* queue,
                                       nri::Fence* waitFence, uint64_t waitValue,
                                       nri::Fence* signalFence, uint64_t signalValue) {
        if (commandBuffers.empty()) return true;

        std::vector<const nri::CommandBuffer*> nativeCmdBuffers;
        nativeCmdBuffers.reserve(commandBuffers.size());
        for (const auto* cb : commandBuffers) {
            if (!cb || !cb->IsValid() || cb->IsRecording() || !cb->IsExecutable()) return false;
            nativeCmdBuffers.push_back(cb->GetNative());
        }
        if (nativeCmdBuffers.empty()) return true;

        return SubmitBatch(nativeCmdBuffers.data(), static_cast<uint32_t>(nativeCmdBuffers.size()),
                           queue, waitFence, waitValue, signalFence, signalValue);
    }

    bool RHICommandBuffer::SubmitBatch(const nri::CommandBuffer* const* commandBuffers, uint32_t count,
                                       nri::Queue* queue,
                                       nri::Fence* waitFence, uint64_t waitValue,
                                       nri::Fence* signalFence, uint64_t signalValue) {
        if (!count) return true;
        if (!commandBuffers) return false;
        for (uint32_t i = 0; i < count; ++i) if (!commandBuffers[i]) return false;
        if (!RHIDevice::Get().CanSubmit()) return false;

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

        return rhiDevice.Submit(submitDesc, queue);
    }

    bool RHICommandBuffer::Reset() {
        if (!IsValid() || !m_CommandAllocator) return false;
        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.GetResources().HasAccessValidator(m_CommandBuffer)) return false;
        if (!rhiDevice.CanSubmit()) return false;

        const auto& core = rhiDevice.GetNRICore();
        if (!rhiDevice.WaitIdle()) return false;
        if (m_NeedsRecreation) {
            const bool initialized = m_OwnsAllocator ? Init() : Init(m_CommandAllocator);
            if (!initialized) return false;
        }
        if (m_OwnsAllocator && core.ResetCommandAllocator) {
            core.ResetCommandAllocator(*m_CommandAllocator);
        }
        rhiDevice.GetResources().AbandonRecording(m_CommandBuffer);
        rhiDevice.GetResources().CollectAfterDrain();
        m_IsRecording = false;
        m_IsExecutable = false;
        return true;
    }

}
