# M14-U6 — Temporal, Post-Process and Diagnostics Ownership

Status: **Accepted**

## Delivered contracts

- `TemporalService` (`Engine/Renderer/include/Axle/TemporalService.hpp`) owns the two-slot history contract, first-frame bypass, motion/disocclusion validity, previous view-projection/jitter, reset invalidation, and commit-time slot flipping.
- `PostProcessService` (`Engine/Renderer/include/Axle/PostProcessService.hpp`) owns the view-local exposure state and canonical policy for tone mapping, bloom, auto exposure, HDR/SDR output and AA selection.
- `RenderDiagnostics` (`Engine/Renderer/include/Axle/RenderDiagnostics.hpp`) owns per-pass records, frame counters, target generation, fallback/error reasons and GPU timing validity. `Unknown`/`Unavailable`/`Invalid` are distinct from `Valid`.
- `ViewContext` owns one instance of each service and resets them on view identity changes. `RHISceneRenderer` uses the services for TAA history, previous matrices, adapted exposure, pass records and timestamp validity.

## Verification

- `cmake --build build --config Debug --target AxleRenderUnitTests --parallel 6` — passed.
- `build/Tests/Debug/AxleRenderUnitTests.exe` — passed, including `TemporalPostDiagnostics` ownership/reset/validity tests and the existing renderer contract suite.
- `git diff --check` — passed.

## Acceptance mapping

| Requirement | Evidence |
|---|---|
| History slots and flip timing | `TemporalService::Begin` / `Commit`; unit test verifies 0/1 then 1/0 |
| First-frame and reset invalidation | `HistoryValid`, `MotionValid`, `DisocclusionValid`; reset test |
| Exposure/bloom/tone/AA policy | `PostProcessPolicy` and HDR/SDR + TAA test |
| Per-pass diagnostics and fallback reasons | `RenderDiagnostics::BeginPass` / `EndPass` and renderer graph pass hook |
| GPU timestamp validity | zero delta maps to `Unknown`, never valid zero; unit test covers this |
