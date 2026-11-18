# M15 U5-H1/H2 consolidated Codex review

Date: 2026-11-18. Reviewer: Codex. Scope: all eight S5.8 requirements
`M15-U5-46.1` through `46.8`, combining H1 `bb20102b` and H2 `3a2f197b`.
The delivery base is `6daa53aa` (which already contains H1 and the unrelated,
preserved U10 acceptance documentation). Gemini's two reports remain historical
implementer evidence; the verification below is independently executed by Codex.
The historical suggested-batch table names S5.8 U5-I; the actual released task
and these commits name it U5-H1/H2. Requirement IDs, not that old alias, define scope.

## Disposition

Automated review and necessary repairs are complete: all eight requirements are
accounted for, both configurations build all 13 required targets, and the final
matrix is **34/34 PASS**. No remaining P1/P2 source blocker was found by the
separate-context correction recheck. **Full S5.8 acceptance remains open** for
the user-deferred interactive Editor visual gates 46.5 and 46.8.

Local correction commits:
- `19af7efd`: capture-camera/range, confidence contract and stable receiver identity;
  focused contract tests PASS in Debug and Release before this checkpoint commit.
- `eee4ef4b`: atomic publication, cache ownership/recovery, live resolve controls,
  clip/jitter callers and native regressions; final source verified by native
  Debug DX12/Vulkan before this checkpoint commit and the final Release reruns.

## Findings and repairs

All findings describe inspected pre-repair behavior; the added regressions were
executed against the corrected tree, not represented as before/after execution.

| ID | Severity | Trigger and observed defect | Correction and regression |
|---|---|---|---|
| H-R1 | P1 | Selecting mirror A, then B, then A can reuse A's metadata while the single published texture contains B's pixels. A direct capture into the exposed view target can similarly leave a scheduled receipt valid. | Each successful publication replaces the native texture generation and retires every receipt for its old handle. Explicit overwrites retire matching receipts before recording. Native A/B/A uses distinguishable green/blue captures and checks A's green pixels on return; direct overwrite checks receipt retirement. |
| H-R2 | P2 | Scratch-to-published copy modifies the last-good target before submission/wait success is known. A failure can therefore preserve an ID while corrupting its pixels. | GPU work only touches a fresh candidate. Commit its ownership after checked completion. Inject failure after completed GPU work and verify the old native handle, generation ID and blue pixels remain unchanged. |
| H-R3 | P2 | Component clip bias is added twice by default, and biased capture plane is reused to locate the receiver. A positive 0.03m offset can erase reflection on the real surface. | The surface supplies authored bias once; resolve uses the unbiased geometric plane. CPU camera-plane and native HDR receiver checks cover both. |
| H-R4 | P2 | Capture near/far and component resolution are serialized and hashed but do not control capture. | Rebuild capture projection from authored depth range. Capture at min(component scale, view cap); resample into a fresh publication at the view target extent. CPU range and native 32px capture under a 64px view cap checks. |
| H-R5 | P2 | All failed input revisions suppress automatic retry indefinitely. Changing capture resolution also rejects a still-live last-good publication before a replacement is ready. | Suppress only deterministic camera/preflight failures. Retry transient failures on unchanged inputs; retain same-epoch/same-handle publication across resolution edits and failed recapture. Native automatic retry and resize-with-failure checks. |
| H-R6 | P2 | Manual/stale resolve uses cached reflector controls, so intensity and other resolve controls remain stale until a new capture. | Resolve uses current controls with the projection of the retained captured image. Native intensity 0/.5/1 changes keep one manual generation and check HDR composition with SSR off/on. |
| H-R7 | P2 | Depth is rendered with jitter but planar reconstruction inverts the unjittered projection. CPU confidence thresholds also differ from native smoothstep, plane thickness, fade and roughness caps. | Pass actual main-view `projZO`; align geometric confidence and cap edge fade consistently. CPU numerical cases plus native capture/main TAA isolation and HDR checks. |
| H-R8 | P2 | Explicit capture waits on the public last-render receipt, which a rejected render can clear while the actual main view still owns GPU work. | Synchronize against `m_ViewContext.GetCompletion()`. Inspected completion-retained RHI wrappers and independent capture ViewContext; native validation/isolated main-history tests exercise the callers. |
| H-R9 | P2 | Nearest invalid/back-facing mirror suppresses a usable candidate. A mirror at valid EnTT index zero is not self-excluded because the filter treats zero as absent. | Prefer valid camera/surface candidates before distance. Exclude receiver by nonzero stable UUID as well as nonzero entity ID; align capture stamps. CPU ID-zero filter test and native ID-zero red self-geometry, behind-plane blue blocker and green single-sided target oracle. |

