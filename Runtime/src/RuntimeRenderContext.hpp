#pragma once
#include <Axle/ApplicationUI.hpp>
#include <Axle/RHI/RHIPresentationContext.hpp>

namespace Axle {
    class RuntimeRenderContext final : public ApplicationRenderContext {
    public:
        explicit RuntimeRenderContext(const Window& window);
        bool Present(const Window& window) override;
        void Shutdown() override;
        uint64_t GetPresentCount() const { return m_Presenter.GetPresentCount(); }
    private:
        RHI::RHIPresentationContext m_Presenter;
    };
}
