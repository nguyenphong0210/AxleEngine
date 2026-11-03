# Phase 40: Skeletal Animation Studio Completion Plan

> **Priority update (2026-11-03):** [Phase 41 rendering foundations](RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md)
> execute before new Phase 40 authoring. Existing deliveries and open acceptance
> remain recorded. P41-23/24 reuse F1-F3 rendering contracts; do not implement a
> competing skinning/picking pipeline. Read the shared exchange for actual scope.

Date: 2026-11-03. Source baseline: `538163b`.
Status: **foundation implementation in progress**. Actual completed packages and
verification are recorded in the [foundation report](ANIMATION_PHASE_40_FOUNDATION_REPORT.md);
unchecked packages below remain implementation/acceptance requirements.

Parent: [ROADMAP.md](../../ROADMAP.md). Implementation contracts and the Gemini
assignment: [coding guide](ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md).
General rules: [development conventions](../guides/DEVELOPMENT.md) and
[architecture assessment](../architecture/ASSESSMENT.md).

This is the canonical scope, work breakdown and acceptance plan. The companion
explains algorithms and integration; proposed names there are not existing APIs.
Reinspect HEAD and user changes before starting. Existing Phase 38.3 manual
material checks and Phase 39 interactive/OpenGL checks remain open and separately
tracked. The user's Phase 40 planning/handoff permits preparing and implementing
these scoped packages without claiming that those earlier phases passed.

## 1. Intended user workflow

1. Explicitly import a supported rigged FBX/glTF/GLB, or create the deterministic
   procedural reference rig. Inspect the skeleton, meshes and available takes.
2. Open an imported clip as read-only source data; use **Create Editable Copy**
   before authoring an independent clip asset. Never overwrite the source model.
3. Open the Animation bottom dock, choose a rig/clip, scrub or play its private
   preview, select bones and edit local translation/quaternion/scale keys.
4. Box-select, move, copy, paste and delete keys; edit typed event markers;
   Undo/Redo; save, close and reopen without losing authored data.
5. Create an Animator Controller asset. Add states, parameters and transitions;
   author 1D/2D blend spaces; preview parameter changes and active transitions.
6. Assign the rig and controller to an entity, duplicate it, save/reopen the scene
   or assembly, and run both characters independently in Editor Play and Runtime.
7. Obtain the same pose evaluation and supported material appearance in private
   preview and scene on DX12/Vulkan, with existing OpenGL behavior preserved.

The phase delivers this bounded workflow. It does not establish complete Godot,
Unity or Unreal feature parity.

## 2. Inspected foundation and gaps

The following are source observations, not failures reproduced in a new runtime
session. No engine build, GPU run or interactive animation test was performed
for this planning-only change.

