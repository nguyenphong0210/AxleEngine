#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/RHI/RHIFrameContext.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>

namespace Axle::RHI {
    struct RHIPresentationContext::Storage {
        RHISurface Surface;
        RHISwapChain SwapChain;
        RHISwapChainDesc Desc;
        std::vector<RHIFrameContext> Frames;
        std::vector<nri::Descriptor*> Views;
        std::vector<RHIResourceLease> ViewLeases;
        uint64_t Epoch = 0, Presented = 0;
        uint32_t Slot = 0;
        bool BuildFrameStorage() {
            auto& device = RHIDevice::Get(); const auto core = device.GetNRICore();
            std::vector<RHIFrameContext> frames(Desc.FramesInFlight);
            for (auto& frame : frames) if (!frame.Init()) return false;
            std::vector<nri::Descriptor*> views;
            std::vector<RHIResourceLease> leases;
            for (uint32_t i = 0; i < SwapChain.GetBufferCount(); ++i) {
                auto* texture = SwapChain.GetTexture(i);
                nri::TextureViewDesc desc{}; desc.texture = texture; desc.type = nri::TextureView::COLOR_ATTACHMENT;
                desc.format = core.GetTextureDesc(*texture).format; desc.mipNum = desc.layerNum = 1;
                nri::Descriptor* view = nullptr;
                if (ScopedRHIFault::Invoke("CreatePresentView", [&] { return core.CreateTextureView(desc, view); }) != nri::Result::SUCCESS || !view) return false;
                auto lease = device.GetResources().Publish(RHIResourceKind::DescriptorSet, "Presentation attachment", {view},
                    [core, view] { core.DestroyDescriptor(view); }, {device.GetResources().AcquireNative(texture)});
                if (!lease) { core.DestroyDescriptor(view); return false; }
                views.push_back(view); leases.push_back(std::move(lease));
            }
            Frames = std::move(frames); Views = std::move(views); ViewLeases = std::move(leases); Slot = 0; return true;
        }
        bool Reconfigure(uint32_t width, uint32_t height, bool vsync) {
            auto& device = RHIDevice::Get();
            if (!device.WaitIdle()) return false;
            for (auto& frame : Frames) if (!frame.Abandon()) return false;
            Frames.clear(); Views.clear(); ViewLeases.clear(); device.GetResources().CollectAfterDrain();
            Surface.Resize(width, height);
            if (!SwapChain.Configure(width, height, vsync)) return false;
            return BuildFrameStorage();
        }
    };
    RHIPresentationContext::RHIPresentationContext() = default;
    RHIPresentationContext::~RHIPresentationContext() { Shutdown(); }
    bool RHIPresentationContext::Init(GLFWwindow* window, const RHISwapChainDesc& desc) {
        auto& device = RHIDevice::Get();
        if (m_Storage || !device.CanCreateResources() || !window) return false;
        auto candidate = std::make_unique<Storage>(); candidate->Desc = desc; candidate->Epoch = device.GetEpoch();
        if (!candidate->Surface.Init(window) || !candidate->SwapChain.Init(candidate->Surface, desc) || !candidate->BuildFrameStorage()) return false;
        m_Storage = std::move(candidate); return true;
    }
    bool RHIPresentationContext::Shutdown() {
        if (!m_Storage) return true;
        auto& device = RHIDevice::Get();
        if (m_Storage->Epoch == device.GetEpoch() && device.IsNativeReady() && !device.WaitIdle()) return false;
        m_Storage.reset(); return true;
    }
    RHIPresentOutcome RHIPresentationContext::RenderFrame(uint32_t width, uint32_t height, bool vsync,
        const Recorder& prepare, const Recorder& draw, nri::Texture* blitSource) {
        auto& device = RHIDevice::Get();
        if (!m_Storage || m_Storage->Epoch != device.GetEpoch()) return RHIPresentOutcome::Failed;
        auto& state = *m_Storage;
        if (!width || !height) { state.SwapChain.Resize(0, 0); return RHIPresentOutcome::Skipped; }
        if (state.SwapChain.NeedsResize() || state.SwapChain.IsMinimized() || width != state.SwapChain.GetWidth() ||
            height != state.SwapChain.GetHeight() || vsync != state.SwapChain.IsVSync())
            if (!state.Reconfigure(width, height, vsync)) return RHIPresentOutcome::Failed;
        if (!device.CanSubmit() || state.Frames.empty()) return RHIPresentOutcome::Failed;
        auto& frame = state.Frames[state.Slot];
        if (!frame.Begin()) return RHIPresentOutcome::Failed;
        if (!state.SwapChain.BeginFrame()) {
            frame.Abandon();
            return state.SwapChain.NeedsResize() ? RHIPresentOutcome::RebuildRequired : RHIPresentOutcome::Failed;
        }
        auto abandon = [&] {
            frame.Abandon(); state.SwapChain.RequestRebuild(); return RHIPresentOutcome::Failed;
        };
        auto* texture = state.SwapChain.GetCurrentTexture();
        const auto format = device.GetNRICore().GetTextureDesc(*texture).format;
        auto& commands = *frame.GetCommands();
        if (prepare && !prepare(commands, format)) return abandon();

        if (blitSource) {
            commands.TransitionTexture(blitSource, RHIResourceLayout::Undefined, RHIResourceLayout::CopySource);
            commands.TransitionTexture(texture, RHIResourceLayout::Undefined, RHIResourceLayout::CopyDest);
            commands.CopyTexture(*texture, nullptr, *blitSource, nullptr);
            commands.TransitionTexture(blitSource, RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
        }

        const auto index = state.SwapChain.GetCurrentTextureIndex();
        if (index >= state.Views.size() || !commands.KeepAlive(state.ViewLeases[index])) return abandon();

        if (draw) {
            commands.TransitionTexture(texture, blitSource ? RHIResourceLayout::CopyDest : RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
            nri::AttachmentDesc color{}; color.descriptor = state.Views[index];
            color.loadOp = blitSource ? nri::LoadOp::LOAD : nri::LoadOp::CLEAR;
            color.storeOp = nri::StoreOp::STORE; color.clearValue.color.f = {0.04f, 0.05f, 0.07f, 1.0f};
            nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
            commands.BeginRendering(rendering);
            if (!draw(commands, format)) return abandon();
            commands.EndRendering();
            commands.TransitionTexture(texture, RHIResourceLayout::ColorAttachment, RHIResourceLayout::Present);
        } else if (blitSource) {
            commands.TransitionTexture(texture, RHIResourceLayout::CopyDest, RHIResourceLayout::Present);
        } else {
            commands.TransitionTexture(texture, RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
            nri::AttachmentDesc color{}; color.descriptor = state.Views[index]; color.loadOp = nri::LoadOp::CLEAR;
            color.storeOp = nri::StoreOp::STORE; color.clearValue.color.f = {0.04f, 0.05f, 0.07f, 1.0f};
            nri::RenderingDesc rendering{}; rendering.colors = &color; rendering.colorNum = 1;
            commands.BeginRendering(rendering);
            commands.EndRendering();
            commands.TransitionTexture(texture, RHIResourceLayout::ColorAttachment, RHIResourceLayout::Present);
        }

        if (!frame.End()) return abandon();
        const nri::CommandBuffer* native = commands.GetNative();
        const auto before = device.GetLastSubmittedToken();
        const bool presented = state.SwapChain.PresentFrame(&native, 1);
        const auto completion = device.GetLastSubmittedToken();
        if (completion != before) {
            if (!frame.AdoptSubmission(completion)) return RHIPresentOutcome::Failed;
            state.Slot = (state.Slot + 1) % static_cast<uint32_t>(state.Frames.size());
        }
        if (presented) { ++state.Presented; return RHIPresentOutcome::Presented; }
        return state.SwapChain.NeedsResize() ? RHIPresentOutcome::RebuildRequired : RHIPresentOutcome::Failed;
    }
    uint64_t RHIPresentationContext::GetPresentCount() const { return m_Storage ? m_Storage->Presented : 0; }
    bool RHIPresentationContext::IsVSync() const { return m_Storage && m_Storage->SwapChain.IsVSync(); }
}
