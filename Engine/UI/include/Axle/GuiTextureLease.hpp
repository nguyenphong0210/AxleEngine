#pragma once
#include <cstdint>
#include <memory>
#include <functional>

namespace Axle {
    namespace RHI { class RHITexture; }
    struct GuiTextureRegistration;
    // GetID acquires the current draw-list pin before publishing the image ID.
    // Registration cleanup owns its arena and never calls a panel or UI layer.
    class GuiTextureLease {
    public:
        GuiTextureLease() = default;
        uint64_t GetID() const;
        bool IsValid() const;
        void Reset() { m_Registration.reset(); }
    private:
        friend class ImGuiLayer;
        std::shared_ptr<GuiTextureRegistration> m_Registration;
    };
    using GuiTextureRegistrar = std::function<GuiTextureLease(const RHI::RHITexture&)>;
    using GuiDeferredReleaser = std::function<void(std::function<void()>)>;
}