| Priority | Current source evidence | Work required |
| --- | --- | --- |
| Foundation | `Skeleton`, `AnimationClip`, `AnimationPose`, `Animator`, `AnimationStateMachine`, `BlendSpace1D` already exist in `Engine/Animation` | Extend/adapt these; do not create a competing evaluator |
| P0 | `Scene::DuplicateEntity` and `Scene::Copy` create another Animator but pass the same mutable `AnimationStateMachine` pointer | Independent clocks, parameters, triggers, transitions and event cursors per instance |
| P0 | `AnimationClip::SamplePose` uses identity/default TRS for missing components within an existing channel | Preserve the corresponding bind-pose components, e.g. a rotation-only channel must retain bind translation |
| P0 | Skeleton/clip/pose FK loops assume parent indices precede children; importers patch parent IDs after adding joints | Validate topology and evaluate a derived topological order without corrupting vertex joint indices |
| P0 | `SkeletalMesh` constructor immediately calls `BuildBuffers`, which constructs OpenGL VAOs/buffers | CPU import/evaluation must not require an OpenGL context; lazy backend-owned rendering resources |
| P0 | `SkinningPipeline` and `Scene::RenderSkeletalMeshes` implement legacy OpenGL; `RHISceneRenderer` gathers `MeshInstance3D`, not `SkeletalMeshComponent` | Real DX12/Vulkan skinned mesh submission and palette binding |
| Foundation | `RHISkinnedVertex3D` and seven-attribute layout helpers already exist in `RHIVertexLayout.hpp` | Reuse the 96-byte layout; `RHIMesh::Init` currently accepts static `RHIVertex3D` only |
| P1 | glTF importer selects skin zero, concatenates mesh primitives, links only immediate joint parents, and reads sampler output without interpolation-mode handling | Correct mesh/skin association, intermediary-node transforms, ordering, bind basis and interpolation; diagnose unsupported cases |
| P1 | Clip channels expose mutable vectors alongside cached lookup maps | Validated edit APIs/rebuild of derived lookup state after mutation and deserialize |
| P1 | Transition conditions are `std::function<bool()>`; some scene/test lambdas capture the owning shared state machine | Serializable typed rules; remove internal owning capture cycles; preserve explicit legacy adapter behavior |
| P1 | Parameters support Float/Bool/Trigger, no authored Int declarations/defaults | Typed parameter definitions, validation and instance storage |
| P1 | Scene serialization stores playback and three conventional parameters, not a controller definition/reference; file import creates Animator but does not bind the returned clips | Durable controller/clip references, complete inline fallback and resource resolution |
| P1 | `AssemblyNode` has no skeletal/animator payload and assembly codec has no matching support | Preserve animated entities through both direct assembly and assembly-editor data round trips |
| P1 | 1D blending computes phase from time divided by the current effective duration | Explicit phase continuity when parameters change; deterministic 2D blend implementation |
| P1 | No Animation/Animator authoring panel is registered in `EditorLayer`/UI CMake; no animation event hook in `ScriptableEntity` | New document models, panels, selection bridge and queued runtime event adapter |
| P1 | AssetDSL/AssetType classifications have no skeleton/clip/controller/skinned-mesh kinds | Versioned assets, classification, Content Browser routing and packaged resource support |
| P1 | `RHIObjectPicker` gathers static `MeshInstance3D` geometry | Picking must match the animated silhouette; bone picking is a separate authoring tool |
| P2 | Existing Steps 36-41 exercise legacy animation; Step 103 is absent from the runner and Milestone 10 implementation | Always-active CPU suites plus actual GPU/UI acceptance; retain useful legacy regressions |

Source anchors: [Animation module](../../Engine/Animation/CMakeLists.txt),
[clip sampling](../../Engine/Animation/src/AnimationClip.cpp),
[controller](../../Engine/Animation/src/AnimationStateMachine.cpp),
[components](../../Engine/Scene/include/Axle/Components.hpp),
[scene lifecycle](../../Engine/Scene/src/Scene.cpp),
[scene persistence](../../Engine/Scene/src/SceneSerializer.cpp),
[assembly data](../../Engine/Scene/include/Axle/AssemblySerializer.hpp),
[RHI vertex layouts](../../Engine/RHI/include/Axle/RHI/RHIVertexLayout.hpp),
[RHI scene draws](../../Engine/Scene/src/RHISceneRenderer.cpp),
[asset types](../../Engine/Asset/include/Axle/AssetTypes.hpp),
[test runner](../../Editor/src/Test.cpp).

### Rendering boundary discovered during inspection

The inspected RHI scene renderer publishes light shadow parameters but does not
contain a full shadow-map render pass. Phase 40 must not claim animated RHI
shadow support simply by adding a shader or borrowing the legacy OpenGL result.
Preserve OpenGL skinned shadows. Integrate a skinned variant into any RHI shadow
pass that exists at implementation time; otherwise record RHI shadow casting as
a separate renderer limitation. Building a general RHI shadow system is outside
this phase. Color/depth, supported PBR materials, animated entity picking and
skeleton overlays remain required DX12/Vulkan work.

## 3. Scope and design decisions

### Required

- Device-free skeleton/import/clip/controller evaluation with validation and
  deterministic instance ownership. Runtime logic uses seconds and normalized
  phases; timeline FPS is a display/snap/step setting, not a unit conversion.
- Versioned `.axleasset` roots for `Skeleton`, `SkeletalMesh`, `AnimationClip`
  and `AnimatorController`; explicit import/bake, editable clip copies and
  cooked geometry readable through FileSystem/VFS. Append enum values.
- One single-layer controller with Clip, BlendSpace1D and BlendSpace2D motions,
  explicit entry state, directed transitions, Float/Int/Bool/Trigger rules and
  noninterruptible timed crossfades. Stable IDs survive renaming/reordering.
- Bottom Animation dock and Animator graph panel; complete save/dirty/history,
  guarded document replacement, local TRS keys and event-marker authoring.
- Real skinned scene/private-preview rendering; stable per-draw GPU ownership;
  bone/joint overlays and bidirectional timeline/viewport selection.
