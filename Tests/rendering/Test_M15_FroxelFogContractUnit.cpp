#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/LightingService.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/ViewContext.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace Axle;
using namespace Axle::FroxelFogContract;

#ifndef AXLE_RHI_VIEW_TARGETS_SOURCE_PATH
#define AXLE_RHI_VIEW_TARGETS_SOURCE_PATH "Engine/RHI/src/RHIViewTargets.cpp"
#endif

#ifndef AXLE_RENDER_SERVICE_SOURCE_PATH
#define AXLE_RENDER_SERVICE_SOURCE_PATH "Engine/Scene/src/RenderService.cpp"
#endif

#ifndef AXLE_RHI_VIEWPORT_DESCRIPTOR_SOURCE_PATH
#define AXLE_RHI_VIEWPORT_DESCRIPTOR_SOURCE_PATH "Engine/UI/src/RHIViewportDescriptor.cpp"
#endif

#ifndef AXLE_SCENE_RENDERER_SOURCE_PATH
#define AXLE_SCENE_RENDERER_SOURCE_PATH "Engine/Scene/src/RHISceneRenderer.cpp"
#endif

#ifndef AXLE_VOLUMETRIC_FOG_SHADER_PATH
#define AXLE_VOLUMETRIC_FOG_SHADER_PATH "Engine/Resources/Shaders/VolumetricFog.hlsl"
#endif

#ifndef AXLE_FROXEL_FOG_PASS_SOURCE_PATH
#define AXLE_FROXEL_FOG_PASS_SOURCE_PATH "Engine/Scene/src/Passes/FroxelFogPass.cpp"
#endif

