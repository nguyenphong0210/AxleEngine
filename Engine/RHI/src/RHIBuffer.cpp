#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/Log.hpp>

#include <cstring>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <algorithm>
#include <utility>

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

    namespace {
        bool ValidBufferDesc(const RHIBufferDesc& desc) {
            auto& device = RHIDevice::Get();
            if (!device.CanCreateResources()) return false;
            const auto& limits = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice());
            if (!desc.Size || desc.Size > limits.memory.bufferMaxSize || desc.Size > SIZE_MAX ||
                static_cast<uint32_t>(desc.MemoryLocation) > static_cast<uint32_t>(RHIMemoryLocation::HostReadback) ||
                (static_cast<uint32_t>(desc.Usage) & ~127u) ||
                (desc.StructureStride && (desc.StructureStride % 4 || desc.StructureStride > 2048 || desc.Size % desc.StructureStride)) ||
                (desc.ByteAddress && desc.Size % 4)) return false;
            if ((desc.Usage & RHIBufferUsage::Constant) != RHIBufferUsage::None &&
                (desc.Size > limits.memory.constantBufferMaxRange ||
                 desc.Size % std::max(1u, limits.memoryAlignment.constantBufferOffset))) return false;
            if (desc.MemoryLocation == RHIMemoryLocation::HostReadback && desc.Usage != RHIBufferUsage::None) return false;
            return true;
        }

        nri::BufferDesc NativeBufferDesc(const RHIBufferDesc& desc) {
            return {desc.Size, desc.StructureStride, ToNRIBufferUsage(desc.Usage), desc.ByteAddress};
        }

        nri::AccessBits BufferUploadAccess(RHIBufferUsage usage) {
            auto access = nri::AccessBits::NONE;
            if ((usage & RHIBufferUsage::Vertex) != RHIBufferUsage::None) access |= nri::AccessBits::VERTEX_BUFFER;
            if ((usage & RHIBufferUsage::Index) != RHIBufferUsage::None) access |= nri::AccessBits::INDEX_BUFFER;
            if ((usage & RHIBufferUsage::Constant) != RHIBufferUsage::None) access |= nri::AccessBits::CONSTANT_BUFFER;
            if ((usage & RHIBufferUsage::ShaderResource) != RHIBufferUsage::None) access |= nri::AccessBits::SHADER_RESOURCE;
            if ((usage & RHIBufferUsage::ShaderResourceStorage) != RHIBufferUsage::None) access |= nri::AccessBits::SHADER_RESOURCE_STORAGE;
            if ((usage & RHIBufferUsage::Argument) != RHIBufferUsage::None) access |= nri::AccessBits::ARGUMENT_BUFFER;
            return access;
        }
    }

    RHIBuffer::~RHIBuffer() { Shutdown(); }

    RHIBuffer::RHIBuffer(RHIBuffer&& other) noexcept { *this = std::move(other); }

    RHIBuffer& RHIBuffer::operator=(RHIBuffer&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Desc = std::move(other.m_Desc);
            m_Buffer = std::exchange(other.m_Buffer, nullptr);
            m_Memory = std::exchange(other.m_Memory, nullptr);
            m_CBV = std::exchange(other.m_CBV, nullptr);
            m_OwnsMemory = std::exchange(other.m_OwnsMemory, false);
            m_MappedData = std::exchange(other.m_MappedData, nullptr);
            m_MappedOffset = std::exchange(other.m_MappedOffset, 0);
            m_MappedSize = std::exchange(other.m_MappedSize, 0);
        }
        return *this;
    }

    bool RHIBuffer::Init(const RHIBufferDesc& desc, const void* initialData) {
        RHIBuffer candidate;
        if (!candidate.Build(desc, initialData)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIBuffer::BuildView() {
        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        if ((m_Desc.Usage & RHIBufferUsage::Constant) != RHIBufferUsage::None) {
            nri::BufferViewDesc view{};
            view.buffer = m_Buffer; view.type = nri::BufferView::CONSTANT_BUFFER; view.size = m_Desc.Size;
            const auto result = ScopedRHIFault::Invoke("CreateBufferCBV", [&] { return core.CreateBufferView(view, m_CBV); });
            if (result != nri::Result::SUCCESS || !m_CBV) return false;
        }
        if (!m_Desc.DebugName.empty()) core.SetDebugName(m_Buffer, m_Desc.DebugName.c_str());
        return true;
    }

    bool RHIBuffer::Build(const RHIBufferDesc& desc, const void* initialData, nri::Memory* borrowedMemory, uint64_t memoryOffset) {
        ScopedRHIOperation operation(desc.DebugName);
        if (!ValidBufferDesc(desc) || (initialData && desc.MemoryLocation == RHIMemoryLocation::HostReadback)) return RHIDevice::Get().ObserveResult(nri::Result::INVALID_ARGUMENT, "ValidateBufferDesc", desc.DebugName);
        m_Desc = desc;
        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        auto* native = device.GetNRIDevice();
        const auto bufferDesc = NativeBufferDesc(desc);
        auto result = ScopedRHIFault::Invoke("CreateBuffer", [&] { return core.CreateBuffer(*native, bufferDesc, m_Buffer); });
        if (result != nri::Result::SUCCESS || !m_Buffer) return false;
        nri::MemoryDesc memory{};
        core.GetBufferMemoryDesc(*m_Buffer, ToNRIMemoryLocation(desc.MemoryLocation), memory);
        if (!memory.size || !memory.alignment || memoryOffset % memory.alignment ||
            memoryOffset > UINT64_MAX - memory.size || (memory.mustBeDedicated && memoryOffset) ||
            memory.size > core.GetDeviceDesc(*native).memory.allocationMaxSize) return false;
        m_Memory = borrowedMemory;
        if (!borrowedMemory) {
            m_OwnsMemory = true;
            nri::AllocateMemoryDesc allocation{};
            allocation.size = memory.size; allocation.type = memory.type;
            result = ScopedRHIFault::Invoke("AllocateBufferMemory", [&] { return core.AllocateMemory(*native, allocation, m_Memory); });
            if (result != nri::Result::SUCCESS || !m_Memory) return false;
        }
        nri::BindBufferMemoryDesc binding{m_Buffer, m_Memory, memoryOffset};
        result = ScopedRHIFault::Invoke("BindBufferMemory", [&] { return core.BindBufferMemory(&binding, 1); });
        if (result != nri::Result::SUCCESS || !BuildView()) return false;
        return !initialData || UpdateData(initialData, desc.Size);
    }

    bool RHIBuffer::InitPlaced(const RHIBufferDesc& desc, nri::Memory* memory, uint64_t memoryOffset) {
        if (!memory) return false;
        RHIBuffer candidate;
        if (!candidate.Build(desc, nullptr, memory, memoryOffset)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIBuffer::InitAdopt(const RHIBufferDesc& desc, nri::Buffer* existingBuffer, nri::Memory* memory) {
        if (!existingBuffer || existingBuffer == m_Buffer || (memory && memory == m_Memory) || !ValidBufferDesc(desc)) return false;
        const auto& core = RHIDevice::Get().GetNRICore();
        const auto& native = core.GetBufferDesc(*existingBuffer);
        if (native.size != desc.Size || native.structureStride != desc.StructureStride ||
            native.usage != ToNRIBufferUsage(desc.Usage) || native.byteAddress != desc.ByteAddress) return false;
        RHIBuffer candidate;
        candidate.m_Desc = desc;
        candidate.m_Buffer = existingBuffer;
        if (!candidate.BuildView()) {
            // Adoption has not committed. The caller still owns its bound buffer.
            candidate.m_Buffer = nullptr;
            return false;
        }
        candidate.m_Memory = memory;
        candidate.m_OwnsMemory = memory != nullptr;
        *this = std::move(candidate);
        return true;
    }

    void RHIBuffer::Shutdown() {
        auto& device = RHIDevice::Get();
        if (device.IsNativeReady()) {
            Unmap();
            const auto core = device.GetNRICore();
            auto cbv = m_CBV; auto buffer = m_Buffer; auto memory = m_OwnsMemory ? m_Memory : nullptr;
            device.ReleaseNative([core, cbv, buffer, memory] {
                if (cbv) core.DestroyDescriptor(cbv);
                if (buffer) core.DestroyBuffer(buffer);
                if (memory) core.FreeMemory(memory);
            });
        }
        m_CBV = nullptr; m_Buffer = nullptr; m_Memory = nullptr;
        m_OwnsMemory = false; m_MappedData = nullptr;
        m_MappedOffset = m_MappedSize = 0;
        m_Desc = {};
    }

    void* RHIBuffer::Map(uint64_t offset, uint64_t size) {
        if (!RHIDevice::Get().CanCreateResources() || !m_Buffer || m_Desc.MemoryLocation == RHIMemoryLocation::Device || offset >= m_Desc.Size) return nullptr;
        const uint64_t count = size ? size : m_Desc.Size - offset;
        if (count > m_Desc.Size - offset) return nullptr;
        if (m_MappedData) {
            if (offset < m_MappedOffset || offset - m_MappedOffset >= m_MappedSize || count > m_MappedSize - (offset - m_MappedOffset)) return nullptr;
            return static_cast<uint8_t*>(m_MappedData) + (offset - m_MappedOffset);
        }
        auto& device = RHIDevice::Get();
        if (!device.IsNativeReady()) return nullptr;
        const auto result = ScopedRHIFault::Invoke("MapBuffer", [&] {
            m_MappedData = device.GetNRICore().MapBuffer(*m_Buffer, offset, count);
            return m_MappedData ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        if (result != nri::Result::SUCCESS) return nullptr;
        m_MappedOffset = offset; m_MappedSize = count;
        return m_MappedData;
    }

    void RHIBuffer::Unmap() {
        auto& device = RHIDevice::Get();
        if (m_Buffer && m_MappedData && device.IsNativeReady()) device.GetNRICore().UnmapBuffer(*m_Buffer);
        m_MappedData = nullptr; m_MappedOffset = m_MappedSize = 0;
    }

    bool RHIBuffer::UpdateData(const void* data, uint64_t size, uint64_t offset) {
        if (!m_Buffer || !data || !size || offset > m_Desc.Size || size > m_Desc.Size - offset) return false;
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources()) return false;
        if (m_Desc.MemoryLocation == RHIMemoryLocation::HostUpload || m_Desc.MemoryLocation == RHIMemoryLocation::DeviceUpload) {
            const bool alreadyMapped = m_MappedData != nullptr;
            void* ptr = Map(offset, size);
            if (!ptr) return false;
            std::memcpy(ptr, data, static_cast<size_t>(size));
            if (!alreadyMapped) Unmap();
            return true;
        }
        if (m_Desc.MemoryLocation == RHIMemoryLocation::Device) {
            // NRI 190 uploads the entire buffer; a smaller source would be overread.
            if (offset || size != m_Desc.Size) return false;
            nri::BufferUploadDesc upload{};
            upload.buffer = m_Buffer; upload.data = data;
            upload.after.access = BufferUploadAccess(m_Desc.Usage);
            upload.after.stages = nri::StageBits::ALL;
            return ScopedRHIFault::Invoke("UploadBufferData", [&] {
                return device.GetNRIHelper().UploadData(*device.GetGraphicsQueue(), nullptr, 0, &upload, 1);
            }) == nri::Result::SUCCESS;
        }
        return false;
    }

    RHIVideoMemoryInfo RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation location) {
        RHIVideoMemoryInfo outInfo = {};
        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsNativeReady()) return outInfo;

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
        auto& device = RHIDevice::Get();
        if (!device.CanCreateResources() || descs.size() > UINT32_MAX || !preferredChunkSize) return false;
        const auto& core = device.GetNRICore();
        const auto& helper = device.GetNRIHelper();
        std::vector<std::unique_ptr<RHIBuffer>> candidates;
        std::vector<nri::Buffer*> buffers(descs.size(), nullptr);
        std::vector<nri::Memory*> chunks;
        auto fail = [&] {
            candidates.clear();
            for (auto* buffer : buffers) if (buffer) core.DestroyBuffer(buffer);
            FreeAllocatedChunks(chunks);
            return false;
        };
        for (size_t i = 0; i < descs.size(); ++i) {
            auto desc = descs[i]; desc.MemoryLocation = location;
            if (!ValidBufferDesc(desc)) return fail();
            const auto native = NativeBufferDesc(desc);
            const auto result = ScopedRHIFault::Invoke("CreateGroupBuffer", [&] { return core.CreateBuffer(*device.GetNRIDevice(), native, buffers[i]); });
            if (result != nri::Result::SUCCESS || !buffers[i]) return fail();
        }
        if (!buffers.empty()) {
            nri::ResourceGroupDesc group{};
            group.memoryLocation = ToNRIMemoryLocation(location);
            group.buffers = buffers.data(); group.bufferNum = static_cast<uint32_t>(buffers.size());
            group.preferredMemorySize = preferredChunkSize; group.vma = true;
            const auto count = helper.CalculateAllocationNumber(*device.GetNRIDevice(), group);
            if (!count) return fail();
            chunks.resize(count, nullptr);
            const auto result = ScopedRHIFault::Invoke("AllocateGroupMemory", [&] { return helper.AllocateAndBindMemory(*device.GetNRIDevice(), group, chunks.data()); });
            // The pinned helper frees and nulls its allocations on failure.
            if (result != nri::Result::SUCCESS) return fail();
        }
        for (size_t i = 0; i < descs.size(); ++i) {
            auto desc = descs[i]; desc.MemoryLocation = location;
            auto candidate = std::make_unique<RHIBuffer>();
            if (!candidate->InitAdopt(desc, buffers[i])) return fail();
            buffers[i] = nullptr;
            candidates.push_back(std::move(candidate));
        }
        // Destroy old bound resources before releasing the memory they borrowed.
        outBuffers.clear();
        FreeAllocatedChunks(outAllocatedChunks);
        outBuffers = std::move(candidates);
        outAllocatedChunks = std::move(chunks);
        return true;
    }

    void RHIMemoryManager::FreeAllocatedChunks(std::vector<nri::Memory*>& chunks) {
        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsNativeReady()) return;
        const auto& core = rhiDevice.GetNRICore();
        if (!core.FreeMemory) return;

        for (auto* chunk : chunks) {
            if (chunk) {
                auto functions = core;
                rhiDevice.ReleaseNative([functions, chunk] { functions.FreeMemory(chunk); });
            }
        }
        chunks.clear();
    }

}