- Runtime C++ event dispatch with explicit seek/loop/crossfade/ownership policy.
- End-to-end scene, assembly and standalone package persistence; actionable
  missing/incompatible-resource diagnostics and last-good reload behavior.

### Deliberate limits

- One rig/skin per imported rig asset initially. Preserve submesh/material slots.
  A source with several skins requires explicit selection; never merge them
  silently into skin zero. Retargeting between different skeletons is excluded.
- Four strongest normalized influences per vertex and a validated 128-joint GPU
  palette cap, matching the existing legacy cap. Larger rigs are rejected with
  diagnostics, not silently truncated; limits include preserved helper nodes.
- Imported STEP and LINEAR channels work. Imported CUBICSPLINE must be correctly
  decoded and evaluated or explicitly baked with an error bound; it cannot be
  read as one ordinary value per time. Editable authored clips require Step and
  Linear/SLERP; a cubic source may be baked into an editable copy with a warning.
- Signed nonzero TRS scale is supported and tested. Singular transforms, animated
  shear and unsupported source features produce explicit import diagnostics.
- Locomotion is in-place: character controller owns entity movement. Blend phase
  matching reduces discontinuities but does **not** guarantee no foot sliding.
  Root-motion extraction/application, foot IK, retargeting, animation layers,
  masks, additive poses, nested blend graphs, IK/constraints and network sync
  are future work. Keep authored root-bone tracks as pose data.
- Built-in material rendering is required; arbitrary custom vertex programs on
  skinned meshes require a compatible interface. Diagnose unsupported custom
  programs and use the documented standard fallback, preserving asset values.
- Reverse runtime playback/events and transition interruption are excluded.
  Timeline backward seek is required and must not execute gameplay callbacks.
- Use existing Dear ImGui/ImGuizmo and `Engine/Resources/Icons`. Do not add a node
  editor dependency or rewrite docking/renderer architecture for this phase.

## 4. Ownership and module contracts

| Layer | Owns | Must not own |
| --- | --- | --- |
| Immutable assets | Validated skeleton, mesh, clips and controller definitions; stable IDs and resource refs | Scene handles, callback closures, GPU descriptors, clocks |
| Animator instance | Parameter values, triggers, current/target times/phases, poses, palette and event cursors | Editable shared definition or another entity's mutable instance |
| Document model | Working value definition, saved revision, history and authoring selection | Scene simulation authority or GPU resources |
| Preview session | Independent animator/private scene, clip seek clock, preview parameters | User scene scripts/physics/gameplay events |
| Render bridge | Immutable mesh upload/cache, transient per-draw palettes and pass state | Clip authoring and playback clocks |
| Scene integration | Binding of assets/instances to entities; clone/start/stop; event delivery | Another editor document's dirty state |

New CPU data/evaluator/codec sources belong in `Engine/Animation`. Editor-only
models/widgets/panels belong in `Engine/UI`; their device-free sources may use a
small separately buildable authoring target for tests. `Engine/Asset` can add
kinds/generic bake hooks but cannot depend on Animation (Animation already links
Asset). `Engine/Renderer` cannot start depending on Animation (Animation already
links Renderer). Put the typed render/resource bridge in Scene, or a new narrow
AnimationRendering target below Scene with explicitly acyclic dependencies.
Do not depend on VFX just to reuse its transport/history behavior; copy contracts
or extract a small genuinely shared helper with targeted regression coverage.

## 5. Atomic implementation packages

Each row is a separate tested local commit. These are **proposed commit subjects**,
not an existing commit log. Never push automatically. If a row proves too broad,
split it further and retain its parent ID in the progress report. Tests accompany
implementation; K integrates coverage rather than postponing all tests.

