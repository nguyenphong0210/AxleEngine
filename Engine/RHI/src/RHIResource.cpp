#include <Axle/RHI/RHIResource.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Axle::RHI {
    struct RHITrackedState {
        bool Texture = false, Defined = false;
        uint16_t Mips = 1, Layers = 1;
        std::vector<nri::PlaneBits> Planes;
        std::vector<nri::AccessLayoutStage> Textures;
        nri::AccessStage Buffer{};
        nri::AccessBits HostAccess = nri::AccessBits::NONE;
        uint64_t Version = 0;
    };
    struct RHIPendingState {
        RHIResourceLease Resource;
        uint64_t BaseVersion = 0;
        RHITrackedState State;
    };
    struct RHIStateSubmission { std::vector<RHIPendingState> Published; };
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
        std::atomic<uint32_t> Pins{0};
        enum class CommandState { Empty, Recording, Executable, Submitted, Abandoned };
        CommandState Recording = CommandState::Empty;
        bool InRendering = false;
        RHIRecordingLease RecordedUses;
        std::unordered_map<const void*, std::pair<nri::AccessBits, nri::Layout>> Views;
        std::unordered_map<uint64_t, std::vector<RHIResourceAccess>> RangeAccesses;
        const void* ValidatorOwner = nullptr;
        std::function<bool(const RHIResourceAccess&)> AccessValidator;
        RHITrackedState State;
        nri::Texture* NativeTexture = nullptr;
        nri::Buffer* NativeBuffer = nullptr;
        std::unordered_map<RHIResourceGeneration*, RHIPendingState> PendingStates;
        ~RHIResourceGeneration() { Retire(); }
        void Retire() {
            if (!Live.exchange(false)) return;
            auto cleanup = std::move(Cleanup);
            auto dependencies = std::move(Dependencies);
            auto ranges = std::move(Ranges);
            auto recorded = std::move(RecordedUses);
            Owner->Retire([cleanup = std::move(cleanup), dependencies = std::move(dependencies), ranges = std::move(ranges), recorded = std::move(recorded)] {
                if (cleanup) cleanup();
            });
        }
    };

    struct RHIRecordingState {
        uint64_t Epoch = 0;
        RHIResourceGeneration* Excluded = nullptr;
        std::vector<RHIResourceLease> Resources;
        std::unordered_set<RHIResourceGeneration*> Unique;
        ~RHIRecordingState() {
            for (auto& resource : Resources) resource.m_Generation->Pins.fetch_sub(1);
        }
        bool Append(const RHIResourceLease& lease) {
            if (!lease.IsLive() || (Epoch && Epoch != lease.GetHandle().Epoch)) return false;
            auto* generation = lease.m_Generation.get();
            if (generation == Excluded) return false;
            if (!Unique.insert(generation).second) return true;
            Epoch = lease.GetHandle().Epoch;
            Resources.push_back(lease);
            generation->Pins.fetch_add(1);
            for (const auto& dependency : generation->Dependencies) if (!Append(dependency)) return false;
            for (const auto& [range, dependencies] : generation->Ranges)
                for (const auto& dependency : dependencies) if (!Append(dependency)) return false;
            return true;
        }
    };

    bool RHIRecordingLease::Empty() const { return !m_State || m_State->Resources.empty(); }
    bool RHIRecordingLease::IsLive() const {
        if (Empty()) return false;
        for (const auto& resource : m_State->Resources) if (!resource.IsLive()) return false;
        return true;
    }

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
        if (kind == RHIResourceKind::Texture && aliases.size())
            generation->NativeTexture = static_cast<nri::Texture*>(const_cast<void*>(*aliases.begin()));
        if (kind == RHIResourceKind::Buffer && aliases.size())
            generation->NativeBuffer = static_cast<nri::Buffer*>(const_cast<void*>(*aliases.begin()));
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
    void RHIResourceService::DeferUntilDrain(std::function<void()> release) {
        if (!release) return;
        if (m_Owner && m_Owner->Live) m_Owner->Retire(std::move(release));
        else release();
    }
    std::function<void(std::function<void()>)> RHIResourceService::GetDeferredReleaser() const {
        std::weak_ptr<RHIResourceOwner> owner = m_Owner;
        return [owner](std::function<void()> release) {
            if (!release) return;
            if (auto context = owner.lock(); context && context->Live) context->Retire(std::move(release));
            else release();
        };
    }
    bool RHIResourceService::AppendPin(RHIRecordingLease& pins, const RHIResourceLease& resource) const {
        if (!IsOwnerThread() || !resource.IsLive() || resource.GetHandle().Epoch != GetEpoch()) return false;
        if (!pins.m_State) pins.m_State = std::make_shared<RHIRecordingState>();
        else if (pins.m_State.use_count() > 1) {
            auto replacement = std::make_shared<RHIRecordingState>();
            replacement->Excluded = pins.m_State->Excluded;
            for (const auto& prior : pins.m_State->Resources) if (!replacement->Append(prior)) return false;
            pins.m_State = std::move(replacement);
        }
        return pins.m_State->Append(resource);
    }
    RHIRecordingLease RHIResourceService::Pin(const RHIResourceLease& resource) const {
        RHIRecordingLease pins;
        if (!AppendPin(pins, resource)) pins.Reset();
        return pins;
    }
    bool RHIResourceService::IsInUse(const RHIResourceLease& resource) const {
        return resource.m_Generation && resource.m_Generation->Pins.load() != 0;
    }
    bool RHIResourceService::BeginRecording(const void* commands) {
        auto lease = AcquireNative(commands);
        if (!lease.IsLive() || lease.m_Generation->Kind != RHIResourceKind::Commands || IsInUse(lease) ||
            lease.m_Generation->Recording != RHIResourceGeneration::CommandState::Empty) return false;
        lease.m_Generation->Recording = RHIResourceGeneration::CommandState::Recording;
        return true;
    }
    bool RHIResourceService::EndRecording(const void* commands, bool executable) {
        auto lease = AcquireNative(commands);
        if (!lease.IsLive() || lease.m_Generation->Kind != RHIResourceKind::Commands) return false;
        executable = executable && !lease.m_Generation->InRendering && lease.m_Generation->Recording == RHIResourceGeneration::CommandState::Recording;
        lease.m_Generation->Recording = executable ? RHIResourceGeneration::CommandState::Executable : RHIResourceGeneration::CommandState::Abandoned;
        return executable;
    }
    bool RHIResourceService::IsRecording(const void* commands) const {
        auto lease = AcquireNative(commands);
        return lease.IsLive() && lease.m_Generation->Kind == RHIResourceKind::Commands &&
            lease.m_Generation->Recording == RHIResourceGeneration::CommandState::Recording;
    }
    bool RHIResourceService::SetRendering(const void* commands, bool rendering) {
        auto command = AcquireNative(commands);
        if (!IsRecording(commands) || command.m_Generation->InRendering == rendering) return false;
        command.m_Generation->InRendering = rendering;
        return true;
    }
    bool RHIResourceService::CanAliasAfterCopySource(const void* commands, const RHIResourceLease& previous, const RHIResourceLease& next) const {
        if (!IsOwnerThread() || !IsRecording(commands) || !previous.IsLive() || !next.IsLive() ||
            previous.GetHandle().Epoch != GetEpoch() || next.GetHandle().Epoch != GetEpoch() || previous.GetHandle() == next.GetHandle()) return false;
        const auto command = AcquireNative(commands);
        if (command.m_Generation->InRendering) return false;
        auto state = [&](const RHIResourceLease& resource) -> const RHITrackedState& {
            const auto found = command.m_Generation->PendingStates.find(resource.m_Generation.get());
            return found == command.m_Generation->PendingStates.end() ? resource.m_Generation->State : found->second.State;
        };
        const auto& before = state(previous); const auto& after = state(next);
        if (!before.Defined || !after.Defined || before.Texture != after.Texture) return false;
        if (!before.Texture) return before.Buffer.access == nri::AccessBits::COPY_SOURCE && after.Buffer.access == nri::AccessBits::NONE;
        return !before.Textures.empty() && !after.Textures.empty() &&
            std::all_of(before.Textures.begin(), before.Textures.end(), [](const auto& s) {
                return s.layout == nri::Layout::COPY_SOURCE && s.access == nri::AccessBits::COPY_SOURCE;
            }) && std::all_of(after.Textures.begin(), after.Textures.end(), [](const auto& s) {
                return s.layout == nri::Layout::UNDEFINED && s.access == nri::AccessBits::NONE;
            });
    }
    bool RHIResourceService::IsExecutable(const void* commands) const {
        auto lease = AcquireNative(commands);
        return lease.IsLive() && lease.m_Generation->Kind == RHIResourceKind::Commands &&
            lease.m_Generation->Recording == RHIResourceGeneration::CommandState::Executable;
    }
    bool RHIResourceService::RecordUse(const void* commands, const RHIResourceLease& resource) {
        auto lease = AcquireNative(commands);
        if (!lease.IsLive() || lease.m_Generation->Kind != RHIResourceKind::Commands ||
            lease.m_Generation->Recording != RHIResourceGeneration::CommandState::Recording) return false;
        if (!resource.IsLive()) { lease.m_Generation->Recording = RHIResourceGeneration::CommandState::Abandoned; return false; }
        if (lease.GetHandle() == resource.GetHandle()) return false;
        if (lease.m_Generation->AccessValidator) {
            for (const auto& access : GetAccesses(resource))
                if (!lease.m_Generation->AccessValidator(access)) {
                    lease.m_Generation->Recording = RHIResourceGeneration::CommandState::Abandoned;
                    return false;
                }
        }
        auto& uses = lease.m_Generation->RecordedUses;
        if (!uses.m_State) { uses.m_State = std::make_shared<RHIRecordingState>(); uses.m_State->Excluded = lease.m_Generation.get(); }
        if (!AppendPin(uses, resource)) {
            uses.Reset();
            lease.m_Generation->Recording = RHIResourceGeneration::CommandState::Abandoned;
            return false;
        }
        return true;
    }
    bool RHIResourceService::RecordNativeUse(const void* commands, const void* alias) {
        if (!IsOwnerThread()) return false;
        if (!alias) return true;
        auto command = AcquireNative(commands);
        if (!command) return !m_Owner || !m_Owner->Aliases.contains(commands);
        auto resource = AcquireNative(alias);
        if (!resource && m_Owner->Aliases.contains(alias)) {
            command.m_Generation->Recording = RHIResourceGeneration::CommandState::Abandoned;
            return false;
        }
        if (command.m_Generation->AccessValidator && !resource) {
            command.m_Generation->Recording = RHIResourceGeneration::CommandState::Abandoned;
            return false;
        }
        if (resource && resource.m_Generation->Views.contains(alias)) {
            const auto [access, layout] = resource.m_Generation->Views.at(alias);
            if (!ValidateAccess(commands, alias, access, layout)) return false;
        }
        return !resource || RecordUse(commands, resource);
    }
    void RHIResourceService::DefineViewAccess(const void* alias, nri::AccessBits access, nri::Layout layout) {
        auto resource = AcquireNative(alias);
        if (IsOwnerThread() && resource.IsLive()) resource.m_Generation->Views[alias] = {access, layout};
    }
    std::vector<RHIResourceAccess> RHIResourceService::GetAccesses(const RHIResourceLease& resource) const {
        std::vector<RHIResourceAccess> result;
        const auto pins = Pin(resource);
        if (!pins.IsLive()) return result;
        for (const auto& lease : pins.m_State->Resources) {
            const auto& generation = *lease.m_Generation;
            if (generation.NativeTexture || generation.NativeBuffer)
                result.push_back({lease, generation.NativeTexture, generation.NativeBuffer});
            for (const auto& [slot, accesses] : generation.RangeAccesses)
                result.insert(result.end(), accesses.begin(), accesses.end());
        }
        return result;
    }
    bool RHIResourceService::SetAccessValidator(const void* commands, const void* owner,
        std::function<bool(const RHIResourceAccess&)> validator) {
        auto command = AcquireNative(commands);
        if (!owner || !validator || !IsRecording(commands) ||
            (command.m_Generation->ValidatorOwner && command.m_Generation->ValidatorOwner != owner)) return false;
        command.m_Generation->ValidatorOwner = owner;
        command.m_Generation->AccessValidator = std::move(validator);
        return true;
    }
    void RHIResourceService::ClearAccessValidator(const void* commands, const void* owner) {
        auto command = AcquireNative(commands);
        if (command.IsLive() && command.m_Generation->ValidatorOwner == owner) {
            command.m_Generation->AccessValidator = {};
            command.m_Generation->ValidatorOwner = nullptr;
        }
    }
    bool RHIResourceService::HasAccessValidator(const void* commands) const {
        auto command = AcquireNative(commands);
        return command.IsLive() && bool(command.m_Generation->AccessValidator);
    }
    bool RHIResourceService::ValidateAccess(const void* commands, const void* alias, nri::AccessBits access, nri::Layout layout) {
        auto command = AcquireNative(commands);
        if (!IsRecording(commands)) return false;
        if (!command.m_Generation->AccessValidator) return true;
        auto resource = AcquireNative(alias);
        if (resource.IsLive() && command.m_Generation->AccessValidator({resource,
            resource.m_Generation->NativeTexture, resource.m_Generation->NativeBuffer, access, layout})) return true;
        command.m_Generation->Recording = RHIResourceGeneration::CommandState::Abandoned;
        return false;
    }
    bool RHIResourceService::RecordUses(const void* commands, const RHIRecordingLease& resources) {
        if (resources.Empty()) return true;
        if (!resources.IsLive()) return false;
        for (const auto& resource : resources.m_State->Resources) if (!RecordUse(commands, resource)) return false;
        return true;
    }
    bool RHIResourceService::SnapshotSubmission(const void* const* commands, uint32_t count, RHIRecordingLease& pins) const {
        if (!IsOwnerThread() || (count && !commands)) return false;
        std::unordered_set<const void*> unique;
        for (uint32_t i = 0; i < count; ++i) {
            if (!unique.insert(commands[i]).second) return false;
            if (!IsExecutable(commands[i])) return false;
            auto lease = AcquireNative(commands[i]);
            if (!AppendPin(pins, lease)) return false;
            const auto& uses = lease.m_Generation->RecordedUses;
            if (uses.m_State) for (const auto& resource : uses.m_State->Resources)
                if (!AppendPin(pins, resource)) return false;
        }
        return true;
    }
    void RHIResourceService::MarkSubmitted(const void* const* commands, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            auto lease = AcquireNative(commands[i]);
            if (lease.IsLive()) {
                lease.m_Generation->Recording = RHIResourceGeneration::CommandState::Submitted;
                lease.m_Generation->RecordedUses.Reset();
                lease.m_Generation->PendingStates.clear();
            }
        }
    }
    void RHIResourceService::AbandonRecording(const void* commands) {
        auto lease = AcquireNative(commands);
        if (lease.IsLive() && !IsInUse(lease)) {
            lease.m_Generation->Recording = RHIResourceGeneration::CommandState::Empty;
            lease.m_Generation->InRendering = false;
            lease.m_Generation->RecordedUses.Reset();
            lease.m_Generation->PendingStates.clear();
        }
    }
    bool RHIResourceService::ReplaceDescriptorRange(const RHIResourceLease& lease, uint32_t range, uint32_t baseSlot, std::vector<RHIResourceLease> resources, const std::vector<const void*>& aliases) {
        if (resources.size() != aliases.size()) return false;
        if (!IsOwnerThread() || !lease.IsLive() || lease.GetHandle().Epoch != GetEpoch() ||
            lease.m_Generation->Kind != RHIResourceKind::DescriptorSet || IsInUse(lease)) return false;
        for (const auto& resource : resources) if (!resource.IsLive() || resource.GetHandle().Epoch != GetEpoch()) return false;
        if (resources.size() > UINT32_MAX - baseSlot) return false;
        auto replacement = lease.m_Generation->Ranges;
        auto accessReplacement = lease.m_Generation->RangeAccesses;
        for (uint32_t i = 0; i < resources.size(); ++i) {
            const auto key = (static_cast<uint64_t>(range) << 32) | (baseSlot + i);
            auto accesses = GetAccesses(resources[i]);
            if (const auto view = resources[i].m_Generation->Views.find(aliases[i]); view != resources[i].m_Generation->Views.end())
                accesses.push_back({resources[i], resources[i].m_Generation->NativeTexture,
                    resources[i].m_Generation->NativeBuffer, view->second.first, view->second.second});
            accessReplacement[key] = std::move(accesses);
            replacement[key] = {std::move(resources[i])};
        }
        lease.m_Generation->Ranges.swap(replacement);
        lease.m_Generation->RangeAccesses.swap(accessReplacement);
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
// Included by RHIResource.cpp: state lives in the same generation as its payload.
namespace {
    bool SameState(nri::AccessLayoutStage a, nri::AccessLayoutStage b) {
        return a.access == b.access && a.layout == b.layout && a.stages == b.stages;
    }
    bool SameState(nri::AccessStage a, nri::AccessStage b) { return a.access == b.access && a.stages == b.stages; }
    nri::StageBits MergeReadStages(nri::StageBits a, nri::StageBits b) {
        // ALL is NRI's zero-valued sentinel, not a normal bitmask member.
        return a == nri::StageBits::ALL || b == nri::StageBits::ALL ? nri::StageBits::ALL : static_cast<nri::StageBits>(a | b);
    }
    bool Writes(nri::AccessBits access) {
        constexpr auto writes = nri::AccessBits::COLOR_ATTACHMENT_WRITE | nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE |
            nri::AccessBits::SHADER_RESOURCE_STORAGE | nri::AccessBits::COPY_DESTINATION | nri::AccessBits::RESOLVE_DESTINATION |
            nri::AccessBits::HOST_WRITE | nri::AccessBits::SCRATCH_BUFFER;
        return access & writes;
    }
    bool TextureRange(const RHITrackedState& state, uint16_t mip, uint16_t mipNum, uint16_t layer, uint16_t layerNum,
                      nri::PlaneBits planes, std::vector<size_t>& indices) {
        if (!state.Defined || !state.Texture || mip >= state.Mips || layer >= state.Layers) return false;
        const uint32_t mips = mipNum == nri::REMAINING ? state.Mips - mip : mipNum;
        const uint32_t layers = layerNum == nri::REMAINING ? state.Layers - layer : layerNum;
        if (!mips || !layers || mips > state.Mips - mip || layers > state.Layers - layer) return false;
        uint8_t available = 0; for (auto plane : state.Planes) available |= static_cast<uint8_t>(plane);
        const auto requested = planes == nri::PlaneBits::ALL ? available : static_cast<uint8_t>(planes);
        if (!requested || (requested & ~available)) return false;
        for (size_t p = 0; p < state.Planes.size(); ++p) if (requested & static_cast<uint8_t>(state.Planes[p]))
            for (uint32_t l = layer; l < layer + layers; ++l)
                for (uint32_t m = mip; m < mip + mips; ++m)
                    indices.push_back((p * state.Layers + l) * state.Mips + m);
        return true;
    }
}

bool RHIResourceService::DefineTextureState(const RHIResourceLease& resource, uint16_t mips, uint16_t layers,
    nri::PlaneBits planes, nri::AccessLayoutStage initial) {
    if (!IsOwnerThread() || !resource.IsLive() || resource.GetHandle().Epoch != GetEpoch() || !mips || !layers ||
        resource.m_Generation->State.Defined || resource.m_Generation->Kind != RHIResourceKind::Texture) return false;
    RHITrackedState state; state.Texture = state.Defined = true; state.Mips = mips; state.Layers = layers;
    const auto mask = static_cast<uint8_t>(planes);
    if (!mask || mask & static_cast<uint8_t>(nri::PlaneBits::NONE)) return false;
    for (unsigned bit = 0; bit < 7; ++bit) if (mask & (1u << bit)) state.Planes.push_back(static_cast<nri::PlaneBits>(1u << bit));
    state.Textures.assign(static_cast<size_t>(mips) * layers * state.Planes.size(), initial);
    resource.m_Generation->State = std::move(state);
    return true;
}
bool RHIResourceService::DefineBufferState(const RHIResourceLease& resource, nri::AccessStage initial) {
    if (!IsOwnerThread() || !resource.IsLive() || resource.GetHandle().Epoch != GetEpoch() ||
        resource.m_Generation->State.Defined || resource.m_Generation->Kind != RHIResourceKind::Buffer) return false;
    auto& state = resource.m_Generation->State; state.Defined = true; state.Buffer = initial;
    state.HostAccess = static_cast<nri::AccessBits>(initial.access & (nri::AccessBits::HOST_READ | nri::AccessBits::HOST_WRITE));
    return true;
}
RHIResourceLease RHIResourceService::ImportTexture(nri::Texture* texture, const RHIResourceLease& owner,
    uint16_t mips, uint16_t layers, nri::PlaneBits planes, nri::AccessLayoutStage initial) {
    if (!texture || !owner.IsLive() || owner.GetHandle().Epoch != GetEpoch()) return {};
    auto imported = Publish(RHIResourceKind::Texture, "Imported texture", {texture}, {}, {owner});
    if (!DefineTextureState(imported, mips, layers, planes, initial)) return {};
    return imported;
}
RHIResourceLease RHIResourceService::ImportBuffer(nri::Buffer* buffer, const RHIResourceLease& owner, nri::AccessStage initial) {
    if (!buffer || !owner.IsLive() || owner.GetHandle().Epoch != GetEpoch()) return {};
    auto imported = Publish(RHIResourceKind::Buffer, "Imported buffer", {buffer}, {}, {owner});
    if (!DefineBufferState(imported, initial)) return {};
    return imported;
}
bool RHIResourceService::SetTextureState(const RHIResourceLease& resource, nri::AccessLayoutStage next,
    uint16_t mip, uint16_t mips, uint16_t layer, uint16_t layers, nri::PlaneBits planes) {
    if (!IsOwnerThread() || !resource.IsLive() || IsInUse(resource) || resource.GetHandle().Epoch != GetEpoch()) return false;
    auto& state = resource.m_Generation->State;
    std::vector<size_t> indices;
    if (!TextureRange(state, mip, mips, layer, layers, planes, indices)) return false;
    for (auto index : indices) state.Textures[index] = next;
    ++state.Version; return true;
}
bool RHIResourceService::SetBufferState(const RHIResourceLease& resource, nri::AccessStage next) {
    if (!IsOwnerThread() || !resource.IsLive() || IsInUse(resource) || resource.GetHandle().Epoch != GetEpoch()) return false;
    auto& state = resource.m_Generation->State;
    if (!state.Defined || state.Texture) return false;
    state.Buffer = next; ++state.Version; return true;
}
std::optional<nri::AccessLayoutStage> RHIResourceService::GetTextureState(RHIResourceHandle handle, uint16_t mip,
    uint16_t layer, nri::PlaneBits plane) const {
    auto resource = Acquire(handle); if (!resource.IsLive()) return {};
    const auto& state = resource.m_Generation->State;
    std::vector<size_t> indices;
    if (!TextureRange(state, mip, 1, layer, 1, plane, indices) || indices.empty()) return {};
    auto result = state.Textures[indices.front()];
    for (auto index : indices) if (!SameState(result, state.Textures[index])) return {};
    return result;
}
std::optional<nri::AccessStage> RHIResourceService::GetBufferState(RHIResourceHandle handle) const {
    auto resource = Acquire(handle);
    if (!resource.IsLive() || !resource.m_Generation->State.Defined || resource.m_Generation->State.Texture) return {};
    return resource.m_Generation->State.Buffer;
}
bool RHIResourceService::BuildTextureBarriers(const void* commands, const nri::TextureBarrierDesc& request,
    std::vector<nri::TextureBarrierDesc>& barriers) {
    auto command = AcquireNative(commands), resource = AcquireNative(request.texture);
    if (!IsRecording(commands) || !resource.IsLive() || !resource.m_Generation->State.Defined || request.srcQueue || request.dstQueue) return false;
    if (!ValidateAccess(commands, request.texture, request.after.access, request.after.layout)) return false;
    auto* target = resource.m_Generation.get();
    std::vector<size_t> indices;
    if (!TextureRange(target->State, request.mipOffset, request.mipNum, request.layerOffset, request.layerNum, request.planes, indices)) return false;
    auto& pending = command.m_Generation->PendingStates;
    auto found = pending.find(target);
    if (found == pending.end()) found = pending.emplace(target, RHIPendingState{resource, target->State.Version, target->State}).first;
    auto& state = found->second.State;
    bool changed = false;
    for (auto index : indices) {
        const auto before = state.Textures[index];
        auto after = request.after;
        const bool readOnly = before.layout == after.layout && !Writes(before.access) && !Writes(after.access) &&
            (after.access != nri::AccessBits::NONE || before.access == nri::AccessBits::NONE);
        if (readOnly && before.access != nri::AccessBits::NONE && after.access != nri::AccessBits::NONE) {
            after.access |= before.access; after.stages = MergeReadStages(after.stages, before.stages);
        }
        const bool barrierNeeded = !readOnly && (!SameState(before, after) || Writes(before.access) || Writes(after.access));
        if (barrierNeeded) {
            if (command.m_Generation->InRendering) return false;
            changed = true;
            auto barrier = request;
            barrier.before = before;
            barrier.after = after;
            barrier.mipOffset = static_cast<uint16_t>(index % state.Mips); barrier.mipNum = 1;
            barrier.layerOffset = static_cast<uint16_t>((index / state.Mips) % state.Layers); barrier.layerNum = 1;
            barrier.planes = state.Planes[index / (static_cast<size_t>(state.Mips) * state.Layers)];
            auto& device = RHIDevice::Get();
            if (before.layout == nri::Layout::UNDEFINED && after.layout != nri::Layout::UNDEFINED &&
                after.layout != nri::Layout::COPY_DESTINATION && device.GetBackend() == RHIType::Direct3D12 && device.IsNativeReady() &&
                resource.GetHandle().Epoch == device.GetEpoch()) {
                const auto& desc = device.GetNRICore().GetTextureDesc(*request.texture);
                nri::AccessLayoutStage initialization{};
                if (desc.usage & nri::TextureUsageBits::COLOR_ATTACHMENT)
                    initialization = {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT, nri::StageBits::COLOR_ATTACHMENT};
                else if (desc.usage & nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT)
                    initialization = {nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE, nri::Layout::DEPTH_STENCIL_ATTACHMENT, nri::StageBits::DEPTH_STENCIL_ATTACHMENT};
                // Only compressed attachment layouts initialize placed-resource metadata
                // through an enhanced discard barrier. Resolve layouts alone do not.
                if (initialization.access != nri::AccessBits::NONE && after.layout != initialization.layout) {
                    auto initialize = barrier; initialize.after = initialization;
                    barriers.push_back(initialize); barrier.before = initialization;
                }
            }
            barriers.push_back(barrier);
        }
        changed = changed || !SameState(before, after);
        state.Textures[index] = after;
    }
    if (changed) ++state.Version;
    return true;
}
bool RHIResourceService::BuildBufferBarrier(const void* commands, const nri::BufferBarrierDesc& request,
    std::vector<nri::BufferBarrierDesc>& barriers) {
    auto command = AcquireNative(commands), resource = AcquireNative(request.buffer);
    if (!IsRecording(commands) || !resource.IsLive() || !resource.m_Generation->State.Defined || resource.m_Generation->State.Texture) return false;
    if (!ValidateAccess(commands, request.buffer, request.after.access)) return false;
    auto* target = resource.m_Generation.get();
    auto& pending = command.m_Generation->PendingStates;
    auto found = pending.find(target);
    if (found == pending.end()) found = pending.emplace(target, RHIPendingState{resource, target->State.Version, target->State}).first;
    auto& state = found->second.State;
    auto after = request.after;
    const bool hostBefore = state.Buffer.access & (nri::AccessBits::HOST_WRITE | nri::AccessBits::HOST_READ);
    if (after.access & (nri::AccessBits::HOST_WRITE | nri::AccessBits::HOST_READ)) return false;
    const bool reads = !hostBefore && !Writes(state.Buffer.access) && !Writes(after.access) &&
        (after.access != nri::AccessBits::NONE || state.Buffer.access == nri::AccessBits::NONE);
    if (reads && state.Buffer.access != nri::AccessBits::NONE && after.access != nri::AccessBits::NONE) {
        after.access |= state.Buffer.access; after.stages = MergeReadStages(after.stages, state.Buffer.stages);
    }
    const auto& device = RHIDevice::Get();
    const bool fixedHost = device.IsNativeReady() && resource.GetHandle().Epoch == device.GetEpoch() &&
        device.GetBackend() == RHIType::Direct3D12 && state.HostAccess != nri::AccessBits::NONE;
    if (fixedHost && ((state.HostAccess == nri::AccessBits::HOST_READ && after.access != nri::AccessBits::COPY_DESTINATION && after.access != nri::AccessBits::NONE) ||
        (state.HostAccess == nri::AccessBits::HOST_WRITE && Writes(after.access)))) return false;
    if (fixedHost && Writes(after.access) && command.m_Generation->InRendering) return false;
    const bool barrierNeeded = !fixedHost && !reads && !hostBefore && (!SameState(state.Buffer, after) || Writes(after.access));
    if (barrierNeeded) {
        if (command.m_Generation->InRendering) return false;
        auto barrier = request; barrier.before = state.Buffer; barrier.after = after; barriers.push_back(barrier);
    }
    if (!SameState(state.Buffer, after) || barrierNeeded) {
        ++state.Version;
    }
    // Host access is completed/flushed before submission. Pinned NRI forbids
    // HOST access masks in buffer barriers; the submission provides visibility.
    state.Buffer = after;
    return true;
}
std::shared_ptr<RHIStateSubmission> RHIResourceService::PrepareSubmissionStates(const void* const* commands, uint32_t count) const {
    if (!IsOwnerThread()) return {};
    auto result = std::make_shared<RHIStateSubmission>();
    std::unordered_map<RHIResourceGeneration*, size_t> predicted;
    for (uint32_t i = 0; i < count; ++i) {
        auto command = AcquireNative(commands[i]); if (!command.IsLive()) return {};
        for (const auto& [target, pending] : command.m_Generation->PendingStates) {
            auto it = predicted.find(target);
            const auto version = it == predicted.end() ? target->State.Version : result->Published[it->second].State.Version;
            if (!pending.Resource.IsLive() || pending.BaseVersion != version) return {};
            if (it == predicted.end()) { predicted[target] = result->Published.size(); result->Published.push_back(pending); }
            else result->Published[it->second] = pending;
        }
    }
    return result;
}
void RHIResourceService::CommitSubmissionStates(const std::shared_ptr<RHIStateSubmission>& submission) {
    if (!submission || !IsOwnerThread()) return;
    for (auto& pending : submission->Published) pending.Resource.m_Generation->State = std::move(pending.State);
}
std::vector<nri::Texture*> RHIResourceService::GetPinnedTextures(const RHIRecordingLease& pins) const {
    std::vector<nri::Texture*> textures;
    if (!IsOwnerThread() || pins.Empty()) return textures;
    // Texture aliases include views. The native texture is the first published
    // alias, retained explicitly in generation metadata below.
    for (const auto& resource : pins.m_State->Resources)
        if (resource.IsLive() && resource.m_Generation->State.Texture && resource.m_Generation->NativeTexture)
            textures.push_back(resource.m_Generation->NativeTexture);
    return textures;
}
std::vector<nri::Buffer*> RHIResourceService::GetPinnedBuffers(const RHIRecordingLease& pins) const {
    std::vector<nri::Buffer*> buffers;
    if (!IsOwnerThread() || pins.Empty()) return buffers;
    for (const auto& resource : pins.m_State->Resources)
        if (resource.IsLive() && resource.m_Generation->State.Defined && !resource.m_Generation->State.Texture && resource.m_Generation->NativeBuffer)
            buffers.push_back(resource.m_Generation->NativeBuffer);
    return buffers;
}
nri::Texture* RHIResourceService::GetTextureForAlias(const void* alias) const {
    auto resource = AcquireNative(alias);
    return resource.IsLive() ? resource.m_Generation->NativeTexture : nullptr;
}
nri::Buffer* RHIResourceService::GetBufferForAlias(const void* alias) const {
    auto resource = AcquireNative(alias);
    return resource.IsLive() ? resource.m_Generation->NativeBuffer : nullptr;
}

    RHIResourceLease AcquireRHIResource(const void* native) { return RHIDevice::Get().GetResources().AcquireNative(native); }
}
