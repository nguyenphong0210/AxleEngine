# Phase 41: RHI-Primary Renderer Implementation Plan

> **Execution:** follow [OPT-00..11](../ROADMAP.md). This document is a
> technical contract, not a separate phase queue. Old Phase/G labels and
> source-audit baselines are retained for traceability. Early Vehicle retirement
> is delivered; acceptance and new plugin implementation remain open.

Date: 2026-11-03 (Asia/Saigon). Planning baseline: `492e6d1`.
Status: **Phase 41 rendering workstream; architecture-first; no package implemented by this document**.

Read the [architecture contract](RHI_PRIMARY_RENDERER_ARCHITECTURE.md) and
[implementation guide](RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md).
The [shared exchange](../agents/CODEX_GEMINI_EXCHANGE.json) records the current
assignment, implementer evidence and independent review. This plan does not
release all packages to an agent. Consult the live task card for the current release.

## Priority and execution policy

**Updated coordinator (2026-11-04):** The
[master plan](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) owns cross-track order and
additional prerequisites; the [NRI contract](NRI_INTEGRATION_CONTRACT.md) specifies
the pinned integration. P41-00..20/25/26 are the rendering architecture-first scope.
P41-21..24 and complete P41-27..29 remain deferred feature/full-phase acceptance
work. They are not removed or marked complete. Architecture evidence may feed
later measurement/acceptance without closing an unmet package.

P41-19 explicitly includes actual shipping Canvas GPU submission using the common
resource/frame services; layout generation or DrawCalls statistics alone do not
establish Runtime UI rendering. P41-26 verifies/reconciles AF/PLG boundaries rather
than separately reimplementing Core extraction, plugin splitting or static composition.

Rendering foundation work executes ahead of new Phase 40 authoring and Phase 39.1/39.2 expansion.
Its historical number preserves package references; OPT order in the
[optimization roadmap](../ROADMAP.md) is the execution priority. Existing material/VFX acceptance obligations and submitted
animation work remain recorded, not silently completed or discarded.

Phase 40 G1 remains delivered awaiting independent review. D1/D2 are deferred.
Only animation CPU prerequisites explicitly needed by P41-23/24 may be reviewed
or corrected within that integration assignment; this is not permission to start
G2 or another animation editor. Reuse Phase 40 F1-F3 contracts and evidence.

Use separately reviewable behavior/test commits. Split a package into named
subcommits when it is too large; do not equate 30 packages with 30 equal-sized
changes or a delivery-time estimate. No push unless requested. A build or GPU
environment limitation is NOT RUN, never an inferred PASS.

## Gates and dependency order

| Gate | Packages | Runnable outcome |
| --- | --- | --- |
| G0: safety and visible primitives | P41-00..04 | Real backend/fallback identity, failed creation stays safe, Cube/Sphere/Cylinder/Cone/Plane create and round-trip correctly |
| G1: frame/resource foundation | P41-05..11 | Generations, recording/submission leases, state tracking, frame slots, presentation, safe targets and uploads |
| G2: common scene rendering | P41-12..14 | Immutable RenderWorld, Editor service integration and working GL compatibility adapter |
| G3: all consumers | P41-15..20 | Material/VFX/assembly/thumbnail views, Runtime rendering and declared shader/material behavior |
| G4: functional parity | P41-21..25 | Shadows, HDR environment, animation bridge, matching picking and physics debug |
| G5: production cutover | P41-26..29 | Runtime build without Editor, measured frame behavior, required acceptance and documented modern defaults |

The table below defines dependencies, not merely suggested ordering. Execute in
listed order by default. Available independent packages still require a scoped
assignment; source-writing tasks do not run concurrently in the shared checkout.