namespace {
    std::string ReadText(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
}

bool RunM15FroxelFogContractUnitTests() {
    std::cout << "[M15FroxelFog] Running Milestone 15 Unit 6 Batch A (Froxel 3D Resources & Tiers) Contract Tests...\n";

    // 1. M15-U6-01.1: RHITextureDesc::StorageTexture3D & RHIViewTargetDesc froxel contract
    {
        const auto texDesc = RHI::RHITextureDesc::StorageTexture3D(
            160u, 90u, 64u, RHI::RHITextureFormat::RGBA16_FLOAT, "FroxelRef");
        RENDER_CHECK(texDesc.Width == 160u && texDesc.Height == 90u && texDesc.Depth == 64u &&
                     texDesc.MipLevels == 1u && texDesc.ArrayLayers == 1u && texDesc.SampleCount == 1u &&
                     texDesc.IsVolume && !texDesc.IsCubeMap &&
                     texDesc.Format == RHI::RHITextureFormat::RGBA16_FLOAT &&
                     (texDesc.Usage & RHI::RHITextureUsage::ShaderResource) &&
                     (texDesc.Usage & RHI::RHITextureUsage::Storage),
            "M15-U6-01.1: StorageTexture3D descriptor declares single-mip 3D RGBA16F SRV+UAV volume");

        RHI::RHIViewTargetDesc defaultViewDesc;
        RENDER_CHECK(!defaultViewDesc.EnableVolumetricFog &&
                     defaultViewDesc.FroxelQuality == static_cast<uint8_t>(FroxelFogQuality::High),
            "M15-U6-01.1: RHIViewTargetDesc defaults volumetric fog disabled with High tier");

        // ResolveViewTargetOptions gating across MSAA, sub-capture, and all 4 tiers
        SceneRenderSettings settings;
        bool enable = true;
        uint8_t quality = 99u;
        ResolveViewTargetOptions(settings, false, true, enable, quality);
        RENDER_CHECK(!enable && quality == static_cast<uint8_t>(FroxelFogQuality::High),
            "M15-U6-01.1: disabled volumetric fog allocates no froxel targets and normalizes tier");

        settings.EnableVolumetricFog = true;
        for (uint8_t q = 0; q <= RHI::RHIViewTargets::MaxFroxelQualityTier; ++q) {
            settings.FogQuality = static_cast<FroxelFogQuality>(q);
            settings.MSAASamples = 1u;
            ResolveViewTargetOptions(settings, false, true, enable, quality);
            RENDER_CHECK(enable && quality == q,
                "M15-U6-01.1 / 01.2: single-sample main view forwards requested froxel quality tier");

            ResolveViewTargetOptions(settings, true, true, enable, quality);
            RENDER_CHECK(!enable && quality == static_cast<uint8_t>(FroxelFogQuality::High),
                "M15-U6-01.1: sub-capture views suppress froxel allocation");
        }

        for (uint8_t msaa : { uint8_t{2}, uint8_t{4}, uint8_t{8} }) {
            settings.MSAASamples = msaa;
            settings.FogQuality = FroxelFogQuality::Ultra;
            ResolveViewTargetOptions(settings, false, true, enable, quality);
            RENDER_CHECK(!enable,
                "M15-U6-01.1: MSAA views do not allocate unusable single-sample 3D froxel targets");
        }
        settings.MSAASamples = 1u;
        settings.FogQuality = static_cast<FroxelFogQuality>(255u);
        ResolveViewTargetOptions(settings, false, true, enable, quality);
        RENDER_CHECK(enable && quality == static_cast<uint8_t>(FroxelFogQuality::High),
            "Invalid authored quality falls back to the reference tier");
        ResolveViewTargetOptions(settings, false, false, enable, quality);
        RENDER_CHECK(!enable, "Depthless views allocate no froxel targets");
        settings.FogNearDistance = std::numeric_limits<float>::quiet_NaN();
        ResolveViewTargetOptions(settings, false, true, enable, quality);
        RENDER_CHECK(!enable, "An invalid depth range suppresses 3D froxel allocation");
    }

    // 2. M15-U6-01.1: ValidateResourcePolicy rejects invalid configurations and enforces budgets
    {
        RHI::RHIViewTargetDesc validDesc;
        validDesc.Width = 1920u;
        validDesc.Height = 1080u;
        validDesc.EnableVolumetricFog = true;
        validDesc.FroxelQuality = static_cast<uint8_t>(FroxelFogQuality::High);

        FroxelFogResourcePolicy policy;
        std::string reason;
        RENDER_CHECK(ValidateResourcePolicy(validDesc, policy, &reason) && reason.empty(),
            "M15-U6-01.1: valid 1080p High froxel policy validates cleanly");

        auto zeroExt = validDesc;
        zeroExt.Width = 0u;
        RENDER_CHECK(!ValidateResourcePolicy(zeroExt, policy, &reason) && !reason.empty(),
            "M15-U6-01.1: rejects zero viewport extent");

        auto noAux = validDesc;
        noAux.RenderAuxiliary = false;
        RENDER_CHECK(!ValidateResourcePolicy(noAux, policy, &reason) && !reason.empty(),
            "M15-U6-01.1: rejects froxel allocation when auxiliary targets are disabled");

        auto msaaDesc = validDesc;
        msaaDesc.Samples = 4u;
        RENDER_CHECK(!ValidateResourcePolicy(msaaDesc, policy, &reason) && !reason.empty(),
            "M15-U6-01.1: rejects MSAA froxel policy without explicit opt-in");

        auto badTier = validDesc;
        badTier.FroxelQuality = 4u;
        RENDER_CHECK(!ValidateResourcePolicy(badTier, policy, &reason) && !reason.empty(),
            "M15-U6-01.2: rejects out-of-range froxel quality tier");

        auto badNear = policy;
        badNear.NearDistance = 0.01f;
        RENDER_CHECK(!ValidateResourcePolicy(validDesc, badNear, &reason),
            "M15-U6-01.1: rejects near distance below MinNearDistance");

        auto badFar = policy;
        badFar.MaxDistance = policy.NearDistance;
        RENDER_CHECK(!ValidateResourcePolicy(validDesc, badFar, &reason),
            "M15-U6-01.1: rejects far distance <= near distance");
        badFar.MaxDistance = std::nextafter(policy.NearDistance, std::numeric_limits<float>::infinity());
        RENDER_CHECK(!ValidateResourcePolicy(validDesc, badFar, &reason),
            "M15-U6-01.1: rejects a depth interval too narrow to represent distinct float slices");

        auto tightBudget = policy;
        tightBudget.MaxBudgetBytes = 8ull * 1024ull * 1024ull;
        RENDER_CHECK(!ValidateResourcePolicy(validDesc, tightBudget, &reason),
            "M15-U6-07.5: rejects froxel allocation exceeding explicit budget ceiling");
    }

    // 3. M15-U6-01.2: Quality tier scaling, 160x90x64 reference, strict monotonicity & Z-slice mapping
    {
        RENDER_CHECK(GetQualityTierCount() == 4u,
            "M15-U6-01.2: exposes 4 quality tiers (Low, Medium, High, Ultra)");

        const auto high720  = ComputeGridExtent(1280u, 720u, FroxelFogQuality::High);
        const auto high1080 = ComputeGridExtent(1920u, 1080u, FroxelFogQuality::High);
        const auto high1440 = ComputeGridExtent(2560u, 1440u, FroxelFogQuality::High);
        const auto high4k   = ComputeGridExtent(3840u, 2160u, FroxelFogQuality::High);
        const auto highOdd  = ComputeGridExtent(1919u, 1079u, FroxelFogQuality::High);
        const RHI::RHIFroxelGridExtent refExtent{ ReferenceGridWidth, ReferenceGridHeight, ReferenceGridDepth };
        RENDER_CHECK(high720 == refExtent && high1080 == refExtent && high1440 == refExtent &&
                     high4k == refExtent && highOdd == refExtent,
            "M15-U6-01.2: High tier produces exact 160x90x64 documented reference across 720p/1080p/1440p/4K/odd");

        // Invalid dimensions and tiers return zero extents cleanly
        RENDER_CHECK(ComputeGridExtent(0u, 1080u, FroxelFogQuality::High) == RHI::RHIFroxelGridExtent{} &&
                     ComputeGridExtent(1920u, 0u, FroxelFogQuality::High) == RHI::RHIFroxelGridExtent{} &&
                     ComputeGridExtent(1920u, 1080u, static_cast<FroxelFogQuality>(9)) == RHI::RHIFroxelGridExtent{},
            "M15-U6-01.2: rejects zero extents and invalid quality tiers cleanly");

        // Verify FroxelFogContract matches RHIViewTargets across tiers and resolutions + strict monotonicity
        const std::vector<std::pair<uint32_t, uint32_t>> testResolutions = {
            {1u, 1u}, {3u, 5u}, {64u, 64u}, {128u, 128u}, {640u, 360u},
            {1280u, 720u}, {1919u, 1079u}, {1920u, 1080u}, {2560u, 1440u}, {3840u, 2160u}
        };
        for (const auto [w, h] : testResolutions) {
            uint64_t prevTotalFroxels = 0u;
            for (uint8_t tier = 0u; tier < 4u; ++tier) {
                const auto q = static_cast<FroxelFogQuality>(tier);
                const auto contractExtent = ComputeGridExtent(w, h, q);
                const auto rhiExtent = RHI::RHIViewTargets::ComputeFroxelGridExtent(w, h, tier);
                const auto spec = GetTierSpec(q);
                RENDER_CHECK(contractExtent == rhiExtent &&
                             contractExtent.Width >= 1u && contractExtent.Height >= 1u &&
                             contractExtent.Width <= spec.Limit.Width &&
                             contractExtent.Height <= spec.Limit.Height &&
                             contractExtent.Depth == spec.Limit.Depth,
                    "M15-U6-01.2: FroxelFogContract and RHIViewTargets agree and stay within bounded tier caps");
                const uint64_t totalFroxels = uint64_t(contractExtent.Width) * contractExtent.Height * contractExtent.Depth;
                RENDER_CHECK(totalFroxels > prevTotalFroxels,
                    "M15-U6-01.2: total froxel count increases strictly monotonically across Low < Medium < High < Ultra");
                prevTotalFroxels = totalFroxels;
            }
        }

        // Exponential Z-slice mapping invariants across all tier slice counts (32, 48, 64, 96)
        constexpr float nearZ = 0.25f;
        constexpr float farZ = 100.0f;
        for (uint8_t tier = 0u; tier < 4u; ++tier) {
            const uint32_t slices = GetTierSpec(static_cast<FroxelFogQuality>(tier)).Limit.Depth;
            const float zStart = SliceBoundaryToViewDistance(0u, slices, nearZ, farZ);
            const float zEnd = SliceBoundaryToViewDistance(slices, slices, nearZ, farZ);
            RENDER_CHECK(std::abs(zStart - nearZ) < 1e-5f && std::abs(zEnd - farZ) < 1e-4f,
                "M15-U6-01.1: slice boundaries cover exact [near, max] range");
            float prevThickness = 0.0f;
            for (uint32_t s = 0u; s < slices; ++s) {
                const float b0 = SliceBoundaryToViewDistance(s, slices, nearZ, farZ);
                const float c = SliceCenterToViewDistance(s, slices, nearZ, farZ);
                const float b1 = SliceBoundaryToViewDistance(s + 1u, slices, nearZ, farZ);
                const float thickness = EvaluateSliceThickness(s, slices, nearZ, farZ);
                const float coord = ViewDistanceToSliceCoord(c, slices, nearZ, farZ);
                const float recovered = SliceCoordToViewDistance(coord, nearZ, farZ);
                RENDER_CHECK(b0 < c && c < b1 && thickness > prevThickness &&
                             std::abs(thickness - (b1 - b0)) < 1e-4f &&
                             std::abs(recovered - c) < 1e-3f,
                    "M15-U6-01.1: exponential depth slices are monotonic, invert cleanly, and preserve near precision");
                prevThickness = thickness;
            }
        }
    }

    // 4. M15-U6-07.5 (initial): Exact logical memory budgets per tier at 1080p/1440p
    {
        const auto low = EstimateMemoryBudget(1920u, 1080u, FroxelFogQuality::Low);
        const auto med = EstimateMemoryBudget(1920u, 1080u, FroxelFogQuality::Medium);
        const auto high = EstimateMemoryBudget(1920u, 1080u, FroxelFogQuality::High);
        const auto ultra = EstimateMemoryBudget(1920u, 1080u, FroxelFogQuality::Ultra);

        RENDER_CHECK(low.FroxelCount == 115200ull && low.TotalLogicalBytes == 3686400ull &&
                     med.FroxelCount == 442368ull && med.TotalLogicalBytes == 14155776ull &&
                     high.FroxelCount == 921600ull && high.TotalLogicalBytes == 29491200ull &&
                     ultra.FroxelCount == 3110400ull && ultra.TotalLogicalBytes == 99532800ull &&
                     ultra.TotalLogicalBytes < 100ull * 1024ull * 1024ull,
            "M15-U6-07.5: 4x RGBA16F froxel volume budgets match exact byte totals and stay bounded under 100 MiB");

        const auto high4k = EstimateMemoryBudget(3840u, 2160u, FroxelFogQuality::High);
        RENDER_CHECK(high4k.TotalLogicalBytes == high.TotalLogicalBytes,
            "M15-U6-07.5: 4K High tier stays capped at reference 160x90x64 memory budget");
    }

    // 5. ViewContext policy hashing, backend compatibility & source wiring parity
    {
        SceneRenderSettings base;
        base.EnableVolumetricFog = true;
        base.FogQuality = FroxelFogQuality::High;
        const uint64_t hHigh = HashViewFeaturePolicy(base);

        auto tierChange = base;
        tierChange.FogQuality = FroxelFogQuality::Low;
        RENDER_CHECK(HashViewFeaturePolicy(tierChange) != hHigh,
            "M15-U6-01.2: HashViewFeaturePolicy tracks FogQuality changes");

        auto nearChange = base;
        nearChange.FogNearDistance = 0.5f;
        RENDER_CHECK(HashViewFeaturePolicy(nearChange) != hHigh,
            "M15-U6-01.1: HashViewFeaturePolicy tracks FogNearDistance changes");

        auto maxChange = base;
        maxChange.FogMaxDistance = 200.0f;
        RENDER_CHECK(HashViewFeaturePolicy(maxChange) != hHigh,
            "M15-U6-01.1: HashViewFeaturePolicy tracks FogMaxDistance changes");

        const auto gl = RenderBackendPolicy::FromBackend(RendererBackendType::OpenGL_Compatibility);
        const auto resolvedGL = gl.ResolveSettings(base);
        RENDER_CHECK(!resolvedGL.EnableVolumetricFog,
            "M15-U6-01.1: OpenGL compatibility policy keeps volumetric fog disabled without mutating authored tier");

        const std::string rhiViewTargetsSrc = ReadText(AXLE_RHI_VIEW_TARGETS_SOURCE_PATH);
        const std::string renderServiceSrc = ReadText(AXLE_RENDER_SERVICE_SOURCE_PATH);
        const std::string viewportDescSrc = ReadText(AXLE_RHI_VIEWPORT_DESCRIPTOR_SOURCE_PATH);
        const std::string sceneRendererSrc = ReadText(AXLE_SCENE_RENDERER_SOURCE_PATH);
        RENDER_CHECK(rhiViewTargetsSrc.find("FroxelScattering") != std::string::npos &&
                     rhiViewTargetsSrc.find("FroxelIntegrated") != std::string::npos &&
                     rhiViewTargetsSrc.find("FroxelHistory0") != std::string::npos &&
                     rhiViewTargetsSrc.find("FroxelHistory1") != std::string::npos &&
                     renderServiceSrc.find("ResolveViewTargetOptions") != std::string::npos &&
                     viewportDescSrc.find("ResolveViewTargetOptions") != std::string::npos &&
                     sceneRendererSrc.find("ResolveViewTargetOptions") != std::string::npos,
            "M15-U6-01.1: RHIViewTargets and all three view-target builders share ResolveViewTargetOptions");
    }

    // 6. M15-U6-05.3 & 06: ResolveMediaState removes clamps and invented fallbacks
    {
        LightingService lightingSvc;

        RenderWorld unlitWorld;
        SceneRenderSettings unlitSettings;
        unlitSettings.EnableVolumetricFog = true;
        unlitSettings.FogDensity = 0.0f;
        unlitSettings.FogHeightFalloff = 0.0f;
        unlitSettings.FogNearDistance = 0.25f;
        unlitSettings.FogMaxDistance = 100.0f;
        const auto unlitFrame = lightingSvc.BuildFrame(unlitWorld, unlitSettings);
        const auto zeroMedia = ResolveMediaState(unlitWorld, unlitSettings, unlitFrame);
        RENDER_CHECK(zeroMedia.Valid &&
                     zeroMedia.Density == 0.0f &&
                     zeroMedia.HeightFalloff == 0.0f &&
                     zeroMedia.EffectiveBaseDensity == 0.0f &&
                     !zeroMedia.HasDirectionalSun &&
                     !zeroMedia.HasAmbientLight &&
                     zeroMedia.SunRadiance == glm::vec3(0.0f) &&
                     zeroMedia.AmbientRadiance == glm::vec3(0.0f) &&
                     zeroMedia.Diagnostic.find("froxel_fog_no_directional_sun") != std::string::npos,
            "M15-U6-05.3 / 06: zero density & falloff are unclamped and unlit scenes have zero radiance with explicit diagnostic");

        // Zero intensity on world component also collapses EffectiveBaseDensity to exact zero
        RenderWorld zeroIntensityWorld;
        zeroIntensityWorld.VolumetricFog = RenderVolumetricFog{
            .Enabled = true,
            .Density = 0.12f,
            .HeightFalloff = 0.0f,
            .ScatteringAsymmetry = 0.65f,
            .Color = glm::vec3(0.6f, 0.7f, 0.9f),
            .MaxDistance = 80.0f,
            .Intensity = 0.0f
        };
        const auto zeroIntMedia = ResolveMediaState(zeroIntensityWorld, unlitSettings, lightingSvc.BuildFrame(zeroIntensityWorld, unlitSettings));
        RENDER_CHECK(zeroIntMedia.Valid && zeroIntMedia.FromWorldComponent &&
                     std::abs(zeroIntMedia.Density - 0.12f) < 1e-6f &&
                     zeroIntMedia.Intensity == 0.0f &&
                     zeroIntMedia.EffectiveBaseDensity == 0.0f,
            "M15-U6-05.3: VolumetricFog Intensity=0 zeros EffectiveBaseDensity cleanly");

        // Active DirectionalLight uses unit toward-light direction and artist-to-radiance scale
        RenderWorld litWorld;
        RenderDirectionalLight sun;
        sun.Direction = glm::normalize(glm::vec3(1.0f, 2.0f, -1.0f));
        sun.Color = glm::vec3(1.0f, 0.9f, 0.75f);
        sun.Intensity = 2.0f;
        sun.AmbientIntensity = 0.08f;
        litWorld.DirectionalLight = sun;
        litWorld.VolumetricFog = RenderVolumetricFog{
            .Enabled = true,
            .Density = 0.05f,
            .HeightFalloff = 0.25f,
            .ScatteringAsymmetry = 0.7f,
            .Color = glm::vec3(0.8f, 0.85f, 0.95f),
            .MaxDistance = 64.0f,
            .Intensity = 1.5f
        };
        const auto litFrame = lightingSvc.BuildFrame(litWorld, unlitSettings);
        const auto litMedia = ResolveMediaState(litWorld, unlitSettings, litFrame);
        const glm::vec3 expectedRadiance = sun.Color * (sun.Intensity * 100000.0f * DirectLuxToRadiance);
        RENDER_CHECK(litMedia.Valid && litMedia.HasDirectionalSun && litMedia.HasAmbientLight &&
                     glm::length(litMedia.SunDirectionToLight - sun.Direction) < 1e-5f &&
                     glm::length(litMedia.SunRadiance - expectedRadiance) < 1e-4f &&
                     std::abs(litMedia.EffectiveBaseDensity - (0.05f * 1.5f)) < 1e-6f,
            "M15-U6-03.2 / 06: directional sun preserves toward-light sign and scene-linear radiance calibration");

        // Invalid authored MaxDistance triggers diagnostic fallback to settings range
        RenderWorld badDistWorld = litWorld;
        badDistWorld.VolumetricFog->MaxDistance = 0.1f; // <= nearDistance
        const auto fallbackMedia = ResolveMediaState(badDistWorld, unlitSettings, litFrame);
        RENDER_CHECK(fallbackMedia.Valid && fallbackMedia.AuthoredMaxDistanceInvalidFallback &&
                     std::abs(fallbackMedia.MaxDistance - unlitSettings.FogMaxDistance) < 1e-5f &&
                     fallbackMedia.Diagnostic.find("froxel_fog_depth_fallback") != std::string::npos,
            "M15-U6-06: invalid authored VolumetricFog MaxDistance falls back cleanly with explicit diagnostic");

        SceneRenderSettings invalidSettings = unlitSettings;
        invalidSettings.FogMaxDistance = 0.1f;
        const auto disabledMedia = ResolveMediaState(badDistWorld, invalidSettings, litFrame);
        RENDER_CHECK(!disabledMedia.Valid &&
                     disabledMedia.Diagnostic.find("froxel_fog_disabled:invalid_depth_range") != std::string::npos,
            "M15-U6-06: invalid global depth range disables fog explicitly with diagnostic");
    }

    // 7. M15-U6-03.2: Henyey-Greenstein phase normalization & anisotropy ordering
    {
        constexpr double kPi = 3.14159265358979323846;
        const float iso = EvaluateHenyeyGreenstein(0.37f, 0.0f);
        RENDER_CHECK(std::abs(iso - static_cast<float>(1.0 / (4.0 * kPi))) < 1e-6f,
            "M15-U6-03.2: Henyey-Greenstein g=0 equals 1/(4*pi) across all angles");

        const float fwd = EvaluateHenyeyGreenstein(1.0f, 0.7f);
        const float side = EvaluateHenyeyGreenstein(0.0f, 0.7f);
        const float back = EvaluateHenyeyGreenstein(-1.0f, 0.7f);
        RENDER_CHECK(fwd > side && side > back && fwd > 5.0f * back,
            "M15-U6-03.2: forward scattering g=0.7 peaks cleanly along +cosTheta and decreases monotonically");

        for (float g : { -0.75f, 0.0f, 0.7f, 0.95f }) {
            double integral = 0.0;
            constexpr int N = 100000;
            const double du = 2.0 / static_cast<double>(N);
            for (int k = 0; k < N; ++k) {
                const float u = static_cast<float>(-1.0 + (static_cast<double>(k) + 0.5) * du);
                integral += 2.0 * kPi * static_cast<double>(EvaluateHenyeyGreenstein(u, g)) * du;
            }
            RENDER_CHECK(std::abs(integral - 1.0) < 1e-3,
                "M15-U6-03.2: Henyey-Greenstein integrates to 1 over the unit sphere");
        }
    }

    // 8. M15-U6-03.1 & 03.2: Beer-Lambert integration & 3D froxel slice quadrature vs analytic/reference
    {
        LightingService lightingSvc;
        RenderWorld world;
        RenderDirectionalLight sun;
        sun.Direction = glm::normalize(glm::vec3(0.0f, 0.4f, -0.9f));
        sun.Color = glm::vec3(1.0f, 0.95f, 0.85f);
        sun.Intensity = 1.5f;
        sun.AmbientIntensity = 0.05f;
        world.DirectionalLight = sun;

        SceneRenderSettings settings;
        settings.EnableVolumetricFog = true;
        settings.FogDensity = 0.06f;
        settings.FogHeightFalloff = 0.0f;
        settings.FogScattering = 0.65f;
        settings.FogColor = glm::vec3(0.75f, 0.85f, 0.95f);
        settings.FogNearDistance = 0.25f;
        settings.FogMaxDistance = 100.0f;

        const auto uniformMedia = ResolveMediaState(world, settings, lightingSvc.BuildFrame(world, settings));
        const glm::vec3 rayOrigin{ 0.0f, 1.5f, 0.0f };
        const glm::vec3 obliqueRay{ 0.35f, 0.15f, -1.0f };
        const float rayLen = glm::length(obliqueRay);
        const glm::vec3 unitRay = obliqueRay / rayLen;

        const float boundaryZ48 = SliceBoundaryToViewDistance(48u, 64u, settings.FogNearDistance, settings.FogMaxDistance);
        const auto froxelBoundary = EvaluateFroxelColumnQuadrature(uniformMedia, rayOrigin, obliqueRay, 64u, boundaryZ48);
        const auto analyticBoundary = EvaluateAnalyticHomogeneousFog(uniformMedia, unitRay, boundaryZ48, rayLen);
        RENDER_CHECK(std::abs(froxelBoundary.Transmittance - analyticBoundary.Transmittance) < 1e-5f &&
                     glm::length(froxelBoundary.InScattering - analyticBoundary.InScattering) < 1e-5f,
            "M15-U6-03.1 / 03.2: 64-slice froxel quadrature at slice boundary matches analytic Beer-Lambert to <1e-5");

        const auto froxelUniform = EvaluateFroxelColumnQuadrature(uniformMedia, rayOrigin, obliqueRay, 64u, 45.0f);
        const auto analyticUniform = EvaluateAnalyticHomogeneousFog(uniformMedia, unitRay, 45.0f, rayLen);
        RENDER_CHECK(std::abs(froxelUniform.Transmittance - analyticUniform.Transmittance) < 3e-3f &&
                     glm::length(froxelUniform.InScattering - analyticUniform.InScattering) < 3e-3f,
            "M15-U6-03.1 / 03.2: 64-slice froxel trilinear interpolation between slices closely matches analytic Beer-Lambert");

        // Height-falloff scenario against 2048-step continuous quadrature reference
        settings.FogHeightFalloff = 0.3f;
        settings.FogDensity = 0.12f;
        const auto falloffMedia = ResolveMediaState(world, settings, lightingSvc.BuildFrame(world, settings));
        const auto froxelFalloff = EvaluateFroxelColumnQuadrature(falloffMedia, rayOrigin, obliqueRay, 64u, 60.0f);
        const auto refFalloff = EvaluateReferenceRayQuadrature(falloffMedia, rayOrigin, obliqueRay, 60.0f, 2048u);
        RENDER_CHECK(std::abs(froxelFalloff.Transmittance - refFalloff.Transmittance) < 5e-3f &&
                     glm::length(froxelFalloff.InScattering - refFalloff.InScattering) < 5e-3f,
            "M15-U6-03.1 / 03.2: 64-slice exponential froxel quadrature matches 2048-step height-falloff reference");

        // Zero-density and small optical depth Taylor edge check
        const auto tinyStep = EvaluateHomogeneousFogStep(glm::vec3(0.4f, 0.5f, 0.6f), 1e-6f, 0.1f);
        const auto zeroStep = EvaluateHomogeneousFogStep(glm::vec3(0.4f, 0.5f, 0.6f), 0.0f, 1.0f);
        RENDER_CHECK(std::abs(tinyStep.Transmittance - std::exp(-1e-7f)) < 1e-7f &&
                     glm::length(tinyStep.InScattering - glm::vec3(0.04f, 0.05f, 0.06f)) < 1e-5f &&
                     zeroStep.Transmittance == 1.0f &&
                     glm::length(zeroStep.InScattering - glm::vec3(0.4f, 0.5f, 0.6f)) < 1e-6f,
            "M15-U6-03.2: small optical depth Taylor stabilization avoids 0/0 underflow");

        // Unlit absorptive medium: sigmaT > 0 attenuates transmittance without inventing in-scattering
        RenderWorld absorptiveOnlyWorld;
        const auto absorptiveMedia = ResolveMediaState(absorptiveOnlyWorld, settings, lightingSvc.BuildFrame(absorptiveOnlyWorld, settings));
        const auto unlitFroxel = EvaluateFroxelColumnQuadrature(absorptiveMedia, rayOrigin, obliqueRay, 64u, 40.0f);
        RENDER_CHECK(unlitFroxel.InScattering == glm::vec3(0.0f) &&
                     unlitFroxel.Transmittance < 0.99f && unlitFroxel.Transmittance > 0.0f,
            "M15-U6-06: unlit medium produces zero in-scattering while preserving Beer-Lambert extinction");
    }

    // 9. Canonical shader & pass source parity checks
    {
        const std::string fogShaderSrc = ReadText(AXLE_VOLUMETRIC_FOG_SHADER_PATH);
        const std::string fogPassSrc = ReadText(AXLE_FROXEL_FOG_PASS_SOURCE_PATH);
        RENDER_CHECK(fogShaderSrc.find("CSInjectMedia") != std::string::npos &&
                     fogShaderSrc.find("CSIntegrate") != std::string::npos &&
                     fogShaderSrc.find("HenyeyGreenstein") != std::string::npos &&
                     fogShaderSrc.find("0.0001") == std::string::npos &&
                     fogShaderSrc.find("0.001f") == std::string::npos &&
                     fogPassSrc.find("FogInject") != std::string::npos &&
                     fogPassSrc.find("FogIntegrate") != std::string::npos &&
                     fogPassSrc.find("BuildConstants") != std::string::npos &&
                     fogPassSrc.find("media.EffectiveBaseDensity") != std::string::npos &&
                     fogPassSrc.find("0.45f, 0.85f, 0.25f") == std::string::npos &&
                     fogPassSrc.find("-glm::normalize") == std::string::npos,
            "M15-U6-02.1 / 05.3 / 06: VolumetricFog.hlsl and FroxelFogPass.cpp match 3D froxel architecture without clamps or invented sun");
    }

    // 10. M15-U6-C: Light & Shadow Sampling (Point, Spot, Rect/Disk & CSM/LocalShadow Contract)
    {
        LightingService lightingSvc;
        RenderWorld unlitWorld;
        SceneRenderSettings settings{};
        settings.EnableVolumetricFog = true;
        settings.FogDensity = 0.05f;
        settings.FogHeightFalloff = 0.0f;
        settings.FogScattering = 0.6f;
        settings.FogColor = glm::vec3(0.8f, 0.9f, 1.0f);
        const auto media = ResolveMediaState(unlitWorld, settings, lightingSvc.BuildFrame(unlitWorld, settings));
        RENDER_CHECK(media.Valid && std::abs(LocalIntensityScale - 0.05f) < 1e-6f,
            "M15-U6-02.2: media baseline and LocalIntensityScale constant match ForwardPBR contract");

        // Point light scattering: range cutoff, inverse-square decay, shadow factor & directional mode
        LightingPointFrame point{};
        point.Position = glm::vec3(0.0f, 0.0f, -10.0f);
        point.Radius = 15.0f;
        point.Color = glm::vec3(1.0f, 0.5f, 0.25f);
        point.Intensity = 40.0f;
        point.Falloff = 1.0f;
        point.MinRadius = 0.2f;
        point.Directional = false;

        const glm::vec3 unitRay = glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));
        const glm::vec3 sampleNear = glm::vec3(0.0f, 0.0f, -12.0f);
        const glm::vec3 sampleFar = glm::vec3(0.0f, 0.0f, -26.0f);
        const glm::vec3 pointLit = EvaluatePointLightInScatter(media, point, sampleNear, unitRay, 1.0f);
        const glm::vec3 pointHalfShadow = EvaluatePointLightInScatter(media, point, sampleNear, unitRay, 0.5f);
        const glm::vec3 pointOccluded = EvaluatePointLightInScatter(media, point, sampleNear, unitRay, 0.0f);
        const glm::vec3 pointOutOfRange = EvaluatePointLightInScatter(media, point, sampleFar, unitRay, 1.0f);
        RENDER_CHECK(pointLit.r > pointLit.g && pointLit.g > pointLit.b && pointLit.b > 0.0f &&
                     glm::length(pointHalfShadow - 0.5f * pointLit) < 1e-6f &&
                     pointOccluded == glm::vec3(0.0f) &&
                     pointOutOfRange == glm::vec3(0.0f),
            "M15-U6-02.2 / 07.1: point light in-scattering obeys color ratio, range cutoff, and shadow visibility scaling");

