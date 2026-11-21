#pragma once

#include <Axle/SceneRenderSettings.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <cstdint>
#include <string>

namespace Axle {

    struct RenderQualityTierBudget {
        RenderQualityPreset Preset = RenderQualityPreset::High;
        const char* Name = "High";
        bool HalfResAO = true;
        bool EnableShadows = true;
        bool EnableSkybox = true;
        bool EnableGTAO = true;
        uint32_t GTAOSliceCount = 8u;
        uint32_t GTAOStepCount = 7u;
        bool EnableContactShadows = true;
        uint32_t ContactShadowSteps = 16u;
        bool EnableSSR = true;
        uint32_t SSRMaxSteps = 64u;
        bool EnablePlanarReflections = true;
        float PlanarResolutionScale = 0.50f;
        bool EnableSubsurfaceScattering = true;
        bool EnableVolumetricFog = true;
        FroxelFogQuality FogQuality = FroxelFogQuality::High;
        float FogTemporalWeight = 0.80f;
        bool EnableBloom = true;
        uint32_t BloomLevels = 6u;
        bool EnableAutoExposure = true;
        bool EnableFXAA = false;
        bool EnableTAA = true;
        uint8_t MSAASamples = 1u;
        uint32_t ReflectionMaxProbesPerFrame = 2u;
        uint32_t ReflectionMaxFacesPerFrame = 12u;
        uint64_t ReflectionCacheBudgetBytes = 64ull * 1024ull * 1024ull;
        bool ExperimentalDDGIOptIn = false;
        bool SupportsOptionalDDGI = false;
        bool RayTracingEnabled = false;
    };

    struct RenderQualityBudgetReport {
        RenderQualityPreset ResolvedPreset = RenderQualityPreset::Custom;
        std::string PresetName = "Custom";
        uint32_t ViewWidth = 0u;
        uint32_t ViewHeight = 0u;
        uint32_t AOWidth = 0u;
        uint32_t AOHeight = 0u;
        uint32_t GTAOSliceCount = 0u;
        uint32_t GTAOStepCount = 0u;
        uint32_t ContactShadowSteps = 0u;
        uint32_t SSRMaxSteps = 0u;
        uint32_t PlanarWidth = 0u;
        uint32_t PlanarHeight = 0u;
        RHI::RHIFroxelGridExtent FroxelExtent{};
        uint32_t HiZMipLevels = 0u;
        uint32_t BloomLevels = 0u;
        uint8_t ActiveMSAASamples = 1u;
        uint32_t ReflectionMaxProbesPerFrame = 0u;
        uint32_t ReflectionMaxFacesPerFrame = 0u;
        uint64_t ReflectionCacheBudgetBytes = 0ull;
        uint64_t FroxelMemoryBytes = 0ull;
        uint64_t DDGIMemoryBytes = 0ull;
        uint64_t ViewTargetEstimatedBytes = 0ull;
        uint64_t TotalEstimatedBudgetBytes = 0ull;
        std::string DDGIPolicy;
        std::string RayTracingPolicy;
        std::string Summary;
    };

    const char* RenderQualityPresetName(RenderQualityPreset preset);
    bool ParseRenderQualityPreset(const std::string& text, RenderQualityPreset& outPreset);
    const RenderQualityTierBudget& GetRenderQualityTierBudget(RenderQualityPreset preset);
    void ApplyRenderQualityPreset(SceneRenderSettings& settings,
                                  RenderQualityPreset preset,
                                  bool enableOptionalDDGI = false);
    RenderQualityPreset DetectRenderQualityPreset(const SceneRenderSettings& settings);
    RenderQualityBudgetReport ComputeRenderQualityBudgetReport(
        uint32_t viewWidth,
        uint32_t viewHeight,
        const SceneRenderSettings& authored,
        const RenderBackendPolicy* policy = nullptr,
        const RHI::RHIDDGIAtlasExtent* ddgiGrid = nullptr);

} // namespace Axle
