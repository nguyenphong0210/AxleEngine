#include <Axle/RenderTransientFrame.hpp>
#include <algorithm>
#include <unordered_set>

namespace Axle {
    using Graph = RenderTransientGraph;
    namespace {
        bool CompileDeclarations(const std::vector<Graph::ResourceDesc>& resources, const std::vector<Graph::Pass>& passes,
                                 Graph::Plan& plan, std::string& error, bool allowAliasing = false) {
            Graph graph;
            for (const auto& resource : resources) graph.AddResource(resource);
            for (const auto& pass : passes) graph.AddPass(pass);
            return graph.Compile(plan, {allowAliasing}, &error);
        }
        bool AliasClassSupported(const Graph::Plan& plan, const Graph::Lifetime& life) {
            const auto& desc = plan.Resources[life.Id];
            if (!desc.AllowAlias || life.Key.MustBeDedicated) return false;
            const auto& first = plan.Passes[plan.PassOrder[life.FirstPass]].Uses;
            const auto& last = plan.Passes[plan.PassOrder[life.LastPass]].Uses;
            const auto initial = std::find_if(first.begin(), first.end(), [&](const auto& use) { return use.Id == life.Id; });
            const auto final = std::find_if(last.begin(), last.end(), [&](const auto& use) { return use.Id == life.Id; });
            if (final->State != Graph::Usage::CopySource || final->Mode != Graph::Access::Read || initial->Mode != Graph::Access::Write) return false;
            if (const auto* texture = std::get_if<RHI::RHITextureDesc>(&desc.Description)) {
                if (texture->Format != RHI::RHITextureFormat::RGBA8_UNORM || texture->MipLevels != 1 || texture->Depth != 1 ||
                    texture->ArrayLayers != 1 || texture->SampleCount != 1 || texture->IsCubeMap ||
                    !(texture->Usage & RHI::RHITextureUsage::ColorAttachment) ||
                    (texture->Usage & RHI::RHITextureUsage::Storage) || (texture->Usage & RHI::RHITextureUsage::HostTransfer)) return false;
                return initial->State == Graph::Usage::CopyDest ||
                    (initial->State == Graph::Usage::ColorAttachment && initial->LoadOp == Graph::Load::Clear);
            }
            return initial->State == Graph::Usage::CopyDest;
        }
        bool Transition(RHI::RHICommandBuffer& commands, const RHI::RHITransientView& view, const Graph::Use& use) {
            using Usage = Graph::Usage;
            using Layout = RHI::RHIResourceLayout;
            using Access = RHI::RHIAccess;
            if (!commands.KeepAlive(view.Lease)) return false;
            if (view.Texture) {
                Layout layout;
                switch (use.State) {
                    case Usage::ShaderResource: layout = Layout::ShaderResource; break;
                    case Usage::Storage: layout = Layout::ShaderResourceStorage; break;
                    case Usage::ColorAttachment: layout = Layout::ColorAttachment; break;
                    case Usage::DepthAttachment: layout = use.Mode == Graph::Access::Read ? Layout::DepthStencilRead : Layout::DepthStencilWrite; break;
                    case Usage::CopySource: layout = Layout::CopySource; break;
                    case Usage::CopyDest: layout = Layout::CopyDest; break;
                    case Usage::ResolveSource: layout = Layout::ResolveSource; break;
                    case Usage::ResolveDest: layout = Layout::ResolveDest; break;
                    default: return false;
                }
                commands.TransitionTexture(view.Texture, Layout::Undefined, layout);
            } else if (view.Buffer) {
                Access access;
                switch (use.State) {
                    case Usage::ShaderResource: access = Access::ShaderResourceRead; break;
                    case Usage::Storage: access = Access::ShaderResourceStorage; break;
                    case Usage::CopySource: access = Access::CopySource; break;
                    case Usage::CopyDest: access = Access::CopyDest; break;
                    case Usage::VertexBuffer: access = Access::VertexBuffer; break;
                    case Usage::IndexBuffer: access = Access::IndexBuffer; break;
                    case Usage::ConstantBuffer: access = Access::ConstantBuffer; break;
                    case Usage::IndirectBuffer: access = Access::ArgumentBuffer; break;
                    default: return false;
                }
                commands.TransitionBuffer(view.Buffer, Access::None, access);
            } else return false;
            return commands.IsRecording();
        }
    }
    struct RenderTransientFrame::Storage {
        uint64_t Epoch = 0;
        RHI::RHITransientResources Resources;
        std::vector<RHI::RHITransientView> Views;
        std::vector<uint32_t> Slots;
        Graph::Plan Plan;
        Diagnostics Info;
        std::string FirstError;
        size_t NextPass = 0;
        bool Begun = false, Failed = false, Poisoned = false, CallbackActive = false;
        RHI::RHICommandBuffer Commands;
    };
    RenderTransientFrame::RenderTransientFrame() = default;
    RenderTransientFrame::~RenderTransientFrame() = default;
    bool RenderTransientFrame::Fail(std::string error) {
        if (m_Pending && (m_Pending->Failed || m_Pending->Poisoned)) {
            if (m_Pending->FirstError.empty()) m_Pending->FirstError = error;
            m_Error = m_Pending->FirstError;
        } else m_Error = std::move(error);
        return false;
    }
    bool RenderTransientFrame::Prepare(const Graph::Plan& declarations, const std::vector<Import>& imports, AllocationMode mode) {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanSubmit() || m_Pending || m_Generation == UINT64_MAX ||
            (mode != AllocationMode::Committed && mode != AllocationMode::PlacedUnaliased && mode != AllocationMode::AliasingIfValidated))
            return Fail("Cannot prepare transient frame in current owner/state");
        auto candidate = std::make_unique<Storage>(); candidate->Epoch = device.GetEpoch();
        if (!CompileDeclarations(declarations.Resources, declarations.Passes, candidate->Plan, m_Error)) return false;
        candidate->Views.resize(candidate->Plan.Resources.size());
        candidate->Slots.resize(candidate->Plan.Resources.size(), UINT32_MAX);
        std::unordered_set<const void*> nativeImports;
        for (const auto& import : imports) {
            if (import.Id >= candidate->Views.size() || candidate->Plan.Resources[import.Id].Type != Graph::Kind::Imported ||
                candidate->Views[import.Id].IsLive() || !RHI::ValidateTransientView(import.View) ||
                !RHI::SameTransientDescription(import.View.Description, candidate->Plan.Resources[import.Id].Description))
                return Fail("Invalid/stale transient graph import");
            const void* native = import.View.Texture ? static_cast<const void*>(import.View.Texture) : import.View.Buffer;
            if (!native || !nativeImports.insert(native).second ||
                device.GetResources().AcquireNative(native).GetHandle() != import.View.Lease.GetHandle())
                return Fail("Imported native generation mismatch or duplicate alias");
            candidate->Views[import.Id] = import.View;
        }
        std::vector<RHI::RHITransientDesc> descriptions;
        std::vector<Graph::Resource> transientIds;
        for (Graph::Resource id = 0; id < candidate->Plan.Resources.size(); ++id) {
            const auto& desc = candidate->Plan.Resources[id];
            if (desc.Type == Graph::Kind::Imported) {
                if (!candidate->Views[id].IsLive()) return Fail("Missing transient graph import");
            } else { candidate->Slots[id] = static_cast<uint32_t>(descriptions.size()); transientIds.push_back(id); descriptions.push_back(desc.Description); }
        }
        auto actual = candidate->Plan.Resources;
        const bool pack = mode == AllocationMode::AliasingIfValidated && device.GetCapabilities().SupportsTransientResourceAliasing;
        std::vector<uint32_t> blocks;
        if (pack) {
            for (uint32_t i = 0; i < transientIds.size(); ++i) {
                RHI::RHITransientRequirements req;
                if (!RHI::RHITransientResources::QueryRequirements(descriptions[i], req)) return Fail("Transient placement requirements unavailable");
                actual[transientIds[i]].Memory = {req.SizeBytes, req.Alignment, 1, req.MemoryType, req.MustBeDedicated};
            }
            for (const auto& life : candidate->Plan.Lifetimes)
                if (!AliasClassSupported(candidate->Plan, life)) actual[life.Id].AllowAlias = false;
            if (!CompileDeclarations(actual, candidate->Plan.Passes, candidate->Plan, m_Error, true)) return false;
            for (const auto& allocation : candidate->Plan.Allocations) blocks.push_back(allocation.Block);
        }
        const bool committed = mode == AllocationMode::Committed || (mode == AllocationMode::AliasingIfValidated && !pack);
        const bool allocated = committed ? candidate->Resources.InitCommitted(descriptions) : candidate->Resources.InitPlaced(descriptions, blocks);
        if (!allocated) return Fail("Transient native allocation failed");
        for (uint32_t index = 0; index < transientIds.size(); ++index) {
            const auto id = transientIds[index];
            const auto* view = candidate->Resources.Get(index);
            const auto* req = candidate->Resources.GetRequirements(index);
            if (!view || !req) return Fail("Transient generation invalid after allocation");
            candidate->Views[id] = *view;
            actual[id].Memory = {req->SizeBytes, req->Alignment, 1, req->MemoryType, req->MustBeDedicated};
            if (candidate->Info.NativeRequestedBytes > UINT64_MAX - req->SizeBytes) return Fail("Native request statistics overflow");
            candidate->Info.NativeRequestedBytes += req->SizeBytes;
        }
        if (!CompileDeclarations(actual, candidate->Plan.Passes, candidate->Plan, m_Error, pack) || !candidate->Commands.Init())
            return Fail("Transient native plan/command creation failed");
        if (pack) for (uint32_t i = 0; i < blocks.size(); ++i)
            if (candidate->Plan.Allocations[i].Block != blocks[i]) return Fail("Native alias requirements changed during preparation");
        candidate->Plan.Diagnostic = committed ? "CommittedTransient: native candidates; aliasing disabled" :
            candidate->Plan.AliasEvents.empty() ? "PlacedUnaliased: distinct native blocks" : "AliasedTransient: validated copy/clear class; owner alias barriers";
        if (mode == AllocationMode::AliasingIfValidated && !pack) candidate->Plan.Diagnostic += "; unverified/unsupported aliasing: committed fallback";
        auto& info = candidate->Info;
        info.Valid = true; info.RequestedMode = mode; info.UsesPlacedMemory = !committed; info.UsesAliasing = !candidate->Plan.AliasEvents.empty();
        info.DeviceEpoch = device.GetEpoch(); info.Generation = m_Generation + 1;
        info.EstimatedPlanBytes = candidate->Plan.PlannedAllocationBytes; info.AllocatedBackingBytes = candidate->Resources.GetAllocatedBytes();
        info.ResourceCount = static_cast<uint32_t>(candidate->Plan.Resources.size());
        info.TransientCount = static_cast<uint32_t>(transientIds.size()); info.BlockCount = candidate->Plan.BlockCount;
        info.AliasEventCount = static_cast<uint32_t>(candidate->Plan.AliasEvents.size()); info.Diagnostic = candidate->Plan.Diagnostic;
        m_Pending = std::move(candidate); m_Error.clear();
        return true;
    }
    bool RenderTransientFrame::Record(Graph::PassId passId, const Recorder& recorder) {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanSubmit() || !m_Pending || m_Pending->Epoch != device.GetEpoch()) return Fail("No live pending transient generation");
        auto& frame = *m_Pending;
        if (frame.Failed || frame.Poisoned || frame.CallbackActive || !recorder || frame.NextPass >= frame.Plan.PassOrder.size() ||
            frame.Plan.PassOrder[frame.NextPass] != passId) {
            frame.Failed = true; return Fail("Transient pass differs from compiled order");
        }
        if (!frame.Begun) {
            if (!frame.Commands.Begin()) { frame.Failed = true; return Fail("Transient recording Begin failed"); }
            frame.Begun = true;
        }
        for (const auto& event : frame.Plan.AliasEvents) if (event.AfterPass == frame.NextPass) {
            const auto& uses = frame.Plan.Passes[passId].Uses;
            const auto initial = std::find_if(uses.begin(), uses.end(), [&](const auto& use) { return use.Id == event.Next; });
            const auto access = initial->State == Graph::Usage::ColorAttachment ? RHI::RHIAccess::ColorAttachment : RHI::RHIAccess::CopyDest;
            if (!frame.Resources.RecordAlias(frame.Commands, frame.Slots[event.Previous], frame.Slots[event.Next], access)) {
                frame.Failed = true; return Fail("Transient alias barrier failed");
            }
        }
        for (const auto& use : frame.Plan.Passes[passId].Uses) {
            if (!Transition(frame.Commands, frame.Views[use.Id], use)) {
                frame.Failed = true; return Fail("Transient use/transition failed");
            }
        }
        frame.CallbackActive = true;
        try {
            const bool recorded = recorder(frame.Commands);
            frame.CallbackActive = false;
            if (!recorded || frame.Failed || !frame.Commands.IsRecording()) {
                frame.Failed = true; return Fail("Transient pass recording failed");
            }
        } catch (...) { frame.CallbackActive = false; frame.Failed = true; return Fail("Transient pass callback threw"); }
        ++frame.NextPass;
        return true;
    }
    bool RenderTransientFrame::Submit() {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanSubmit() || !m_Pending || m_Pending->Epoch != device.GetEpoch()) return Fail("No live pending transient submission");
        auto& frame = *m_Pending;
        if (frame.CallbackActive) { frame.Failed = true; return Fail("Cannot submit inside a recording callback"); }
        if (frame.Failed || frame.Poisoned || frame.NextPass != frame.Plan.PassOrder.size()) return Fail("Incomplete transient frame");
        if (!frame.Begun && !frame.Commands.Begin()) { frame.Failed = true; return Fail("Empty transient Begin failed"); }
        if (!frame.Commands.End()) { frame.Failed = true; return Fail("Transient recording End failed"); }
        if (!frame.Commands.Submit()) { frame.Poisoned = true; return Fail("Transient submit failed; generation quarantined"); }
        m_Completion = device.GetLastSubmittedToken();
        m_Published = std::move(m_Pending); ++m_Generation; m_Error.clear();
        return true;
    }
    bool RenderTransientFrame::Abandon() {
        if (!RHI::RHIDevice::Get().IsOwnerThread() || (m_Pending && m_Pending->Poisoned)) return false;
        if (m_Pending && m_Pending->CallbackActive) { m_Pending->Failed = true; return false; }
        m_Pending.reset(); m_Error.clear(); return true;
    }
    const RHI::RHITransientView* RenderTransientFrame::Get(Graph::Resource resource) const {
        auto& device = RHI::RHIDevice::Get();
        const auto* frame = m_Pending ? m_Pending.get() : m_Published.get();
        if (!device.IsOwnerThread() || !frame || frame->Epoch != device.GetEpoch() || resource >= frame->Views.size()) return nullptr;
        if (frame->Plan.Mode == Graph::AllocationKind::IntervalPacked && frame->Plan.Resources[resource].Type != Graph::Kind::Imported) {
            if (!m_Pending || frame->NextPass >= frame->Plan.PassOrder.size()) return nullptr;
            const auto& uses = frame->Plan.Passes[frame->Plan.PassOrder[frame->NextPass]].Uses;
            if (std::none_of(uses.begin(), uses.end(), [&](const auto& use) { return use.Id == resource; })) return nullptr;
        }
        return frame->Views[resource].IsLive() ? &frame->Views[resource] : nullptr;
    }
    const Graph::Plan* RenderTransientFrame::GetPlan() const {
        const auto* frame = m_Pending ? m_Pending.get() : m_Published.get();
        return RHI::RHIDevice::Get().IsOwnerThread() && frame && frame->Epoch == RHI::RHIDevice::Get().GetEpoch() ? &frame->Plan : nullptr;
    }
    bool RenderTransientFrame::IsPoisoned() const { return m_Pending && m_Pending->Poisoned; }
    RenderTransientFrame::Diagnostics RenderTransientFrame::GetDiagnostics() const {
        auto& device = RHI::RHIDevice::Get();
        const auto* frame = m_Pending ? m_Pending.get() : m_Published.get();
        if (!device.IsOwnerThread() || !frame || frame->Epoch != device.GetEpoch()) return {};
        auto info = frame->Info;
        info.LastPublishedGeneration = m_Generation; info.FirstError = frame->FirstError;
        if (!m_Pending) { info.Lifecycle = State::Submitted; info.Completion = m_Completion; }
        else {
            info.RetainedPublishedBackingBytes = m_Published ? m_Published->Info.AllocatedBackingBytes : 0;
            info.Lifecycle = frame->Poisoned ? State::Poisoned : frame->Failed ? State::Failed :
                frame->NextPass == frame->Plan.PassOrder.size() && frame->Begun ? State::Ready :
                frame->Begun ? State::Recording : State::Prepared;
        }
        return info;
    }
}
