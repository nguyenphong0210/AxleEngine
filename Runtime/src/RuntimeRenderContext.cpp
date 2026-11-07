#include "RuntimeRenderContext.hpp"
#include <Axle/RenderService.hpp>
#include <Axle/Window.hpp>
#include <stdexcept>

namespace Axle {
    RuntimeRenderContext::RuntimeRenderContext(const Window& window) {
        RHI::RHISwapChainDesc desc; desc.VSync = window.IsVSync(); desc.FramesInFlight = 3;
        if (!m_Presenter.Init(window.GetNativeWindow(), desc)) throw std::runtime_error("Runtime presentation initialization failed");
    }
    bool RuntimeRenderContext::Present(const Window& window) {
        auto* target = RenderService::Get().GetDefaultMainColorTarget();
        if (!target || !target->IsValid()) return false;
        return m_Presenter.RenderFrame(window.GetWidth(),window.GetHeight(),window.IsVSync(),{},{},target->GetNative()) != RHI::RHIPresentOutcome::Failed;
    }
    void RuntimeRenderContext::Shutdown() { m_Presenter.Shutdown(); }
}
