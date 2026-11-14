# M14 Render Architecture U0-U9 — Independent Codex Review

**Review boundary:** M14 Render Architecture only (`docs/milestones/MILESTONE_14_RENDER_ARCHITECTURE`). M15 lighting/material fidelity is excluded. **Review base/head:** `ac4941c..8b04055` (HEAD `8b04055`). No production source was changed during this review.

## Verdict

**Acceptance recommendation: HOLD / PARTIAL.** The repository has a strong set of contracts and passing CPU/native/backend smoke evidence, but the final `U9 ACCEPTED` claim is ahead of the implementation. U0, U1, U6, U7 and most of U8 are verified. U2, U4 and U5 remain partial at the integration boundary; U3 is contract-level pass with a graph-material integration gap; U9 is blocked until those structural gaps and the missing current runtime evidence are closed.

The key distinction is between a tested interface and an interface that owns the live renderer. The current tests prove the former in several places; source inspection shows the latter is not yet true for the scheduler and lighting/reflection/GI services.

## Unit scorecard

| Unit | Result | Independent conclusion |
|---|---|---|
| U0 Baseline/migration contract | **PASS** | Baseline source inventory, ownership rules and deferred M15 boundary are documented. The evidence correctly records the remaining monolithic renderer as a known limitation (`U0_BASELINE_EVIDENCE.md:134-141`). |
| U1 ViewContext/frame ownership | **PASS** | `ViewContext` owns identity/reset/frame completion (`ViewContext.hpp:57-117`, `ViewContext.cpp:75-142`); current CPU contract tests passed. |
| U2 Pass interface/orchestration | **PARTIAL** | The interface and scheduler unit tests pass, but the live scheduler contains five no-op callback adapters (`RHISceneRenderer.cpp:86-100`). The real graph declaration and recording still occur manually in `RHISceneRenderer` (`RHISceneRenderer.cpp:3042-3167`, `3191-4258`). |
| U3 ResolvedSurface/material contract | **PARTIAL** | Built-in/shadow paths use `MaterialResolver` (`RHISceneRenderer.cpp:3421-3427`, `RHIShadowDepthAtlas.cpp:137-147`), and CPU packing tests pass. Custom Material Graph draws still use `ResolveMaterialProperties`/`PackMaterialUniforms` directly (`RHISceneRenderer.cpp:3374-3416`), so the CPU `ResolvedSurface` generation/validity contract is not universal. |
| U4 Resource lifetime/RenderGraph | **PARTIAL** | Target generations, leases and strict graph negatives pass. However graph imports/validates render targets while direct material/IBL/refraction/mesh descriptor resources are bound outside graph declarations (`RHISceneRenderer.cpp:3250-3255`, `3526-3577`); strict compile cannot reject those undeclared graph reads. `ScreenStorage` validation checks live/UAV only, not extent/format (`RHIViewTargets.cpp:33-35`). |
| U5 Service boundaries | **PARTIAL** | Service classes and isolated tests exist, but no production caller invokes `LightingService`, `ReflectionService` or `GIService` (repository search finds only their definitions/tests). Live work remains in `RHISceneRenderer` (`2647-2650`, `2706-2717`, `2744-2844`, `3739-3765`). |
| U6 Temporal/post/diagnostics | **PASS** | Temporal, post-process and diagnostics ownership is wired into the view path (`RHISceneRenderer.cpp:2661-2665`, `3030-3036`, `4250-4264`); timestamp zero/unavailable handling is explicit (`RHISceneRenderer.cpp:2513-2533`). |
| U7 Backend/fallback parity | **PASS** | Policy is centralized and current Debug native D3D12/Vulkan acceptance plus GL preview fallback passed. The reduced OpenGL subset is explicitly documented. |
| U8 Migration/tests/tooling | **PASS** | Editor/Runtime product callers use `RenderService`; architecture checker/dependency map/tooling tests passed. This does not compensate for U2/U5 live ownership gaps. |
| U9 Final acceptance/M15 handoff | **BLOCKED** | The scorecard depends on U2/U4/U5 structural PASS. A current runtime fixture attempt could not launch (`AxleRuntimeRenderTests.exe dx12` returned Windows `0xC0000135` in the available build), so runtime evidence is reported but not independently verified in this checkout. |

## Findings

### M14-R1 — Scheduler is an unconnected no-op adapter (HIGH)

**Source:** `Engine/Scene/src/RHISceneRenderer.cpp:86-100`; `Engine/Renderer/include/Axle/RenderPass.hpp:68-74`; `Engine/Scene/src/RHISceneRenderer.cpp:3167-3189`.

**Expected:** each live pass/group declares resources and records through `IRenderPass`/`RenderPassScheduler`; the facade owns only orchestration.

**Observed:** `BuildPassSchedule()` registers five `CallbackRenderPass` objects with no prepare/record/declare callbacks. `CallbackRenderPass::Declare()` therefore adds nothing, while the actual `graph.AddPass(...)` and all GPU recording remain in the monolithic function. The scheduler unit test proves the generic API, not the production pass ownership.

**Recommendation:** extract at least the five named groups into concrete `IRenderPass` implementations whose `Declare/Prepare/Record/Finalize` callbacks are the live path, or change U2/U9 claims to an explicitly incomplete migration checkpoint and keep M14 on hold.

### M14-R2 — Lighting/reflection/GI services are test-only seams (HIGH)

**Source:** `Engine/Renderer/src/{LightingService,ReflectionService,GIService}.cpp`; production search has no callers. Direct implementations remain at `RHISceneRenderer.cpp:2647-2650`, `2706-2717`, `2744-2844` and `3739-3765`.

