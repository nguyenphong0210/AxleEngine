# Architecture Optimization Roadmap

Updated: **2026-11-05**. Ordering baseline: `7869fce` (post-Vehicle retirement).
Status: **execution plan; this update changes documentation only**.

## Start here

This is the single execution order and progress checklist for the architecture
cleanup. Work down **OPT-00 -> OPT-10**; **OPT-11 is deferred** until that cleanup
is accepted. Finish and verify one bounded package before starting the next.
Use the phase's detailed contract only when it becomes relevant.

**Current assignment: OPT-01 AF-01, assigned to Gemini.** The user requested
Codex correct O1-R1..R4 and provide the next English prompt. AF-00/EJ-00 corrections
in `af5721b` plus the completed inventory satisfy baseline entry checks: Debug and
Release CTest 15/15, 18 tool regressions, and nine acceptance controls per native
configuration. [Correction evidence](reviews/OPT_01_BASELINE_CORRECTIONS.md) explicitly
records Codex-authored verification, not a separate post-fix independent review.
The [AF-01 handoff](agents/handoffs/OPT_01_AF01_GEMINI.md) releases stable asset
identity and safe persistence only. P41-00, PLG-00 and later packages remain planned.
This user-directed progression retains pending OPT-00 full product verification
as a recorded evidence gap; OPT-00 is not marked accepted. Existing production
ECS/jobs faults remain visible for their later EJ packages.

**Prior checkpoint: OPT-00, clean-removal follow-up awaiting independent verification.**
The user confirmed on **2026-11-05** that Engine must be clean: old data formats,
legacy Vehicle payload preservation, compatibility holders and automatic migration
are not requirements. The earlier [review](reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md)
remains evidence for its original baseline; its legacy preservation and special
body-suppression requirements are superseded, not acceptance blockers.
Gemini delivered complete removal in `be7b071` after interim fixes in `8478ec9`
and reports build success, CTest 7/7 and integration 68/68. Codex has not independently
rerun those results. Verify current-format scene/assembly operations, normal physics
and lifecycle behavior, source/build cleanliness and VFX Step 47 before acceptance.
No AF/EJ/P41/PLG package is closed by this scope clarification.

The first implementation unit after OPT-00 is **AF-00 + EJ-00**, followed by the
remaining OPT-01 packages in their listed order. OPT-02 starts after OPT-01 exits.
The [exchange](agents/CODEX_GEMINI_EXCHANGE.json) records actual assignments and
evidence. The current handoff releases AF-01 only; the remaining plan does not
authorize further implementation.

## One sequence, stable technical identifiers

OPT numbers express execution order. Historical **Phase 41, 42 and 43** are now
domain labels only: rendering/NRI, ECS/jobs and plugins. Their package identifiers
**P41, EJ, PLG and AF remain unchanged**, preserving contracts and review history.
There is no need to switch between three phase queues or interpret W/G labels.

The 80 existing packages appear exactly once below: **73 architecture packages**
in OPT-01..10 and **7 deferred rendering packages** in OPT-11. OPT-00 is a review
checkpoint outside that count. Package count is not a time estimate.

| Order | Phase | Result before moving on |
| --- | --- | --- |
| OPT-00 | Verify the Vehicle retirement | Review the delivered removal before treating it as an accepted baseline. |
| OPT-01 | Establish a reproducible baseline | Capture the current system once and make regressions visible before restructuring it. |
| OPT-02 | Repair threading, scripts and physics jobs | Close the known P1 thread, DLL and task lifetime hazards. |
| OPT-03 | Separate Core, scene data and assets; unify simulation | Finish the CPU ownership boundaries consumed by both rendering and plugins. |
| OPT-04 | Normalize the NRI device and GPU lifetime foundation | Build safe GPU services on the accepted application/data ownership boundaries. |
| OPT-05 | Unify rendering across Editor, previews and Runtime | Give every product and preview one rendering service and resource lifetime model. |
| OPT-06 | Deliver the plugin framework and Plugins window | Prove a general Engine/project plugin workflow before implementing Vehicle. |
| OPT-07 | Write Vehicle as a complete plugin | Reintroduce Vehicle through accepted generic extension points. |
| OPT-08 | Convert remaining optional systems and project scripts | Apply the same product boundary to existing Spline/Terrain and scripts. |
| OPT-09 | Build and export the exact selected Runtime | Make project selection determine the actual shipped product. |
| OPT-10 | Accept and enforce the architecture | Close the architecture effort using current product evidence. |
| OPT-11 | Resume advanced rendering and feature work later | Keep advanced features and full rendering rollout out of the architecture prerequisite path. |