        LightingPointFrame extraDir{};
        extraDir.Position = glm::vec3(0.0f, 0.0f, 1.0f); // Direction toward light = -normalize(Position) -> -Z
        extraDir.Color = glm::vec3(0.4f, 0.8f, 1.0f);
        extraDir.Intensity = 20.0f;
        extraDir.Directional = true;
        const glm::vec3 dirSampleA = EvaluatePointLightInScatter(media, extraDir, sampleNear, unitRay, 1.0f);
        const glm::vec3 dirSampleB = EvaluatePointLightInScatter(media, extraDir, sampleFar, unitRay, 1.0f);
        RENDER_CHECK(dirSampleA.b > 0.0f && glm::length(dirSampleA - dirSampleB) < 1e-6f,
            "M15-U6-02.2: Directional-mode light entry scatters uniformly without inverse-square position attenuation");

        // Spot light scattering: inner cone plateau, smooth outer falloff, cone culling, shadow attenuation
        LightingSpotFrame spot{};
        spot.Position = glm::vec3(0.0f, 0.0f, -5.0f);
        spot.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
        spot.Range = 25.0f;
        spot.Intensity = 60.0f;
        spot.Color = glm::vec3(1.0f, 0.85f, 0.7f);
        spot.CosInnerAngle = std::cos(glm::radians(15.0f));
        spot.CosOuterAngle = std::cos(glm::radians(30.0f));
        spot.Falloff = 1.0f;

