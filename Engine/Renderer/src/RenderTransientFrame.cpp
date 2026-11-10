#include <Axle/RenderTransientFrame.hpp>
#include <unordered_set>

namespace Axle {
    using Graph = RenderTransientGraph;
    namespace {
        bool CompileDeclarations(const std::vector<Graph::ResourceDesc>& resources, const std::vector<Graph::Pass>& passes,
                                 Graph::Plan& plan, std::string& error) {
            Graph graph;
            for (const auto& resource : resources) graph.AddResource(resource);
            for (const auto& pass : passes) graph.AddPass(pass);
            return graph.Compile(plan, &error);
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
        Graph::Plan Plan;
        size_t NextPass = 0;
        bool Begun = false, Failed = false, Poisoned = false;
        RHI::RHICommandBuffer Commands;
    };
    RenderTransientFrame::RenderTransientFrame() = default;
    RenderTransientFrame::~RenderTransientFrame() = default;
    bool RenderTransientFrame::Fail(std::string error) { m_Error = std::move(error); return false; }
    bool RenderTransientFrame::Prepare(const Graph::Plan& declarations, const std::vector<Import>& imports) {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanSubmit() || m_Pending || m_Generation == UINT64_MAX) return Fail("Cannot prepare transient frame in current owner/state");
        auto candidate = std::make_unique<Storage>(); candidate->Epoch = device.GetEpoch();
        if (!CompileDeclarations(declarations.Resources, declarations.Passes, candidate->Plan, m_Error)) return false;
        candidate->Views.resize(candidate->Plan.Resources.size());
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
            } else { transientIds.push_back(id); descriptions.push_back(desc.Description); }
        }
        if (!candidate->Resources.InitCommitted(descriptions)) return Fail("Transient committed allocation failed");
        auto actual = candidate->Plan.Resources;
        for (uint32_t index = 0; index < transientIds.size(); ++index) {
            const auto id = transientIds[index];
            const auto* view = candidate->Resources.Get(index);
            const auto* req = candidate->Resources.GetRequirements(index);
            if (!view || !req) return Fail("Transient generation invalid after allocation");
            candidate->Views[id] = *view;
            actual[id].Memory = {req->SizeBytes, req->Alignment, 1, req->MemoryType, req->MustBeDedicated};
        }
        if (!CompileDeclarations(actual, candidate->Plan.Passes, candidate->Plan, m_Error) || !candidate->Commands.Init())
            return Fail("Transient native plan/command creation failed");
        m_Pending = std::move(candidate); m_Error.clear();
        return true;
    }
    bool RenderTransientFrame::Record(Graph::PassId passId, const Recorder& recorder) {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanSubmit() || !m_Pending || m_Pending->Epoch != device.GetEpoch()) return Fail("No live pending transient generation");
        auto& frame = *m_Pending;
        if (frame.Failed || frame.Poisoned || !recorder || frame.NextPass >= frame.Plan.PassOrder.size() ||
            frame.Plan.PassOrder[frame.NextPass] != passId) {
            frame.Failed = true; return Fail("Transient pass differs from compiled order");
        }
        if (!frame.Begun) {
            if (!frame.Commands.Begin()) { frame.Failed = true; return Fail("Transient recording Begin failed"); }
            frame.Begun = true;
        }
        for (const auto& use : frame.Plan.Passes[passId].Uses) {
            if (!Transition(frame.Commands, frame.Views[use.Id], use)) {
                frame.Failed = true; return Fail("Transient use/transition failed");
            }
        }
        try {
            if (!recorder(frame.Commands) || !frame.Commands.IsRecording()) {
                frame.Failed = true; return Fail("Transient pass recording failed");
            }
        } catch (...) { frame.Failed = true; return Fail("Transient pass callback threw"); }
        ++frame.NextPass;
        return true;
    }
    bool RenderTransientFrame::Submit() {
        auto& device = RHI::RHIDevice::Get();
        if (!device.CanSubmit() || !m_Pending || m_Pending->Epoch != device.GetEpoch()) return Fail("No live pending transient submission");
        auto& frame = *m_Pending;
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
        m_Pending.reset(); return true;
    }
    const RHI::RHITransientView* RenderTransientFrame::Get(Graph::Resource resource) const {
        auto& device = RHI::RHIDevice::Get();
        const auto* frame = m_Pending ? m_Pending.get() : m_Published.get();
        if (!device.IsOwnerThread() || !frame || frame->Epoch != device.GetEpoch() || resource >= frame->Views.size()) return nullptr;
        return frame->Views[resource].IsLive() ? &frame->Views[resource] : nullptr;
    }
    const Graph::Plan* RenderTransientFrame::GetPlan() const {
        const auto* frame = m_Pending ? m_Pending.get() : m_Published.get();
        return RHI::RHIDevice::Get().IsOwnerThread() && frame && frame->Epoch == RHI::RHIDevice::Get().GetEpoch() ? &frame->Plan : nullptr;
    }
    bool RenderTransientFrame::IsPoisoned() const { return m_Pending && m_Pending->Poisoned; }
}