| Package | Depends on | Primary responsibility |
| --- | --- | --- |
| P41-00 | None | Always-active regression harness and evidence baseline |
| P41-01 | 00 | Backend/readiness/fallback contract |
| P41-02 | 01 | Transactional GPU creation and validation |
| P41-03 | 01, 02 | Structured GPU errors and lost-device shutdown |
| P41-04 | 01, 02 | Canonical primitive creation and persistence |
| P41-05 | 02, 03 | Resource identities, generations and ownership |
| P41-06 | 05 | Recording/submission leases and deferred retirement |
| P41-07 | 03, 05 | Submission-aware resource state tracking |
| P41-08 | 06, 07 | Frame slots and completion tokens |
| P41-09 | 08 | Shared presentation and UI submission boundary |
| P41-10 | 06, 08, 09 | Transactional per-view targets and GUI texture leases |
| P41-11 | 05, 06, 07 | CPU decoding, upload and GPU cache boundary |
| P41-12 | 04, 05, 11 | Shared render extraction and immutable RenderWorld |
| P41-13 | 08, 09, 10, 12 | RenderService and initial sequential render graph |
| P41-14 | 01, 12, 13 | OpenGL compatibility adapter |
| P41-15 | 13, 14 | Asset/material preview migration |
| P41-16 | 13, 14 | VFX preview migration |
| P41-17 | 13, 14 | Assembly preview migration |
| P41-18 | 10, 13, 14 | Thumbnail rendering migration |
| P41-19 | 13, 14 | Standalone Runtime service integration |
| P41-20 | 12, 13, 14 | Shader/material/color and capability contracts |
| P41-21 | 13, 20 | Static shadows and cascade passes |
| P41-22 | 11, 13, 20 | HDR skybox and IBL asset integration |
| P41-23 | 12, 13, 20, 21; reviewed animation prerequisites | Shared animation rendering bridge |
| P41-24 | 13, 20, 23 | Common geometry/pose/material picking |
| P41-25 | 13, 14, 19 | Physics debug integration |
| P41-26 | 15..19 | Module and product build boundaries |
| P41-27 | 08..10, 13, 19..25 | Measured frame/upload performance |
| P41-28 | 00..27 | Full acceptance and failure/stress matrix |
| P41-29 | 28 | Default rollout and obsolete path removal |

Numbers in the dependency column refer to P41 package suffixes. Existing bug-fix
commits are regression baselines, not substitutes for these package contracts.

## Package contracts

### P41-00 — Baseline and always-active render tests

- Inspect source/CMake/HEAD and record current D3D12, Vulkan, explicit GL and
  automatic fallback behavior. Record unavailable environments individually.
- Add device-free tests for pure contracts and always-active checks in Debug and
  Release. Add a deliberately failing self-test to verify the harness fails.
- Convert side-effecting assertions in the touched legacy RHI tests; do not
  mechanically rewrite every unrelated test. Preserve VFX/material regressions.
- Prepare separate opt-in hardware/windowed/product targets and failure exits.
- Files: root/Editor CMake, `Tests/src/Test_Milestone8_Part2.cpp`, proposed
  `Tests/rendering/` harness. Completion: new harness and failure probe both behave
  correctly in Debug/Release; baseline is evidence, not a full acceptance claim.

### P41-01 — Backend readiness and fallback equivalence

- Introduce explicit selected backend, context-ready and native-NRI-ready queries.
  Keep compatibility wrappers while migrating callers; audit generic
  `IsInitialized()` branches in Asset/Renderer/UI/Scene and default resource code.
- Make explicit GL and automatic GL fallback use identical creation/loading
  policy. Successful backend selection does not certify renderer or UI readiness.
- Complete product initialization checks: device, window, required interfaces,
  renderer and UI bridge. Roll back partial startup before trying another backend.
- Files: `RHIDevice`, `RHISwitcher`, `Editor/src/Main.cpp`, `Application`,
  `Window`, `AssetSerializer`, resource call sites.
- Verify injected modern-backend startup rejection without exhausting hardware,
  strict/no-fallback failure, native-device guards and identical GL mesh pixels.

### P41-02 — Transactional GPU resource construction

- Validate descriptors against queried device/format/sample limits before casts.
  Handle zero/overflowing dimensions and unsupported formats/usages explicitly.
- Clean partially created NRI wrappers on failure. Construct texture/buffer/views
  as candidates; publish only after every required operation succeeds.
- New-object failure leaves no valid native handle. Replacement failure preserves
  the previous generation when the device remains usable.
- Files: `RHITexture`, `RHIBuffer`, `RHIMesh`, descriptor/pipeline helpers and
  intermediate-target callers that currently ignore `Init()` results.
- Verify allocation failure after real wrapper creation, SRV/RTV/DSV failure,
  reinitialization recovery and no invalid draw/registration. No OOM stress on the
  user's GPU is required; inject at the native-operation boundary.

### P41-03 — Error propagation and device-loss state

- Carry structured create/upload/record/submit/present results to the coordinator.
  Preserve the first error, backend, operation and relevant resource/pass name.
- Model ready/surface-rebuild-required/lost/stopping states. Stop submissions on
  device loss and choose bounded cleanup; do not repeat indefinite fence waits.
