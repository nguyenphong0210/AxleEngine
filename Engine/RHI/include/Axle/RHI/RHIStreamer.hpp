#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIBuffer.hpp>

#include <NRI.h>
#include <Extensions/NRIStreamer.h>

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

#ifdef _WIN32
#undef min
#undef max
#endif

namespace Axle::RHI {

    class RHICommandBuffer;

    // Identifier for a batch of destination copy requests scoped to an RHIStreamer (0 is invalid)
    using RHICopyBatch = uint64_t;

    struct RHIStreamerDesc {
        // Statically allocated ring-buffer for dynamic uniforms / constants
        RHIMemoryLocation ConstantMemoryLocation = RHIMemoryLocation::HostUpload;
        uint64_t ConstantBufferSize = 16 * 1024 * 1024; // 16 MB ring buffer for uniform constants

        // Dynamically grown ring-buffer for staging uploads, immediate drawing, and indirect arguments
        RHIMemoryLocation DynamicMemoryLocation = RHIMemoryLocation::HostUpload;
        RHIBufferUsage DynamicBufferUsage = RHIBufferUsage::Vertex | RHIBufferUsage::Index | RHIBufferUsage::ShaderResource | RHIBufferUsage::Argument;
        uint32_t QueuedFrameNum = 3; // Frames-in-flight (matches swapchain synchronization)

        // Statically allocated read/write ring-buffer for generic CPU data
        uint64_t HostDataCapacity = 4 * 1024 * 1024; // 4 MB CPU host data ring
    };

    struct RHIStreamedBuffer {
        nri::Buffer* Buffer = nullptr;
        uint64_t Offset = 0;

        bool IsValid() const { return Buffer != nullptr; }
        operator bool() const { return IsValid(); }
    };

    struct RHIStreamChunk {
        const void* Data = nullptr;
        uint64_t Size = 0;
    };

    struct RHIStreamTextureDesc {
        const void* Data = nullptr;
        uint32_t DataRowPitch = 0;
        uint32_t DataSlicePitch = 0;

        RHICopyBatch CopyBatch = 0;
        nri::Texture* DstTexture = nullptr;

        uint16_t MipOffset = 0;
        uint16_t LayerOffset = 0;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Z = 0;
        uint32_t Width = 0;  // 0 = full texture mip width (WHOLE_SIZE)
        uint32_t Height = 0; // 0 = full texture mip height (WHOLE_SIZE)
        uint32_t Depth = 1;
    };

    class RHIStreamer {
    public:
        RHIStreamer() = default;
        ~RHIStreamer();

        RHIStreamer(const RHIStreamer&) = delete;
        RHIStreamer& operator=(const RHIStreamer&) = delete;

        RHIStreamer(RHIStreamer&& other) noexcept;
        RHIStreamer& operator=(RHIStreamer&& other) noexcept;

        // Initialize streamer with active RHIDevice singleton
        bool Init(const RHIStreamerDesc& desc = RHIStreamerDesc());

        // Initialize streamer with custom RHIDevice
        bool Init(RHIDevice& device, const RHIStreamerDesc& desc = RHIStreamerDesc());

        // Release streamer resources
        void Shutdown();

        bool IsValid() const { return m_Streamer != nullptr; }
        nri::Streamer* GetNative() const { return m_Streamer; }
        operator nri::Streamer*() const { return m_Streamer; }
        const RHIStreamerDesc& GetDesc() const { return m_Desc; }

        // Statically allocated constant ring-buffer
        nri::Buffer* GetConstantBuffer() const;

        // Streams data into the constant ring buffer. Returns byte offset in GetConstantBuffer()
        uint32_t StreamConstantData(const void* data, uint32_t size);

        template <typename T>
        uint32_t StreamConstant(const T& val) {
            return StreamConstantData(&val, static_cast<uint32_t>(sizeof(T)));
        }

        // Streams generic CPU host data. Returns valid pointer until wrap-around
        void* StreamHostData(const void* data, uint64_t size, uint32_t alignment = 16);

        // Begin a batch of destination copy requests (consumed by CmdCopyStreamedData)
        RHICopyBatch BeginCopyBatch();

        // Stream data to dynamic ring buffer for immediate drawing/binding without copy
        RHIStreamedBuffer StreamBufferData(const void* data, uint64_t size, uint32_t alignment = 16);

        // Stream data into dynamic staging buffer and enqueue copy to destination buffer in copyBatch
        RHIStreamedBuffer StreamBufferUpload(RHICopyBatch copyBatch, nri::Buffer* dstBuffer, uint64_t dstOffset, const void* data, uint64_t size, uint32_t alignment = 16);
        RHIStreamedBuffer StreamBufferUpload(RHICopyBatch copyBatch, RHIBuffer& dstBuffer, uint64_t dstOffset, const void* data, uint64_t size, uint32_t alignment = 16);

        // Stream multiple data chunks concatenated into dynamic staging buffer and enqueue copy
        RHIStreamedBuffer StreamBufferUploadChunks(RHICopyBatch copyBatch, nri::Buffer* dstBuffer, uint64_t dstOffset, const std::vector<RHIStreamChunk>& chunks, uint32_t alignment = 16);
        RHIStreamedBuffer StreamBufferUploadChunks(RHICopyBatch copyBatch, RHIBuffer& dstBuffer, uint64_t dstOffset, const std::vector<RHIStreamChunk>& chunks, uint32_t alignment = 16);

        // Stream texture mipmap/subresource data into staging ring buffer and enqueue copy
        RHIStreamedBuffer StreamTextureData(const RHIStreamTextureDesc& desc);

        // Record copies of all batched staging requests into command buffer (destinations must be in CopyDest state)
        void CmdCopyStreamedData(nri::CommandBuffer& cmdBuffer, RHICopyBatch copyBatch);
        void CmdCopyStreamedData(RHICommandBuffer& cmdBuffer, RHICopyBatch copyBatch);

        // Must be called once at the end of every frame (advances ring buffer slot & recycles garbage)
        void EndFrame();

    private:
        nri::Streamer* m_Streamer = nullptr;
        RHIStreamerDesc m_Desc;
        RHIDevice* m_Device = nullptr;
    };

}
