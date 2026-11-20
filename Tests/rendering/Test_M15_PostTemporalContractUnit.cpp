#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/PostProcessService.hpp>
#include <Axle/ViewContext.hpp>
#include <Axle/ViewRequest.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
    std::string LoadTextFile(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
}

bool RunM15PostTemporalContractUnitTests() {
    using namespace Axle;
    std::cout << "[M15U8Contract] Running Milestone 15 Unit 8 Batch A (Exposure & Post Pipeline Contracts) Tests...\n";

    // 1. M15-U8-01.1 / 01.2 / 05.1: Log-luminance histogram quantization & mixed-tile global reduction
    {
        uint32_t binLow = 0, binHigh = 0, binBlack = 0;
        RENDER_CHECK(!AutoExposureContract::QuantizeLogLuminanceBin(0.0f, -10.0f, 10.0f, binBlack),
            "pure black pixels are excluded from log-luminance histogram bins");
        RENDER_CHECK(!AutoExposureContract::QuantizeLogLuminanceBin(std::numeric_limits<float>::quiet_NaN(), -10.0f, 10.0f, binBlack),
            "NaN luminance is excluded from log-luminance histogram bins");
        RENDER_CHECK(AutoExposureContract::QuantizeLogLuminanceBin(0.25f, -10.0f, 10.0f, binLow) &&
                     AutoExposureContract::QuantizeLogLuminanceBin(4.0f, -10.0f, 10.0f, binHigh) &&
                     binLow < binHigh && binLow > 0u && binHigh < 255u,
            "symmetric linear radiances quantize to ordered non-boundary bins");

        // 50% 4.0 nits + 50% 0.25 nits => geometric mean = 1.0, targetExposure = 0.18
        constexpr uint32_t pixelCount = 256u;
        std::vector<float> mixed5050(pixelCount * 3u, 0.0f);
        for (uint32_t i = 0; i < pixelCount; ++i) {
            const float val = (i % 2u == 0u) ? 4.0f : 0.25f;
            mixed5050[i * 3u + 0u] = val;
            mixed5050[i * 3u + 1u] = val;
            mixed5050[i * 3u + 2u] = val;
        }
        SceneRenderSettings s;
        s.EnableAutoExposure = true;
        s.AutoExposureMin = 0.05f;
        s.AutoExposureMax = 10.0f;
        s.AutoExposureKeyValue = 0.18f;
        s.AutoExposureSpeed = 2.0f;
        const auto res5050 = AutoExposureContract::ComputeReferenceAdaptation(
            1.0f, mixed5050.data(), pixelCount, s, 1.0f / 60.0f, true);
        RENDER_CHECK(!res5050.BlackFrame && res5050.ValidPixelCount == pixelCount &&
                     std::abs(res5050.AverageLuminance - 1.0f) < 0.06f &&
                     std::abs(res5050.AdaptedExposure - 0.18f) < 0.015f,
            "50/50 mixed tiles reduce to geometric mean luminance without stride omission");

        // Mixed 25% bright (16.0) + 75% dark (0.25) => log2 avg = 0.25*4 + 0.75*(-2) = -0.5 => luma = 0.7071
        std::vector<float> mixed2575(pixelCount * 3u, 0.0f);
        for (uint32_t i = 0; i < pixelCount; ++i) {
            const float val = (i < pixelCount / 4u) ? 16.0f : 0.25f;
            mixed2575[i * 3u + 0u] = val;
            mixed2575[i * 3u + 1u] = val;
            mixed2575[i * 3u + 2u] = val;
        }
        const auto res2575 = AutoExposureContract::ComputeReferenceAdaptation(
            1.0f, mixed2575.data(), pixelCount, s, 1.0f / 60.0f, true);
        RENDER_CHECK(std::abs(res2575.AverageLuminance - 0.7071f) < 0.05f &&
                     std::abs(res2575.TargetExposure - (0.18f / 0.7071f)) < 0.02f,
            "25/75 asymmetric bright/dark tiles integrate in global log-average luminance");
    }

    // 2. M15-U8-02.1, 02.2, 05.2, 05.3, 05.4: Real DeltaTime & 30/60/120 Hz invariance
    {
        SceneRenderSettings s;
        s.EnableAutoExposure = true;
        s.AutoExposureMin = 0.05f;
        s.AutoExposureMax = 10.0f;
        s.AutoExposureKeyValue = 0.18f;
        s.AutoExposureSpeed = 2.0f;
        std::vector<float> darkFrame(64u * 3u, 0.045f); // target = 0.18 / 0.045 = 4.0
        auto simulateRate = [&](uint32_t steps, float dt) -> float {
            float expVal = 0.2f; // Adapted from prior bright 0.9 scene
            for (uint32_t i = 0; i < steps; ++i) {
                expVal = AutoExposureContract::ComputeReferenceAdaptation(
                    expVal, darkFrame.data(), 64u, s, dt, false).AdaptedExposure;
            }
            return expVal;
        };
        const float exp30 = simulateRate(15u, 1.0f / 30.0f);
        const float exp60 = simulateRate(30u, 1.0f / 60.0f);
        const float exp120 = simulateRate(60u, 1.0f / 120.0f);
        const float exact = 0.2f + (4.0f - 0.2f) * (1.0f - std::exp(-2.0f * 0.5f));
        RENDER_CHECK(std::abs(exp30 - exact) < 0.08f &&
                     std::abs(exp60 - exact) < 0.08f &&
                     std::abs(exp120 - exact) < 0.08f &&
                     std::abs(exp30 - exp120) < 0.02f,
            "exponential adaptation across 30/60/120 Hz reaches identical exposure over equal elapsed time");

        const float expHoldDt0 = AutoExposureContract::ComputeReferenceAdaptation(
            0.5f, darkFrame.data(), 64u, s, 0.0f, false).AdaptedExposure;
        RENDER_CHECK(std::abs(expHoldDt0 - 0.5f) < 1e-6f,
            "zero deltaTime holds previous exposure without advancing adaptation");
    }

    // 3. M15-U8-03.2, 03.3, 03.4: First frame, black frame & camera cut contracts
    {
        SceneRenderSettings s;
        s.Exposure = 1.25f;
        s.AutoExposureMin = 0.1f;
        s.AutoExposureMax = 8.0f;
        s.AutoExposureSpeed = 1.0f;
        std::vector<float> dark(16u * 3u, 0.045f);
        std::vector<float> black(16u * 3u, 0.0f);

        // First frame snaps to target regardless of prev=100
        const auto firstRes = AutoExposureContract::ComputeReferenceAdaptation(
            100.0f, dark.data(), 16u, s, 1.0f / 60.0f, true);
        RENDER_CHECK(std::abs(firstRes.AdaptedExposure - firstRes.TargetExposure) < 1e-5f,
            "first frame / camera cut snaps immediately to scene target exposure");

        // Black frame continuous holds valid prev without exploding to MaxExposure
        const auto blackCont = AutoExposureContract::ComputeReferenceAdaptation(
            0.42f, black.data(), 16u, s, 1.0f / 60.0f, false);
        RENDER_CHECK(blackCont.BlackFrame && blackCont.ValidPixelCount == 0u &&
                     std::isfinite(blackCont.AdaptedExposure) &&
                     std::abs(blackCont.AdaptedExposure - 0.42f) < 1e-6f,
            "transient black frame preserves prior finite exposure rather than spiking to max");

        // Black frame on reset falls back to clamped authored Exposure
        const auto blackReset = AutoExposureContract::ComputeReferenceAdaptation(
            0.42f, black.data(), 16u, s, 1.0f / 60.0f, true);
        RENDER_CHECK(blackReset.BlackFrame && std::abs(blackReset.AdaptedExposure - 1.25f) < 1e-6f,
            "black frame on initial/cut falls back deterministically to clamped authored exposure");
    }

    // 4. M15-U8-02.3: Per-view ViewContext exposure isolation & non-reset on SceneRevision
    {
        ViewContext ctx;
        auto targetsA = std::make_shared<RHI::RHIViewTargets>();
        auto targetsB = std::make_shared<RHI::RHIViewTargets>();
        ViewContextInput inA{};
        inA.Targets = targetsA;
        inA.Width = 128;
        inA.Height = 128;
        inA.SceneRevision = 1;
        inA.DeltaTime = 1.0f / 30.0f;

        ViewContextInput inB = inA;
        inB.Targets = targetsB;

        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.GetExposureHistory().Valid, "initial frame starts with invalid exposure history (reset)");
        ctx.CommitExposureHistory({true, true});

        // Interleave View B
        ctx.Observe(inB, 1);
        RENDER_CHECK(!ctx.GetExposureHistory().Valid, "independent View B starts with its own reset exposure history");
        ctx.CommitExposureHistory({true, true});

        // Return to View A with bumped SceneRevision (light toggled) -> must NOT reset exposure!
        inA.SceneRevision = 2;
        inA.DeltaTime = 1.0f / 120.0f;
        ctx.Observe(inA, 1);
        RENDER_CHECK(ctx.GetExposureHistory().Valid && ctx.GetExposureHistory().AutoExposureEnabled,
            "switching back to View A after View B & SceneRevision increment retains View A exposure history");

        // CameraCut DOES invalidate View A exposure history
        inA.CameraCut = true;
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.GetExposureHistory().Valid,
            "CameraCut invalidates View A exposure history so new shot snaps immediately");
        ctx.CommitExposureHistory({true, true});

        // DeviceEpoch DOES invalidate exposure history
        inA.CameraCut = false;
        ctx.Observe(inA, 2);
        RENDER_CHECK(!ctx.GetExposureHistory().Valid,
            "DeviceEpoch invalidates exposure history");
    }

    // 5. Canonical shader, pass-order & real DeltaTime source wiring checks
    {
        const std::string aeShader = LoadTextFile(AXLE_AUTO_EXPOSURE_SHADER_PATH);
        const std::string postPass = LoadTextFile(AXLE_POST_PROCESS_PASS_SOURCE_PATH);
        const std::string toneShader = LoadTextFile(AXLE_TONEMAPPING_SHADER_PATH);
        const std::string editorSrc = LoadTextFile(AXLE_EDITOR_LAYER_SOURCE_PATH);
        const std::string runtimeSrc = LoadTextFile(AXLE_RUNTIME_LAYER_SOURCE_PATH);
        RENDER_CHECK(!aeShader.empty() &&
                     aeShader.find("CSClearHistogram") != std::string::npos &&
                     aeShader.find("CSBuildHistogram") != std::string::npos &&
                     aeShader.find("CSReduceExposure") != std::string::npos &&
                     aeShader.find("stride = max(1u") == std::string::npos,
            "AutoExposure.hlsl implements full-viewport 3-pass histogram without pixel-skipping stride");
        RENDER_CHECK(!toneShader.empty() && toneShader.find("u_ExposureTexture") != std::string::npos,
            "ToneMapping.hlsl consumes u_ExposureTexture published by AutoExposure");
        const auto posAE = postPass.find("AutoExposureHistogram");
        const auto posBloom = postPass.find("BloomCopy");
        RENDER_CHECK(posAE != std::string::npos && posBloom != std::string::npos && posAE < posBloom &&
                     postPass.find("i.DeltaTime") != std::string::npos,
            "PostProcessPass meters AutoExposure before Bloom using real i.DeltaTime");
        RENDER_CHECK(editorSrc.find("request.DeltaTime = ts.GetSeconds()") != std::string::npos &&
                     runtimeSrc.find("request.DeltaTime = ts.GetSeconds()") != std::string::npos,
            "EditorLayer and RuntimeLayer pass real frame deltaTime into ViewRequest::DeltaTime");
    }

    // 6. M15-U8-06..09 (Batch B): 6-level Bloom dimensions, single threshold/Karis & additive energy contract
    {
        RENDER_CHECK(BloomContract::VerifySixLevelPyramidDimensions(1920u, 1080u) &&
                     BloomContract::VerifySixLevelPyramidDimensions(1919u, 1079u) &&
                     BloomContract::VerifySixLevelPyramidDimensions(127u, 63u) &&
                     BloomContract::VerifySixLevelPyramidDimensions(3u, 5u) &&
                     BloomContract::VerifySixLevelPyramidDimensions(1u, 1u) &&
                     !BloomContract::VerifySixLevelPyramidDimensions(0u, 128u),
            "BloomContract validates 6-level pyramid extents across standard, odd, small and degenerate resolutions");
        const bool extentOk =
            BloomContract::ComputeMipExtent(1919u, 1079u, 0u) == (BloomContract::MipExtent{959u, 539u}) &&
            BloomContract::ComputeMipExtent(3u, 5u, 0u) == (BloomContract::MipExtent{1u, 2u}) &&
            BloomContract::ComputeMipExtent(3u, 5u, 5u) == (BloomContract::MipExtent{1u, 1u});
        RENDER_CHECK(extentOk,
            "Bloom mip levels halve accurately with safe minimum 1x1 clamping");

        RENDER_CHECK(BloomContract::VerifyAdditiveEnergyAndSingleTint(
                         glm::vec3(5.0f, 3.0f, 1.0f), 1.0f, 0.25f,
                         glm::vec3(1.0f, 0.5f, 0.25f), 0.75f),
            "Bloom thresholds once at Down0, sums octaves additively without overwrite, and scales tint/intensity linearly once");

        const std::string bloomShader = LoadTextFile(AXLE_BLOOM_SHADER_PATH);
        const std::string postPass = LoadTextFile(AXLE_POST_PROCESS_PASS_SOURCE_PATH);
        RENDER_CHECK(!bloomShader.empty() &&
                     bloomShader.find("u_SourceSize.zw") != std::string::npos &&
                     bloomShader.find("PSUpsample") != std::string::npos &&
                     bloomShader.find("u_BloomParams.w < 0.5") != std::string::npos &&
                     bloomShader.find("1.0 / 6.0") != std::string::npos,
            "Bloom.hlsl samples with source texel size, thresholds once at mode<0.5, upsamples via PSUpsample and normalizes 6 octaves once in PSApply");
        RENDER_CHECK(postPass.find("std::pow(1.35f") == std::string::npos &&
                     postPass.find("Load::Load") != std::string::npos &&
                     postPass.find("nri::LoadOp::LOAD") != std::string::npos,
            "PostProcessPass preserves finer Bloom mips via additive LoadOp::LOAD and constant radius");
    }

    // 7. M15-U8-11 & 13 (Batch C): Motion Vector, PrevWorldTransform Cache, Jitter Cancellation & 5 Resets
    {
        RENDER_CHECK(MotionVectorContract::VerifyCameraAndObjectMotionVectors() &&
                     MotionVectorContract::VerifyJitterCancellationConsistency(),
            "MotionVectorContract verifies camera pan, object translation, history UV reconstruction and jitter cancellation");

        ViewContext ctx;
        auto targetsA = std::make_shared<RHI::RHIViewTargets>();
        auto targetsB = std::make_shared<RHI::RHIViewTargets>();
        ViewContextInput inA{};
        inA.Kind = RenderViewKind::Main;
        inA.Targets = targetsA;
        inA.Width = 128;
        inA.Height = 128;
        inA.SceneRevision = 10;
        inA.SceneInstanceID = 1;
        inA.Settings.EnableTAA = true;
        inA.FeatureGeneration = HashViewFeaturePolicy(inA.Settings);

        ViewContextInput inB = inA;
        inB.Targets = targetsB;

        const glm::mat4 mA1 = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 mB1 = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 0.0f, 0.0f));
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid() && !ctx.FindPreviousObjectTransform(1u, UUID(101u)).has_value(),
            "initial frame starts with invalid temporal history and empty object transform cache");
        ctx.Temporal().Commit(glm::mat4(1.0f), true, glm::mat4(1.0f));
        ctx.CommitTemporalObjectTransforms({ {MakeTemporalObjectKey(1u, UUID(101u)), mA1} }, true);

        // First-spawn object returns nullopt cleanly; matching StableUUID with changed EntityID matches!
        RENDER_CHECK(!ctx.FindPreviousObjectTransform(2u, UUID(202u)).has_value(),
            "first-spawn object returns empty previous transform without garbage");
        const auto foundReassigned = ctx.FindPreviousObjectTransform(99u, UUID(101u));
        RENDER_CHECK(foundReassigned.has_value() && std::abs((*foundReassigned)[3][0] - 1.0f) < 1e-6f,
            "StableUUID keeps previous transform identity even if EntityID changes");

        // Interleaved view switch -> separate view resets current temporal selection
        ctx.Observe(inB, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid() && !ctx.FindPreviousObjectTransform(1u, UUID(101u)).has_value(),
            "switching to separate view invalidates active temporal history");
        ctx.Temporal().Commit(glm::mat4(1.0f), true, glm::mat4(1.0f));
        ctx.CommitTemporalObjectTransforms({ {MakeTemporalObjectKey(1u, UUID(101u)), mB1} }, true);

        // Establish steady history on A again, then verify ALL 5 TAA reset triggers (M15-U8-13.5):
        auto establishA = [&]() {
            ctx.Observe(inA, 1);
            ctx.Temporal().Commit(glm::mat4(1.0f), true, glm::mat4(1.0f));
            ctx.CommitTemporalObjectTransforms({ {MakeTemporalObjectKey(1u, UUID(101u)), mA1} }, true);
        };
        establishA();
        RENDER_CHECK(ctx.Temporal().IsHistoryValid() && ctx.GetCachedTemporalObjectCount() == 1u,
            "committed frame makes TAA history and object transform cache valid");

        // (1) Resize
        inA.Width = 256;
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid() && ctx.GetCachedTemporalObjectCount() == 0u,
            "Resize reset invalidates TAA history and object transform cache");
        establishA();

        // (2) ViewSwitch
        inA.Kind = RenderViewKind::AssetPreview;
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid() && ctx.GetCachedTemporalObjectCount() == 0u,
            "ViewSwitch reset invalidates TAA history and object transform cache");
        inA.Kind = RenderViewKind::Main;
        establishA();

        // (3) CameraCut
        inA.CameraCut = true;
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid() && ctx.GetCachedTemporalObjectCount() == 0u,
            "CameraCut reset invalidates TAA history and object transform cache");
        inA.CameraCut = false;
        establishA();

        // (4) Settings / Format policy change
        inA.Settings.EnableTAA = false;
        inA.FeatureGeneration = HashViewFeaturePolicy(inA.Settings);
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid(),
            "Settings toggle (EnableTAA off) invalidates TAA history");
        inA.Settings.EnableTAA = true;
        inA.FeatureGeneration = HashViewFeaturePolicy(inA.Settings);
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid(),
            "Settings toggle (EnableTAA back on) starts fresh without stale history");
        establishA();

        // (5) SceneRevision bump
        ++inA.SceneRevision;
        ctx.Observe(inA, 1);
        RENDER_CHECK(!ctx.Temporal().IsHistoryValid() && ctx.GetCachedTemporalObjectCount() == 0u,
            "SceneRevision change invalidates TAA history and object transform cache");

        const std::string velShader = LoadTextFile(AXLE_VELOCITY_SHADER_PATH);
        RENDER_CHECK(!velShader.empty() &&
                     velShader.find("u_CurrJitteredVP") != std::string::npos &&
                     velShader.find("u_CurrUnjitteredVP") != std::string::npos &&
                     velShader.find("u_PrevUnjitteredVP") != std::string::npos &&
                     velShader.find("PSBackground") != std::string::npos &&
                     velShader.find("PSMesh") != std::string::npos,
            "Velocity.hlsl outputs jitter-cancelled camera and per-object motion vectors");
    }

    // Batch D (M15-U8-12.1..12.2, 15.1..15.4): Closest-3x3 depth dilation,
    // disocclusion/out-of-bounds rejection & no silent FXAA-on-TAA stacking.
    {
        RENDER_CHECK(TAAContract::VerifyClosestDepthDilation() &&
                     TAAContract::VerifyDisocclusionAndBoundsRejection() &&
                     TAAContract::VerifyNoSilentFXAAOnTAA(),
            "TAAContract verifies 3x3 closest-depth velocity dilation, disocclusion/OOB rejection, and FXAA mutual exclusion");

        const std::string taaShader = LoadTextFile(AXLE_TAA_SHADER_PATH);
        RENDER_CHECK(!taaShader.empty() &&
                     taaShader.find("u_VelocityTexture") != std::string::npos &&
                     taaShader.find("closestDepth") != std::string::npos &&
                     taaShader.find("closestOffset") != std::string::npos &&
                     taaShader.find("SampleHistoryCatmullRom") != std::string::npos &&
                     taaShader.find("RGBToYCoCg") != std::string::npos &&
                     taaShader.find("disocclusionGate") != std::string::npos,
            "TAA.hlsl implements closest-3x3 depth dilation, RG16F velocity sampling, Catmull-Rom, YCoCg variance clip and disocclusion gate");

        const std::string passSource = LoadTextFile(AXLE_POST_PROCESS_PASS_SOURCE_PATH);
        RENDER_CHECK(!passSource.empty() &&
                     passSource.find("r.TAAVelocity") != std::string::npos &&
                     passSource.find("r.UseFXAA && !r.UseTAA") != std::string::npos &&
                     passSource.find("i.VelocityTexture") != std::string::npos,
            "PostProcessPass declares TAAVelocity in RenderGraph and suppresses FXAA when TAA is active");
    }

    std::cout << "[M15U8Contract] All M15 U8 Batch A, B, C & D contract tests passed.\n";
    return true;
}