- Keep startup fallback distinct from recovery after a live device is lost.
- Files: `RHIDevice`, `RHICommandBuffer`, swapchain/UI bridge, renderer entry points.
- Verify submit/present failure policy and timeout/teardown decisions with injected
  results. Native diagnostics are collected when available; do not induce a GPU reset.

### P41-04 — Primitive creation and canonical mesh identity

- Create each primitive's render component independently of GL VAO availability.
  Assign explicit builtin identity and use a shared resolver for both backends.
- Preserve static/dynamic physics, names, transform defaults and legacy colors.
- Files: `SceneHierarchyPanel::CreatePrimitive`, `MeshInstance3D`, mesh resolver,
  scene/assembly serialization and product creation entry points.
- Verify all five primitives through actual menu/factory APIs, rename, duplicate,
  Scene::Copy, save/reopen and GL/D3D12/Vulkan pixels. A renderer-name fallback is
  not an acceptable fix. A saved legacy scene without a mesh component cannot be
  safely reconstructed from an arbitrary name; report this limitation explicitly.

### P41-05 — Resource identity and generation ownership

- Define asset identity versus device-specific resource handle/lease. Include a
  generation and device epoch or equivalent stale-reference protection.
- Assign resource ownership to a render resource service. Existing component
  native fields may remain as migration caches; new callers use shared contracts.
- Preserve old published generations while frames hold them. Prevent a stale
  destructor from destroying resources through a different/new device.
- Verify stale handle rejection, slot reuse, shared immutable assets, independent
  instances and resource teardown before owning device destruction.

### P41-06 — Unified retirement for recorded and submitted work

- Track resources as soon as CPU recording/UI draw lists reference them, then bind
  the lease to successful submission/completion. Account for abandoned frames.
- Retire textures, views, descriptor registrations and upload backing memory as
  one generation. Callbacks must not depend on a destroyed panel/UI owner.
- Migrate Inspector, thumbnails and preview teardown to the shared mechanism;
  adapt the existing `ImGuiLayer::DeferRelease` boundary during transition.
- Verify invalidate/reload/close before submission, previous frames in flight,
  allocation-slot reuse, failed submission, nested retirement and shutdown order.
  GPU readback must retain original pixels until completion, then allow reuse.

### P41-07 — Resource state tracking and declared usage

- Integrate state tracking into scene/post-process/upload/picking/native-UI paths.
  Track first-use, resource generation and mip/layer/plane ranges used by passes.
- Keep pending recording state separate from submitted state; commit on success.
  Import external resources with explicit state/ownership expectations.
- Begin with one graphics queue. Reject unsupported queue-transfer usage rather
  than silently pretending multi-queue correctness.
- Verify new targets, repeated frames, resize, depth copies, MSAA resolve,
  abandoned recordings and submit failure. Use real API validation on both modern
  backends; an empty validation run without the relevant pass is insufficient.

### P41-08 — Frame contexts, completion and allocator reuse

- Add frame slots owning command allocators, descriptor arenas, uniform/upload
  ranges and retained generations. Reuse only after the slot's completion token.
- Keep the current conservative waits until all reused storage is accounted for.
  Introduce overlap only with safe arena/resource lifetime and bounded pressure.
- Verify a deliberately busy slot, multiple views, failed frames, ring-buffer
  wrap, shutdown and no overwriting mapped data still read by GPU work.

### P41-09 — One presentation owner and UI render bridge

- Make application/render composition own acquire, UI/view recording, submission
  and present. Reuse/refactor `RHISwapChain`; retire competing UI-owned frame loops.
- ImGui may use its native backend to record into coordinator-owned commands;
  native interop is contained in the bridge, with explicit descriptor/state use.
- Apply one actual VSync/surface/minimize policy. Handle out-of-date/suboptimal
  surfaces and preserve structured failure status.
- Verify windowed D3D12/Vulkan resize, minimize/restore, VSync request propagation,
  UI-only frames and standalone present without ImGui. Do not claim this gate
  from headless offscreen rendering alone.

### P41-10 — View targets and external GUI leases

- Move color/depth/MSAA/history ownership into a per-view object. Resize/format
  changes construct an entire replacement set before swapping it in.
- Replace raw panel descriptor ownership with a leased GUI texture token whose
  lifetime covers current recording and submitted frames. Preserve external
  target ownership explicitly.
- Verify close/resize after queuing an image, rapid HDR/MSAA changes, zero-sized
  views, target allocation failure and multiple previews with different sizes.

