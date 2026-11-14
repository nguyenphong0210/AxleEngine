#include "RenderTestCommon.hpp"
#include <Axle/TemporalService.hpp>
#include <Axle/PostProcessService.hpp>
#include <Axle/RenderDiagnostics.hpp>

bool RunTemporalPostDiagnosticsUnitTests() {
    using namespace Axle;
    std::cout << "  [TemporalPostDiagnostics] Running ownership and validity tests...\n";

    TemporalService temporal;
    auto first = temporal.Begin(2, {0.25f, -0.25f});
    RENDER_CHECK(first.ReadSlot == 0 && first.WriteSlot == 1 && !first.HistoryValid && !first.MotionValid,
        "first temporal frame bypasses uninitialized history and motion vectors");
    temporal.Commit(glm::mat4(2.0f), true);
    auto second = temporal.Begin(2, {0.0f, 0.0f});
    RENDER_CHECK(second.ReadSlot == 1 && second.WriteSlot == 0 && second.HistoryValid && second.MotionValid &&
                 temporal.GetPreviousViewProjection()[0][0] == 2.0f,
        "successful temporal commit flips history and publishes previous view state");
    temporal.Reset(1u << 3);
    auto afterCut = temporal.Begin(2);
    RENDER_CHECK(!afterCut.HistoryValid && !afterCut.DisocclusionValid && temporal.GetCommittedFrameCount() == 0,
        "camera or target reset invalidates history and disocclusion feedback");

    SceneRenderSettings settings;
    settings.EnableToneMapping = true;
    settings.EnableBloom = true;
    settings.EnableTAA = true;
    settings.EnableAutoExposure = true;
    settings.AutoExposureMin = 0.25f;
    settings.AutoExposureMax = 4.0f;
    PostProcessService post;
    post.BeginFrame(settings, true);
    RENDER_CHECK(post.GetPolicy().Output == PostProcessOutput::HDR &&
                 post.GetPolicy().AntiAliasing == PostProcessAA::TAA &&
                 post.GetPolicy().Bloom && post.GetPolicy().ToneMapping,
        "post-process policy owns HDR, bloom, tone mapping and AA selection");
    const float bright = post.UpdateExposure(0.01f, 1.0f);
    RENDER_CHECK(bright <= settings.AutoExposureMax && bright >= settings.AutoExposureMin,
        "auto exposure is clamped to the configured safety range");
    post.Reset();
    RENDER_CHECK(!post.IsConfigured(), "post-process reset drops view-local adaptation state");

    RenderDiagnostics diagnostics;
    diagnostics.BeginFrame(7, 42);
    diagnostics.BeginPass("TAAResolve");
    diagnostics.EndPass(DiagnosticStatus::Fallback, 0.2, 0.0, DiagnosticValidity::Unknown, 1, 0,
                        "history_invalid_first_frame");
    diagnostics.SetFrameGPUTime(0.0, DiagnosticValidity::Unknown);
    diagnostics.AddFallback("gpu_timestamp_delta_zero");
    diagnostics.SetCounters(3, 2);
    RENDER_CHECK(diagnostics.GetFrame().TargetGeneration == 42 &&
                 diagnostics.GetFrame().GPUValidity == DiagnosticValidity::Unknown &&
                 diagnostics.GetFrame().Passes.size() == 1 &&
                 diagnostics.GetFrame().Passes[0].Status == DiagnosticStatus::Fallback &&
                 diagnostics.GetFrame().DrawCalls == 3 && diagnostics.GetFrame().Dispatches == 2 &&
                 !diagnostics.IsGPUTimeValid(),
        "diagnostics preserves fallback reasons and reports unavailable GPU time as unknown");

    std::cout << "  [TemporalPostDiagnostics] Ownership and validity tests passed.\n";
    return true;
}
