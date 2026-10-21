#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/Log.hpp>

#include <cstring>

namespace Axle::RHI {

    nri::BufferUsageBits ToNRIBufferUsage(RHIBufferUsage usage) {
        uint16_t bits = 0;
        if ((usage & RHIBufferUsage::Vertex) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::VERTEX);
        }
        if ((usage & RHIBufferUsage::Index) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::INDEX);
        }
        if ((usage & RHIBufferUsage::Constant) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::CONSTANT);
        }
        if ((usage & RHIBufferUsage::ShaderResource) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::SHADER_RESOURCE);
        }
        if ((usage & RHIBufferUsage::ShaderResourceStorage) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::SHADER_RESOURCE_STORAGE);
        }
        if ((usage & RHIBufferUsage::Argument) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::ARGUMENT);
        }
        if ((usage & RHIBufferUsage::Scratch) != RHIBufferUsage::None) {
            bits |= static_cast<uint16_t>(nri::BufferUsageBits::SCRATCH);
        }
        return static_cast<nri::BufferUsageBits>(bits);
    }

    nri::MemoryLocation ToNRIMemoryLocation(RHIMemoryLocation location) {
        switch (location) {
            case RHIMemoryLocation::Device:         return nri::MemoryLocation::DEVICE;
            case RHIMemoryLocation::DeviceUpload:   return nri::MemoryLocation::DEVICE_UPLOAD;
            case RHIMemoryLocation::HostUpload:     return nri::MemoryLocation::HOST_UPLOAD;
            case RHIMemoryLocation::HostReadback:   return nri::MemoryLocation::HOST_READBACK;
            default:                                return nri::MemoryLocation::DEVICE;
        }
    }

    // =========================================================================
    // RHIBuffer Implementation
    // =========================================================================

    RHIBuffer::~RHIBuffer() {
        Shutdown();
    }

    RHIBuffer::RHIBuffer(RHIBuffer&& other) noexcept
        : m_Desc(std::move(other.m_Desc))
        , m_Buffer(other.m_Buffer)
        , m_Memory(other.m_Memory)
        , m_IsCommitted(other.m_IsCommitted)
        , m_MappedData(other.m_MappedData)
    {
        other.m_Buffer = nullptr;
        other.m_Memory = nullptr;
        other.m_IsCommitted = false;
        other.m_MappedData = nullptr;
    }

    RHIBuffer& RHIBuffer::operator=(RHIBuffer&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Desc = std::move(other.m_Desc);
            m_Buffer = other.m_Buffer;
            m_Memory = other.m_Memory;
            m_IsCommitted = other.m_IsCommitted;
            m_MappedData = other.m_MappedData;

            other.m_Buffer = nullptr;
            other.m_Memory = nullptr;
            other.m_IsCommitted = false;
            other.m_MappedData = nullptr;
        }
        return *this;
    }

    bool RHIBuffer::Init(const RHIBufferDesc& desc, const void* initialData) {
        Shutdown();

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHIBuffer::Init failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& core = rhiDevice.GetNRICore();
        const auto& helper = rhiDevice.GetNRIHelper();
        auto* queue = rhiDevice.GetGraphicsQueue();

        if (!nativeDevice) return false;

        m_Desc = desc;

        nri::BufferDesc bufDesc = {};
        bufDesc.size = m_Desc.Size;
        bufDesc.structureStride = m_Desc.StructureStride;
        bufDesc.usage = ToNRIBufferUsage(m_Desc.Usage);
        bufDesc.byteAddress = m_Desc.ByteAddress;

        nri::MemoryLocation memLoc = ToNRIMemoryLocation(m_Desc.MemoryLocation);

        nri::Result res = core.CreateCommittedBuffer(*nativeDevice, memLoc, 1.0f, bufDesc, m_Buffer);
        if (res != nri::Result::SUCCESS || !m_Buffer) {
            AXLE_CORE_ERROR("RHIBuffer::Init: CreateCommittedBuffer failed with code {0}!", static_cast<uint32_t>(res));
            return false;
        }

        m_IsCommitted = true;

        if (!m_Desc.DebugName.empty() && core.SetDebugName) {
            core.SetDebugName((nri::Object*)m_Buffer, m_Desc.DebugName.c_str());
        }

        // Upload initial data if provided
        if (initialData != nullptr) {
            if (m_Desc.MemoryLocation == RHIMemoryLocation::HostUpload || m_Desc.MemoryLocation == RHIMemoryLocation::DeviceUpload) {
                void* ptr = Map(0, m_Desc.Size);
                if (ptr) {
                    std::memcpy(ptr, initialData, static_cast<size_t>(m_Desc.Size));
                    Unmap();
                }
            } else if (m_Desc.MemoryLocation == RHIMemoryLocation::Device && queue && helper.UploadData) {
                nri::BufferUploadDesc upDesc = {};
                upDesc.buffer = m_Buffer;
                upDesc.data = initialData;
                upDesc.after.access = (m_Desc.Usage & RHIBufferUsage::Vertex) != RHIBufferUsage::None
                    ? nri::AccessBits::VERTEX_BUFFER
                    : nri::AccessBits::SHADER_RESOURCE;
                upDesc.after.stages = nri::StageBits::ALL;
                helper.UploadData(*queue, nullptr, 0, &upDesc, 1);
            }
        }

        return true;
    }

    bool RHIBuffer::InitPlaced(const RHIBufferDesc& desc, nri::Memory* memory, uint64_t memoryOffset) {
        Shutdown();

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHIBuffer::InitPlaced failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& core = rhiDevice.GetNRICore();
        if (!nativeDevice || !memory) return false;

        m_Desc = desc;
        m_Memory = memory;
        m_IsCommitted = false;

        nri::BufferDesc bufDesc = {};
        bufDesc.size = m_Desc.Size;
        bufDesc.structureStride = m_Desc.StructureStride;
        bufDesc.usage = ToNRIBufferUsage(m_Desc.Usage);
        bufDesc.byteAddress = m_Desc.ByteAddress;

        nri::Result res = core.CreatePlacedBuffer(*nativeDevice, memory, memoryOffset, bufDesc, m_Buffer);
        if (res != nri::Result::SUCCESS || !m_Buffer) {
            AXLE_CORE_ERROR("RHIBuffer::InitPlaced: CreatePlacedBuffer failed with code {0}!", static_cast<uint32_t>(res));
            m_Memory = nullptr;
            return false;
        }

        if (!m_Desc.DebugName.empty() && core.SetDebugName) {
            core.SetDebugName((nri::Object*)m_Buffer, m_Desc.DebugName.c_str());
        }

        return true;
    }

    bool RHIBuffer::InitAdopt(const RHIBufferDesc& desc, nri::Buffer* existingBuffer, nri::Memory* memory) {
        Shutdown();

        if (!existingBuffer) return false;

        m_Desc = desc;
        m_Buffer = existingBuffer;
        m_Memory = memory;
        m_IsCommitted = false;

        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.IsInitialized() && !m_Desc.DebugName.empty()) {
            const auto& core = rhiDevice.GetNRICore();
            if (core.SetDebugName) {
                core.SetDebugName((nri::Object*)m_Buffer, m_Desc.DebugName.c_str());
            }
        }

        return true;
    }

    void RHIBuffer::Shutdown() {
        if (m_MappedData) {
            Unmap();
        }

        auto& rhiDevice = RHIDevice::Get();
        if (rhiDevice.IsInitialized()) {
            const auto& core = rhiDevice.GetNRICore();
            if (m_Buffer && core.DestroyBuffer) {
                core.DestroyBuffer(m_Buffer);
            }
            if (m_Memory && !m_IsCommitted && core.FreeMemory) {
                core.FreeMemory(m_Memory);
            }
        }

        m_Buffer = nullptr;
        m_Memory = nullptr;
        m_IsCommitted = false;
        m_MappedData = nullptr;
    }

    void* RHIBuffer::Map(uint64_t offset, uint64_t size) {
        if (!m_Buffer) return nullptr;
        if (m_MappedData) return static_cast<uint8_t*>(m_MappedData) + offset;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (!core.MapBuffer) return nullptr;

        uint64_t mapSize = (size == 0) ? (m_Desc.Size - offset) : size;
        m_MappedData = core.MapBuffer(*m_Buffer, offset, mapSize);
        return m_MappedData;
    }

    void RHIBuffer::Unmap() {
        if (!m_Buffer || !m_MappedData) return;

        auto& rhiDevice = RHIDevice::Get();
        const auto& core = rhiDevice.GetNRICore();
        if (core.UnmapBuffer) {
            core.UnmapBuffer(*m_Buffer);
        }
        m_MappedData = nullptr;
    }

    bool RHIBuffer::UpdateData(const void* data, uint64_t size, uint64_t offset) {
        if (!m_Buffer || !data || size == 0 || (offset + size > m_Desc.Size)) return false;

        auto& rhiDevice = RHIDevice::Get();
        const auto& helper = rhiDevice.GetNRIHelper();
        auto* queue = rhiDevice.GetGraphicsQueue();

        if (m_Desc.MemoryLocation == RHIMemoryLocation::HostUpload || m_Desc.MemoryLocation == RHIMemoryLocation::DeviceUpload) {
            void* ptr = Map(offset, size);
            if (!ptr) return false;
            std::memcpy(ptr, data, static_cast<size_t>(size));
            Unmap();
            return true;
        }

        if (m_Desc.MemoryLocation == RHIMemoryLocation::Device && queue && helper.UploadData) {
            nri::BufferUploadDesc upDesc = {};
            upDesc.buffer = m_Buffer;
            upDesc.data = data;
            upDesc.after.access = (m_Desc.Usage & RHIBufferUsage::Vertex) != RHIBufferUsage::None
                ? nri::AccessBits::VERTEX_BUFFER
                : nri::AccessBits::SHADER_RESOURCE;
            upDesc.after.stages = nri::StageBits::ALL;
            nri::Result res = helper.UploadData(*queue, nullptr, 0, &upDesc, 1);
            return (res == nri::Result::SUCCESS);
        }

        return false;
    }

    // =========================================================================
    // RHIMemoryManager Implementation
    // =========================================================================

    RHIVideoMemoryInfo RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation location) {
        RHIVideoMemoryInfo outInfo = {};
        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) return outInfo;

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& helper = rhiDevice.GetNRIHelper();
        if (!nativeDevice || !helper.QueryVideoMemoryInfo) return outInfo;

        nri::VideoMemoryInfo nriInfo = {};
        nri::Result res = helper.QueryVideoMemoryInfo(*nativeDevice, ToNRIMemoryLocation(location), nriInfo);
        if (res == nri::Result::SUCCESS) {
            outInfo.BudgetBytes = nriInfo.budgetSize;
            outInfo.UsageBytes = nriInfo.usageSize;
        }
        return outInfo;
    }

    bool RHIMemoryManager::AllocateBufferGroup(const std::vector<RHIBufferDesc>& descs,
                                             std::vector<std::unique_ptr<RHIBuffer>>& outBuffers,
                                             std::vector<nri::Memory*>& outAllocatedChunks,
                                             RHIMemoryLocation location,
                                             uint64_t preferredChunkSize)
    {
        outBuffers.clear();
        outAllocatedChunks.clear();

        if (descs.empty()) return true;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) {
            AXLE_CORE_ERROR("RHIMemoryManager::AllocateBufferGroup failed: RHIDevice is not initialized!");
            return false;
        }

        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& core = rhiDevice.GetNRICore();
        const auto& helper = rhiDevice.GetNRIHelper();
        if (!nativeDevice || !helper.CalculateAllocationNumber || !helper.AllocateAndBindMemory) {
            return false;
        }

        // 1. Create native unplaced Buffer objects
        std::vector<nri::Buffer*> nativeBuffers;
        nativeBuffers.reserve(descs.size());

        for (const auto& desc : descs) {
            nri::BufferDesc bufDesc = {};
            bufDesc.size = desc.Size;
            bufDesc.structureStride = desc.StructureStride;
            bufDesc.usage = ToNRIBufferUsage(desc.Usage);
            bufDesc.byteAddress = desc.ByteAddress;

            nri::Buffer* buf = nullptr;
            nri::Result res = core.CreateBuffer(*nativeDevice, bufDesc, buf);
            if (res != nri::Result::SUCCESS || !buf) {
                AXLE_CORE_ERROR("RHIMemoryManager::AllocateBufferGroup: CreateBuffer failed with code {0}!", static_cast<uint32_t>(res));
                for (auto* b : nativeBuffers) {
                    core.DestroyBuffer(b);
                }
                return false;
            }
            nativeBuffers.push_back(buf);
        }

        // 2. Setup ResourceGroupDesc for optimized memory chunk allocation (VMA / D3D12MA)
        nri::ResourceGroupDesc groupDesc = {};
        groupDesc.memoryLocation = ToNRIMemoryLocation(location);
        groupDesc.buffers = nativeBuffers.data();
        groupDesc.bufferNum = static_cast<uint32_t>(nativeBuffers.size());
        groupDesc.preferredMemorySize = preferredChunkSize;
        groupDesc.vma = true;

        // 3. Calculate allocation chunk count and allocate
        uint32_t allocationCount = helper.CalculateAllocationNumber(*nativeDevice, groupDesc);
        outAllocatedChunks.resize(allocationCount, nullptr);

        nri::Result allocRes = helper.AllocateAndBindMemory(*nativeDevice, groupDesc, outAllocatedChunks.data());
        if (allocRes != nri::Result::SUCCESS) {
            AXLE_CORE_ERROR("RHIMemoryManager::AllocateBufferGroup: AllocateAndBindMemory failed with code {0}!", static_cast<uint32_t>(allocRes));
            for (auto* b : nativeBuffers) {
                core.DestroyBuffer(b);
            }
            outAllocatedChunks.clear();
            return false;
        }

        // 4. Wrap into RHIBuffer objects adopting the pre-allocated and bound buffers
        outBuffers.reserve(descs.size());
        for (size_t i = 0; i < descs.size(); ++i) {
            auto rhiBuf = std::make_unique<RHIBuffer>();
            rhiBuf->InitAdopt(descs[i], nativeBuffers[i]);
            outBuffers.push_back(std::move(rhiBuf));
        }

        AXLE_CORE_INFO("RHIMemoryManager: Successfully allocated group of {0} buffer(s) into {1} memory chunk(s) via NRIHelper.",
            descs.size(), outAllocatedChunks.size());
        return true;
    }

    void RHIMemoryManager::FreeAllocatedChunks(std::vector<nri::Memory*>& chunks) {
        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsInitialized()) return;
        const auto& core = rhiDevice.GetNRICore();
        if (!core.FreeMemory) return;

        for (auto* chunk : chunks) {
            if (chunk) {
                core.FreeMemory(chunk);
            }
        }
        chunks.clear();
    }

}
