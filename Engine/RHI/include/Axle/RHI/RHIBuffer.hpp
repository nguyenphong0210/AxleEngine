#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIBarrier.hpp>

#include <NRI.h>
#include <Extensions/NRIHelper.h>

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#ifdef _WIN32
#undef min
#undef max
#endif

namespace Axle::RHI {

    enum class RHIBufferUsage : uint32_t {
        None                    = 0,
        Vertex                  = 1 << 0,
        Index                   = 1 << 1,
        Constant                = 1 << 2,
        ShaderResource          = 1 << 3, // Read-only Structured/ByteAddress (SRV)
        ShaderResourceStorage   = 1 << 4, // Read-Write Structured/ByteAddress (UAV)
        Argument                = 1 << 5, // Draw/Dispatch indirect arguments
        Scratch                 = 1 << 6  // Ray-tracing/acceleration structure scratch
    };

    inline constexpr RHIBufferUsage operator|(RHIBufferUsage a, RHIBufferUsage b) {
        return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline constexpr RHIBufferUsage operator&(RHIBufferUsage a, RHIBufferUsage b) {
        return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline constexpr RHIBufferUsage operator~(RHIBufferUsage a) {
        return static_cast<RHIBufferUsage>(~static_cast<uint32_t>(a));
    }
    inline RHIBufferUsage& operator|=(RHIBufferUsage& a, RHIBufferUsage b) {
        a = a | b;
        return a;
    }
    inline RHIBufferUsage& operator&=(RHIBufferUsage& a, RHIBufferUsage b) {
        a = a & b;
        return a;
    }

    enum class RHIMemoryLocation : uint8_t {
        Device = 0,         // Device-local high-speed VRAM
        DeviceUpload,       // Resizable BAR / Direct GPU Upload
        HostUpload,         // CPU-to-GPU dynamic staging memory
        HostReadback        // GPU-to-CPU readback memory
    };

    nri::BufferUsageBits ToNRIBufferUsage(RHIBufferUsage usage);
    nri::MemoryLocation ToNRIMemoryLocation(RHIMemoryLocation location);

    struct RHIBufferDesc {
        uint64_t Size = 0;
        uint32_t StructureStride = 0; // Stride in bytes for structured buffers (0 for raw/byte-address)
        RHIBufferUsage Usage = RHIBufferUsage::None;
        RHIMemoryLocation MemoryLocation = RHIMemoryLocation::Device;
        bool ByteAddress = false;     // Enable Raw/ByteAddress buffer views
        std::string DebugName;

        // Convenient factory presets
        static RHIBufferDesc Vertex(uint64_t size, RHIMemoryLocation location = RHIMemoryLocation::Device, const std::string& name = "") {
            RHIBufferDesc desc;
            desc.Size = size;
            desc.Usage = RHIBufferUsage::Vertex;
            desc.MemoryLocation = location;
            desc.DebugName = name;
            return desc;
        }

        static RHIBufferDesc Index(uint64_t size, RHIMemoryLocation location = RHIMemoryLocation::Device, const std::string& name = "") {
            RHIBufferDesc desc;
            desc.Size = size;
            desc.Usage = RHIBufferUsage::Index;
            desc.MemoryLocation = location;
            desc.DebugName = name;
            return desc;
        }

        static RHIBufferDesc Constant(uint64_t size, RHIMemoryLocation location = RHIMemoryLocation::HostUpload, const std::string& name = "") {
            RHIBufferDesc desc;
            // Constant buffers in D3D12/Vulkan typically align to 256 bytes
            desc.Size = (size + 255) & ~255ULL;
            desc.Usage = RHIBufferUsage::Constant;
            desc.MemoryLocation = location;
            desc.DebugName = name;
            return desc;
        }

        static RHIBufferDesc Structured(uint64_t elementCount, uint32_t elementStride, bool readWrite = false,
                                        RHIMemoryLocation location = RHIMemoryLocation::Device, const std::string& name = "") {
            RHIBufferDesc desc;
            desc.Size = elementCount * elementStride;
            desc.StructureStride = elementStride;
            desc.Usage = readWrite
                ? (RHIBufferUsage::ShaderResource | RHIBufferUsage::ShaderResourceStorage)
                : RHIBufferUsage::ShaderResource;
            desc.MemoryLocation = location;
            desc.DebugName = name;
            return desc;
        }

        static RHIBufferDesc Indirect(uint64_t size, RHIMemoryLocation location = RHIMemoryLocation::Device, const std::string& name = "") {
            RHIBufferDesc desc;
            desc.Size = size;
            desc.Usage = RHIBufferUsage::Argument | RHIBufferUsage::ShaderResourceStorage;
            desc.MemoryLocation = location;
            desc.DebugName = name;
            return desc;
        }
    };

    class RHIBuffer {
    public:
        RHIBuffer() = default;
        ~RHIBuffer();

        RHIBuffer(const RHIBuffer&) = delete;
        RHIBuffer& operator=(const RHIBuffer&) = delete;

        RHIBuffer(RHIBuffer&& other) noexcept;
        RHIBuffer& operator=(RHIBuffer&& other) noexcept;

        // Initialize committed buffer and optionally upload initial data
        bool Init(const RHIBufferDesc& desc, const void* initialData = nullptr);

        // Initialize placed buffer on externally allocated/bound memory
        bool InitPlaced(const RHIBufferDesc& desc, nri::Memory* memory, uint64_t memoryOffset);

        // Adopt an already created & bound native NRI buffer (e.g. from group allocation)
        bool InitAdopt(const RHIBufferDesc& desc, nri::Buffer* existingBuffer, nri::Memory* memory = nullptr);

        // Shutdown and release NRI resources
        void Shutdown();

        // Map buffer into host memory address space (only valid for HostUpload / DeviceUpload / HostReadback)
        void* Map(uint64_t offset = 0, uint64_t size = 0);
        void Unmap();

        // Synchronously update data (if host-visible, memcpy directly; if device-local, via helper.UploadData)
        bool UpdateData(const void* data, uint64_t size, uint64_t offset = 0);

        bool IsValid() const { return m_Buffer != nullptr; }
        uint64_t GetSize() const { return m_Desc.Size; }
        uint32_t GetStructureStride() const { return m_Desc.StructureStride; }
        RHIBufferUsage GetUsage() const { return m_Desc.Usage; }
        RHIMemoryLocation GetMemoryLocation() const { return m_Desc.MemoryLocation; }
        const RHIBufferDesc& GetDesc() const { return m_Desc; }

        nri::Buffer* GetNative() const { return m_Buffer; }
        nri::Memory* GetNativeMemory() const { return m_Memory; }
        nri::Descriptor* GetCBV() const { return m_CBV; }
        operator nri::Buffer*() const { return m_Buffer; }

    private:
        RHIBufferDesc m_Desc;
        nri::Buffer* m_Buffer = nullptr;
        nri::Memory* m_Memory = nullptr;
        nri::Descriptor* m_CBV = nullptr;
        bool m_IsCommitted = false;
        void* m_MappedData = nullptr;
    };

    // =========================================================================
    // GPU Memory Budget & Group Allocation Manager
    // =========================================================================

    struct RHIVideoMemoryInfo {
        uint64_t BudgetBytes = 0;
        uint64_t UsageBytes = 0;
        float UsageMB() const { return static_cast<float>(UsageBytes) / (1024.0f * 1024.0f); }
        float BudgetMB() const { return static_cast<float>(BudgetBytes) / (1024.0f * 1024.0f); }
        float UsagePercent() const { return BudgetBytes > 0 ? (static_cast<float>(UsageBytes) / static_cast<float>(BudgetBytes)) * 100.0f : 0.0f; }
    };

    class RHIMemoryManager {
    public:
        // Query active GPU VRAM budget & usage
        static RHIVideoMemoryInfo QueryVRAMInfo(RHIMemoryLocation location = RHIMemoryLocation::Device);

        // Group memory allocation for a collection of buffers using AMD VMA / D3D12MA
        // Allocates large chunks (up to preferredChunkSize, default 256MB) to prevent heap allocation explosion
        static bool AllocateBufferGroup(const std::vector<RHIBufferDesc>& descs,
                                       std::vector<std::unique_ptr<RHIBuffer>>& outBuffers,
                                       std::vector<nri::Memory*>& outAllocatedChunks,
                                       RHIMemoryLocation location = RHIMemoryLocation::Device,
                                       uint64_t preferredChunkSize = 256 * 1024 * 1024);

        // Free group allocated memory chunks
        static void FreeAllocatedChunks(std::vector<nri::Memory*>& chunks);
    };

}