### P41-11 — Asset decoding, upload and shared GPU cache

- Separate context-free mesh/image decoding from backend resource creation.
  Route explicit/fallback GL through the same CPU data and interpretation policy.
- Key GPU cache entries by source/import/color-space interpretation and device
  generation; preserve last-good resources on recoverable reload failure.
- Move decode/upload requests out of arbitrary panel draw helpers. Initial uploads
  may be synchronous through the central owner; async upload is not mandatory yet.
- Verify raw/descriptor texture forms, linear/sRGB, bad files, role changes,
  rename/delete/reload, duplicate views and non-default asset/VFS directories.

### P41-12 — RenderWorld and shared scene extraction

- Extract immutable mesh/material/light/world-transform/camera/selection records
  without allocating GPU resources during ECS iteration. Use shared resolution.
- Snapshot stable entity identity and optional pose references; reject stale
  selection readbacks. Parent transforms apply consistently to meshes and lights.
- Keep physics/simulation outside Renderer. Test extraction without a GPU or UI.
- Verify naming independence, clone isolation, nested transforms, entity deletion
  and extraction not mutating authored components or registry structure.

### P41-13 — RenderService and minimal modern render graph

- Add view request/result contracts and one coordinator used by main Editor view.
  Convert existing passes incrementally to declared resource reads/writes.
- First graph is sequential and validates dependencies/load-store/first-use.
  Preserve PBR, textures, transparent ordering, VFX and post-processing behavior.
- External view/UI textures are imported/leased, not incorrectly graph-destroyed.
- Verify dependency errors, unused optional passes, repeated/empty views, private
  targets and actual existing material/VFX GPU outputs through the new entry point.

### P41-14 — OpenGL compatibility behind the same service

- Adapt legacy GL rendering to the same RenderWorld/ViewRequest/result contract.
  Keep GL-specific shaders/resources behind the adapter and preserve existing
  shadows/animation behavior. Do not route GL through an absent NRI device.
- Declare the compatibility feature baseline; disabled advanced settings retain
  authored values and report unsupported behavior.
- Verify explicit GL and injected automatic GL fallback, primitives, imports,
  hierarchy, materials, selection, resize and startup/shutdown on a real GL context.

### P41-15 — Asset and material previews

- Migrate `AssetPreviewPanel` to private service views, shared asset resolution
  and leased output textures. Preserve material editing, two preview lights,
  camera/object controls, checkerboard and icons.
- Verify different main/preview materials, edits/reload, close/reopen and resize
  within the same frame. No private renderer submission or panel idle wait remains.

### P41-16 — VFX preview

- Migrate `VFXEditorPanel` render targets/recording through the service while
  retaining private deterministic simulation and neutral empty-effect workflow.
- Preserve LMB orbit/RMB-MMB pan capture and unsaved document behavior.
- Verify main+preview effect isolation, authored textures/materials, seek, close,
  resize, multi-emitter draw storage and no hidden scene/effect insertion.

### P41-17 — Assembly preview

- Replace the GL-only Assembly Editor viewport with a private service scene/view
  built from the authored hierarchy and shared mesh/material references.
- Preserve selection, transform editing, wireframe where supported, history and
  save/instantiate behavior. Show honest capability diagnostics otherwise.
- Verify builtins and imported multi-part assemblies, parent transforms,
  save/reopen, editing isolation and real D3D12/Vulkan/GL preview pixels.

### P41-18 — Thumbnail render jobs

- Render mesh/assembly thumbnails through bounded private service jobs; reuse
  decoded/GPU assets and leased results. Preserve existing texture/material output.
- Cache by relevant asset revision and view settings; invalidate safely while
  content-browser images are already queued. Bound work and memory per frame.
- Verify RHI mesh/assembly silhouettes, missing assets, rapid directory changes,
  invalidate-before-submit, eviction and no descriptor reuse ahead of completion.

### P41-19 — Standalone Runtime render integration

- Share backend resolution, RenderService, view/camera requests and present
  without requiring an ImGui window. Apply project/CLI precedence consistently.
- Preserve physics/scripts/audio/simulation behavior; renderer migration is not
  permission to alter scheduling or insert sample scenes/UI.
- Verify cooked and loose asset startup, fallback, material/VFX views, resize and
  shutdown on all supported paths. Use tiny fixtures outside user assets.
- This package establishes runtime behavior. Editor-free build closure is a
  separate requirement of P41-26 and remains open until that package passes.

### P41-20 — Material/shader/color/capability contract