| ID | Depends on | Implementation and owned files | Required evidence before package closure | Proposed commit subject |
| --- | --- | --- | --- | --- |
| A0 | none | CPU test main/harness and `AxleAnimationUnitTests` in Editor CMake; isolate it from AxleTests window startup | Release checks actually fail on a failing expectation; no window/device creation | `test(animation): add a device-free animation test harness` |
| A1 | A0 | Skeleton validation/topological order, partial-TRS bind fallback, pose validation and lookup rebuilding in Animation | Child-before-parent, multi-root, cycles, rotation-only track, antipodal quaternion, signed scale | `fix(animation): validate rigs and preserve partial bind transforms` |
| A2 | A1 | SkeletalMeshImporter and CPU mesh preparation; mesh/skin selection, source basis, intermediary nodes, interpolation, four influences | Tiny glTF fixtures with known transforms; unsupported/malformed input diagnosed; no GL context | `fix(animation): normalize skeletal imports without graphics state` |
| A3 | A1 | Animator/controller definition-instance boundary; duplicate/copy/start/stop adapters in Scene/Components | Two entities and an edit/play scene copy cannot share clocks/triggers; no owning lambda cycle | `fix(animation): isolate animator runtime instances` |
| B1 | A1, A3 | Stable value definitions/validation for skeleton, rig mesh, clip, controller, keys/events/graph layout; AssetDSL kinds/types append-only | Invalid IDs/links/types/numerics rejected; two similar names remain distinct | `feat(animation): define versioned authored animation assets` |
| B2 | B1, A2 | Shared text codecs, atomic saves, immutable resource loader and geometry bake/VFS read | Full round trip, failed replacement preserves old file, truncated binary rejected, source unavailable after bake | `feat(animation): persist and bake animation resources for VFS` |
| B3 | B2, A3 | Scene and both assembly paths; controller references/inline fallback, rig refs, typed default overrides, resource resolve/legacy migration | Scene, assembly editor and direct assembly round trip; missing source and legacy biped remain usable | `feat(scene): round-trip skeletal rigs and animator controllers` |
| C1 | B1, A3 | Typed parameter declarations/accessors and pure condition evaluator; script/component Int API | Type mismatch, stable-ID rename, trigger peek without consumption, legacy Speed/IsGrounded compatibility | `feat(animation): evaluate typed animator parameters and conditions` |
| C2 | C1 | Deterministic transition selection, exit-time crossings, source/target clocks and crossfade runtime | Priority, boundary crossing, zero-duration, target sampling, large dt, reset and atomic trigger consumption | `fix(animation): define deterministic transitions and crossfades` |
| D1 | C2 | Harden BlendSpace1D weights and phase tracking; preserve existing adapter API | Endpoints, duplicates, changing parameters and duration, nonloop clamp | `fix(animation): preserve phase in one-dimensional blend spaces` |
| D2 | D1, B1 | BlendSpace2D topology/weights, stable triangulation, runtime N-way TRS blend | Triangle centroid/edge/hull tests; 0/1/2/collinear points; finite normalized weights and quaternions | `feat(animation): evaluate deterministic two-dimensional blend spaces` |
| E1 | C2, D2 | Event definitions, bounded interval enumerator and instance event-owner cursors | Start/end/loop, several loops, seek, pause, crossfade and blend-leaf ownership fixtures | `feat(animation): collect bounded animation event occurrences` |
| E2 | E1, B3 | Scene queue and `ScriptableEntity::OnAnimationEvent` adapter, ABI rebuild and stale-handle checks | Real script receives typed marker once; preview receives log only; delete/stop/reload invalidates delivery safely | `feat(scene): dispatch animation events through live script instances` |
| F1 | A2, B2 | CPU-to-`RHISkinnedVertex3D` converter; Scene-side immutable RHI skeletal mesh upload with submeshes/bounds | CPU deformation reference and layout/limit tests; build RHI adapter without GL-only calls | `feat(renderer): upload skeletal meshes through the active RHI` |
| F2 | F1, C2 | Per-draw palette CBV, skinned built-in PBR vertex variant, material/depth/MSAA/HDR scene collection | DXIL/SPIR-V shader build entry points; external pixel checks specified for two distinct palettes | `feat(renderer): render skinned PBR meshes in DX12 and Vulkan` |
| F3 | F2 | RHI entity-ID picking uses same skin positions; pass/resource teardown; preserve legacy OpenGL skin/shadow behavior | Pixel/ID harness covers deformed silhouette, draw order, resize and backend failures; shadow limitation recorded honestly | `fix(renderer): match animated picking and skeletal draw lifetimes` |
| G1 | B2, B3 | Device-free clip/controller documents, stable selections, bounded history, saved revision, last-good reload | Dirty/Undo/Redo branch, failed save/open, no-op edits, delete selection and unsaved-document guards | `feat(animation-editor): add transactional animation documents` |
| G2 | G1, F2 | Animation panel/private preview, transport and bottom docking; explicit import/editable-copy/open route | Preview-independent CPU fixture; actual RHI render call into private viewport; UI checks left pending externally | `feat(animation-editor): add isolated clip preview and transport` |
| G3 | G2, A1 | Dopesheet TRS tracks, keys/box selection/drag/snap/copy/paste/delete, value inspector and event markers | Pure edit commands test collision/retime/quaternion policies; one drag is one Undo; source import remains unchanged | `feat(animation-editor): implement undoable dopesheet and event editing` |
| H1 | G2, F3 | CPU bone/joint overlay geometry and RHI/legacy render adapters; hierarchy/viewport selection bridge | World/rig/mesh basis fixtures; zero-length bones; hidden/closed preview releases resources | `feat(animation-editor): visualize and select animated skeleton bones` |
| H2 | H1, G3 | Bone hit testing, track reveal and focus/input routing against gizmos/camera/entity picking | CPU ray/segment/near-plane tests; selection does not move the entity or overwrite another document | `fix(animation-editor): synchronize bone selection and timeline focus` |
| I1 | G1, C2 | Animator graph nodes/edges, entry/transition inspector, typed parameter panel, layout/pan/zoom/history | Add/rename/delete/reconnect commands preserve IDs; active state/edge telemetry is read-only | `feat(animation-editor): author animator states parameters and transitions` |
| I2 | I1, D2, G2 | 1D/2D point editors, mesh/clip resource pickers, graph preview controls and incompatibility diagnostics | Editing blend points changes real compiled definition/preview; Undo and save/reopen keep topology/values | `feat(animation-editor): author blend spaces with live controller preview` |
| J1 | B3, G3, H2, I2, E2 | Entity inspector assignment, Content Browser create/open/import, scene detach/rebind, explicit Apply/Make Unique/reload | Closed-loop author -> save -> assign -> duplicate -> reload; scene selection cannot reset dirty assets | `feat(editor): integrate animation assets with scene authoring` |
| J2 | J1, B2 | Pre-cook bake/validation bridge and Runtime loading; no Editor callback/UI dependency; reference assets | Device-free package load with source files absent; external standalone visual/event run specified | `feat(runtime): load cooked animation assets independently of editor tools` |
| K1 | all CPU contracts | Consolidate CPU suites, retained Steps 36-41, register actual Step 103/aliases and honest failure propagation | Exact commands/exit codes; unknown/missing suites cannot produce a false PASS | `test(animation): register Phase 40 CPU and integration acceptance` |
| K2 | F3, H2, I2, J2 | Physical `AxleAnimationGPUTests`, DX12/Vulkan opt-in CTests, deterministic references/captures | Test implementation compiles where possible; physical results are NOT RUN until executed by a capable reviewer | `test(animation): add physical skeletal rendering acceptance fixtures` |
| K3 | K1, K2 and external UI | User guide, support matrix, measured acceptance report and roadmap evidence | Record actual CPU/build/GPU/UI/runtime status separately; unchecked gates remain open | `docs(animation): record supported workflows and acceptance evidence` |

