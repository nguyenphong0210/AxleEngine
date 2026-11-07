#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <cstring>
#include <algorithm>

namespace Axle::RHI {

    RHIStreamer::~RHIStreamer() {
        Shutdown();
    }

    RHIStreamer::RHIStreamer(RHIStreamer&& other) noexcept
        : m_Lease(std::move(other.m_Lease))
        , m_CopyResources(std::move(other.m_CopyResources))
        , m_Backings(std::move(other.m_Backings))
        , m_Streamer(other.m_Streamer)
        , m_Desc(other.m_Desc)
        , m_Device(other.m_Device)
        , m_ConstantEnd(other.m_ConstantEnd), m_HostUsed(other.m_HostUsed)
        , m_CanWrapConstants(other.m_CanWrapConstants), m_HostBlocks(std::move(other.m_HostBlocks))
    {
        other.m_Streamer = nullptr;
        other.m_Device = nullptr;
    }

    RHIStreamer& RHIStreamer::operator=(RHIStreamer&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Lease = std::move(other.m_Lease);
            m_CopyResources = std::move(other.m_CopyResources);
            m_Backings = std::move(other.m_Backings);
            m_Streamer = other.m_Streamer;
            m_Desc = other.m_Desc;
            m_Device = other.m_Device;
            m_ConstantEnd = other.m_ConstantEnd; m_HostUsed = other.m_HostUsed;
            m_CanWrapConstants = other.m_CanWrapConstants; m_HostBlocks = std::move(other.m_HostBlocks);

            other.m_Streamer = nullptr;
            other.m_Device = nullptr;
        }
        return *this;
    }

    bool RHIStreamer::Init(const RHIStreamerDesc& desc) {
        return Init(RHIDevice::Get(), desc);
    }

    bool RHIStreamer::Init(RHIDevice& device, const RHIStreamerDesc& desc) {
        RHIStreamer candidate;
        if (!candidate.Build(device, desc)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIStreamer::Build(RHIDevice& device, const RHIStreamerDesc& desc) {

        if (!device.CanCreateResources() || desc.ConstantBufferSize > UINT32_MAX) {
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
        auto destroy = streamerInterface.DestroyStreamer; auto streamer = m_Streamer;
        m_Lease = device.GetResources().Publish(RHIResourceKind::Streamer, "Streamer", {streamer},
            [destroy, streamer] { destroy(streamer); });
        if (!m_Lease.IsLive()) return false;

        AXLE_CORE_INFO("RHIStreamer successfully initialized (ConstantRing: {0} MB, FramesInFlight: {1}).",
            desc.ConstantBufferSize / (1024 * 1024), desc.QueuedFrameNum);
        return true;
    }

    void RHIStreamer::Shutdown() {
        m_HostBlocks.clear(); m_HostUsed = m_ConstantEnd = 0; m_CanWrapConstants = true;
        m_CopyResources.clear();
        m_Backings.clear();
        if (m_Lease) m_Lease.Reset();
        else if (m_Streamer && m_Device) {
            const auto& streamerInterface = m_Device->GetNRIStreamer();
            if (streamerInterface.DestroyStreamer) {
                auto destroy = streamerInterface.DestroyStreamer; auto streamer = m_Streamer;
                m_Device->ReleaseNative([destroy, streamer] { destroy(streamer); });
            }
        }
        m_Streamer = nullptr;
        m_Device = nullptr;
    }

    nri::Buffer* RHIStreamer::GetConstantBuffer() const {
        if (!IsValid() || !m_Device) return nullptr;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.GetStreamerConstantBuffer) return nullptr;
        auto* buffer = streamerInterface.GetStreamerConstantBuffer(*m_Streamer);
        return RegisterBuffer(buffer) ? buffer : nullptr;
    }

    uint32_t RHIStreamer::StreamConstantData(const void* data, uint32_t size) {
        if (!IsValid() || !m_Device || !m_Device->CanSubmit() || !data || !size || size > m_Desc.ConstantBufferSize) return UINT32_MAX;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.StreamConstantData) return UINT32_MAX;
        const auto alignment = m_Device->GetNRICore().GetDeviceDesc(*m_Device->GetNRIDevice()).memoryAlignment.constantBufferOffset;
        const auto offset = (m_ConstantEnd + alignment - 1) / alignment * alignment;
        if ((offset > m_Desc.ConstantBufferSize || size > m_Desc.ConstantBufferSize - offset) && !m_CanWrapConstants) return UINT32_MAX;
        const auto actual = streamerInterface.StreamConstantData(*m_Streamer, data, size);
        m_ConstantEnd = static_cast<uint64_t>(actual) + size; m_CanWrapConstants = false;
        return actual;
    }

    void* RHIStreamer::StreamHostData(const void* data, uint64_t size, uint32_t alignment) {
        if (!IsValid() || !m_Device || !data || size == 0) return nullptr;
        alignment = (std::max)(alignment, 1u);
        if (m_HostUsed > m_Desc.HostDataCapacity || alignment - 1 > m_Desc.HostDataCapacity - m_HostUsed ||
            size > m_Desc.HostDataCapacity - m_HostUsed - (alignment - 1)) return nullptr;
        const auto allocation = size + alignment - 1;
        auto block = std::make_unique<unsigned char[]>(static_cast<size_t>(allocation));
        const auto address = reinterpret_cast<uintptr_t>(block.get());
        auto* result = block.get() + ((alignment - address % alignment) % alignment);
        std::memcpy(result, data, static_cast<size_t>(size));
        m_HostBlocks.push_back(std::move(block)); m_HostUsed += allocation; return result;
    }

    RHICopyBatch RHIStreamer::BeginCopyBatch() {
        if (!IsValid() || !m_Device) return 0;
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (!streamerInterface.BeginStreamerCopyBatch) return 0;
        RHICopyBatch batch = 0;
        ScopedRHIFault::Invoke("BeginStreamerCopyBatch", [&] {
            batch = streamerInterface.BeginStreamerCopyBatch(*m_Streamer);
            return batch ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        return batch;
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferData(const void* data, uint64_t size, uint32_t alignment) {
        if (!IsValid() || !m_Device || !data || size == 0) return {};
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

        nri::BufferOffset bo{};
        ScopedRHIFault::Invoke("StreamBufferData", [&] {
            bo = streamerInterface.StreamBufferData(*m_Streamer, desc);
            return bo.buffer ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        if (bo.buffer && !RegisterBuffer(bo.buffer)) return {};
        return { bo.buffer, bo.offset };
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferUpload(RHICopyBatch copyBatch, nri::Buffer* dstBuffer, uint64_t dstOffset,
                                                    const void* data, uint64_t size, uint32_t alignment)
    {
        if (!IsValid() || !m_Device || !dstBuffer || !data || size == 0 || copyBatch == 0) return {};
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

        nri::BufferOffset bo{};
        ScopedRHIFault::Invoke("StreamBufferData", [&] {
            bo = streamerInterface.StreamBufferData(*m_Streamer, desc);
            return bo.buffer ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        if (bo.buffer && !RegisterBuffer(bo.buffer)) return {};
        if (bo.buffer) {
            auto destination = m_Device->GetResources().AcquireNative(dstBuffer);
            if (destination.IsLive()) m_CopyResources[copyBatch].push_back(std::move(destination));
        }
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
        if (!IsValid() || !m_Device || !dstBuffer || chunks.empty() || copyBatch == 0) return {};
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

        nri::BufferOffset bo{};
        ScopedRHIFault::Invoke("StreamBufferData", [&] {
            bo = streamerInterface.StreamBufferData(*m_Streamer, desc);
            return bo.buffer ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        if (bo.buffer && !RegisterBuffer(bo.buffer)) return {};
        if (bo.buffer) {
            auto destination = m_Device->GetResources().AcquireNative(dstBuffer);
            if (destination.IsLive()) m_CopyResources[copyBatch].push_back(std::move(destination));
        }
        return { bo.buffer, bo.offset };
    }

    RHIStreamedBuffer RHIStreamer::StreamBufferUploadChunks(RHICopyBatch copyBatch, RHIBuffer& dstBuffer, uint64_t dstOffset,
                                                          const std::vector<RHIStreamChunk>& chunks, uint32_t alignment)
    {
        return StreamBufferUploadChunks(copyBatch, dstBuffer.GetNative(), dstOffset, chunks, alignment);
    }

    RHIStreamedBuffer RHIStreamer::StreamTextureData(const RHIStreamTextureDesc& desc) {
        if (!IsValid() || !m_Device || !desc.DstTexture || !desc.Data || desc.CopyBatch == 0) return {};
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
        if (bo.buffer && !RegisterBuffer(bo.buffer)) return {};
        if (bo.buffer) {
            auto destination = m_Device->GetResources().AcquireNative(desc.DstTexture);
            if (destination.IsLive()) m_CopyResources[desc.CopyBatch].push_back(std::move(destination));
        }
        return { bo.buffer, bo.offset };
    }

    void RHIStreamer::CmdCopyStreamedData(nri::CommandBuffer& cmdBuffer, RHICopyBatch copyBatch) {
        if (!IsValid() || !m_Device || copyBatch == 0) return;
        auto& resources = m_Device->GetResources();
        if (!resources.RecordNativeUse(&cmdBuffer, m_Streamer)) return;
        auto found = m_CopyResources.find(copyBatch);
        if (found != m_CopyResources.end() && resources.AcquireNative(&cmdBuffer))
            for (const auto& resource : found->second) if (!resources.RecordUse(&cmdBuffer, resource)) return;
        std::vector<nri::TextureBarrierDesc> textures;
        std::vector<nri::BufferBarrierDesc> buffers;
        if (found != m_CopyResources.end()) for (const auto& resource : found->second) {
            const auto pinned = resources.Pin(resource);
            for (auto* texture : resources.GetPinnedTextures(pinned)) {
                nri::TextureBarrierDesc request{}; request.texture = texture;
                request.after = {nri::AccessBits::COPY_DESTINATION, nri::Layout::COPY_DESTINATION, nri::StageBits::COPY};
                request.mipNum = request.layerNum = nri::REMAINING;
                if (!resources.BuildTextureBarriers(&cmdBuffer, request, textures)) { resources.EndRecording(&cmdBuffer, false); return; }
            }
            for (auto* buffer : resources.GetPinnedBuffers(pinned)) {
                nri::BufferBarrierDesc request{}; request.buffer = buffer;
                request.after = {nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY};
                if (!resources.BuildBufferBarrier(&cmdBuffer, request, buffers)) { resources.EndRecording(&cmdBuffer, false); return; }
            }
        }
        nri::BarrierDesc barriers{}; barriers.textures = textures.data(); barriers.textureNum = static_cast<uint32_t>(textures.size());
        barriers.buffers = buffers.data(); barriers.bufferNum = static_cast<uint32_t>(buffers.size());
        if (barriers.textureNum || barriers.bufferNum) m_Device->GetNRICore().CmdBarrier(cmdBuffer, barriers);
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (streamerInterface.CmdCopyStreamedData) {
            streamerInterface.CmdCopyStreamedData(cmdBuffer, *m_Streamer, copyBatch);
            m_CopyResources.erase(copyBatch);
        }
    }

    void RHIStreamer::CmdCopyStreamedData(RHICommandBuffer& cmdBuffer, RHICopyBatch copyBatch) {
        if (cmdBuffer.GetNative()) {
            CmdCopyStreamedData(*cmdBuffer.GetNative(), copyBatch);
        }
    }

    bool RHIStreamer::EndFrame() {
        if (!IsValid() || !m_Device) return false;
        if (!m_Device->WaitIdle() || m_Device->GetResources().IsInUse(m_Lease)) return false;
        m_Backings.clear();
        const auto& streamerInterface = m_Device->GetNRIStreamer();
        if (streamerInterface.EndStreamerFrame) {
            streamerInterface.EndStreamerFrame(*m_Streamer);
            m_CopyResources.clear(); // Native retirement also discards unrecorded copy batches.
            m_CanWrapConstants = true; m_HostBlocks.clear(); m_HostUsed = 0;
            return true;
        }
        return false;
    }

    bool RHIStreamer::RegisterBuffer(nri::Buffer* buffer) const {
        if (!buffer || !IsValid() || !m_Device) return false;
        auto& resources = m_Device->GetResources();
        auto existing = resources.AcquireNative(buffer);
        if (existing.IsLive()) return resources.GetBufferState(existing.GetHandle()).has_value();
        auto lease = resources.Publish(RHIResourceKind::Buffer, "Streamer backing", {buffer}, {}, {m_Lease});
        if (!lease.IsLive() || !resources.DefineBufferState(lease, {nri::AccessBits::HOST_WRITE, nri::StageBits::HOST})) return false;
        m_Backings[buffer] = std::move(lease);
        return true;
    }

}
