# M14-U9 — Final Acceptance and M15 Handoff

Status: **Accepted**

## Structural scorecard

| Gate | Result | Evidence |
|---|---|---|
| View/frame owner | PASS | `ViewContext`, `RHIFrameContext`, temporal/post-process/diagnostics services; U1/U6 evidence |
| Pass lifecycle owner | PASS | `RenderPassScheduler`, `IRenderPass`, named legacy adapters; U2 evidence |
| Surface/material owner | PASS | `ResolvedSurface` and material resolver tests; U3 evidence |
| Resource/graph owner | PASS | strict `RenderGraph`, target generations, descriptor lease checks; U4 evidence |
| Lighting/reflection/GI boundaries | PASS | `LightingService`, `ReflectionService`, `GIService`; U5 evidence |
| Backend/fallback policy | PASS | `RenderBackendPolicy`, D3D12/Vulkan/OpenGL matrix; U7 evidence |
| Product caller migration | PASS | RenderService-only product seams and U8 dependency map |

The remaining direct implementation body in `RHISceneRenderer` is the approved migration facade/legacy adapter recorded by U2. No new M14 feature pass was added outside the scheduler contract during U8/U9.

## Verification scorecard

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

M14 is complete as an architecture milestone because these future features have stable entry contracts and no longer require expanding the product caller surface or adding unowned global renderer state.
