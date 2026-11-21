#pragma once
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Axle::RenderSettingsSync {

    inline int ClampMSAA(int samples) {
        if (samples >= 8) return 8;
        if (samples >= 4) return 4;
        if (samples >= 2) return 2;
        return 1;
    }

    inline float ClampFinite(float v, float lo, float hi, float fallback) {
        if (!std::isfinite(v)) return fallback;
        return std::clamp(v, lo, hi);
    }

    template<typename TProjectRenderSettings>
    inline void ClampProjectRenderSettings(TProjectRenderSettings& cfg) {
        cfg.FogQuality = std::clamp(cfg.FogQuality, 0, 3);
        cfg.FogTemporalWeight = ClampFinite(cfg.FogTemporalWeight, 0.0f, 0.9f, 0.8f);
        cfg.FogOverride = std::clamp(cfg.FogOverride, -1, 1);
        cfg.DDGIOverride = std::clamp(cfg.DDGIOverride, -1, 1);

        cfg.ToneMappingMode = std::clamp(cfg.ToneMappingMode, 0, 3);
        cfg.Exposure = ClampFinite(cfg.Exposure, 0.05f, 16.0f, 1.0f);
        cfg.Gamma = ClampFinite(cfg.Gamma, 1.0f, 4.0f, 2.2f);
        cfg.WhitePoint = ClampFinite(cfg.WhitePoint, 0.5f, 32.0f, 4.0f);
        cfg.AutoExposureMin = ClampFinite(cfg.AutoExposureMin, 0.001f, 16.0f, 0.1f);
        cfg.AutoExposureMax = std::clamp(
            std::isfinite(cfg.AutoExposureMax) ? cfg.AutoExposureMax : 10.0f,
            std::max(0.05f, cfg.AutoExposureMin + 0.01f),
            64.0f);
        cfg.AutoExposureSpeed = ClampFinite(cfg.AutoExposureSpeed, 0.05f, 32.0f, 3.0f);
        cfg.AutoExposureKeyValue = ClampFinite(cfg.AutoExposureKeyValue, 0.01f, 2.0f, 0.18f);
        cfg.MSAASamples = ClampMSAA(cfg.MSAASamples);

        cfg.PlanarReflectionResolutionScale = ClampFinite(
            cfg.PlanarReflectionResolutionScale,
            RHI::RHIViewTargets::MinPlanarResolutionScale,
            RHI::RHIViewTargets::MaxPlanarResolutionScale,
            0.5f);
        cfg.PlanarReflectionStrength = ClampFinite(cfg.PlanarReflectionStrength, 0.0f, 2.0f, 1.0f);
        cfg.PlanarReflectionMaxRoughness = ClampFinite(cfg.PlanarReflectionMaxRoughness, 0.02f, 1.0f, 0.45f);
        cfg.ReflectionCaptureMaxProbesPerFrame = std::clamp(cfg.ReflectionCaptureMaxProbesPerFrame, 1, 8);
        cfg.ReflectionCaptureMaxFacesPerFrame = std::clamp(cfg.ReflectionCaptureMaxFacesPerFrame, 1, 48);
        constexpr uint64_t kMinBudget = 8ull * 1024ull * 1024ull;
        constexpr uint64_t kMaxBudget = 512ull * 1024ull * 1024ull;
        cfg.ReflectionCaptureCacheBudgetBytes = std::clamp(cfg.ReflectionCaptureCacheBudgetBytes, kMinBudget, kMaxBudget);
    }

    template<typename TProjectRenderSettings>
    inline void ApplyToSceneSettings(const TProjectRenderSettings& project,
                                     SceneRenderSettings& dst,
                                     const std::optional<bool>& fogOverride = std::nullopt,
                                     bool hasAuthoredFog = false,
                                     const std::optional<bool>& ddgiOverride = std::nullopt,
                                     bool hasAuthoredDDGI = false,
                                     const std::optional<bool>& skyboxOverride = std::nullopt) {
        TProjectRenderSettings clamped = project;
        ClampProjectRenderSettings(clamped);

        dst.EnableShadows = clamped.EnableShadows;
        dst.EnableSkybox = skyboxOverride.value_or(clamped.EnableSkybox);
        dst.EnableGTAO = clamped.EnableGTAO;
        dst.EnableContactShadows = clamped.EnableContactShadows;
        dst.EnableBloom = clamped.EnableBloom;
        dst.EnableSubsurfaceScattering = clamped.EnableSubsurfaceScattering;

        const std::optional<bool> projectFogOverride = (clamped.FogOverride >= 0)
            ? std::optional<bool>(clamped.FogOverride != 0)
            : std::nullopt;
        const std::optional<bool> effectiveFogOverride = fogOverride.has_value() ? fogOverride : projectFogOverride;
        dst.EnableVolumetricFog = effectiveFogOverride.value_or(clamped.EnableVolumetricFog && hasAuthoredFog);
        dst.FogQuality = static_cast<FroxelFogQuality>(clamped.FogQuality);
        dst.FogTemporalWeight = clamped.FogTemporalWeight;

        const std::optional<bool> projectDDGIOverride = (clamped.DDGIOverride >= 0)
            ? std::optional<bool>(clamped.DDGIOverride != 0)
            : std::nullopt;
        const std::optional<bool> effectiveDDGIOverride = ddgiOverride.has_value() ? ddgiOverride : projectDDGIOverride;
        dst.EnableDDGI = effectiveDDGIOverride.value_or(clamped.EnableDDGI && hasAuthoredDDGI);

        dst.EnableToneMapping = clamped.EnableToneMapping;
        dst.ToneMappingMode = clamped.ToneMappingMode;
        dst.Exposure = clamped.Exposure;
        dst.Gamma = clamped.Gamma;
        dst.WhitePoint = clamped.WhitePoint;
        dst.EnableAutoExposure = clamped.EnableAutoExposure;
        dst.AutoExposureMin = clamped.AutoExposureMin;
        dst.AutoExposureMax = clamped.AutoExposureMax;
        dst.AutoExposureSpeed = clamped.AutoExposureSpeed;
        dst.AutoExposureKeyValue = clamped.AutoExposureKeyValue;

        dst.EnableTAA = clamped.EnableTAA;
        dst.EnableFXAA = clamped.EnableFXAA && !clamped.EnableTAA;
        dst.MSAASamples = static_cast<uint8_t>(clamped.MSAASamples);

        dst.EnableSSR = clamped.EnableSSR;
        dst.EnablePlanarReflections = clamped.EnablePlanarReflections;
        dst.PlanarReflectionResolutionScale = clamped.PlanarReflectionResolutionScale;
        dst.PlanarReflectionStrength = clamped.PlanarReflectionStrength;
        dst.PlanarReflectionMaxRoughness = clamped.PlanarReflectionMaxRoughness;
        dst.ReflectionCaptureMaxProbesPerFrame = static_cast<uint32_t>(clamped.ReflectionCaptureMaxProbesPerFrame);
        dst.ReflectionCaptureMaxFacesPerFrame = static_cast<uint32_t>(clamped.ReflectionCaptureMaxFacesPerFrame);
        dst.ReflectionCaptureCacheBudgetBytes = clamped.ReflectionCaptureCacheBudgetBytes;
    }

    template<typename TProjectRenderSettings>
    inline void SyncFromSceneSettings(const SceneRenderSettings& src,
                                      TProjectRenderSettings& dst,
                                      int fogOverride = -1,
                                      int ddgiOverride = -1) {
        dst.EnableShadows = src.EnableShadows;
        dst.EnableSkybox = src.EnableSkybox;
        dst.EnableGTAO = src.EnableGTAO;
        dst.EnableContactShadows = src.EnableContactShadows;
        dst.EnableBloom = src.EnableBloom;
        dst.EnableSubsurfaceScattering = src.EnableSubsurfaceScattering;
        dst.FogQuality = static_cast<int>(src.FogQuality);
        dst.FogTemporalWeight = src.FogTemporalWeight;
        dst.FogOverride = fogOverride;
        dst.DDGIOverride = ddgiOverride;

        dst.EnableToneMapping = src.EnableToneMapping;
        dst.ToneMappingMode = src.ToneMappingMode;
        dst.Exposure = src.Exposure;
        dst.Gamma = src.Gamma;
        dst.WhitePoint = src.WhitePoint;
        dst.EnableAutoExposure = src.EnableAutoExposure;
        dst.AutoExposureMin = src.AutoExposureMin;
        dst.AutoExposureMax = src.AutoExposureMax;
        dst.AutoExposureSpeed = src.AutoExposureSpeed;
        dst.AutoExposureKeyValue = src.AutoExposureKeyValue;
        dst.EnableFXAA = src.EnableFXAA;
        dst.EnableTAA = src.EnableTAA;
        dst.MSAASamples = static_cast<int>(src.MSAASamples);

        dst.EnableSSR = src.EnableSSR;
        dst.EnablePlanarReflections = src.EnablePlanarReflections;
        dst.PlanarReflectionResolutionScale = src.PlanarReflectionResolutionScale;
        dst.PlanarReflectionStrength = src.PlanarReflectionStrength;
        dst.PlanarReflectionMaxRoughness = src.PlanarReflectionMaxRoughness;
        dst.ReflectionCaptureMaxProbesPerFrame = static_cast<int>(src.ReflectionCaptureMaxProbesPerFrame);
        dst.ReflectionCaptureMaxFacesPerFrame = static_cast<int>(src.ReflectionCaptureMaxFacesPerFrame);
        dst.ReflectionCaptureCacheBudgetBytes = src.ReflectionCaptureCacheBudgetBytes;

        ClampProjectRenderSettings(dst);
    }

} // namespace Axle::RenderSettingsSync
