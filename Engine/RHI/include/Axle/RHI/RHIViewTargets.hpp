#pragma once
#include <Axle/RHI/RHITexture.hpp>
#include <array>
#include <memory>
#include <cstdint>

namespace Axle::RHI {
    enum class RHIViewTarget : uint8_t {
        Color, Depth, MSAAColor, MSAADepth, HDR, FXAA,
        OpaqueColor, OpaqueDepth, ParticleDepth, ScreenNormal, ScreenStorage, History0, History1,
        GTAO, Bloom0, Bloom1, Bloom2, Bloom3, Bloom4, Bloom5, Exposure, DDGI,
        HiZ0, HiZ1, HiZ2, HiZ3, HiZ4, HiZ5, HiZ6, HiZ7, Count
    };
    struct RHIViewTargetDesc {
        uint32_t Width = 1, Height = 1;
        RHITextureFormat ColorFormat = RHITextureFormat::RGBA8_UNORM;
        RHITextureFormat DepthFormat = RHITextureFormat::D32_FLOAT;
        RHITextureFormat NormalFormat = RHITextureFormat::RGBA16_FLOAT;
        uint8_t Samples = 1, HistoryCount = 0;
        bool EnableDepth = true, HDR = false, FXAA = false, OwnPrimary = true, RenderAuxiliary = true;
        bool EnableNormal = false, EnableScreenStorage = false, EnableHiZ = false, EnableBloom = false, EnableGTAO = false, EnableAutoExposure = false, EnableDDGI = false;
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
        RHITexture* GetHiZ(uint32_t level) const {
            return level < 8 ? Get(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::HiZ0) + level)) : nullptr;
        }
        RHITexture* GetGTAO() const { return Get(RHIViewTarget::GTAO); }
        RHITexture* GetExposure() const { return Get(RHIViewTarget::Exposure); }
        RHITexture* GetDDGI() const { return Get(RHIViewTarget::DDGI); }
        RHITexture* GetBloom(uint32_t level) const {
            return level < 6 ? Get(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::Bloom0) + level)) : nullptr;
        }
        const RHIViewTargetDesc& GetDesc() const { return m_Desc; }
        uint64_t GetGeneration() const { return m_Generation; }
        bool IsValid() const;
        // Validates the published generation against its descriptor contract.
        bool Validate() const;
    private:
        RHIViewTargetDesc m_Desc;
        std::array<std::unique_ptr<RHITexture>, static_cast<size_t>(RHIViewTarget::Count)> m_Textures;
        bool m_Initialized = false;
        uint64_t m_Generation = 0;
    };
}
