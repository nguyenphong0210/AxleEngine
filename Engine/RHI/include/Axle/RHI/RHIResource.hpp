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
        Memory, Surface, Streamer, Buffer, Texture, Sampler, Layout, Pool, DescriptorSet, Pipeline, UIBackend, Commands
    };
    struct RHIResourceGeneration;
    struct RHIResourceOwner;
    struct RHIRecordingState;

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
        friend struct RHIRecordingState;
        explicit RHIResourceLease(std::shared_ptr<RHIResourceGeneration> generation) : m_Generation(std::move(generation)) {}
        std::shared_ptr<RHIResourceGeneration> m_Generation;
    };

    // Pins an immutable snapshot of transitive native dependencies. Copies share
    // one pin set, so it can move from a CPU draw list to a submitted batch.
    class RHIRecordingLease {
    public:
        bool IsLive() const;
        bool Empty() const;
        void Reset() { m_State.reset(); }
    private:
        friend class RHIResourceService;
        std::shared_ptr<RHIRecordingState> m_State;
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
        void DeferUntilDrain(std::function<void()> release);
        std::function<void(std::function<void()>)> GetDeferredReleaser() const;
        RHIRecordingLease Pin(const RHIResourceLease& resource) const;
        bool AppendPin(RHIRecordingLease& pins, const RHIResourceLease& resource) const;
        bool IsInUse(const RHIResourceLease& resource) const;
        bool BeginRecording(const void* commands);
        bool EndRecording(const void* commands, bool executable);
        bool IsRecording(const void* commands) const;
        bool IsExecutable(const void* commands) const;
        bool RecordUse(const void* commands, const RHIResourceLease& resource);
        bool RecordUses(const void* commands, const RHIRecordingLease& resources);
        // Raw migration bindings may borrow external native objects; their owner
        // must retain them through completion. Managed aliases acquire a pin.
        bool RecordNativeUse(const void* commands, const void* alias);
        bool SnapshotSubmission(const void* const* commands, uint32_t count, RHIRecordingLease& pins) const;
        void MarkSubmitted(const void* const* commands, uint32_t count);
        void AbandonRecording(const void* commands);
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