## Requirement-to-evidence audit

The paths below are in addition to the source correction diff; this audit includes
serialization, extraction, Editor, runtime policy and failure paths delivered in H1/H2.

| Requirement | Implementation and affected callers | Independent evidence | Status |
|---|---|---|---|
| 46.1 Surface contract | `RenderContracts.hpp/.cpp`, `RenderWorld.hpp`, `Components.hpp`; `SceneExtraction.cpp` inverse-transpose normal/basis; `SceneSerializer.cpp`, `AssemblySerializer.cpp`, `RuntimeComponentCodec.cpp` | Contract/extraction/serialization CPU tests; Assembly preview/save/remove round-trip; authored range, clip bias and resolution regressions | PASS (automated) |
| 46.2 Independent capture | `PerspectiveCamera`, `RHISceneRenderer::CapturePlanarReflection`, `m_PlanarViewContext`, state guard and `FilterCaptureWorld` | Native self-exclusion, recursion stripping, reflected winding, frame-index/TAA-history isolation; actual completion token inspection | PASS (automated) |
| 46.3 Targets/lifetime | `RHIViewTargets`, renderer scheduling/publication, `RenderService`, `RHIViewportDescriptor` | CPU target-size contract, native storage/targets, component scale resampling, resize failure retention, device-reset native regression and RHI validation | PASS (automated) |
| 46.4 Dedicated pass | `Passes/PlanarReflectionPass`, `RHISceneRendererPasses.cpp`, Scene CMake registration | CPU DAG contract and native planar-only / SSR+planar runs; capture owns a separate graph, main graph declares copy/resolve/release resources | PASS (automated) |
| 46.5 Energy/fallback | `PlanarReflection.hlsl`, reflection MRT built-in/custom pipelines, legacy custom capability fallback, shared SSR/probe/sky energy path | Native ON/OFF and roughness fallback; new linear-HDR confidence midpoint and receiver-emission preservation with SSR on/off; source trace of probe/sky composition | PASS (automated); NOT RUN (EDITOR VISUAL, user-deferred) |
| 46.6 Clip/extents/self | Oblique camera contract, reflected winding for built-in/custom/normal pipelines, receiver geometric plane and extent gate | Native green HDR target (radiance 8) with red ID-zero self and blue behind-plane blocker excluded; CPU extent/fade and clipped/behind-camera checks; biased receiver native regression | PASS (automated) |
| 46.7 Invalidation/retention | Domain stamps, scene/device eviction, Manual/Automatic requests, atomic publication, live controls | CPU all-domain invalidation; native hit, edit, manual stale/refresh, automatic unchanged retry, pre/post-submit fault retention, capture resolution failure, A/B/A and direct overwrite | PASS (automated) |
| 46.8 Editor/native evidence | `EntityInspector`, `SceneHierarchyPanel`, `AssemblyEditorPanel`, vector icons, Editor feature menus; `RenderService` UI facade | Source trace; actual PluginSession Assembly preview/edit/save/remove test; strict empty/U4E startup in both configs/backends | PASS (automated); NOT RUN (EDITOR VISUAL, user-deferred) |

## Design and lifetime recheck

The view owns one selected planar source, not an unbounded texture cache per
reflector. A valid front-side candidate wins over invalid candidates, then distance
selects the plane. A changed selected plane must either own the current published
handle or recapture; evicted/manual surfaces use SSR/probe/sky fallback. The global
view resolution setting is a cap: scratch capture honors the lower component
scale, while publication remains at the view's allocated extent. Captures are
synchronous and nonrecursive; no claim of asynchronous scheduling or simultaneous
multi-plane resolve is made.

A publication allocates one temporary HDR candidate in addition to the retained
last-good image. Failed candidate work does not modify last-good pixels. Command
wrappers retain native resource generations, descriptor sets/pools, constants,
layouts and pipeline references through completion, including the resampling path.
Scene replacement, target generation replacement and device epoch changes cannot
reactivate an old receipt. Manual images retain their captured projection until
refresh, while current receiver/strength controls are applied at resolve time.

