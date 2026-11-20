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

    std::cout << "[M15U8Contract] All M15 U8 Batch A Exposure & Post Pipeline contract tests passed.\n";
    return true;
}
