#pragma once

#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RenderCapabilities.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Axle {

    enum class BackendFeature : uint8_t {
        Compute,
        StorageTextures,
        TimestampQueries,
        DescriptorSets,
        HDRFormats,
        MSAA,
        FXAA,
        RayTracing,
        Count
    };

    enum class BackendFeatureState : uint8_t { Disabled, Fallback, Unavailable, Native };
    enum class BackendQualityTier : uint8_t { None, Compatibility, Standard, Advanced };

    struct BackendFeatureDecision {
        BackendFeatureState State = BackendFeatureState::Disabled;
        std::string Reason;
    };

    struct BackendPassCapabilities {
        bool NativeRHI = false;
        bool Compute = false;
        bool StorageTextures = false;
        bool Timestamps = false;
        bool RayTracing = false;
    };

    // Central backend policy. It converts raw device facts into the feature
    // contract consumed by passes and produces a safe copy of scene settings.
    // Authored settings are never mutated when a backend lacks a feature.
    class RenderBackendPolicy {
    public:
        static RenderBackendPolicy FromDevice(const RHI::RHIDeviceCapabilities& capabilities);
        static RenderBackendPolicy FromBackend(RendererBackendType backend);

        BackendFeatureDecision GetDecision(BackendFeature feature) const;
        bool Supports(BackendFeature feature) const;
        BackendQualityTier GetQualityTier() const { return m_QualityTier; }
        RHI::RHIType GetBackend() const { return m_Backend; }
        const std::string& GetBackendName() const { return m_BackendName; }
        const BackendPassCapabilities& GetPassCapabilities() const { return m_PassCapabilities; }

        SceneRenderSettings ResolveSettings(const SceneRenderSettings& authored) const;
        std::vector<std::string> CollectFallbacks(const SceneRenderSettings& authored) const;

    private:
        void Set(BackendFeature feature, BackendFeatureState state, std::string reason);

        RHI::RHIType m_Backend = RHI::RHIType::None;
        BackendQualityTier m_QualityTier = BackendQualityTier::None;
        std::string m_BackendName = "None";
        std::array<BackendFeatureDecision, static_cast<size_t>(BackendFeature::Count)> m_Features{};
        BackendPassCapabilities m_PassCapabilities{};
    };
}