        const glm::vec3 spotAxisPt = glm::vec3(0.0f, 0.0f, -10.0f); // 5m down -Z axis (angle 0)
        const float midAngle = glm::radians(22.5f);
        const glm::vec3 spotMidPt = spot.Position + 5.0f * glm::vec3(std::sin(midAngle), 0.0f, -std::cos(midAngle));
        const float outAngle = glm::radians(35.0f);
        const glm::vec3 spotOutPt = spot.Position + 5.0f * glm::vec3(std::sin(outAngle), 0.0f, -std::cos(outAngle));

        const glm::vec3 spotCenter = EvaluateSpotLightInScatter(media, spot, spotAxisPt, unitRay, 1.0f);
        const glm::vec3 spotMid = EvaluateSpotLightInScatter(media, spot, spotMidPt, unitRay, 1.0f);
        const glm::vec3 spotOutside = EvaluateSpotLightInScatter(media, spot, spotOutPt, unitRay, 1.0f);
        const glm::vec3 spotShadowed = EvaluateSpotLightInScatter(media, spot, spotAxisPt, unitRay, 0.0f);
        RENDER_CHECK(spotCenter.r > spotMid.r && spotMid.r > 0.0f &&
                     spotOutside == glm::vec3(0.0f) &&
                     spotShadowed == glm::vec3(0.0f),
            "M15-U6-02.2 / 07.1: spot light in-scattering decays smoothly between inner and outer cone and zeroes under shadow");

