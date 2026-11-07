#pragma once
#include <Axle/RHI/RHITexture.hpp>
#include <array>
#include <memory>

namespace Axle::RHI {
    enum class RHIViewTarget : uint8_t {
        Color, Depth, MSAAColor, MSAADepth, HDR, FXAA,
        OpaqueColor, OpaqueDepth, ParticleDepth, History0, History1, Count
    };
    struct RHIViewTargetDesc {
        uint32_t Width = 1, Height = 1;
        RHITextureFormat ColorFormat = RHITextureFormat::RGBA8_UNORM;
        RHITextureFormat DepthFormat = RHITextureFormat::D32_FLOAT;
        uint8_t Samples = 1, HistoryCount = 0;
        bool EnableDepth = true, HDR = false, FXAA = false, OwnPrimary = true, RenderAuxiliary = true;
        bool operator==(const RHIViewTargetDesc&) const = default;
    };
    // One view owns its complete target generation. Reconfiguration constructs
    // every required native texture/view before publishing any replacement.
    class RHIViewTargets {
    public:
        bool Configure(const RHIViewTargetDesc& desc, const RHITexture* externalColor = nullptr,
                       const RHITexture* externalDepth = nullptr);
        RHITexture* Get(RHIViewTarget target) const {
            const auto slot = static_cast<size_t>(target); return slot < m_Textures.size() ? m_Textures[slot].get() : nullptr;
        }
        const RHIViewTargetDesc& GetDesc() const { return m_Desc; }
        bool IsValid() const;
    private:
        RHIViewTargetDesc m_Desc;
        std::array<std::unique_ptr<RHITexture>, static_cast<size_t>(RHIViewTarget::Count)> m_Textures;
        bool m_Initialized = false;
    };
}
