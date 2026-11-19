#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderBackendPolicy.hpp>
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
                     !texDesc.IsCubeMap &&
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
        ResolveViewTargetOptions(settings, false, enable, quality);
        RENDER_CHECK(!enable && quality == static_cast<uint8_t>(FroxelFogQuality::High),
            "M15-U6-01.1: disabled volumetric fog allocates no froxel targets and normalizes tier");

        settings.EnableVolumetricFog = true;
        for (uint8_t q = 0; q <= RHI::RHIViewTargets::MaxFroxelQualityTier; ++q) {
            settings.FogQuality = static_cast<FroxelFogQuality>(q);
            settings.MSAASamples = 1u;
            ResolveViewTargetOptions(settings, false, enable, quality);
            RENDER_CHECK(enable && quality == q,
                "M15-U6-01.1 / 01.2: single-sample main view forwards requested froxel quality tier");

            ResolveViewTargetOptions(settings, true, enable, quality);
            RENDER_CHECK(!enable && quality == static_cast<uint8_t>(FroxelFogQuality::High),
                "M15-U6-01.1: sub-capture views suppress froxel allocation");
        }

        for (uint8_t msaa : { uint8_t{2}, uint8_t{4}, uint8_t{8} }) {
            settings.MSAASamples = msaa;
            settings.FogQuality = FroxelFogQuality::Ultra;
            ResolveViewTargetOptions(settings, false, enable, quality);
            RENDER_CHECK(!enable,
                "M15-U6-01.1: MSAA views do not allocate unusable single-sample 3D froxel targets");
        }
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
                             contractExtent.Width <= spec.MaxWidth &&
                             contractExtent.Height <= spec.MaxHeight &&
                             contractExtent.Depth == spec.DepthSlices,
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
            const uint32_t slices = GetTierSpec(static_cast<FroxelFogQuality>(tier)).DepthSlices;
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

    std::cout << "[M15FroxelFog] All Unit 6 Batch A contract tests PASSED.\n";
    return true;
}