        // Rect & Disk area lights: backface culling, two-sided symmetry, disk pi/4 area ratio
        LightingRectFrame rect{};
        rect.Position = glm::vec3(0.0f, 0.0f, -10.0f);
        rect.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
        rect.Up = glm::vec3(0.0f, 1.0f, 0.0f);
        rect.Width = 4.0f;
        rect.Height = 2.0f;
        rect.Range = 20.0f;
        rect.Color = glm::vec3(0.9f, 0.95f, 1.0f);
        rect.SignedIntensity = 25.0f;
        rect.Shape = AreaLightShape::Rectangle;

        const glm::vec3 frontPt = glm::vec3(0.0f, 0.0f, -14.0f);
        const glm::vec3 backPt = glm::vec3(0.0f, 0.0f, -6.0f);
        const glm::vec3 rectFront = EvaluateRectLightInScatter(media, rect, frontPt, unitRay);
        const glm::vec3 rectBack = EvaluateRectLightInScatter(media, rect, backPt, unitRay);

        rect.SignedIntensity = -25.0f;
        const glm::vec3 twoSidedFront = EvaluateRectLightInScatter(media, rect, frontPt, unitRay);
        const glm::vec3 twoSidedBack = EvaluateRectLightInScatter(media, rect, backPt, unitRay);