The required separate-context reviewer inspected the actual corrections and
callers, found the resolution-retention and CPU/native confidence mismatches,
and rechecked their repairs. It also caught the initial wrong filter hunk in the
ID-zero repair; that hunk was moved to the planar namespace and the cubemap filter
was restored. The final source recheck reports no remaining P1/P2 blocker.
This is independent source review, not independently executed test evidence.

## Executed verification

All evidence below was executed by Codex against the corrected production source.
The final executable includes the corrected SSR-target fixture in both configs.
Raw commands, arguments, exit codes and log paths: `build/Codex-U5H-results.json`.

| Gate | Actual result |
|---|---|
| All 13 targets, Debug + Release | 13/13 each, exit 0; `build/Codex-U5H-build-{Debug,Release}.log` |
| Focused Hi-Z / reflection capture / lighting services | 6/6 commands, exit 0 |
| Full CPU and environment bake | 4/4 commands, exit 0 |
| CTest Hi-Z / reflection / environment / MaterialGraph | 9/9 tests each config, exit 0 |
| PluginSession Assembly preview/edit/save/remove | Both configs exit 0 |
| RHI storage + targets, DX12 + Vulkan | 8/8 commands, exit 0 |
| Full native environment/capture/material/planar GPU suites | 4/4 combinations, exit 0; zero RHI validation errors |
| Strict Editor empty + U4E startup, DX12 + Vulkan | 8/8 commands, exit 0, `fallback=none` |
| Architecture | exit 0; 615 files / 25 CMake files; `build/Codex-U5H-architecture.log` |
| Exchange validation and whitespace | exit 0 |

The 34 gate records include grouped CTest suites, not merely 34 individual assertions.
All six DX12 presentation gates blocked in Gemini's report now independently
PASS in the current display context; that historical environment block is resolved.
Editor startup is still not interactive visual acceptance.

Native linear-HDR oracles agree across Debug/Release DX12/Vulkan:
- Direct planar capture center `(0,8,0)` excludes red ID-zero receiver geometry
  and the behind-plane blue blocker while preserving reflected front-face winding.
- A/B/A publication reads green `8`, blue `5.5`, green `8` rather than reusing B.
- Half-confidence midpoint error with SSR off is at most `0.0011` per channel;
  with SSR on it is below `0.0003`, and receiver emission remains above `1.9`.
- Quarter-scale capture is actually 32x32 under a 64x64 publication cap; positive
  clip bias leaves the real receiver visible. Pre/post-submit failure tests retain
  the old native generation and pixels, and unchanged transient inputs retry.

Commands: build `AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`,
`AxleEnvironmentBakeTests`, `AxleRHIResourceViewTests`, `AxleEditor`, `AxleRuntime`,
`AxleMaterialGraphTests`, `AxleMaterialGraphCompilerTests`,
`AxleMaterialGraphOpenGLPolicyTests`, `AxleMaterialGraphSlangTests`,
`AxleMaterialGraphRuntimeTests`, `AxleMaterialGraphResourceTests`,
`AxlePluginSessionTests` using `cmake --build build --config {Debug,Release}
--target <the 13 targets above> --parallel 8`. Execute the 34 commands in
`build/Codex-U5H-verify.ps1`. The two affected native Release reruns are recorded
by `build/Codex-U5H-retry.ps1` with exact arguments in the final JSON.

The first Release GPU executions failed the **new test fixture** when SSR was
turned on without declaring its additional screen-storage/Hi-Z view targets.
The fixture now configures those required targets before rendering each mode;
production source was unchanged. Both native executables were rebuilt (exit 0;
`build/Codex-U5H-energy-build-{Debug,Release}.log`). Debug ran with that correction;
both Release GPU suites were rerun successfully. Initial exits/logs/arguments are
preserved in `InitialExit`, `InitialLog`, `InitialArguments` on the JSON records,
including `*.pre-fixture-fix.log`; final PASS does not conceal those initial failures.

## Open acceptance gates

- `46.5` and `46.8`: user-deferred interactive Editor ON/OFF images with feature
  policy visible, Debug/Release capture inspection and visual comparison. Strict
  startup, UI serialization tests and offscreen pixels do not close these gates.
- No U6 or later package is released by this review. GTAO halo work is outside scope.
