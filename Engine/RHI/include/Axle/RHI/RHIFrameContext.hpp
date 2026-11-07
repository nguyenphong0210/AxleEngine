#pragma once
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <memory>

namespace Axle::RHI {
    struct RHIFrameRange {
        nri::Buffer* Buffer = nullptr;
        // Uniform descriptors already include Offset; bind with dynamic offset 0.
        nri::Descriptor* Descriptor = nullptr;
        uint64_t Offset = 0, Size = 0;
        explicit operator bool() const { return Buffer != nullptr; }
    };

    // A slot owns every mutable arena it exposes. CPU writes are append-only
    // during recording and sealed before submission. Reuse requires completion.
    class RHIFrameContext {
    public:
        RHIFrameContext();
        ~RHIFrameContext();
        RHIFrameContext(RHIFrameContext&&) noexcept;
        RHIFrameContext& operator=(RHIFrameContext&&) noexcept;
        RHIFrameContext(const RHIFrameContext&) = delete;
        RHIFrameContext& operator=(const RHIFrameContext&) = delete;
        bool Init(uint32_t uniformBytes = 65536, uint32_t uploadBytes = 1048576);
        bool Begin(uint32_t timeoutMilliseconds = 2000);
        RHIFrameRange WriteUniform(const void* data, uint64_t size);
        RHIFrameRange WriteUpload(const void* data, uint64_t size, uint32_t alignment = 16);
        bool End();
        bool Submit();
        bool AdoptSubmission(RHICompletionToken token);
        bool Abandon();
        RHICommandBuffer* GetCommands();
        RHIDescriptorPool* GetDescriptors();
        RHICompletionToken GetCompletion() const;
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