- Consolidate existing vertex/constant/texture layouts, reflection and pipeline
  keys. Preserve custom shader last-good generations and serialization semantics.
- Specify exposure/tone-map/sRGB ownership, clip/depth conventions, alpha/culling
  and renderer feature support separately from physical device capabilities.
- Verify actual binding/packing plus output pixels for required roles, surface
  modes, invalid reload and mixed views. GL unsupported advanced features must be
  explicit, not a silent authored-property reset. No new shader language is needed.

### P41-21 — Modern shadow passes

- Implement directional shadow maps/cascades, pass resources, bias/filter/quality
  settings and sampling in the modern material path. Declare static baseline and
  honest limits for other light shadows. Integrate through graph/resource service.
- Verify caster movement changes receiver pixels, disabling shadows removes them,
  cascade/resize behavior and alpha-tested silhouettes on D3D12/Vulkan.
- Animated shadow coverage is added/verified by P41-23, not claimed here.

### P41-22 — Authored HDR skybox and IBL

- Resolve scene HDRPath, decode/upload/generate environment, irradiance, prefilter
  mips and BRDF LUT as needed. Bind actual authored maps rather than default ones.
- Respect enabled/type/intensity/exposure and preserve procedural sky behavior.
- Verify two distinct HDR fixtures change sky and material reflections, reload,
  invalid-file fallback and multiple isolated environments on D3D12/Vulkan.

### P41-23 — Animation bridge, once

- Review the required existing immutable rig/geometry/pose and G1 submission
  contracts before relying on them. Reuse Phase 40 F1/F2 requirements; avoid a
  parallel importer/controller or a second animation renderer architecture.
- Add skinned render records, palette upload, vertex layout/pipeline variants,
  current pose/bounds and geometry/shadow recording with frame-safe storage.
- Verify two instances at different times, duplicate isolation, posed silhouettes,
  animated shadows, reload and GL regression. CPU animation PASS is not GPU skinning.

### P41-24 — Geometry/pose/material-consistent picking

- Use the same resolved resource generation, world transform and pose as visible
  rendering. Define alpha-test and transparent selection policy and test it.
- Remove renderer/picker name guesses. Reject readbacks for deleted/reused entity
  handles or an incompatible frame mapping. Map Phase 40 F3 picking evidence here.
- Verify renamed builtins, imported submeshes, alpha holes, skinned silhouettes,
  camera/depth conventions, odd resize and selection after entity deletion.

### P41-25 — Physics debug and overlays

- Supply active edit/play PhysicsScene through the common view request or bridge;
  do not rely on a runtime-only scene pointer while editing.
- Preserve collider/vehicle debug flags, depth/bias/alpha and view isolation.
- Verify Edit/Play/Pause/Stop, new/open scene and actual collider lines on the
  supported backends; no debug draw must require GL mesh creation.

### P41-26 — Build and ownership boundaries

- Move ECS/PBR/light policy out of low-level RHI behind existing contracts. Split
  runtime/editor plugin targets and product composition. Remove hidden upward
  include dependencies from Core as affected services move to composition.
- Add real build switches for Editor, Runtime, tests and GL compatibility. Preserve
  public transition adapters until every caller migrates.
- Verify configure/build Runtime with Editor OFF and no ImGui/AxleUI/editor plugin
  link dependencies; verify the full Editor configuration separately. CPU tests
  must remain buildable without a GPU/window. Avoid a global folder-only rename.

### P41-27 — Performance after correctness

- Measure baseline and migrated main+multiple-preview frames, uploads, waits,
  descriptors and VRAM. Remove per-view queue-idle synchronization only after
  frame-slot storage and leases prove safe. Keep intentional diagnostic/shutdown waits.
- Add bounded cache/thumbnail scheduling and upload pressure handling. Introduce
  batching/async work only where measured benefit justifies the complexity.
- Compare fixed workloads/configurations and report results; no invented FPS or
  universal hardcoded budget. Stress more than the configured in-flight frame count.

### P41-28 — Acceptance matrix and reproducibility

- Run the matrix below against rebuilt binaries and identified hardware/config.
  Save first errors, logs/readbacks, fixtures, commands/exits and build metadata.
- Cover Debug/Release checks, modern backends, explicit/fallback GL, native Editor
  workflows and standalone cooked playback. Track unsupported versus NOT RUN.
- Keep material/VFX/animation acceptance independent; cross-reference overlapping
  evidence without automatically checking off their whole phases.

