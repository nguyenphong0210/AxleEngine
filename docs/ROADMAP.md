Collaboration policy (2026-11-07): for future released scopes, Gemini implements; Codex performs one consolidated final review and owns repairs, including substantial redesign. Follow the [shared workflow](agents/WORKING_PROTOCOL.md#default-implementation-and-final-review-workflow). Required verification gates remain unchanged; no next phase is activated.

# Architecture Optimization Roadmap

Updated: **2026-11-10**. Ordering baseline: `7869fce` (post-Vehicle retirement).
Status: **OPT-07/08 plugin foundations accepted after repairs and native verification; detailed plugin features are deferred by user scope. OPT-09 selected Runtime/export foundations are implemented and verified; OPT-10 is released and in progress. Earlier accepted prerequisites remain accepted**.

## Start here

This is the single execution order and progress checklist for the architecture
cleanup. The original order is **OPT-00 -> OPT-10 -> OPT-11**. The user's later
explicit release activates bounded OPT-11 work without closing outstanding
OPT-10 acceptance gates. Finish and verify one bounded package before starting the next.
Use the phase's detailed contract only when it becomes relevant.

**Accepted prerequisite: OPT-05, P41-12..20 and P41-25.** Gemini delivered
the initial candidate; Codex owns the corrected design, implementation and final
verification. Significant corrections received separate-context source rechecks.
The [acceptance report](reviews/OPT_05_CODEX_REVIEW.md) records source `de97d9b`,
all repaired findings, complete Debug/Release builds, 50/50 CTest and 35/35 physical
native API cases per configuration, actual consumer/Runtime pixels and remaining
explicit later-scope limits.

**Accepted prerequisite: OPT-06 consolidated review, PLG-01..13.** The
original candidate `20e0714` was rejected. Codex completed repairs through
`af3f7b2`, obtained separate-context correction rechecks and accepted G0/G1/G2.
The [acceptance review](reviews/OPT_06_CODEX_REVIEW.md) closes OPT06-R1..R10 and
records Debug/Release 59/59 CTest, real external DLL authoring, actual Editor
restart/recovery/documents, native UI and D3D12/Vulkan submitted-lifetime pixels.
OPT-07/08 were subsequently delivered by Gemini and reviewed under the narrowed
foundation scope below.

**Accepted prerequisite: OPT-07/08 plugin foundation review.** On 2026-11-08
the user explicitly requested build/load/lifecycle and architecture verification,
with detailed plugin functionality to be completed later. Corrected source
`bd22550` passes Debug/Release 11/11 focused checks, actual six-DLL Editor startup
on D3D12/Vulkan, persistence/copy/Play/Stop/unload and shipping script isolation.
The [bounded acceptance](reviews/OPT_07_08_FOUNDATION_REVIEW.md) distinguishes the
working SDK foundations from advanced development prototypes. No Engine source
changed. This does not establish full feature completeness. OPT-09 was subsequently delivered; the latest user assignment now releases OPT-10.

**Latest completed assignment: Codex OPT-09, PLG-22/23 and P41-26.** Static selected Runtime and snapshot-bound export pass final Debug/Release 69/69 CTest, five exact product combinations per configuration, D3D12/Vulkan detached player launches and package/error checks (30/30 command outcomes each). Separate-context source rechecks found no remaining concrete P1/P2; execution evidence is Codex coordinator evidence. See the [delivery record](reviews/OPT_09_IMPLEMENTATION.md). Detailed plugin features stay deferred; OPT-10 is released and in progress.

**Accepted prerequisite: Codex OPT-04, P41-01..P41-11.** The user reassigned
implementation from Gemini to Codex on 2026-11-07; see the execution record for the released scope.
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
The [exchange](agents/CODEX_GEMINI_EXCHANGE.json) preserves the earlier implementation
messages and phase evidence. The separate OPT-00 acceptance gap remains open.

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

The accepted architecture work contains **73 packages in OPT-01..10**.
Post-OPT-10 is a separately planned OPT-11..OPT-20 program; its planned units
are listed in [the program plan](design/POST_OPT10_PROGRAM_PLAN.md) and are not
accepted packages yet. OPT-00 is a review checkpoint outside that count. Package
count is not a time estimate.

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
| OPT-11..20 | Post-OPT-10 evolution | Execute the independent Render Thread, Shader, Lighting, Nanite, LogicLOD, autoLOD, Animation, VFX, UI and rollout OPTs in the program plan. |

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

**Assignment:** Codex implementation with one consolidated separate-context Codex review after P41-11 and supplementary Gemini review. Complete P41-01..11 and stop after the final review gate.

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

**Current review:** Foundation accepted on `bd22550` under the user's narrowed
scope; [evidence and explicit feature limits](reviews/OPT_07_08_FOUNDATION_REVIEW.md).
Native data/Inspector/lifecycle work. Jolt driving and audio/VFX integration remain
development prototypes for later completion. The self-verification notes below
record prototype regressions and do not claim complete native feature integration.

**Purpose:** Reintroduce Vehicle through accepted generic extension points.

**Work:** Create new data/component IDs, Jolt simulation, reset/copy/stop lifecycle, Editor authoring/debug and scoped audio/VFX adapters. Verify early retirement rather than deleting or restoring the old module again.

- [x] **PLG-14:** New Vehicle data and codecs (Gemini implemented and self-verified; Plugins/Vehicle skeleton, Vehicle.axleplugin, VehicleChassis and VehicleWheel components, validation boundaries, codec round-trips, entity remapping and deliberate legacy payload nonrecognition verified in AxleVehiclePluginDataTests).
- [x] **PLG-15:** New Jolt Vehicle system (Gemini implemented and self-verified; Jolt VehicleConstraint, 4-wheel assembly, powertrain controller, driver inputs, fixed stepping, post-step telemetry synchronization, reset, rollback on malformed configs, and repeated teardown/reinitialization verified in AxleVehiclePhysicsTests).
- [x] **PLG-16:** Vehicle authoring and Editor Play (Gemini implemented and self-verified; VehicleEditor Inspectors for Chassis and Wheel with real-time configuration diagnostics and live telemetry, sample scene Plugins/Vehicle/Content/Scenes/SampleVehicle.axlescene, usage guide Plugins/Vehicle/README.md, entity duplication/remapping, and Play/Stop edit-scene restoration verified in AxleVehicleEditorTests).
- [x] **PLG-17:** Verify early Vehicle retirement and close remaining plugin cutover gaps (Gemini implemented and self-verified; full codebase audit verified zero Engine/ includes of Vehicle, all generic targets AxleAllTests build cleanly without Vehicle roots, and plugin suites pass).
- [x] **PLG-18:** Vehicle audio/VFX integrations (Gemini implemented and self-verified; VehicleAudioAdapter with RPM pitch synthesis and tire squeal acoustic fade, VehicleVFXAdapter with tire burnout smoke and high-RPM throttle lift-off exhaust backfire bursts, graceful missing-asset handling, and clean pause/reset/stop lifecycle verified in AxleVehicleAudioVFXTests).

**Exit:** The new plugin works in the real Editor fixture and relevant headless physics tests. Engine consumers contain no Vehicle-specific ownership or fallback. Use the new component schema; retired Vehicle data formats require no support, preservation or conversion.

**Evidence:** Native Vehicle Runtime/Editor DLLs build, load, persist/copy data and
run through the shared lifecycle in Debug/Release. See the bounded foundation
review for actual commands, correction recheck and advanced feature deferrals.

### OPT-08 - Convert remaining optional systems and project scripts

**Current review:** Foundation accepted on `bd22550` under the user's narrowed
scope. Native Spline/Terrain DLLs, dependencies, owned data/Inspectors and lifecycle
work; full mesh/collider/sculpting/LOD functionality remains in development
prototypes. Script boundary acceptance covers the matched ABI, UI-free shipping
baseline and domain exclusion; new plugin-aware scripting features are deferred.

**Purpose:** Apply the same product boundary to existing Spline/Terrain and scripts.

**Work:** Separate Runtime/Editor modules for Spline and Terrain; declare project script dependencies against shared hosts and the SDK. These are conversions of existing capabilities, not new terrain or scripting feature expansion.

- [x] **PLG-19:** Spline plugin conversion.
- [x] **PLG-20:** Terrain plugin conversion.
- [x] **PLG-21:** Project script boundary.

**Exit:** Optional systems use the common plugin lifecycle and selection graph. Scripts do not pull Editor UI or unselected domain libraries into Runtime.

**Evidence:** Debug/Release 11/11 focused checks and real six-DLL Editor startup
on both native backends; Runtime uses a distinct shipping script DLL and explicitly
rejects unsupported selected composition. See the [foundation review](reviews/OPT_07_08_FOUNDATION_REVIEW.md).

### OPT-09 - Build and export the exact selected Runtime

**Purpose:** Make project selection determine the actual shipped product.

**Work:** Statically compose only selected Runtime modules; cook/export from the same validated selection snapshot; reconcile final module and product boundaries.

- [x] **PLG-22:** Selected static Runtime.
- [x] **PLG-23:** Snapshot-bound cook/export.
- [x] **P41-26:** Build and ownership boundaries.

**Exit:** A packaged player launches with the expected content. Runtime builds with Editor disabled; excluded plugins are absent from configuration/linking/shipped code, not merely idle. Stale build receipts and missing required data prevent misleading export.

**Evidence:** [Final implementation, correction review and verification](reviews/OPT_09_IMPLEMENTATION.md): 69/69 CTest and 30/30 selected native export/player command outcomes per configuration. Editor-free CPU suite 2/2; Editor-only configure passes. The GL option controls backend admission, with compatibility implementation retained.

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

**Evidence:** [Current OPT-10 implementation and open acceptance gates](reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md). Native UI and automated verification remain separate; required NOT RUN gates prevent final closure.

### OPT-11..OPT-20 - Post-OPT-10 evolution

**Purpose:** Execute the independent Post-OPT-10 OPTs only after the OPT-10
boundary is accepted or explicitly deferred.

**Work:** Follow [POST_OPT10_PROGRAM_PLAN.md](design/POST_OPT10_PROGRAM_PLAN.md).
The plan separates Render Thread, Shader/Material, Lighting, Nanite/GPU
Geometry, Logic LOD, offline autoLOD, Animation, VFX, authoring UI and final
performance rollout. Every unit has its own evidence and acceptance state.

**Status, user release 2026-11-10:** C11-05 transient allocation/aliasing is
implemented through slice .6, including native D3D12/Vulkan evidence; see the
[delivery record](design/OPT_11_C11_05_DELIVERY.md) for commits, review and limits.
C11-06 lifetime stress is implemented and reviewed through `dc9b90c`, with
Debug/Release 26/26 CTest and 12/12 additional native regressions per configuration;
see [C11-06 evidence](design/OPT_11_C11_06_DELIVERY.md).
C11-07 has delivered a [qualified acceptance/rollout decision](reviews/OPT_11_C11_07_ACCEPTANCE.md)
through source `119123c`: Debug/Release 91/91 full CTest, 47/47 actual-product
cases per configuration, native parity/latency and alias/lifetime regression.
The synchronous product default and tested foundation scope are accepted.
Product asynchronous ownership, native parallel recording and scene aliasing
remain DEFERRED/default OFF; full OPT-11 product deployment is not accepted.
The new executor is opt-in; current scene targets remain persistent/imported.
C11-04 full hybrid/NRI fork stays deferred. This checkpoint does not close OPT-11
as a fully deployed product phase or release OPT-12..20.

**C12-01 direct user release, 2026-11-10:** the approved Material Graph format is
implemented as a CPU authoring document, explicit node-definition registry,
validated IR and identity-safe persistence. See the
[C12-01 delivery](reviews/OPT_12_C12_01_DELIVERY.md) for source review, final
Debug/Release evidence and the corrected current-format fixture. This accepts
the C12-01 CPU boundary only. Shader generation, material rendering, Slang and
node-editor integration remain separate packages; C11-07 deferrals are unchanged.

**C12-02 completed under direct user assignment, 2026-11-11:** extensible
registry-driven HLSL lowering and the existing DXC bridge produce real DXIL and
SPIR-V. Physical D3D12/Vulkan fixtures verify generated source through the existing
custom-material renderer, including hardware sRGB filtering and explicit
LinearRepeat sampling. Final Debug build covers all tests, Editor and Runtime;
Debug CTest **95/95**, focused Release CTest **5/5** and architecture checks pass.
See the [C12-02 delivery](reviews/OPT_12_C12_02_DELIVERY.md) for review provenance,
native artifacts, exact commands and limits. This accepts the released compiler
and bounded native baseline. Automatic product graph loading, managed
layout/cache/reload, full resource policy and graph UI remain later work;
C12-03 onward are unreleased and C11-07 deferrals are unchanged.

**C12-03 completed under direct user assignment, 2026-11-11:** graph reflection/layout
compatibility, shader/pipeline cache identities and transactional last-known-good
reload, with typed authored-name overrides and programmatic rename. Implementation
and native verification are recorded in the [C12-03 delivery](reviews/OPT_12_C12_03_DELIVERY.md);
Debug CTest **98/98**, focused Release **8/8**, architecture and independent
review pass. Automatic product graph loading/UI remain separate; C12-04 onward
remain unreleased.

**C12-04 completed under direct user assignment, 2026-11-11:** explicit texture color/sampler/
fallback and authored dependency semantics. Rooted raw/descriptor/VFS resolution,
four sampler modes, normal unpacking and resource-aware native generations are
complete, with Debug **101/101**, focused Release **11/11**, architecture and
independent correction rechecks passing. See the
[C12-04 delivery](reviews/OPT_12_C12_04_DELIVERY.md).

**C12-05 completed under direct user assignment, 2026-11-11:** optional Slang
graph compilation is implemented as an explicit Windows/MSVC evaluation backend.
Real DXIL/SPIR-V output, strict layout/reflection comparison, transactional errors,
and physical D3D12/Vulkan equivalence are recorded in the
[C12-05 delivery](reviews/OPT_12_C12_05_DELIVERY.md). DXC remains the default;
C12-06 onward remain unreleased.

**Exit:** OPT-11..20 are closed independently against measured backend,
performance, image-quality, reliability and UI evidence. Unsupported features
remain deferred.

## Read documents only when needed

| When | Read | Responsibility |
| --- | --- | --- |
| OPT-00 | [Vehicle delivery](archive/design/reports/VEHICLE_RETIREMENT_DELIVERY.md), [exchange](agents/CODEX_GEMINI_EXCHANGE.json) | Reported delivery and actual review state |
| OPT-01 | [Master contract](design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md), [ECS/jobs review](architecture/ENTT_ENKITS_REVIEW.md) | Shared AF contracts, baseline faults and dependency graph |
| OPT-02..03 | [ECS/jobs plan](design/ENTT_ENKITS_REFACTOR_PLAN.md), [master AF contracts](design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md#7-additional-foundation-packages) | Thread/lifetime/ECS/simulation internals and data boundaries |
| OPT-04..05 | [NRI contract](design/NRI_INTEGRATION_CONTRACT.md), [render architecture](design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md), [package plan](design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md), [coding reference](design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md) | Device/resource/frame/services and real product consumers |
| OPT-06..09 | [Plugin architecture](design/PLUGIN_SYSTEM_ARCHITECTURE.md), [package plan](design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md), [coding reference](design/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | SDK, Plugins window, project builds, fresh Vehicle and selected Runtime |
| OPT-10 | Acceptance matrices in the master and domain plans | Current cross-product evidence and boundary enforcement |
| OPT-11 and later | [OPT-11..OPT-20 program plan](design/POST_OPT10_PROGRAM_PLAN.md), [design archive](archive/design/README.md#deferred-feature-work) | Independent Post-OPT-10 OPTs, deferred features and remaining acceptance obligations |

The [documentation hub](README.md) separates working guides, source snapshots,
future contracts and archived delivery evidence. The [historical product roadmap](../ROADMAP.md)
retains old milestones; it is not another optimization checklist.

## Deferred feature work

After OPT-10, use this order for later work. Fixes required to preserve an existing
workflow may be scoped earlier with explicit evidence; that does not release a
whole feature phase. Old outstanding acceptance is retained, never relabeled PASS.
The detailed Post-OPT-10 decomposition is in the [OPT-11..OPT-20 program plan](design/POST_OPT10_PROGRAM_PLAN.md); this table remains the high-level order.

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
