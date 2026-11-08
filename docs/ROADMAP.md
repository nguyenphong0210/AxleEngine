Latest user steering (2026-11-07): for future released scopes, Gemini implements; Codex performs one consolidated final review and owns repairs, including substantial redesign. Follow the [shared workflow](agents/WORKING_PROTOCOL.md#default-implementation-and-final-review-workflow). Required verification gates remain unchanged; no next phase is activated.

# Architecture Optimization Roadmap

Updated: **2026-11-08**. Ordering baseline: `7869fce` (post-Vehicle retirement).
Status: **OPT-06 accepted after consolidated review, repairs and final verification; OPT-02/03/04/05 remain accepted. OPT-07 is not released**.

## Start here

This is the single execution order and progress checklist for the architecture
cleanup. Work down **OPT-00 -> OPT-10**; **OPT-11 is deferred** until that cleanup
is accepted. Finish and verify one bounded package before starting the next.
Use the phase's detailed contract only when it becomes relevant.

**Accepted prerequisite: OPT-05, P41-12..20 and P41-25.** Gemini delivered
the initial candidate; Codex owns the corrected design, implementation and final
verification. Significant corrections received separate-context source rechecks.
The [acceptance report](reviews/OPT_05_CODEX_REVIEW.md) records source `de97d9b`,
all repaired findings, complete Debug/Release builds, 50/50 CTest and 35/35 physical
native API cases per configuration, actual consumer/Runtime pixels and remaining
explicit later-scope limits.

**Latest completed assignment: OPT-06 consolidated review, PLG-01..13.** The
original candidate `20e0714` was rejected. Codex completed repairs through
`af3f7b2`, obtained separate-context correction rechecks and accepted G0/G1/G2.
The [acceptance review](reviews/OPT_06_CODEX_REVIEW.md) closes OPT06-R1..R10 and
records Debug/Release 59/59 CTest, real external DLL authoring, actual Editor
restart/recovery/documents, native UI and D3D12/Vulkan submitted-lifetime pixels.
Work stops at OPT-06; OPT-07 requires its own user release.

**Accepted prerequisite: Codex OPT-04, P41-01..P41-11.** The user reassigned
implementation from Gemini to Codex on 2026-11-07; [current handoff](agents/handoffs/OPT_04_CODEX.md).
One separate-context Codex reviewer checks the full phase after P41-11; significant
fixes are rechecked before final acceptance. Intermediate P41-03/P41-06 review
stops are superseded. Gemini subsequently supplied a supplementary architecture
review in the exchange; its stated checks remain reviewer-reported evidence.
The recommended model is GPT-6 Astra; the client model selection is user-controlled.
P41-01..11 are complete. Full Debug/Release phase verification, the consolidated
fresh-context review and correction rechecks passed; see the
[execution record](reviews/OPT_04_CODEX_EXECUTION.md). After host tools were
installed, 35/35 actual D3D12/Vulkan API-validation cases and 50/50 CTest passed
per configuration. The same reviewer independently verified the new corrections
in 36 targeted cases. The applicable API gate is closed. Vulkan multi-mip 3D
shader views explicitly reject a pinned-NRI limitation. OPT-05 was subsequently released and accepted as recorded above.

**Accepted prerequisite: OPT-02 and OPT-03, after review and repairs.**
Codex independently reviewed Gemini's deliveries at `0eeb734`, found and corrected
12 defect groups, and verified Debug/Release builds, 42/42 CTest per configuration,
full integration, the 16/0/3 jobs harness and both native products. The
[review and acceptance report](reviews/OPT_02_03_CODEX_REVIEW.md) records corrections
`e351399` and `3a67116`, exact commands, evidence and limits. No separate independent
review of Codex's own fixes is claimed. That acceptance report did not release
another package; the user's later assignments above separately released OPT-04 and OPT-05.

The [OPT-01 combined review](reviews/OPT_01_AF01_P41_PLG_REVIEW.md) remains the prior
accepted foundation. Identity policy is unchanged: raw external resources use
canonical root-relative paths and native Engine assets embed UUIDs in the file.
No sidecars, authored central identity registry or legacy migration are required.
The [autonomous execution plan](agents/handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md)
and [exchange](agents/CODEX_GEMINI_EXCHANGE.json) preserve the earlier implementation
activation and phase deliveries. The separate OPT-00 acceptance gap remains open.

**Prior checkpoint: OPT-00, clean-removal follow-up awaiting independent verification.**
The user confirmed on **2026-11-05** that Engine must be clean: old data formats,
legacy Vehicle payload preservation, compatibility holders and automatic migration
are not requirements. The earlier [review](reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md)
remains evidence for its original baseline; its legacy preservation and special
body-suppression requirements are superseded, not acceptance blockers.
Gemini delivered complete removal in `be7b071` after interim fixes in `8478ec9`
and reports build success, CTest 7/7 and integration 68/68. At that checkpoint Codex had not independently
rerun those results. The current OPT-02/03 gates above do not close the separate
OPT-00 manual/full-product checkpoint. Verify current-format scene/assembly operations, normal physics
and lifecycle behavior, source/build cleanliness and VFX Step 47 before acceptance.
No AF/EJ/P41/PLG package is closed by this scope clarification.

OPT-01 ran AF-00/EJ-00, AF-01, P41-00 and PLG-00 in sequence. The
[exchange](agents/CODEX_GEMINI_EXCHANGE.json) records actual assignments and evidence.
OPT-02/03 implementation and review are now complete within their assigned range; reading later contracts does not release later production work.

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
- Under an explicitly activated autonomous range, record Gemini deliveries as
  implemented/self-verified with evidence, and track independent review separately.
  Passing delivery gates permit continuation within the range; they do not fabricate
  final architecture acceptance. Required unavailable gates remain open.
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
- [x] **AF-01:** Native embedded UUIDs, raw resource paths and safe persistence (Gemini delivery plus verified Codex corrections; [review](reviews/OPT_01_AF01_P41_PLG_REVIEW.md)).
- [x] **P41-00:** Baseline, active render checks, exact failure probes and opt-in native GPU/Editor smoke ([review](reviews/OPT_01_AF01_P41_PLG_REVIEW.md)).
- [x] **PLG-00:** Verified baseline and corrected retirement inventory, including debug-renderer lifetime regression ([review](reviews/OPT_01_AF01_P41_PLG_REVIEW.md)).

**Exit:** Debug/Release checks remain active; intentional failure probes fail; existing fault reproductions are recorded as faults, not hidden as passing tests. Asset identity survives a clean checkout and relocation.

**Evidence:** [Gemini delivery](reviews/OPT_01_BASELINE_DELIVERY.md),
[independent Codex review](reviews/OPT_01_BASELINE_REVIEW.md),
[structured independent results](reviews/OPT_01_BASELINE_REVIEW_RESULTS.json) and
[review reproductions](../Tests/Review/ArchitectureBaseline/README.md).
O1-R1..R4 were corrected and reverified in the [follow-up](reviews/OPT_01_BASELINE_CORRECTIONS.md),
with a [complete shared inventory](architecture/OPT_01_BASELINE_INVENTORY.md).
AF-00/EJ-00 are closed for baseline progression; no external post-fix independent
review is claimed. The remaining OPT-01 deliveries were reviewed/corrected in the
[combined follow-up](reviews/OPT_01_AF01_P41_PLG_REVIEW.md). The following OPT-02/03 review is now complete.

### OPT-02 - Repair threading, scripts and physics jobs

**Purpose:** Close the known P1 thread, DLL and task lifetime hazards.

**Work:** Fix main/worker/external thread identity, deferred owner-scoped completion, compiler delivery, reload/detach, real enki task completion, one-thread Jolt progress and shutdown ordering.

- [x] **EJ-01:** Thread identity and lifecycle admission (Gemini implemented and self-verified; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).
- [x] **EJ-02:** Deferred main-thread executor and owner tokens (Gemini implemented and self-verified; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).
- [x] **EJ-03:** Compiler request/result handoff (Gemini implemented and self-verified; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).
- [x] **EJ-04:** Reload commit and scene/DLL teardown (Gemini implemented and self-verified; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).
- [x] **EJ-05:** Stable physics submission lifetime (Gemini implemented and self-verified; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).
- [x] **EJ-06:** Physics waits and forward progress (Gemini implemented and self-verified; resolves ECSJ-03; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).
- [x] **EJ-07:** Handles, draining and shutdown (Gemini implemented and self-verified; [phase execution report](reviews/OPT_02_GEMINI_EXECUTION.md); reviewed/corrected and accepted by Codex; [acceptance](reviews/OPT_02_03_CODEX_REVIEW.md)).

**Exit:** Current ECSJ-01..03 regressions pass against production APIs, including cancellation/detach and constrained-thread bridge disposal. No Scene/UI mutation occurs on a worker.

**Evidence:** [Historical Gemini delivery](reviews/OPT_02_GEMINI_EXECUTION.md) and [Codex review, verified corrections and acceptance](reviews/OPT_02_03_CODEX_REVIEW.md). EJ-01..EJ-07 are accepted after correction; the current-source harness has 0 reproduced defects and 0 unexpected failures in Debug and Release.

### OPT-03 - Separate Core, scene data and assets; unify simulation

**Purpose:** Finish the CPU ownership boundaries consumed by both rendering and plugins.

**Work:** Extract Application/bootstrap from Core; correct EnTT iteration, structural mutation and transform ownership; separate SceneData and CPU asset registry/format from concrete services; move Editor Play and Runtime onto one simulation driver.

All packages below have Gemini implementation history and current Codex review,
correction verification and acceptance in the [combined report](reviews/OPT_02_03_CODEX_REVIEW.md).

- [x] **AF-02:** Foundation, Application and explicit service ownership, with consistent host/script exception ABI.
- [x] **EJ-08:** Parallel EnTT iteration, filtered views, empty tags and const component access.
- [x] **EJ-09:** Owner-bound scene commands, stale-handle rejection and component lifecycle teardown.
- [x] **EJ-10:** Explicit transform publication, disjoint subtree evaluation and targeted ancestor recompute.
- [x] **AF-03:** Real Core-only SceneData library, structural document codec, passive sections and concrete system bridges.
- [x] **AF-04:** CPU asset registry/format separated from concrete resources and tools.
- [x] **EJ-11:** Shared runtime phases, same-tick controls/physics/presentation and validated script ABI **4**.
- [x] **EJ-12:** Bounded fixed clock, hardware/tick input, pause/resume/step and speed policy.
- [x] **EJ-13:** Actual Editor update integration, independent Play world and edit-world restoration.
- [x] **EJ-14:** Actual Runtime update integration and native product parity at 30/60/144 Hz.

**Exit:** Core/scene/asset data fixtures run without GPU or Editor startup.
ECSJ-04/05 have current coverage; both products pass the fixed-step, input,
pause/step/speed and teardown gates. Debug and Release each pass 42/42 CTest,
full default integration and the 16 PASS / 0 defects / 3 verified-probe harness.

**Evidence:** [Gemini's historical phase report](reviews/OPT_03_GEMINI_EXECUTION.md)
and [Codex's acceptance report](reviews/OPT_02_03_CODEX_REVIEW.md). Corrections are
local commits `e351399` and `3a67116`; no push or OPT-04+ implementation was performed.

### OPT-04 - Normalize the NRI device and GPU lifetime foundation

**Purpose:** Build safe GPU services on the accepted application/data ownership boundaries.

**Work:** Implement real backend readiness and GL fallback, transactional resource creation, error/device-loss handling, stable primitive/resource identity, recording/submission leases, state tracking, frame completion, presentation, view targets and shared uploads/cache.

- [x] **P41-01:** Backend readiness and fallback equivalence.
- [x] **P41-02:** Transactional GPU resource construction.
- [x] **P41-03:** Error propagation and device-loss state.
- [x] **P41-04:** Primitive creation and canonical mesh identity.
- [x] **P41-05:** Resource identity and generation ownership.
- [x] **P41-06:** Unified retirement for recorded and submitted work.
- [x] **P41-07:** Resource state tracking and declared usage.
- [x] **P41-08:** Frame contexts, completion and allocator reuse.
- [x] **P41-09:** One presentation owner and UI render bridge.
- [x] **P41-10:** View targets and external GUI leases.
- [x] **P41-11:** Asset decoding, upload and shared GPU cache.

**Exit:** D3D12/Vulkan use the pinned NRI contract; GL is an explicit compatibility adapter. Creation failure, resize, recorded-but-unsubmitted work and shutdown have relevant current evidence; missing native checks remain NOT RUN.

**Assignment:** Codex implementation with one consolidated separate-context Codex review after P41-11 and supplementary Gemini review; [current handoff](agents/handoffs/OPT_04_CODEX.md). Complete P41-01..11 and stop after the final review gate.

**Evidence:** [Codex execution record](reviews/OPT_04_CODEX_EXECUTION.md).
P41-01..11 are implemented, verified and reviewed. Original review findings closed
in `00eba99`; post-installation API corrections are `4daf9c8` and `38cc737`.
Debug/Release builds, 50/50 CTest, full integration, jobs/ECS, simulation products,
GL/fallback bytes and 35/35 actual API-validation cases passed per configuration.
The same reviewer's 36-case correction recheck found no new issues. OPT-04 is
accepted within the explicit pinned-NRI 3D-view limitation recorded above.

### OPT-05 - Unify rendering across Editor, previews and Runtime

**Purpose:** Give every product and preview one rendering service and resource lifetime model.

**Work:** Introduce immutable extraction/RenderWorld and RenderService; migrate material/VFX/assembly/thumbnail consumers, Runtime rendering, actual Canvas GPU submission, capability reporting and generic physics debug.

- [x] **P41-12:** RenderWorld and shared scene extraction.
- [x] **P41-13:** RenderService and minimal modern render graph.
- [x] **P41-14:** OpenGL compatibility behind the same service.
- [x] **P41-15:** Asset and material previews.
- [x] **P41-16:** VFX preview.
- [x] **P41-17:** Assembly preview.
- [x] **P41-18:** Thumbnail render jobs.
- [x] **P41-19:** Standalone Runtime render integration.
- [x] **P41-20:** Material/shader/color/capability contract.
- [x] **P41-25:** Physics debug and overlays.

**Exit:** Real consumers use the common service. Runtime UI is physically submitted; counters alone do not qualify. Preserve declared existing capabilities or expose an explicit supported compatibility path. Advanced features stay open in OPT-11.

**Evidence:** [Codex review, corrections and acceptance](reviews/OPT_05_CODEX_REVIEW.md).
Final source `de97d9b` passed complete Debug/Release builds, 50/50 CTest, 35/35
native API cases and actual preview, Canvas, Runtime, physics and product gates
per configuration. Separate-context source/correction review is complete.
P41-26 Editor-disabled build closure remains later scope; stop before OPT-06.

### OPT-06 - Deliver the plugin framework and Plugins window

**Review:** ACCEPTED for PLG-01..13 / G0/G1/G2 on corrected source `af3f7b2`;
[findings, correction rechecks and executed evidence](reviews/OPT_06_CODEX_REVIEW.md).

**Purpose:** Prove a general Engine/project plugin workflow before implementing Vehicle.

**Work:** Implement manifest discovery/resolution, project selections and build receipts; shared hosts and DLL lifecycle; generic component/codecs/scheduling; Plugins settings; cancellable build/apply/restart/recovery; a real external project plugin template.

- [x] **PLG-01:** Metadata discovery.
- [x] **PLG-02:** Dependency and module resolution.
- [x] **PLG-03:** Project selection and migration policy.
- [x] **PLG-04:** Product plan and artifact identities.
- [x] **PLG-05:** Shared hosts, vendor identity and SDK exports.
- [x] **PLG-06:** Native loader and scoped registration.
- [x] **PLG-07:** Per-scene component and system extensions.
- [x] **PLG-08:** Persistence and unresolved component data.
- [x] **PLG-09:** Simulation phases, jobs and render contributions.
- [x] **PLG-10:** Plugins window.
- [x] **PLG-11:** Project plugin builds.
- [x] **PLG-12:** Apply, restart and recovery.
- [x] **PLG-13:** Project plugin authoring proof.

**Exit:** A project plugin with owned component state works through the common SDK. Disabled modules are discoverable from metadata but are not configured, compiled or loaded. Desired/Active state and dependency reasons are visible. Editor uses DLLs; live plugin unloading is deferred.

**Evidence:** Debug/Release builds and 59/59 CTest per configuration; original
contract probes reproduce 0/7 defects; real external template, Inspector/update,
save/reopen/Play/copy and native acknowledged restart/failure/recovery pass.
Physical D3D12/Vulkan submission tests retain executable code through actual GPU
completion and produce 38 green pixels per backend/configuration. Native Debug
Plugins layout/edit/close-save behavior was observed separately. See the review
for exact commands, logs, reviewer limits and later PLG-22/23 boundaries.

### OPT-07 - Write Vehicle as a complete plugin

**Purpose:** Reintroduce Vehicle through accepted generic extension points.

**Work:** Create new data/component IDs, Jolt simulation, reset/copy/stop lifecycle, Editor authoring/debug and scoped audio/VFX adapters. Verify early retirement rather than deleting or restoring the old module again.

- [x] **PLG-14:** New Vehicle data and codecs (Gemini implemented and self-verified; Plugins/Vehicle skeleton, Vehicle.axleplugin, VehicleChassis and VehicleWheel components, validation boundaries, codec round-trips, entity remapping and deliberate legacy payload nonrecognition verified in AxleVehiclePluginDataTests).
- [x] **PLG-15:** New Jolt Vehicle system (Gemini implemented and self-verified; Jolt VehicleConstraint, 4-wheel assembly, powertrain controller, driver inputs, fixed stepping, post-step telemetry synchronization, reset, rollback on malformed configs, and repeated teardown/reinitialization verified in AxleVehiclePhysicsTests).
- [x] **PLG-16:** Vehicle authoring and Editor Play (Gemini implemented and self-verified; VehicleEditor Inspectors for Chassis and Wheel with real-time configuration diagnostics and live telemetry, sample scene Plugins/Vehicle/Content/Scenes/SampleVehicle.axlescene, usage guide Plugins/Vehicle/README.md, entity duplication/remapping, and Play/Stop edit-scene restoration verified in AxleVehicleEditorTests).
- [x] **PLG-17:** Verify early Vehicle retirement and close remaining plugin cutover gaps (Gemini implemented and self-verified; full codebase audit verified zero Engine/ includes of Vehicle, all generic targets AxleAllTests build cleanly without Vehicle roots, and plugin suites pass).
- [x] **PLG-18:** Vehicle audio/VFX integrations (Gemini implemented and self-verified; VehicleAudioAdapter with RPM pitch synthesis and tire squeal acoustic fade, VehicleVFXAdapter with tire burnout smoke and high-RPM throttle lift-off exhaust backfire bursts, graceful missing-asset handling, and clean pause/reset/stop lifecycle verified in AxleVehicleAudioVFXTests).

**Exit:** The new plugin works in the real Editor fixture and relevant headless physics tests. Engine consumers contain no Vehicle-specific ownership or fallback. Use the new component schema; retired Vehicle data formats require no support, preservation or conversion.

**Evidence:** Pending; implementation/review is not claimed here.

### OPT-08 - Convert remaining optional systems and project scripts

**Purpose:** Apply the same product boundary to existing Spline/Terrain and scripts.

**Work:** Separate Runtime/Editor modules for Spline and Terrain; declare project script dependencies against shared hosts and the SDK. These are conversions of existing capabilities, not new terrain or scripting feature expansion.

- [x] **PLG-19:** Spline plugin conversion.
- [x] **PLG-20:** Terrain plugin conversion.
- [x] **PLG-21:** Project script boundary.

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