### P41-29 — Cutover and cleanup

- Make product startup consistently prefer the accepted modern backend with
  documented startup fallback. Retire duplicate resolution/submission/presentation
  and component GPU caches only after all consumers have migrated.
- Remove dead feature flags, update ownership/backend docs and record remaining
  compatibility limits. Keep OpenGL fallback operational and testable.
- Completion: all required matrix rows have evidence, Runtime builds without
  Editor, no obsolete active renderer path is accidentally selected, and the
  user-visible defaults match the documented actual backend.

## Acceptance matrix

All rows start **NOT RUN for Phase 41**. Earlier tests establish historical
behavior only; changes require relevant re-verification. Required checks must
PASS to close the phase. An explicitly approved unsupported advanced GL feature
is a documented capability limit, not an unexplained skipped baseline check.

| Scenario | CPU Debug/Release | D3D12 hardware | Vulkan hardware | GL context | Windowed/product |
| --- | --- | --- | --- | --- | --- |
| Readiness, strict selection and fallback equivalence | Required | Required | Required | Required | Editor + Runtime |
| Creation/replacement/upload failure and stale handles | Required policy | Required boundary/readback | Required boundary/readback | Relevant resource path | No crash or leaked live generation |
| Primitive/import/rename/clone/save/open | Required identity/extraction | Pixel/ID checks | Pixel/ID checks | Pixel/selection checks | Actual creation/drop workflows |
| Recording/submission retirement and close/resize | Required | Required | Required | Required | Multiple views, invalidate during UI |
| State/layout, depth copies, MSAA and post-process | Required plan validation | Native validation + pixels | Native validation + pixels | Compatibility baseline | Resize/minimize/restore |
| Materials/VFX/private previews/thumbnails | Relevant CPU suites | Required | Required | Supported baseline | Author/edit/save/reopen |
| Shadows/HDR environment/skinning/picking/debug | Relevant CPU data | Required | Required | Existing GL regression/declared support | Scene + applicable previews |
| Present/VSync/surface changes/device failure policy | Required injected results | Real surface | Real surface | Real surface | No ImGui requirement in Runtime |
| Cooked assets and Editor-free Runtime | Serialization/build checks | Required | Required | Required fallback baseline | Packaged start scene, HUD, scripts, exit |
| Long-run pressure and performance evidence | Bounded scheduling | Required | Required | Required baseline | Fixed main + preview workload |

Native validation reports must be reviewed for relevant resource/sync errors.
Expected injected errors must be labeled and separated from unexpected errors.
Hardware unavailable means NOT RUN with an owner, not silently passing via WARP.

## Check commands and delivery format

Current existing commands (run only after rebuilding their targets):

```powershell
cmake --build build --config Release --target AxleEditor AxleRuntime AxleVFXUnitTests AxleVFXGPUTests --parallel 6
ctest --test-dir build -C Release -R '^VFXUnitTests$' --output-on-failure
$env:AXLE_VFX_TEST_BACKEND = 'D3D12'
& .\build\Editor\Release\AxleVFXGPUTests.exe
$env:AXLE_VFX_TEST_BACKEND = 'Vulkan'
& .\build\Editor\Release\AxleVFXGPUTests.exe
```

These commands are not complete Phase 41 acceptance. New target/option names in
the coding guide are proposed and must be created before their commands are used.
Avoid changing the user's active build options or closing an unsaved Editor just
to make a check pass; use a separate build/output or coordinate the locked binary.

For each package record: ID, baseline/commit(s), production behavior, exact checks
with exit codes, evidence paths, unsupported/NOT RUN cases and remaining findings.
Keep implementation, independent review and physical/native acceptance distinct.
Update this plan's execution ledger and the shared exchange; never infer the next
assignment from unchecked roadmap rows.

## Execution ledger

| Package range | Implementation | Review | Required acceptance |
| --- | --- | --- | --- |
| P41-00 | Delivered in OPT-01 | Accepted predecessor | See OPT-01 review |
| P41-01 | Implemented and self-verified Debug/Release | Final review pending | See OPT-04 execution record; API-validation layers unavailable |
| P41-02 | Implemented and self-verified Debug/Release | Final review pending | See OPT-04 execution record |
| P41-03..11 | NOT STARTED | NOT STARTED | NOT RUN |
| P41-12..29 | NOT STARTED | NOT STARTED | NOT RUN |

Split this row into per-package rows as implementation starts. Documentation
creation and prior Inspector/VFX bug fixes do not complete any package here.
