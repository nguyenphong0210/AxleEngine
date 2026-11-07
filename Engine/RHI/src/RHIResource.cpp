#include <Axle/RHI/RHIResource.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Axle::RHI {
    struct RHIResourceOwner {
        struct Slot { std::weak_ptr<RHIResourceGeneration> Value; uint32_t Generation = 0; };
        uint64_t Epoch = 0;
        std::atomic<bool> Live = true;
        std::thread::id Thread = std::this_thread::get_id();
        std::function<void()> DestroyDevice;
        std::vector<Slot> Slots;
        std::unordered_map<const void*, std::weak_ptr<RHIResourceGeneration>> Aliases;
        std::mutex Mutex;
        std::vector<std::function<void()>> Retired;
        std::vector<std::function<void()>> BeforeClose;
        void Retire(std::function<void()> cleanup) {
            std::lock_guard lock(Mutex);
            Retired.push_back(std::move(cleanup));
        }
    };

    struct RHIResourceGeneration {
        std::shared_ptr<RHIResourceOwner> Owner;
        RHIResourceHandle Handle;
        RHIResourceKind Kind{};
        std::string Name;
        std::atomic<bool> Live = true;
        std::function<void()> Cleanup;
        std::vector<RHIResourceLease> Dependencies;
        std::unordered_map<uint64_t, std::vector<RHIResourceLease>> Ranges;
        ~RHIResourceGeneration() { Retire(); }
        void Retire() {
            if (!Live.exchange(false)) return;
            auto cleanup = std::move(Cleanup);
            auto dependencies = std::move(Dependencies);
            auto ranges = std::move(Ranges);
            Owner->Retire([cleanup = std::move(cleanup), dependencies = std::move(dependencies), ranges = std::move(ranges)] {
                if (cleanup) cleanup();
            });
        }
    };

    bool RHIResourceLease::IsLive() const {
        return m_Generation && m_Generation->Live && m_Generation->Owner->Live;
    }
    RHIResourceHandle RHIResourceLease::GetHandle() const { return m_Generation ? m_Generation->Handle : RHIResourceHandle{}; }

    // An unresolved process-exit device is deliberately quarantined. Never invoke
    // native cleanup through a destroyed RHIDevice or release in-flight storage.
    RHIResourceService::~RHIResourceService() {
        if (m_Owner && m_Owner->Live) {
            (void)new std::shared_ptr<RHIResourceOwner>(std::move(m_Owner));
        }
    }
    bool RHIResourceService::Open(std::function<void()> destroyDevice) {
        if (m_Owner && m_Owner->Live) return false;
        static std::atomic<uint64_t> nextEpoch{1};
        m_Owner = std::make_shared<RHIResourceOwner>();
        m_Owner->Epoch = nextEpoch.fetch_add(1);
        m_Owner->DestroyDevice = std::move(destroyDevice);
        return true;
    }
    uint64_t RHIResourceService::GetEpoch() const { return m_Owner && m_Owner->Live ? m_Owner->Epoch : 0; }
    bool RHIResourceService::IsOwnerThread() const { return m_Owner && m_Owner->Live && m_Owner->Thread == std::this_thread::get_id(); }

    RHIResourceLease RHIResourceService::Publish(RHIResourceKind kind, std::string name,
        std::initializer_list<const void*> aliases, std::function<void()> cleanup, std::vector<RHIResourceLease> dependencies) {
        auto owner = m_Owner;
        if (!owner || !owner->Live || owner->Thread != std::this_thread::get_id()) return {};
        for (const auto& dependency : dependencies) if (!dependency.IsLive() || dependency.GetHandle().Epoch != owner->Epoch) return {};
        for (auto alias : aliases) if (alias && AcquireNative(alias).IsLive()) return {};
        uint32_t index = 0;
        for (; index < owner->Slots.size(); ++index)
            if (owner->Slots[index].Value.expired() && owner->Slots[index].Generation != UINT32_MAX) break;
        if (index == owner->Slots.size()) owner->Slots.emplace_back();
        auto& slot = owner->Slots[index];
        auto generation = std::make_shared<RHIResourceGeneration>();
        generation->Owner = owner;
        generation->Handle = {owner->Epoch, index, ++slot.Generation};
        generation->Kind = kind; generation->Name = std::move(name);
        generation->Cleanup = std::move(cleanup); generation->Dependencies = std::move(dependencies);
        slot.Value = generation;
        for (auto alias : aliases) if (alias) owner->Aliases[alias] = generation;
        return RHIResourceLease(std::move(generation));
    }
    RHIResourceLease RHIResourceService::Acquire(RHIResourceHandle handle) const {
        auto owner = m_Owner;
        if (!owner || !owner->Live || handle.Epoch != owner->Epoch || handle.Slot >= owner->Slots.size() ||
            owner->Thread != std::this_thread::get_id()) return {};
        const auto& slot = owner->Slots[handle.Slot];
        if (slot.Generation != handle.Generation) return {};
        RHIResourceLease lease(slot.Value.lock());
        return lease.IsLive() ? lease : RHIResourceLease{};
    }
    RHIResourceLease RHIResourceService::AcquireNative(const void* alias) const {
        auto owner = m_Owner;
        if (!alias || !owner || !owner->Live || owner->Thread != std::this_thread::get_id()) return {};
        const auto it = owner->Aliases.find(alias);
        if (it == owner->Aliases.end()) return {};
        RHIResourceLease lease(it->second.lock());
        return lease.IsLive() ? lease : RHIResourceLease{};
    }
    void RHIResourceService::AddAlias(const RHIResourceLease& lease, const void* alias) {
        auto existing = AcquireNative(alias);
        if (existing.IsLive() && existing.GetHandle() != lease.GetHandle()) return;
        if (alias && lease.IsLive() && lease.GetHandle().Epoch == GetEpoch() && m_Owner->Thread == std::this_thread::get_id())
            m_Owner->Aliases[alias] = lease.m_Generation;
    }
    void RHIResourceService::BeforeDeviceClose(std::function<void()> closeOwner) {
        if (IsOwnerThread()) m_Owner->BeforeClose.push_back(std::move(closeOwner));
    }
    void RHIResourceService::PrepareDeviceClose() {
        if (!IsOwnerThread()) return;
        auto hooks = std::move(m_Owner->BeforeClose);
        m_Owner->BeforeClose.clear();
        for (auto& close : hooks) close();
    }
    bool RHIResourceService::ReplaceDescriptorRange(const RHIResourceLease& lease, uint32_t range, uint32_t baseSlot, std::vector<RHIResourceLease> resources) {
        if (!IsOwnerThread() || !lease.IsLive() || lease.GetHandle().Epoch != GetEpoch() ||
            lease.m_Generation->Kind != RHIResourceKind::DescriptorSet || lease.m_Generation.use_count() > 2) return false;
        for (const auto& resource : resources) if (!resource.IsLive() || resource.GetHandle().Epoch != GetEpoch()) return false;
        if (resources.size() > UINT32_MAX - baseSlot) return false;
        auto replacement = lease.m_Generation->Ranges;
        for (uint32_t i = 0; i < resources.size(); ++i)
            replacement[(static_cast<uint64_t>(range) << 32) | (baseSlot + i)] = {std::move(resources[i])};
        lease.m_Generation->Ranges.swap(replacement);
        return true;
    }
    void RHIResourceService::CollectAfterDrain() {
        auto owner = m_Owner;
        if (!owner || owner->Thread != std::this_thread::get_id()) return;
        for (;;) {
            std::vector<std::function<void()>> releases;
            { std::lock_guard lock(owner->Mutex); releases.swap(owner->Retired); }
            if (releases.empty()) break;
            for (auto& release : releases) release();
            // Destructors of captured dependencies may enqueue another batch.
        }
        for (auto it = owner->Aliases.begin(); it != owner->Aliases.end();)
            if (it->second.expired()) it = owner->Aliases.erase(it); else ++it;
    }
    void RHIResourceService::CloseAfterDrain() {
        auto owner = m_Owner;
        if (!owner || !owner->Live || owner->Thread != std::this_thread::get_id()) return;
        PrepareDeviceClose();
        owner->Live = false;
        std::vector<std::shared_ptr<RHIResourceGeneration>> resources;
        for (const auto& slot : owner->Slots) if (auto value = slot.Value.lock()) resources.push_back(std::move(value));
        std::stable_sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) { return a->Kind > b->Kind; });
        for (auto& resource : resources) resource->Retire();
        CollectAfterDrain();
        owner->Aliases.clear();
        auto destroy = std::move(owner->DestroyDevice);
        if (destroy) destroy();
    }
    RHIResourceLease AcquireRHIResource(const void* native) { return RHIDevice::Get().GetResources().AcquireNative(native); }
}