### Execution waves and first useful delivery

- **Wave 1: foundations**: A0 -> A1 -> A2/A3 -> B1 -> B2 -> C1 -> C2.
- **Wave 2: executable slice**: B3 -> F1 -> F2 -> G1 -> G2. A procedural or
  imported rig can now save/load, run two states and render an independent preview
  and scene instance. Review this before expanding the authoring UI.
- **Wave 3: authoring**: D1 -> D2 -> E1/E2; G3 -> F3 -> H1/H2 -> I1/I2.
- **Wave 4: delivery**: J1 -> J2 -> K1 -> K2 -> K3 and external acceptance.

Slash-separated packages may be scheduled independently after their actual
prerequisites; that is not permission to make concurrent edits to shared files.
Each wave ends with a compiled, reviewable workflow and a short evidence update.
A3 may introduce the minimal immutable definition/typed-rule boundary needed to
fix instance ownership; B1/C1 complete its authored schema and full parameter
contracts. This is staged extension, not a second set of types or early closure
of those later packages.

The total is 28 packages, with renderer, importer and timeline work likely to
need additional smaller commits. This is a substantial feature phase, not only
an editor-panel task; use package completion rather than speculative day counts.

## 6. Verification matrix and ownership

| Gate | Gemini must implement/run | External reviewer/user must run | Phase completion rule |
| --- | --- | --- | --- |
| CPU | Production data/evaluator/edit-command/codec calls; always-active Release expectations; isolated temp fixtures | Review reports and reproduce CPU commands | Required PASS |
| Build/shaders | Build available CPU/core/UI/renderer targets; compile built-in skinned shader variants to DXIL/SPIR-V when toolchain exists | Full supported Editor/Runtime/test build and legacy regressions | Missing toolchain is NOT RUN, never PASS |
| Physical GPU | Implement real offscreen render/readback assertions and strict requested-backend checks | Run on physical D3D12 and Vulkan, no fallback, inspect captures | Both required PASS for RHI rendering acceptance |
| Editor UI | Implement the real widgets, input routing and lifecycle; headless ImGui tests only support structural behavior | Perform the interactive checklist below | Required; headless commands do not substitute |
| Standalone | Implement cooked-resource loading/event adapter and package fixtures | Launch packaged Runtime with authoring source files unavailable | Required visual/event PASS |
| OpenGL | Preserve existing paths and add compatible fixtures/adapters | Separate animation regression including legacy shadows | Record actual result and remaining limitation |

