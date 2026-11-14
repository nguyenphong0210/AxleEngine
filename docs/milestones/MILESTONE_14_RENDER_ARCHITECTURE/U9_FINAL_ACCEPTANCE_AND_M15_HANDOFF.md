# M14-U9 — Final Acceptance and M15 Handoff

Status: **HOLD - independent review and sequential repairs**

## Structural scorecard

| Gate | Result | Evidence |
|---|---|---|
| View/frame owner | PASS | `ViewContext`, `RHIFrameContext`, temporal/post-process/diagnostics services; U1/U6 evidence |
| Pass lifecycle owner | PASS (current U2 correction) | `RenderPassScheduler`, `IRenderPass`, named legacy adapters; U2 evidence |
| Surface/material owner | PASS (U3 correction accepted) | `86feca7`, `f241eb0`; U3 evidence and [final correction recheck](../../reviews/M14_U3_CORRECTION_RECHECK.md) |
| Resource/graph owner | PASS (U4 follow-up correction accepted) | `4cf8e72`, `171133b`, `da51acf`; U4 evidence and [follow-up recheck](../../reviews/M14_U4_FOLLOWUP_CORRECTION_RECHECK.md) |
| Lighting/reflection/GI boundaries | PARTIAL | `LightingService`, `ReflectionService`, `GIService`; U5 evidence |
| Backend/fallback policy | PASS | `RenderBackendPolicy`, D3D12/Vulkan/OpenGL matrix; U7 evidence |
| Product caller migration | PASS | RenderService-only product seams and U8 dependency map |

Persistent pipeline/descriptor caches remain behind the legacy adapter boundary
recorded by U2. Live orchestration and canonical surface integration are accepted
at their documented boundaries. U4 resource declarations and exact lifetime are
implemented, the typed presentation/frame view correction is accepted, and the
current native/product gates pass. U5 service review and consolidated U9
acceptance remain before the M15 handoff.

Current correction evidence and remaining gates are recorded in
`docs/reviews/M14_RENDER_ARCHITECTURE_REPAIR_STATUS.md`. Historical delivery
checkboxes below do not override this HOLD or release M15.

## Historical verification scorecard

- Debug build: `AxleEditor`, `AxleRuntime`, `AxleRenderUnitTests`, `AxlePreviewRenderTests`, `AxleRHIFoundationTests`, `AxleRenderAcceptanceTests` — PASS.
- Release build: same renderer/product targets — PASS.
- Debug `AxleRenderUnitTests` — PASS.
- Release `AxleRenderUnitTests` — PASS.
- Native D3D12 foundation validation and injected failure/recovery — PASS.
- Native Vulkan foundation validation and injected failure/recovery — PASS.
- OpenGL compatibility/fallback equivalence — PASS.
- D3D12 and Vulkan render acceptance — PASS.
- Editor `--verify-empty-startup --rhi=dx12` — PASS (5 UI frames, 0 entities/assets).
- Runtime cooked D3D12 fixture — PASS (asset package, simulation/input/presentation lifecycle).
- D3D12 preview surface fixture — PASS.
- OpenGL preview surface fixture — PASS.
- Headless renderer contracts, graph/lifetime/history/material tests — PASS.
- `python scripts/check_architecture.py` — PASS (596 files, 25 CMake files).
- `python scripts/generate_m14_dependency_map.py` — PASS (0 architecture/caller violations).
- `python scripts/test_baseline_tools.py` — PASS (23 tests).
- `git diff --check` — PASS.

## Published handoff artifacts

- Source map: `U8_M14_DEPENDENCY_MAP.md` and `U8_M14_DEPENDENCY_MAP.json`.
- Migration/tooling report: `U8_MIGRATION_TESTS_TOOLING_EVIDENCE.md`.
- Final acceptance and M15 handoff: this document.

The cooked D3D12 runtime fixture also fixed a particle-depth regression: `ParticleDepth` is copied only when a particle requests soft intersection (`SoftnessDistance > 0`), eliminating the NRI uninitialized-resource validation error.

## M15 entry contracts

M15 feature work must enter through:

1. `ViewContext` for view-local state, reset and completion ownership.
2. `IRenderPass`/`RenderPassScheduler` for pass lifecycle and ordering.
3. `RenderGraph` and `RHIViewTargets` for declared resources and generations.
4. `ResolvedSurface` for all material outputs consumed by lighting/visibility.
5. `RenderBackendPolicy` for capability and fallback decisions.
6. `LightingService`, `ReflectionService`, `GIService` for lighting/reflection/GI policy.
7. `TemporalService`, `PostProcessService`, `RenderDiagnostics` for history, output policy and telemetry.

## Deferred scope

M14 does not claim final SSR quality, dynamic six-face reflection capture/prefilter, multi-probe blending, DDGI irradiance/visibility feedback, ray-traced reflections, final advanced material fidelity, or a validated GPU budget scorecard. Those are M15+ deliverables and must retain explicit fallback/diagnostic states.

M14 remains on hold until the current architecture repairs and verification
gates are complete. Stable entry contracts alone do not establish live integration.
