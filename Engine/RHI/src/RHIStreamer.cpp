#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    RHIStreamer::~RHIStreamer() {
        Shutdown();
    }

    RHIStreamer::RHIStreamer(RHIStreamer&& other) noexcept
        : m_Streamer(other.m_Streamer)
        , m_Desc(other.m_Desc)
        , m_Device(other.m_Device)
    {
        other.m_Streamer = nullptr;
        other.m_Device = nullptr;
    }

    RHIStreamer& RHIStreamer::operator=(RHIStreamer&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Streamer = other.m_Streamer;
            m_Desc = other.m_Desc;
            m_Device = other.m_Device;

            other.m_Streamer = nullptr;
            other.m_Device = nullptr;
        }
        return *this;
    }

    bool RHIStreamer::Init(const RHIStreamerDesc& desc) {
        return Init(RHIDevice::Get(), desc);
    }

    bool RHIStreamer::Init(RHIDevice& device, const RHIStreamerDesc& desc) {
        Shutdown();

        if (!device.IsInitialized()) {
            AXLE_CORE_ERROR("RHIStreamer::Init failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = device.GetNRIDevice();
        const auto& streamerInterface = device.GetNRIStreamer();

        if (!nativeDevice || !streamerInterface.CreateStreamer) {
            AXLE_CORE_ERROR("RHIStreamer::Init failed: nri::StreamerInterface not available on active backend!");
            return false;
        }

        nri::StreamerDesc streamerDesc = {};
        streamerDesc.constantBufferMemoryLocation = ToNRIMemoryLocation(desc.ConstantMemoryLocation);
        streamerDesc.constantBufferSize = desc.ConstantBufferSize;
        streamerDesc.dynamicBufferMemoryLocation = ToNRIMemoryLocation(desc.DynamicMemoryLocation);
        streamerDesc.dynamicBufferDesc.usage = ToNRIBufferUsage(desc.DynamicBufferUsage);
        streamerDesc.dynamicBufferDesc.structureStride = 0;
        streamerDesc.dynamicBufferDesc.size = 0;
        streamerDesc.queuedFrameNum = desc.QueuedFrameNum;
        streamerDesc.hostDataCapacity = desc.HostDataCapacity;

        nri::Result res = streamerInterface.CreateStreamer(*nativeDevice, streamerDesc, m_Streamer);
        if (res != nri::Result::SUCCESS || !m_Streamer) {
            AXLE_CORE_ERROR("RHIStreamer::Init: CreateStreamer failed with code {0}!", static_cast<uint32_t>(res));
            m_Streamer = nullptr;
            return false;
        }

        m_Desc = desc;
        m_Device = &device;

        AXLE_CORE_INFO("RHIStreamer successfully initialized (ConstantRing: {0} MB, FramesInFlight: {1}).",
            desc.ConstantBufferSize / (1024 * 1024), desc.QueuedFrameNum);
        return true;
    }

    void RHIStreamer::Shutdown() {
        if (m_Streamer && m_Device) {
            const auto& streamerInterface = m_Device->GetNRIStreamer();
            if (streamerInterface.DestroyStreamer) {
                streamerInterface.DestroyStreamer(m_Streamer);
            }
        }
        m_Streamer = nullptr;
        m_Device = nullptr;
    }

    nri::Buffer* RHIStreamer::GetConstantBuffer() const {
        if (!m_Streamer || !m_Device) return nullptr;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.GetStreamerConstantBuffer) return nullptr;
        return streamerInterface.GetStreamerConstantBuffer(*m_Streamer);
    }

    uint32_t RHIStreamer::StreamConstantData(const void* data, uint32_t size) {
        if (!m_Streamer || !m_Device || !data || size == 0) return 0;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamConstantData) return 0;
        return streamerInterface.StreamConstantData(*m_Streamer, data, size);
    }

    void* RHIStreamer::StreamHostData(const void* data, uint64_t size, uint32_t alignment) {
        if (!m_Streamer || !m_Device || !data || size == 0) return nullptr;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamHostData) return nullptr;
        return streamerInterface.StreamHostData(*m_Streamer, data, size, alignment);
    }

    RHICopyBatch RHIStreamer::BeginCopyBatch() {
        if (!m_Streamer || !m_Device) return 0;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.BeginStreamerCopyBatch) return 0;
        return streamerInterface.BeginStreamerCopyBatch(*m_Streamer);
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferData(const void* data, uint64_t size, uint32_t alignment) {
        if (!m_Streamer || !m_Device || !data || size == 0) return {};
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamBufferData) return {};

        nri::DataSize chunk = { data, size };
        nri::StreamBufferDataDesc desc = {};
        desc.dataChunks = &chunk;
        desc.dataChunkNum = 1;
        desc.placementAlignment = alignment;
        desc.copyBatch = 0;
        desc.dstBuffer = nullptr;
        desc.dstOffset = 0;

        nri::BufferOffset bo = streamerInterface.StreamBufferData(*m_Streamer, desc);
        return { bo.buffer, bo.offset };
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferUpload(RHICopyBatch copyBatch, nri::Buffer* dstBuffer, uint64_t dstOffset,
                                                    const void* data, uint64_t size, uint32_t alignment)
    {
        if (!m_Streamer || !m_Device || !dstBuffer || !data || size == 0 || copyBatch == 0) return {};
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamBufferData) return {};

        nri::DataSize chunk = { data, size };
        nri::StreamBufferDataDesc desc = {};
        desc.dataChunks = &chunk;
        desc.dataChunkNum = 1;
        desc.placementAlignment = alignment;
        desc.copyBatch = copyBatch;
        desc.dstBuffer = dstBuffer;
        desc.dstOffset = dstOffset;

        nri::BufferOffset bo = streamerInterface.StreamBufferData(*m_Streamer, desc);
        return { bo.buffer, bo.offset };
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferUpload(RHICopyBatch copyBatch, RHIBuffer& dstBuffer, uint64_t dstOffset,
                                                    const void* data, uint64_t size, uint32_t alignment)
    {
        return StreamBufferUpload(copyBatch, dstBuffer.GetNative(), dstOffset, data, size, alignment);
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferUploadChunks(RHICopyBatch copyBatch, nri::Buffer* dstBuffer, uint64_t dstOffset,
                                                          const std::vector<RHIStreamChunk>& chunks, uint32_t alignment)
    {
        if (!m_Streamer || !m_Device || !dstBuffer || chunks.empty() || copyBatch == 0) return {};
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamBufferData) return {};

        std::vector<nri::DataSize> nriChunks;
        nriChunks.reserve(chunks.size());
        for (const auto& c : chunks) {
            nriChunks.push_back({ c.Data, c.Size });
        }

        nri::StreamBufferDataDesc desc = {};
        desc.dataChunks = nriChunks.data();
        desc.dataChunkNum = static_cast<uint32_t>(nriChunks.size());
        desc.placementAlignment = alignment;
        desc.copyBatch = copyBatch;
        desc.dstBuffer = dstBuffer;
        desc.dstOffset = dstOffset;

        nri::BufferOffset bo = streamerInterface.StreamBufferData(*m_Streamer, desc);
        return { bo.buffer, bo.offset };
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferUploadChunks(RHICopyBatch copyBatch, RHIBuffer& dstBuffer, uint64_t dstOffset,
                                                          const std::vector<RHIStreamChunk>& chunks, uint32_t alignment)
    {
        return StreamBufferUploadChunks(copyBatch, dstBuffer.GetNative(), dstOffset, chunks, alignment);
    }

    RHIStreamedBuffer RHIStreamer::StreamTextureData(const RHIStreamTextureDesc& desc) {
        if (!m_Streamer || !m_Device || !desc.DstTexture || !desc.Data || desc.CopyBatch == 0) return {};
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamTextureData) return {};

        nri::StreamTextureDataDesc nriDesc = {};
        nriDesc.data = desc.Data;
        nriDesc.dataRowPitch = desc.DataRowPitch;
        nriDesc.dataSlicePitch = desc.DataSlicePitch;
        nriDesc.copyBatch = desc.CopyBatch;
        nriDesc.dstTexture = desc.DstTexture;

        nriDesc.dstRegion.x = static_cast<nri::Dim_t>(desc.X);
        nriDesc.dstRegion.y = static_cast<nri::Dim_t>(desc.Y);
        nriDesc.dstRegion.z = static_cast<nri::Dim_t>(desc.Z);
        nriDesc.dstRegion.width = (desc.Width == 0) ? nri::WHOLE_SIZE : static_cast<nri::Dim_t>(desc.Width);
        nriDesc.dstRegion.height = (desc.Height == 0) ? nri::WHOLE_SIZE : static_cast<nri::Dim_t>(desc.Height);
        nriDesc.dstRegion.depth = (desc.Depth == 0) ? nri::WHOLE_SIZE : static_cast<nri::Dim_t>(desc.Depth);
        nriDesc.dstRegion.mipOffset = static_cast<nri::Dim_t>(desc.MipOffset);
        nriDesc.dstRegion.layerOffset = static_cast<nri::Dim_t>(desc.LayerOffset);
        nriDesc.dstRegion.planes = nri::PlaneBits::ALL;

        nri::BufferOffset bo = streamerInterface.StreamTextureData(*m_Streamer, nriDesc);
        return { bo.buffer, bo.offset };
    }

    void RHIStreamer::CmdCopyStreamedData(nri::CommandBuffer& cmdBuffer, RHICopyBatch copyBatch) {
        if (!m_Streamer || !m_Device || copyBatch == 0) return;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (streamerInterface.CmdCopyStreamedData) {
            streamerInterface.CmdCopyStreamedData(cmdBuffer, *m_Streamer, copyBatch);
        }
    }

    void RHIStreamer::CmdCopyStreamedData(RHICommandBuffer& cmdBuffer, RHICopyBatch copyBatch) {
        if (cmdBuffer.GetNative()) {
            CmdCopyStreamedData(*cmdBuffer.GetNative(), copyBatch);
        }
    }

    void RHIStreamer::EndFrame() {
        if (!m_Streamer || !m_Device) return;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (streamerInterface.EndStreamerFrame) {
            streamerInterface.EndStreamerFrame(*m_Streamer);
        }
    }

}