        rect.SignedIntensity = 25.0f;
        rect.Shape = AreaLightShape::Disk;
        const glm::vec3 diskFront = EvaluateRectLightInScatter(media, rect, frontPt, unitRay);
        RENDER_CHECK(rectFront.b > 0.0f &&
                     rectBack == glm::vec3(0.0f) &&
                     glm::length(twoSidedFront - rectFront) < 1e-6f &&
                     twoSidedBack.b > 0.0f &&
                     diskFront.b > 0.0f && diskFront.b < rectFront.b,
            "M15-U6-02.2: area rect/disk in-scattering enforces single-sided backface rejection and disk area factor");

        // Canonical U6-C shader & RenderGraph wiring parity
        const std::string fogShaderSrc = ReadText(AXLE_VOLUMETRIC_FOG_SHADER_PATH);
        const std::string fogPassSrc = ReadText(AXLE_FROXEL_FOG_PASS_SOURCE_PATH);
        const std::string scenePassesSrc = ReadText(AXLE_SCENE_PASSES_SOURCE_PATH);
        RENDER_CHECK(fogShaderSrc.find("MultiLightBuffer") != std::string::npos &&
                     fogShaderSrc.find("CascadeReceiver") != std::string::npos &&
                     fogShaderSrc.find("LocalShadowReceiver") != std::string::npos &&
                     fogShaderSrc.find("u_CascadeDepth") != std::string::npos &&
                     fogShaderSrc.find("u_LocalShadowDepth") != std::string::npos &&
                     fogShaderSrc.find("u_ShadowSampler") != std::string::npos &&
                     fogShaderSrc.find("EvaluateCascadeVolumetricShadow") != std::string::npos &&
                     fogShaderSrc.find("PointShadowVisibilityVolumetric") != std::string::npos &&
                     fogShaderSrc.find("AccumulatePointLights") != std::string::npos &&
                     fogShaderSrc.find("AccumulateSpotLights") != std::string::npos &&
                     fogShaderSrc.find("AccumulateRectLights") != std::string::npos &&
                     fogPassSrc.find("r.UseCascadeAtlas") != std::string::npos &&
                     fogPassSrc.find("r.UseLocalShadowAtlas") != std::string::npos &&
                     fogPassSrc.find("m_FogInjectTextureSet") != std::string::npos &&
                     scenePassesSrc.find("graph.DeclareRead(\"FogInject\", m_LightBuffer.GetCBV())") != std::string::npos &&
                     scenePassesSrc.find("graph.DeclareRead(\"FogInject\", m_CascadedShadows.GetConstantsCBV())") != std::string::npos &&
                     scenePassesSrc.find("graph.DeclareRead(\"FogInject\", m_LocalShadows.GetConstantsCBV())") != std::string::npos &&
                     scenePassesSrc.find("injectFogShadows && shadowAtlas, injectFogShadows && localAtlas") != std::string::npos &&
                     fogShaderSrc.find("AXLE_FOG_SHADOWS_DISABLED") != std::string::npos,
            "M15-U6-02.2: U6-C VolumetricFog.hlsl, FroxelFogPass.cpp, and RHISceneRendererPasses.cpp shadow/light bindings match");
    }

    // 7. M15-U6-E: Transparent/particle depth interaction, no-light classification,
    //    Inspector/Editor/Runtime/OpenGL fallback parity contracts
    {
        LightingService lightingSvc;
        SceneRenderSettings fogSettings;
        fogSettings.EnableVolumetricFog = true;
        fogSettings.FogDensity = 0.05f;
        fogSettings.FogNearDistance = 0.25f;
        fogSettings.FogMaxDistance = 100.0f;

        // Completely unlit world (even with background-only skybox and disabled zero-intensity point light)
        RenderWorld unlitWorld;
        unlitWorld.Skybox.Authored = true;
        unlitWorld.Skybox.Enabled = true;
        unlitWorld.Skybox.EnvironmentLighting = false;
        RenderPointLight zeroPoint{};
        zeroPoint.Position = { 0.0f, 2.0f, -5.0f };
        zeroPoint.Radius = 15.0f;
        zeroPoint.Intensity = 0.0f;
        zeroPoint.Color = { 1.0f, 1.0f, 1.0f };
        unlitWorld.PointLights.push_back(zeroPoint);
        const auto unlitMedia = ResolveMediaState(unlitWorld, fogSettings, lightingSvc.BuildFrame(unlitWorld, fogSettings));
        RENDER_CHECK(unlitMedia.Valid && !unlitMedia.HasDirectionalSun && !unlitMedia.HasLocalLight &&
                     !unlitMedia.HasAmbientLight && !unlitMedia.HasAnyLighting() &&
                     unlitMedia.Diagnostic.find("froxel_fog_no_directional_sun") != std::string::npos &&
                     unlitMedia.Diagnostic.find("froxel_fog_unlit_extinction_only") != std::string::npos,
            "M15-U6-05.2: unlit scene (including background-only sky & disabled lights) emits extinction-only diagnostic");
        const auto unlitIntegral = EvaluateAnalyticHomogeneousFog(unlitMedia, glm::vec3(0.0f, 0.0f, -1.0f), 20.25f, 1.0f);
        RENDER_CHECK(unlitIntegral.InScattering == glm::vec3(0.0f) &&
                     std::abs(unlitIntegral.Transmittance - std::exp(-0.05f * 20.0f)) < 1e-5f,
            "M15-U6-05.2: unlit fog preserves exact Beer-Lambert extinction with zero in-scattering");

        // Local light only (no directional sun, no ambient)
        RenderWorld localOnlyWorld;
        RenderPointLight activePoint{};
        activePoint.Position = { 0.0f, 2.0f, -5.0f };
        activePoint.Radius = 15.0f;
        activePoint.Intensity = 25.0f;
        activePoint.Color = { 1.0f, 0.7f, 0.4f };
        localOnlyWorld.PointLights.push_back(activePoint);
        const auto localMedia = ResolveMediaState(localOnlyWorld, fogSettings, lightingSvc.BuildFrame(localOnlyWorld, fogSettings));
        RENDER_CHECK(localMedia.Valid && !localMedia.HasDirectionalSun && localMedia.HasLocalLight &&
                     !localMedia.HasAmbientLight && localMedia.HasAnyLighting() &&
                     localMedia.Diagnostic.find("froxel_fog_no_directional_sun") != std::string::npos &&
                     localMedia.Diagnostic.find("froxel_fog_unlit_extinction_only") == std::string::npos,
            "M15-U6-05.2: local-light-only scene clears extinction-only diagnostic while keeping no-sun note");

        // Ambient-only (EnvironmentLighting enabled without directional sun)
        RenderWorld ambientOnlyWorld;
        ambientOnlyWorld.Skybox.Authored = true;
        ambientOnlyWorld.Skybox.Enabled = true;
        ambientOnlyWorld.Skybox.EnvironmentLighting = true;
        ambientOnlyWorld.Skybox.Intensity = 0.5f;
        const auto ambientMedia = ResolveMediaState(ambientOnlyWorld, fogSettings, lightingSvc.BuildFrame(ambientOnlyWorld, fogSettings));
        RENDER_CHECK(ambientMedia.Valid && !ambientMedia.HasDirectionalSun && !ambientMedia.HasLocalLight &&
                     ambientMedia.HasAmbientLight && ambientMedia.HasAnyLighting() &&
                     ambientMedia.Diagnostic.find("froxel_fog_unlit_extinction_only") == std::string::npos,
            "M15-U6-05.2: ambient-only scene activates scattering without inventing a directional sun");

        // M15-U6-05.1 pass order & shader parity source check
        const std::string scenePassesSrc = ReadText(AXLE_SCENE_PASSES_SOURCE_PATH);
        const size_t fogPos = scenePassesSrc.find("addPass(\"FroxelFogPass\"");
        const size_t transPos = scenePassesSrc.find("addPass(\"Transparent\"");
        const size_t partPos = scenePassesSrc.find("addPass(\"ParticlesAndResolve\"");
        RENDER_CHECK(fogPos != std::string::npos && transPos != std::string::npos && partPos != std::string::npos &&
                     fogPos < transPos && transPos < partPos,
            "M15-U6-05.1: FroxelFogPass executes before Transparent and ParticlesAndResolve");

        const auto glPolicy = RenderBackendPolicy::FromBackend(RendererBackendType::OpenGL_Compatibility);
        const auto glFallbacks = glPolicy.CollectFallbacks(fogSettings);
        RENDER_CHECK(!glPolicy.ResolveSettings(fogSettings).EnableVolumetricFog && !glFallbacks.empty() &&
                     glFallbacks[0].find("volumetric_fog_disabled") != std::string::npos,
            "M15-U6-07.6: OpenGL compatibility policy disables volumetric fog with explicit fallback reason");
    }

    std::cout << "[M15FroxelFog] All Unit 6 contract tests (A-E) PASSED.\n";
    return true;
}
