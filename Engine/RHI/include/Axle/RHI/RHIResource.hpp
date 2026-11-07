#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace Axle::RHI {
    // Authored identities never contain this device-specific identity.
    struct RHIResourceHandle {
        uint64_t Epoch = 0;
        uint32_t Slot = UINT32_MAX;
        uint32_t Generation = 0;
        bool operator==(const RHIResourceHandle&) const = default;
        explicit operator bool() const { return Epoch && Generation && Slot != UINT32_MAX; }
    };

    enum class RHIResourceKind : uint8_t {
        Memory, Surface, Streamer, Buffer, Texture, Sampler, Layout, Pool, DescriptorSet, Pipeline, Commands
    };
    struct RHIResourceGeneration;
    struct RHIResourceOwner;

    // A strong reference to one complete native generation, independent of its wrapper.
    class RHIResourceLease {
    public:
        RHIResourceLease() = default;
        bool IsLive() const;
        explicit operator bool() const { return m_Generation != nullptr; }
        RHIResourceHandle GetHandle() const;
        void Reset() { m_Generation.reset(); }
    private:
        friend class RHIResourceService;
        explicit RHIResourceLease(std::shared_ptr<RHIResourceGeneration> generation) : m_Generation(std::move(generation)) {}
        std::shared_ptr<RHIResourceGeneration> m_Generation;
    };

    // Owner-thread publication/lookup; releasing a lease from another thread only
    // queues cleanup. Native destruction occurs on the render owner after a drain.
    class RHIResourceService {
    public:
        RHIResourceService() = default;
        ~RHIResourceService();
        RHIResourceService(const RHIResourceService&) = delete;
        RHIResourceService& operator=(const RHIResourceService&) = delete;
        bool Open(std::function<void()> destroyDevice);
        uint64_t GetEpoch() const;
        bool IsOwnerThread() const;
        RHIResourceLease Publish(RHIResourceKind kind, std::string name,
            std::initializer_list<const void*> aliases, std::function<void()> cleanup,
            std::vector<RHIResourceLease> dependencies = {});
        RHIResourceLease Acquire(RHIResourceHandle handle) const;
        // Migration adapter only: raw addresses cannot detect address reuse. New
        // persistent references must keep the handle/lease, not re-look up a pointer.
        RHIResourceLease AcquireNative(const void* alias) const;
        void AddAlias(const RHIResourceLease& lease, const void* alias);
        void BeforeDeviceClose(std::function<void()> closeOwner);
        void PrepareDeviceClose();
        // Call only after checked queue completion (or confirmed device loss).
        void CollectAfterDrain();
        void CloseAfterDrain();
    private:
        friend class RHIDescriptorPool;
        bool ReplaceDescriptorRange(const RHIResourceLease& lease, uint32_t range, uint32_t baseSlot, std::vector<RHIResourceLease> resources);
        std::shared_ptr<RHIResourceOwner> m_Owner;
    };

    RHIResourceLease AcquireRHIResource(const void* native);

    // Transitional member cache: keeps its generation alive and becomes null
    // when the owning device closes, including after another device is opened.
    template<class T> class RHIResourceRef {
    public:
        RHIResourceRef() = default;
        explicit RHIResourceRef(T* native) { *this = native; }
        RHIResourceRef& operator=(T* native) {
            m_Lease = native ? AcquireRHIResource(native) : RHIResourceLease{}; m_Native = native; return *this;
        }
        T* Get() const { return m_Lease.IsLive() ? m_Native : nullptr; }
        operator T*() const { return Get(); }
        const RHIResourceLease& GetLease() const { return m_Lease; }
    private:
        T* m_Native = nullptr;
        RHIResourceLease m_Lease;
    };
}
