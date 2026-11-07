#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <cstring>
#include <limits>

namespace Axle::RHI {
    struct RHIFrameContext::Storage {
        RHICommandBuffer Commands;
        RHIDescriptorPool Descriptors;
        RHIBuffer Uniform, Upload;
        unsigned char* UniformData = nullptr;
        unsigned char* UploadData = nullptr;
        uint64_t UniformUsed = 0, UploadUsed = 0;
        uint32_t UniformAlignment = 256;
        std::vector<RHIResourceLease> UniformViews;
        RHICompletionToken Completion, SubmissionFloor;
        bool Active = false, Sealed = false;
        void Unmap() {
            if (UniformData) Uniform.Unmap();
            if (UploadData) Upload.Unmap();
            UniformData = UploadData = nullptr;
        }
        ~Storage() { Unmap(); }
        RHIFrameRange Write(RHIBuffer& buffer, unsigned char* destination, uint64_t& used,
                            const void* data, uint64_t size, uint32_t alignment) {
            if (!Active || !Commands.IsRecording() || !destination || !data || !size || !alignment ||
                used > UINT64_MAX - (alignment - 1)) return {};
            const uint64_t offset = (used + alignment - 1) / alignment * alignment;
            if (offset > buffer.GetDesc().Size || size > buffer.GetDesc().Size - offset) return {};
            std::memcpy(destination + offset, data, static_cast<size_t>(size));
            used = offset + size;
            return {buffer.GetNative(), buffer.GetCBV(), offset, size};
        }
    };
    RHIFrameContext::RHIFrameContext() = default;
    RHIFrameContext::~RHIFrameContext() = default;
    RHIFrameContext::RHIFrameContext(RHIFrameContext&&) noexcept = default;
    RHIFrameContext& RHIFrameContext::operator=(RHIFrameContext&&) noexcept = default;
    bool RHIFrameContext::Init(uint32_t uniformBytes, uint32_t uploadBytes) {
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources() || !uniformBytes || !uploadBytes) return false;
        auto candidate = std::make_unique<Storage>();
        const auto& desc = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice());
        if (uniformBytes > desc.memory.constantBufferMaxRange) return false;
        candidate->UniformAlignment = desc.memoryAlignment.constantBufferOffset;
        if (!candidate->Commands.Init() || !candidate->Descriptors.Init() ||
            !candidate->Uniform.Init(RHIBufferDesc::Constant(uniformBytes, RHIMemoryLocation::HostUpload, "Frame uniforms"))) return false;
        RHIBufferDesc upload; upload.Size = uploadBytes; upload.MemoryLocation = RHIMemoryLocation::HostUpload;
        upload.DebugName = "Frame uploads";
        if (!candidate->Upload.Init(upload)) return false;
        m_Storage = std::move(candidate); return true;
    }
    bool RHIFrameContext::Begin(uint32_t timeoutMilliseconds) {
        auto& device = RHIDevice::Get();
        if (!m_Storage || m_Storage->Active || !device.CanSubmit()) return false;
        auto& frame = *m_Storage;
        if (frame.Completion && !device.WaitForCompletion(frame.Completion, timeoutMilliseconds)) return false;
        if (!frame.Commands.Reset()) return false; // Conservative whole-queue drain remains intentional.
        frame.UniformViews.clear();
        device.GetResources().CollectAfterDrain();
        RHIDescriptorPool descriptors;
        if (!descriptors.Init(frame.Descriptors.GetDesc())) return false;
        frame.Descriptors = std::move(descriptors);
        if (!device.GetResources().SetBufferState(frame.Uniform.AcquireLease(), {nri::AccessBits::HOST_WRITE, nri::StageBits::HOST}) ||
            !device.GetResources().SetBufferState(frame.Upload.AcquireLease(), {nri::AccessBits::HOST_WRITE, nri::StageBits::HOST})) return false;
        frame.UniformData = static_cast<unsigned char*>(frame.Uniform.Map());
        frame.UploadData = static_cast<unsigned char*>(frame.Upload.Map());
        if (!frame.UniformData || !frame.UploadData || !frame.Commands.Begin(frame.Descriptors.GetNative())) {
            frame.Unmap(); return false;
        }
        frame.UniformUsed = frame.UploadUsed = 0;
        frame.SubmissionFloor = device.GetLastSubmittedToken();
        frame.Completion = {}; frame.Active = true; frame.Sealed = false; return true;
    }
    RHIFrameRange RHIFrameContext::WriteUniform(const void* data, uint64_t size) {
        if (!m_Storage) return {};
        auto& frame = *m_Storage;
        auto range = frame.Write(frame.Uniform, frame.UniformData, frame.UniformUsed, data, size, frame.UniformAlignment);
        if (!range) return {};
        const auto alignment = frame.UniformAlignment;
        const auto viewSize = (size + alignment - 1) / alignment * alignment;
        if (viewSize > frame.Uniform.GetDesc().Size - range.Offset) return {};
        frame.UniformUsed = range.Offset + viewSize;
        nri::BufferViewDesc desc{}; desc.buffer = range.Buffer; desc.type = nri::BufferView::CONSTANT_BUFFER;
        desc.offset = range.Offset; desc.size = viewSize;
        auto& device = RHIDevice::Get(); const auto core = device.GetNRICore();
        nri::Descriptor* view = nullptr;
        if (ScopedRHIFault::Invoke("CreateFrameUniformView", [&] { return core.CreateBufferView(desc, view); }) != nri::Result::SUCCESS || !view) return {};
        auto lease = device.GetResources().Publish(RHIResourceKind::DescriptorSet, "Frame uniform range", {view},
            [core, view] { core.DestroyDescriptor(view); }, {frame.Uniform.AcquireLease()});
        if (!lease) { core.DestroyDescriptor(view); return {}; }
        frame.UniformViews.push_back(std::move(lease)); range.Descriptor = view; return range;
    }
    RHIFrameRange RHIFrameContext::WriteUpload(const void* data, uint64_t size, uint32_t alignment) {
        if (!m_Storage) return {};
        auto& frame = *m_Storage;
        return frame.Write(frame.Upload, frame.UploadData, frame.UploadUsed, data, size, alignment);
    }
    bool RHIFrameContext::End() {
        if (!m_Storage || !m_Storage->Active) return false;
        auto& frame = *m_Storage;
        frame.Unmap(); frame.Active = false;
        frame.Sealed = frame.Commands.End(); return frame.Sealed;
    }
    bool RHIFrameContext::Submit() {
        if (!m_Storage || !m_Storage->Sealed || !m_Storage->Commands.Submit()) return false;
        return AdoptSubmission(RHIDevice::Get().GetLastSubmittedToken());
    }
    bool RHIFrameContext::AdoptSubmission(RHICompletionToken token) {
        auto& device = RHIDevice::Get();
        if (!m_Storage || !m_Storage->Sealed || !token || token.Epoch != device.GetEpoch() ||
            token != device.GetLastSubmittedToken() || token.Value <= m_Storage->SubmissionFloor.Value ||
            m_Storage->Commands.IsExecutable()) return false;
        m_Storage->Completion = token; m_Storage->Sealed = false; return true;
    }
    bool RHIFrameContext::Abandon() {
        if (!m_Storage) return true;
        auto& frame = *m_Storage; frame.Unmap();
        if (!RHIDevice::Get().CanSubmit()) {
            if (!RHIDevice::Get().WaitIdle()) return false;
            frame.Commands.Shutdown(); // A stopped epoch cannot recycle a native allocator.
        } else if (!frame.Commands.Reset()) return false;
        frame.Active = frame.Sealed = false; return true;
    }
    RHICommandBuffer* RHIFrameContext::GetCommands() { return m_Storage ? &m_Storage->Commands : nullptr; }
    RHIDescriptorPool* RHIFrameContext::GetDescriptors() { return m_Storage ? &m_Storage->Descriptors : nullptr; }
    RHICompletionToken RHIFrameContext::GetCompletion() const { return m_Storage ? m_Storage->Completion : RHICompletionToken{}; }
}