**Expected:** M14 services own policy and are the renderer entry boundary so M15 can replace implementations without reopening the monolith.

**Observed:** the classes compile and isolated `[LightingServices]` tests pass, but `RHISceneRenderer` does not construct or call them. M15 would still need to edit the facade's direct probe selection, light packing and DDGI dispatch logic.

**Recommendation:** make the services members of the view/render execution, call them from the live frame path, publish their status/fallback diagnostics, and consume their immutable frame outputs from passes.

### M14-R3 — Strict RenderGraph does not cover all live resource reads (HIGH)

**Source:** graph target declarations `RHISceneRenderer.cpp:2890-3167`; direct descriptor/material binding `RHISceneRenderer.cpp:3250-3255`, `3504-3509`, `3526-3577`; `RenderGraph.cpp:50-77`.

**Expected:** every pass resource read/write is declared once; strict compilation rejects undeclared physical use.

**Observed:** strict compilation validates imported graph textures and their leases, but material textures, IBL maps, refraction copies, mesh vertex/index resources and descriptor-set transitive reads are bound directly in the renderer. They are not represented as graph `Use` entries, so strict graph validation cannot diagnose a missing/incorrect declaration or transition for them. Descriptor lifetime is separately protected by `RHIDescriptorPool`, but that is not the graph authority promised by U4.

**Recommendation:** introduce graph resource tokens for descriptor/mesh reads (or a graph-backed `DeclareDescriptorRead` contract) and make pass `Record` reject any resource not declared by its `Declare` phase.

### M14-R4 — ResolvedSurface is not universal for graph-material runtime data (MEDIUM)

**Source:** `RHISceneRenderer.cpp:3374-3416`; `ResolvedSurface.cpp:52-77`; `MaterialGraphHLSL.cpp:131-149`.

**Expected:** built-in PBR, Material Graph, preview and screen-space consumers share one resolved surface representation or explicit unsupported result.

**Observed:** built-in/shadow draws call `MaterialResolver`, while custom graph draws resolve and pack a separate `MaterialBindingPlan`/custom buffer path. The generated HLSL `MaterialSurface` schema has explicit defaults, which is useful, but there is no shared CPU `ResolvedSurface` generation/validity result for the custom path.

**Recommendation:** define a graph-to-`ResolvedSurface` lowering/status boundary, or document and test the custom graph path as a separate explicitly unsupported backend contract before U3/U9 acceptance.

### M14-R5 — ScreenStorage validation is incomplete (MEDIUM)

**Source:** `Engine/RHI/src/RHIViewTargets.cpp:33-35`.

**Expected:** U4 validates dimensions, format, sample count and UAV/SRV/RTV/DSV usage for every optional target.

**Observed:** `EnableScreenStorage` checks only that the slot is valid and has a UAV; it does not check the configured width/height/format/sample count as the other target checks do.

**Recommendation:** use the same `check2D`/usage contract for `ScreenStorage` and add a negative fixture for mismatched extent/format.

### M14-R6 — U9 runtime evidence is reported, not independently reproducible here (BLOCKER for final acceptance)

**Evidence:** U9 lists a cooked runtime D3D12 PASS (`U9_FINAL_ACCEPTANCE_AND_M15_HANDOFF.md:29-36`), but the available `AxleRuntimeRenderTests.exe dx12` returned `0xC0000135` before running in this review environment. The current editor startup verification did pass: `AxleEditor.exe --verify-empty-startup --rhi=dx12` exited 0 and logged “PASS (0 entities, 0 assets; 5 UI frames)”.

**Recommendation:** rebuild/package the runtime fixture with its required DLL/plugin closure, rerun it from a clean environment, and attach the exact exit code/log to U9. Do not convert the launch failure into a PASS by relying only on the historical report.

## Independently rerun evidence

- Debug build of `AxleRenderUnitTests`, `AxlePreviewRenderTests`, `AxleRenderAcceptanceTests`, `AxleRHIFoundationTests`, `AxleEditor`, `AxleRuntime`: **exit 0**.
- Release build of `AxleRenderUnitTests`: **exit 0**; Release unit executable: **exit 0**.
- `AxleRenderUnitTests`: **exit 0** (all listed contract suites).
- `python scripts/check_architecture.py`: **exit 0**, 596 files / 25 CMake files, no violations.
- `python scripts/generate_m14_dependency_map.py`: **exit 0**, 0 architecture/caller violations.
- `python scripts/test_baseline_tools.py`: **23/23 pass**.
- `AxleRenderAcceptanceTests.exe dx12 ...`: **exit 0**.
- `AxleRenderAcceptanceTests.exe vulkan ...`: **exit 0**.
- `AxlePreviewRenderTests.exe gl surfaces`: **exit 0**.
- `AxleEditor.exe --verify-empty-startup --rhi=dx12`: **exit 0**, 5 UI frames, empty scene/assets.
- `AxleRuntimeRenderTests.exe dx12`: **NOT RUN to completion**; process launch failed with `0xC0000135` (missing runtime dependency in the available build).

These results validate the existing contracts and native paths; they do not close M14-R1 through M14-R5.

## Acceptance recommendation

Keep M14 in **HOLD/PARTIAL**. Accept U0, U1, U6, U7 and U8 as independently verified; record U2, U4 and U5 as partial; keep U9 blocked. The minimum release gate is to wire the scheduler and three policy services into the production frame path, extend graph declarations to all pass resource reads, close the ScreenStorage validation hole, and rerun the packaged Runtime fixture. M15 should remain unreleased until that corrected U9 review is complete.