Proposed CPU commands, available only after A0/K1 introduces the target:

```powershell
cmake -S . -B build
cmake --build build --config Release --target AxleAnimationUnitTests --parallel 8
ctest --test-dir build -C Release -R '^AnimationUnitTests$' --output-on-failure
cmake --build build --config Release --target AxleEditor AxleRuntime AxleTests --parallel 8
.\build\Editor\Release\AxleTests.exe --step103
```

The current AxleTests executable initializes graphics. Its TestCommon.hpp already
keeps test assertions active in Release; preserve that behavior. Its Step 103
integration is not the device-free entry point. Gemini must
run the new CPU target even if AxleTests cannot initialize a device, and report
the latter accurately. Never disable validation, redefine test expectations or
replace backend drawing with mocked counts to obtain a green report. Record
pre-existing warnings separately from new warnings; no warning-free claim without
a warning-free build.

### Concrete acceptance scenes

1. **TwoBoneCalibration**: tiny synthetic mesh and nonzero-bind child offset,
   rotation-only animation, known CPU-deformed vertex positions and colored
   submeshes. Add one nonidentity mesh-to-rig transform fixture.
2. **DualAnimatorIsolation**: two entities sharing the same immutable rig/controller,
   different parameters/times and parent transforms; reverse submission order.
3. **LocomotionReference**: procedural Idle/Walk/Run/Jump clips, 1D Speed and 2D
   direction blend, exit transitions, named footstep/hitbox/VFX marker payloads.
4. **PersistenceReference**: editable clip/controller, referenced and unique scene
   entities, assembly round trip and a cooked package with no FBX/glTF source.

Reference data must be generated deterministically in tests or checked in as
small owned fixtures with provenance. Never modify CarTest, the user's imported
characters, default material or shipping script files to manufacture test output.

### Interactive checklist

- [ ] Open Animation/Animator from View menu and asset double-click; bottom dock
  shares Console/Project and existing saved docking layouts survive upgrades.
- [ ] Import/select a supported rig, inspect diagnostics and create an editable
  clip copy without touching the source model.
- [ ] Play/Pause/Stop/Step/Loop/Speed/FPS/Scrub work at narrow and wide widths;
  FPS changes do not change clip duration or authored key times.
- [ ] Translate/rotate/scale keys, box-select, move, snap, paste and delete; cancel
  a drag; Undo/Redo; save/close/reopen and verify values/events.
- [ ] Bone picking selects/reveals the correct track; selecting a track highlights
  the same current-pose bone. Camera/gizmo shortcuts do not steal timeline input.
- [ ] Graph add/rename/remove/reconnect, entry state, rule editing, trigger tests
  and 1D/2D point dragging all affect the private preview; saved layout persists.
- [ ] Unsaved New/Open/close and failed Save preserve working documents. Entity
  selection/deletion/scene replacement never silently discards dirty asset edits.
- [ ] Scene Apply/Make Unique/reload behave as labelled; two scene animators and
  their play snapshot are independent; Play/Stop restores editor data.
- [ ] Event markers dispatch C++ callbacks only during runtime forward playback;
  preview/seek does not play real sounds or spawn real user-scene effects.
- [ ] Imported/nonuniform/mirrored rig/material appearance, animated silhouette
  picking, HDR/MSAA, resize and close/reopen are inspected on declared backends.
- [ ] Packaged Runtime loads the same controller/clip and visibly animates when
  original source media is unavailable; event callback evidence is recorded.

## 7. Delivery record

Gemini maintains a short `ANIMATION_PHASE_40_IMPLEMENTATION_STATUS.md` only when
implementation starts: baseline, package/commit, behavior delivered, exact CPU
commands/results, build/shader status, deviations and external checks NOT RUN.
The final acceptance report is evidence-oriented, not a copy of this plan.
Keep Phase 40 unchecked until all required gates above have actual results.