## Progress and acceptance rules

- All checkboxes start open. A document, deletion commit or implementer's PASS
  does not close a package; record implementation and verification separately.
- For each package, attach commit(s), exact rebuilt targets/configuration,
  commands/results/log paths, independent review where required, and limitations.
- Run relevant checks with each package. OPT-10 consolidates evidence; it is not
  permission to postpone regression tests until the end.
- Packages inside a phase run in the listed order. Earlier phases and the
  [master's dependency graph](design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md#6-cross-plan-prerequisite-edges)
  remain prerequisites. A reopened prerequisite reopens affected acceptance.
- If a feature is needed for an existing authored project to remain usable,
  provide an explicit capability/compatibility result; never silently discard it
  because its expansion is deferred.
- Keep EnTT, enkiTS, Jolt and pinned NRI. Editor uses shared-host DLL plugins;
  shipped Runtime statically includes only resolved project selections.

## Phase checklist

### OPT-00 - Verify the Vehicle retirement

**Purpose:** Review the delivered removal before treating it as an accepted baseline.

**Work:** Rebuild Editor/Runtime and the relevant tests; verify current-format scene and assembly load/save/copy, ordinary physics startup and Play/Stop without Vehicle. Check removal of old Vehicle targets, components and compatibility storage. Old-format support and preservation tests are not required. Review the later sample cleanup separately from Engine retirement.

- [ ] **RETIREMENT-REVIEW:** Independent review and regression evidence for the delivered removal.

**Exit:** A review report distinguishes source checks, newly run tests, Gemini-reported evidence and NOT RUN checks. Resolve findings before closing this phase.

**Evidence:** [Independent review and findings](reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md),
[machine-readable results](reviews/OPT_00_VEHICLE_RETIREMENT_RESULTS.json), and
[reproduction fixture](../Tests/Review/VehicleRetirement/README.md).
Historical VR-R1..R3 concern the superseded legacy contract. Status: **FOLLOW-UP VERIFICATION PENDING**; checkbox remains open for current Engine correctness and fresh independent evidence, not legacy support.

### OPT-01 - Establish a reproducible baseline

**Purpose:** Capture the current system once and make regressions visible before restructuring it.

**Work:** Maintain permanent jobs/ECS/render failure fixtures, inventory target/include boundaries and legacy consumers, and establish stable asset identity and safe persistence. Reuse common harness infrastructure.

- [x] **AF-00:** Shared baseline and architecture checks (Gemini `95a6c9d`, Codex correction `af5721b` and completed inventory; [verified correction evidence](reviews/OPT_01_BASELINE_CORRECTIONS.md)).
- [x] **EJ-00:** Permanent baseline and meaningful harness (Gemini `95a6c9d`, Codex correction `af5721b`; [verified correction evidence](reviews/OPT_01_BASELINE_CORRECTIONS.md); known Engine defects remain open).
- [ ] **AF-01:** Stable asset identity and safe persistence ([assigned to Gemini](agents/handoffs/OPT_01_AF01_GEMINI.md); implementation/review pending).
- [ ] **P41-00:** Baseline and always-active render tests.
- [ ] **PLG-00:** Baseline and retirement inventory.

**Exit:** Debug/Release checks remain active; intentional failure probes fail; existing fault reproductions are recorded as faults, not hidden as passing tests. Asset identity survives a clean checkout and relocation.

**Evidence:** [Gemini delivery](reviews/OPT_01_BASELINE_DELIVERY.md),
[independent Codex review](reviews/OPT_01_BASELINE_REVIEW.md),
[structured independent results](reviews/OPT_01_BASELINE_REVIEW_RESULTS.json) and
[review reproductions](../Tests/Review/ArchitectureBaseline/README.md).
O1-R1..R4 were corrected and reverified in the [follow-up](reviews/OPT_01_BASELINE_CORRECTIONS.md),
with a [complete shared inventory](architecture/OPT_01_BASELINE_INVENTORY.md).
AF-00/EJ-00 are closed for baseline progression; no external post-fix independent
review is claimed. The user-directed AF-01 handoff releases the next bounded unit,
not the remainder of OPT-01 or OPT-02.

### OPT-02 - Repair threading, scripts and physics jobs

**Purpose:** Close the known P1 thread, DLL and task lifetime hazards.

**Work:** Fix main/worker/external thread identity, deferred owner-scoped completion, compiler delivery, reload/detach, real enki task completion, one-thread Jolt progress and shutdown ordering.

- [ ] **EJ-01:** Thread identity and lifecycle admission.
- [ ] **EJ-02:** Deferred main-thread executor and owner tokens.
- [ ] **EJ-03:** Compiler request/result handoff.
- [ ] **EJ-04:** Reload commit and scene/DLL teardown.
- [ ] **EJ-05:** Stable physics submission lifetime.
- [ ] **EJ-06:** Physics waits and forward progress.
- [ ] **EJ-07:** Handles, draining and shutdown.

**Exit:** Current ECSJ-01..03 regressions pass against production APIs, including cancellation/detach and constrained-thread bridge disposal. No Scene/UI mutation occurs on a worker.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-03 - Separate Core, scene data and assets; unify simulation

**Purpose:** Finish the CPU ownership boundaries consumed by both rendering and plugins.

**Work:** Extract Application/bootstrap from Core; correct EnTT iteration, structural mutation and transform ownership; separate SceneData and CPU asset registry/format from concrete services; move Editor Play and Runtime onto one simulation driver.

- [ ] **AF-02:** Foundation, Application and explicit service ownership.
- [ ] **EJ-08:** Parallel EnTT view correctness.
- [ ] **EJ-09:** Scene command and mutation boundaries.
- [ ] **EJ-10:** Transform evaluation ownership.
- [ ] **AF-03:** Scene data and system bridge boundaries.
- [ ] **AF-04:** Asset registry/format versus resources and tools.
- [ ] **EJ-11:** Extract shared runtime phases and script API.
- [ ] **EJ-12:** Fixed clock, inputs and pause/step.
- [ ] **EJ-13:** Editor cutover.
- [ ] **EJ-14:** Runtime cutover and product parity.

**Exit:** Core/scene/asset data fixtures run without GPU or Editor startup. ECSJ-04/05 have current coverage; both products follow the same fixed-step, input, pause/step/speed and teardown rules.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-04 - Normalize the NRI device and GPU lifetime foundation

**Purpose:** Build safe GPU services on the accepted application/data ownership boundaries.

**Work:** Implement real backend readiness and GL fallback, transactional resource creation, error/device-loss handling, stable primitive/resource identity, recording/submission leases, state tracking, frame completion, presentation, view targets and shared uploads/cache.

- [ ] **P41-01:** Backend readiness and fallback equivalence.
- [ ] **P41-02:** Transactional GPU resource construction.
- [ ] **P41-03:** Error propagation and device-loss state.
- [ ] **P41-04:** Primitive creation and canonical mesh identity.
- [ ] **P41-05:** Resource identity and generation ownership.
- [ ] **P41-06:** Unified retirement for recorded and submitted work.
- [ ] **P41-07:** Resource state tracking and declared usage.
- [ ] **P41-08:** Frame contexts, completion and allocator reuse.
- [ ] **P41-09:** One presentation owner and UI render bridge.
- [ ] **P41-10:** View targets and external GUI leases.
- [ ] **P41-11:** Asset decoding, upload and shared GPU cache.

**Exit:** D3D12/Vulkan use the pinned NRI contract; GL is an explicit compatibility adapter. Creation failure, resize, recorded-but-unsubmitted work and shutdown have relevant current evidence; missing native checks remain NOT RUN.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-05 - Unify rendering across Editor, previews and Runtime

**Purpose:** Give every product and preview one rendering service and resource lifetime model.

**Work:** Introduce immutable extraction/RenderWorld and RenderService; migrate material/VFX/assembly/thumbnail consumers, Runtime rendering, actual Canvas GPU submission, capability reporting and generic physics debug.

- [ ] **P41-12:** RenderWorld and shared scene extraction.
- [ ] **P41-13:** RenderService and minimal modern render graph.
- [ ] **P41-14:** OpenGL compatibility behind the same service.
- [ ] **P41-15:** Asset and material previews.
- [ ] **P41-16:** VFX preview.
- [ ] **P41-17:** Assembly preview.
- [ ] **P41-18:** Thumbnail render jobs.
- [ ] **P41-19:** Standalone Runtime render integration.
- [ ] **P41-20:** Material/shader/color/capability contract.
- [ ] **P41-25:** Physics debug and overlays.

**Exit:** Real consumers use the common service. Runtime UI is physically submitted; counters alone do not qualify. Preserve declared existing capabilities or expose an explicit supported compatibility path. Advanced features stay open in OPT-11.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-06 - Deliver the plugin framework and Plugins window

**Purpose:** Prove a general Engine/project plugin workflow before implementing Vehicle.

**Work:** Implement manifest discovery/resolution, project selections and build receipts; shared hosts and DLL lifecycle; generic component/codecs/scheduling; Plugins settings; cancellable build/apply/restart/recovery; a real external project plugin template.

- [ ] **PLG-01:** Metadata discovery.
- [ ] **PLG-02:** Dependency and module resolution.
- [ ] **PLG-03:** Project selection and migration policy.
- [ ] **PLG-04:** Product plan and artifact identities.
- [ ] **PLG-05:** Shared hosts, vendor identity and SDK exports.
- [ ] **PLG-06:** Native loader and scoped registration.
- [ ] **PLG-07:** Per-scene component and system extensions.
- [ ] **PLG-08:** Persistence and unresolved component data.
- [ ] **PLG-09:** Simulation phases, jobs and render contributions.
- [ ] **PLG-10:** Plugins window.
- [ ] **PLG-11:** Project plugin builds.
- [ ] **PLG-12:** Apply, restart and recovery.
- [ ] **PLG-13:** Project plugin authoring proof.

**Exit:** A project plugin with owned component state works through the common SDK. Disabled modules are discoverable from metadata but are not configured, compiled or loaded. Desired/Active state and dependency reasons are visible. Editor uses DLLs; live plugin unloading is deferred.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-07 - Write Vehicle as a complete plugin

**Purpose:** Reintroduce Vehicle through accepted generic extension points.

**Work:** Create new data/component IDs, Jolt simulation, reset/copy/stop lifecycle, Editor authoring/debug and scoped audio/VFX adapters. Verify early retirement rather than deleting or restoring the old module again.

- [ ] **PLG-14:** New Vehicle data and codecs.
- [ ] **PLG-15:** New Jolt Vehicle system.
- [ ] **PLG-16:** Vehicle authoring and Editor Play.
- [ ] **PLG-17:** Verify early Vehicle retirement and close remaining plugin cutover gaps.
- [ ] **PLG-18:** Vehicle audio/VFX integrations.

**Exit:** The new plugin works in the real Editor fixture and relevant headless physics tests. Engine consumers contain no Vehicle-specific ownership or fallback. Use the new component schema; retired Vehicle data formats require no support, preservation or conversion.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-08 - Convert remaining optional systems and project scripts

**Purpose:** Apply the same product boundary to existing Spline/Terrain and scripts.

**Work:** Separate Runtime/Editor modules for Spline and Terrain; declare project script dependencies against shared hosts and the SDK. These are conversions of existing capabilities, not new terrain or scripting feature expansion.

- [ ] **PLG-19:** Spline plugin conversion.
- [ ] **PLG-20:** Terrain plugin conversion.
- [ ] **PLG-21:** Project script boundary.

**Exit:** Optional systems use the common plugin lifecycle and selection graph. Scripts do not pull Editor UI or unselected domain libraries into Runtime.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-09 - Build and export the exact selected Runtime

**Purpose:** Make project selection determine the actual shipped product.

**Work:** Statically compose only selected Runtime modules; cook/export from the same validated selection snapshot; reconcile final module and product boundaries.

- [ ] **PLG-22:** Selected static Runtime.
- [ ] **PLG-23:** Snapshot-bound cook/export.
- [ ] **P41-26:** Build and ownership boundaries.

**Exit:** A packaged player launches with the expected content. Runtime builds with Editor disabled; excluded plugins are absent from configuration/linking/shipped code, not merely idle. Stale build receipts and missing required data prevent misleading export.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-10 - Accept and enforce the architecture

**Purpose:** Close the architecture effort using current product evidence.

**Work:** Run stress/fault/parity/native DLL/UI/GPU/player checks, reconcile plugin/render seams, retire temporary adapters and baseline boundary allowances, and update maintained guides to the accepted implementation.

- [ ] **EJ-15:** Stress, failure and regression acceptance.
- [ ] **PLG-24:** Cross-product acceptance.
- [ ] **EJ-16:** Plugin and rendering seam agreement.
- [ ] **EJ-17:** Documentation and final delivery.
- [ ] **PLG-25:** Final documentation and handoff.
- [ ] **AF-05:** Architecture acceptance and enforcement.

**Exit:** All 73 architecture packages and their applicable acceptance rows have current evidence. Required unavailable checks remain open. Boundary checks reject deliberate violations. No performance improvement is claimed without measurement.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-11 - Resume advanced rendering and feature work later

**Purpose:** Keep advanced features and full rendering rollout out of the architecture prerequisite path.

**Work:** After OPT-10, implement shadows, HDR/IBL, the animation render bridge and consistent picking; then measure performance, complete full rendering acceptance and perform default rollout. Follow with the separately ordered feature backlog below.

- [ ] **P41-21:** Modern shadow passes.
- [ ] **P41-22:** Authored HDR skybox and IBL.
- [ ] **P41-23:** Animation bridge, once.
- [ ] **P41-24:** Geometry/pose/material-consistent picking.
- [ ] **P41-27:** Performance after correctness.
- [ ] **P41-28:** Acceptance matrix and reproducibility.
- [ ] **P41-29:** Cutover and cleanup.

**Exit:** Full historical Phase 41 closes only after its own seven remaining packages and reviewed animation prerequisites pass. OPT-10 alone does not establish full feature parity or a new backend default.

**Evidence:** Pending; implementation/review is not claimed here.

## Read documents only when needed

| When | Read | Responsibility |
| --- | --- | --- |
| OPT-00 | [Vehicle delivery](archive/design/reports/VEHICLE_RETIREMENT_DELIVERY.md), [exchange](agents/CODEX_GEMINI_EXCHANGE.json) | Reported delivery and actual review state |
| OPT-01 | [Master contract](design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md), [ECS/jobs review](architecture/ENTT_ENKITS_REVIEW.md) | Shared AF contracts, baseline faults and dependency graph |
| OPT-02..03 | [ECS/jobs plan](design/ENTT_ENKITS_REFACTOR_PLAN.md), [master AF contracts](design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md#7-additional-foundation-packages) | Thread/lifetime/ECS/simulation internals and data boundaries |
| OPT-04..05 | [NRI contract](design/NRI_INTEGRATION_CONTRACT.md), [render architecture](design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md), [package plan](design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md), [coding reference](design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md) | Device/resource/frame/services and real product consumers |
| OPT-06..09 | [Plugin architecture](design/PLUGIN_SYSTEM_ARCHITECTURE.md), [package plan](design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md), [coding reference](design/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | SDK, Plugins window, project builds, fresh Vehicle and selected Runtime |
| OPT-10 | Acceptance matrices in the master and domain plans | Current cross-product evidence and boundary enforcement |
| OPT-11 and later | [Design archive](archive/design/README.md#deferred-feature-work) | Deferred features and remaining acceptance obligations |

The [documentation hub](README.md) separates working guides, source snapshots,
future contracts and archived delivery evidence. The [historical product roadmap](../ROADMAP.md)
retains old milestones; it is not another optimization checklist.

## Deferred feature work

After OPT-10, use this order for later work. Fixes required to preserve an existing
workflow may be scoped earlier with explicit evidence; that does not release a
whole feature phase. Old outstanding acceptance is retained, never relabeled PASS.

| Later order | Work | Preserved status / reference |
| --- | --- | --- |
| 1 | OPT-11: remaining rendering features, measurement and full rollout | Seven P41 packages above; review only the animation prerequisites required by P41-23/24 |
| 2 | Finish outstanding material/VFX acceptance and review delivered animation work | [Material acceptance](archive/design/reports/MATERIAL_PHASE_38_3_ACCEPTANCE_REPORT.md), [VFX acceptance](archive/design/reports/VFX_PHASE_39_REVIEW_AND_ACCEPTANCE_REPORT.md), [animation G1 delivery](archive/design/reports/ANIMATION_G1_GEMINI_DELIVERY.md); reported results retain their own limits |
| 3 | Complete animation authoring beyond the rendering prerequisites | [Phase 40 plan](archive/design/deferred/ANIMATION_PHASE_40_COMPLETION_PLAN.md); G1 independent review remains separate from new D1/D2/authoring work |
| 4 | Expand VFX and editor authoring | [VFX backlog](archive/design/deferred/VFX_UNITY_FEATURE_BACKLOG.md), [tooling roadmap](archive/design/deferred/EDITOR_TOOLING_ROADMAP.md) |
| 5 | Additional material/refraction polish or other new features | [Material refraction](archive/design/deferred/MATERIAL_REFRACTION.md); separately scope networking, AI, advanced Vehicle handling and live plugin unloading |

## Maintaining this roadmap

Update progress **here**, not in a second copied package checklist. Keep detailed
API/algorithm/acceptance contracts in their domain documents; keep dependency
edges and AF definitions in the master. If order changes, validate all 80 packages
and the union of internal/cross-plan dependencies before updating this file.
Keep relocated reports indexed in the archive with dates and status;
do not make old embedded prompts into current assignments.
