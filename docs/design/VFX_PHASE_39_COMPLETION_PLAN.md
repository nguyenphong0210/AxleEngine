# Phase 39: VFX Editor Completion Plan

Status: **planned implementation; Phase 39 reopened**.
Reviewed: 2026-11-02.

This plan replaces the earlier interpretation that a three-column panel and
passing Step 102 establish a completed VFX editor. Existing simulation and UI
code are foundations to preserve. The work below is not implemented by this
document, and its proposed names are not existing APIs.

The parent checklist is [ROADMAP.md](../../ROADMAP.md). Related guidance is in
the [editor tooling roadmap](EDITOR_TOOLING_ROADMAP.md). Phase 38.3 material UI
acceptance remains a separate user-owned task; this plan does not close it or
start Phase 40.

## 1. Delivery goal and boundaries

A user can create a reusable VFX asset, author several emitters, assign textures
and materials, preview the complete effect, save and reopen it, instantiate it
in a scene, and obtain equivalent playback in the editor and standalone runtime.
Previewing must not mutate or reset scene simulation. All visible controls must
have a working configuration, persistence and runtime/rendering path.

The reference workflow is a campfire containing Flame, Smoke and Sparks, with
optional collision-triggered secondary sparks. Automotive smoke/backfire is a
second regression fixture. Deliver reliable CPU simulation with GPU-instanced
rendering; GPU simulation is not required for this phase.

In scope: multi-emitter persistence, document lifecycle, preview isolation,
DX12/Vulkan preview, per-draw resource isolation, existing particle modules,
particle-compatible material inputs, playback/scrubbing, undo/redo, scene reuse
and focused CPU/GPU/UI acceptance. Complete the previously advertised mesh
surface, gradient-alpha and sub-emitter paths.

Out of scope: a Niagara-style node graph, compute particle simulation, trails,
volumetric fluid simulation, arbitrary custom-shader graph integration, new PBR
surface models and full Unity/Godot feature parity. Keep existing OpenGL behavior
working; record its verification separately from mandatory DX12/Vulkan evidence.
Do not use the user's vehicle scene or default material as disposable fixtures.

## 2. Current evidence and gaps

The review was read-only. The existing Release executable passed `--step102`;
that result covers its current assertions, not an interactive editor or physical
VFX GPU acceptance run. No new VFX implementation, benchmark or UI acceptance
result is supplied by this planning change.

| Priority | Evidence in current source | User-visible consequence | Package |
| --- | --- | --- | --- |
| P0 | `EditorLayer.cpp` calls `SetSelectedEntity()` every rendered panel frame; the panel reloads stacks and calls `ResetPreview()` | Selection, edits and preview time can reset every frame | A |
| P0 | `ParticleEmitterStack` stores a shared `ParticleSystem`; selection copies those shared pointers into the panel | Preview reset/config changes can affect the scene instance | A |
| P0 | `RenderPreview3D()` requires OpenGL framebuffer/functions; `InitRHI()` only allocates a viewport target | An RHI texture handle is displayed without a particle rendering path into it | D |
| P1 | `SaveToAsset()` serializes one `Config`; scene serialization writes primary-emitter fields | A multi-emitter effect cannot round-trip through those paths | B |
| P1 | Multiple controls change `m_Config` without committing it to the selected stack; Apply writes selected config into the primary config | Edits can have no live effect or overwrite the first emitter | A, E |
| P1 | Scene RHI particle draws pass null texture/normal resources; renderer does not consume `MaterialPath` | Assigned resources do not reach rendered particles | D, F |
| P1 | RHI particle draws repeatedly update one host-upload instance buffer, constant buffer and descriptor set before scene submission | Potential cross-draw data overwrite; confirm with a real multi-emitter GPU test | D |
| P1 | Scrubber callback is empty; `ParticleSystem::Update()` does not implement Duration/Looping | Displayed time and playback controls lack the advertised semantics | C |
| P1 | Child-system registration exists as a runtime API, but the inspected component/panel paths do not resolve the authored target names into it | Sub-emitter UI entries do not establish an executable effect relationship | H |
| P1 | MeshSurface samples a sphere using Radius | The shape name does not describe the actual emission source | I |
| P1 | Gradient widget edits color keys; alpha-key data has no matching full editor workflow | Authored transparency keys are not fully editable | G |
| P2 | VFX panel has no complete document open/save/dirty/undo flow | No reliable standalone asset-authoring session | B, E, G |

Source entry points:

- [VFXEditorPanel.cpp](../../Engine/UI/src/VFXEditorPanel.cpp),
  [EditorLayer.cpp](../../Editor/src/EditorLayer.cpp).
- [Components.hpp](../../Engine/Scene/include/Axle/Components.hpp),
  [SceneSerializer.cpp](../../Engine/Scene/src/SceneSerializer.cpp).
- [ParticleEmitterSerializer.cpp](../../Engine/VFX/src/ParticleEmitterSerializer.cpp),
  [ParticleSystem.cpp](../../Engine/VFX/src/ParticleSystem.cpp).
- [RHIParticleRenderer.cpp](../../Engine/VFX/src/RHIParticleRenderer.cpp),
  [Scene.cpp](../../Engine/Scene/src/Scene.cpp),
  [Particle.hlsl](../../Engine/Resources/Shaders/Particle.hlsl).
- [CurveEditorWidget.cpp](../../Engine/UI/src/CurveEditorWidget.cpp),
  [ParticleEmitter.cpp](../../Engine/VFX/src/ParticleEmitter.cpp),
  [Step 102](../../Editor/src/Tests/Test_Milestone10.cpp).

Step 102 creates multiple component stacks, but its persistence case serializes
one `ParticleEmitterConfig`. Its MeshSurface assertion checks a normalized
direction, not sampling of a supplied mesh. Preserve useful existing assertions
and add coverage for the missing contracts rather than weakening tests.

## 3. Ownership and data contracts

Use ordinary C++20 data structures and the existing asset/RHI infrastructure.
Avoid introducing a general editor framework as a prerequisite. The following
types describe responsibilities; exact names can follow local conventions.

| Proposed responsibility | Owns | Must not own |
| --- | --- | --- |
| `ParticleEffectDefinition` in Engine/VFX | Schema version, effect name, ordered emitter definitions, IDs, authored resources and seed | Scene entity, pool, mutable burst counters, ImGui or GPU descriptors |
| `EmitterDefinition` in Engine/VFX | Stable ID, name, authored enable state, local transform, complete emitter config, texture/normal/material references | A shared live `ParticleSystem` |
| `ParticleEffectInstance` in Engine/VFX | Independent emitter systems, playback state, RNG streams, event routing and collision services | UI widgets or a mutable cached asset shared with unrelated instances |
| VFX editor document/model | Working definition, selected emitter ID, source path, revision/clean marker, edit commands | Scene runtime ownership |
| Preview controller | Its own effect instance, camera, fixed-step clock and preview colliders | The selected scene entity's pools |
| Particle renderer | Per-frame/per-draw GPU resources, descriptor allocations, compatible pipelines | Authored file writes or document dirty state |

Required invariants:

1. Selecting the same scene/entity/document repeatedly is idempotent. Change
   identity includes the scene instance and entity generation, not only an index.
2. Preview and scene instances share immutable definitions or resource caches,
   never live particle pools, burst counters, RNG state or child ownership.
3. Stable emitter IDs survive rename, reorder and save/reload. Display names are
   not the primary key for commands, selection or sub-emitter relationships.
4. Legacy `Config`/`System` represent a primary-emitter compatibility view only.
   Explicit adapters synchronize it; arbitrary selected emitters do not replace it.
5. A single effect clock updates each emitter exactly once per simulation step.
   Define whether trigger-only emitters perform continuous emission; default them
   to event-driven emission so they do not unexpectedly emit on scene startup.
6. Emitter enabled state is authored. Play/Pause/Stop must not silently enable all
   emitters. Editor visibility/solo is session state unless explicitly authored;
   loading the same asset must not silently save transient solo state.
7. CPU simulation, asset serialization and runtime effect code do not depend on
   ImGui or an editor panel. Do not serialize GPU handles, callbacks or simulation
   counters. Retain existing runtime build boundaries.

## 4. Dependency order and implementation packages

Priority order:

`A (isolation) -> B (effect assets) -> C (playback) -> D (RHI rendering)`

Then `E (authoring) -> F (resources/materials) -> G (undo/curves)`;
`H (sub-emitters)` and `I (mesh/collision completion)` build on B/C and integrate
with E/G. J accepts the combined workflow. Each package is split into the atomic
sub-steps below; a package is not one mandatory bulk commit.

### A. Stabilize selection, preview isolation and scene lifecycle

Files: `VFXEditorPanel.hpp/.cpp`, `EditorLayer.cpp`, `Components.hpp`, effect-model
helpers and focused CPU tests.

- A1: stop reloading/resetting an unchanged selection. Preserve selected emitter,
  unsaved edits and time; validate selection when an entity is deleted or a scene
  is replaced. Clear bindings on project/scene teardown.
- A2: create preview runtime instances from authored values rather than copying
  shared live systems. Eliminate duplicate independently simulated preview state;
  a retained `GetPreviewSystem()` compatibility API must refer to the real primary
  preview emitter. Apply copies authored state, not pools or child instances.
- A3: unify all edit paths behind an explicit selected-emitter commit helper.
  Fix Apply so editing stack 2 preserves stack 1. Use the same component update
  path for edit preview and Play; the current edit-mode primary-only update must
  update all eligible stacks exactly once. Stop preserving disabled stacks by
  overwriting their authored enable flags during Play.

Acceptance: repeated same-selection calls do not reset time/pools; preview
reset/Play/Pause and config changes leave scene particles untouched; editing or
deleting a secondary emitter leaves other configs and selection valid; scene
switch, entity deletion and panel close do not retain invalid targets.

Suggested commits: `fix(vfx): preserve editor selection and preview lifetime`,
`refactor(vfx): isolate preview instances from scene simulation`,
`fix(vfx): synchronize emitter edits without replacing the primary stack`.

### B. Versioned multi-emitter assets and scene round-trip

Files: new definition/serializer helpers in Engine/VFX, existing
`ParticleEmitterSerializer`, `Components`, `SceneSerializer`, asset discovery and
loader integration where needed.

- B1: add the authored effect definition and validation. Keep the existing
  ParticleEmitter asset kind and introduce a versioned effect representation
  inside the DSL rather than relying on a file extension change. A proposed v2
  root contains schemaVersion, name, seed, and ordered Emitter child blocks with
  string IDs, resource paths and complete config. Block syntax must be accepted
  by the actual AssetDSL parser; do not hand-write a second parser.
- B2: serialize every emitter and supported module, curves, color/alpha keys,
  bursts, collision planes/boxes, force fields and sub-emitter ID references.
  Resolve callback services such as physics raycasts after loading. Read legacy
  single-emitter assets as one emitter with deterministic migration IDs. Map
  legacy target names only if unambiguous; report unresolved targets.
- B3: round-trip referenced assets and inline effect definitions through scenes.
  Initially support shared asset references and an explicit Make Unique/inline
  copy. Do not introduce an untested sparse override-merging system. Define
  project-relative path resolution and preserve portable references on Save As.

Deserialization must be transactional: invalid data does not partially replace
the active document. Reject duplicate IDs, unsupported future versions, invalid
ranges and impossible references with diagnostics. Missing textures/materials
use defined fallbacks and retain the authored path for repair. Save through a
temporary sibling file and replace the destination only on successful completion;
failed writes preserve both the existing file and unsaved document state.

Acceptance: Flame/Smoke/Sparks retain count, IDs, order, configs, references and
relationships through asset and scene save/reopen; legacy assets still load;
an emitter rename/reorder does not change trigger targets. Truncated/invalid
input and an unwritable destination preserve the previous working state.

Suggested commits: `feat(vfx): define versioned multi-emitter effect assets`,
`feat(vfx): serialize complete emitter modules and migrate legacy assets`,
`feat(scene): round-trip referenced and inline particle effects`.

### C. Deterministic playback, duration, looping and scrubbing

Files: `ParticleSystem`, effect-instance/preview-controller helpers, `VFXCommon`,
`VFXEditorPanel` and transport tests.

- C1: separate authored burst schedules from per-instance counters. Define
  Duration, Looping and PlayOnAwake in runtime code. A non-looping emitter stops
  emitting at duration while existing particles drain. Pause freezes simulation;
  Stop clears particles and time; Restart restores initial seed/counters. Loop
  rollover resets emission schedules without arbitrarily killing living particles.
  Catch up crossed burst times for large steps with a bounded event budget; zero
  intervals/infinite cycles cannot cause an unbounded loop.
- C2: give each effect/emitter independent seeded random state. Include every
  random source, including burst counts currently using `std::rand()`. Stable IDs
  determine streams so unrelated emitter order or preview sessions cannot alter
  the scene's random sequence. Do not reset a global RNG to simulate isolation.
- C3: drive preview with a fixed simulation step, a single-step button and
  playback speed. Seeking backward resets and resimulates; forward seek also
  advances simulation, not only the displayed slider. Limit synchronous seek work
  and show progress/cancel for expensive seeks. Define effect preview range for
  mixed emitter durations and looping effects; optional prewarm uses the same
  clock and RNG rules, not a second simulation algorithm.

Acceptance: same definition/seed/time yields the same CPU result after restart
or seek; step/speed/pause/loop and boundary bursts behave consistently. Compare
simulation on one platform/configuration; do not promise bitwise cross-platform
GPU output. Frame subdivision and event-budget behavior have explicit tests.

Suggested commits: `fix(vfx): implement duration and bounded burst scheduling`,
`refactor(vfx): isolate deterministic random streams per effect instance`,
`feat(vfx-editor): implement fixed-step transport and simulation scrubbing`.

### D. Real RHI preview and safe multi-emitter rendering

Files: `RHIParticleRenderer`, `VFXEditorPanel`, `RHIViewportDescriptor`, scene
particle submission, `Particle.hlsl`, and GPU integration tests.

- D1: first add a regression drawing two emitters with different positions,
  colors and textures in the same command submission. Allocate distinct instance
  ranges, constant data and descriptor sets per draw/frame, and retain resources
  until GPU completion. Do not overwrite descriptors or mapped buffer bytes
  referenced by earlier draws. Existing queue waits are not justification for
  overwriting data before that submission starts. Do not remove unrelated global
  synchronization as part of this fix.
- D2: render the preview instance into its RHI color/depth targets using the
  active backend and real particle renderer. Set an orbit camera, clear color,
  grid/axes and optional bounds. Handle resize/minimize, target transitions,
  upload failures, descriptor exhaustion and close/reopen teardown. Display a
  diagnostic when rendering fails; a valid texture handle alone is not acceptance.
- D3: carry emitter/entity transforms into local-space rendering; world-space
  particles remain where spawned. Define emitter-local offsets, parent motion,
  scale handling and velocity-aligned billboards consistently in preview/scene.
  Define alpha sorting, stable tie order and additive/cutout ordering so batching
  does not silently change blend results. Submit all particle batches beyond
  `MAX_PARTICLES_PER_BATCH` rather than silently truncating a larger authored pool.

Acceptance: non-clear preview pixels on requested DX12/Vulkan; distinct emitters
remain distinct with reversed submission order and over several frames; textures
do not bleed between draws; resize and teardown are valid; moving/rotating a
parent distinguishes local/world space. Cover SDR/HDR, depth and supported MSAA
pipeline variants. Use matching format/state on fallback, not an SDR-only PSO.

Suggested commits: `fix(vfx): isolate particle draw buffers and descriptors`,
`feat(vfx-editor): render preview particles through the active RHI`,
`fix(vfx): preserve emitter transforms sorting and batch capacity`.

### E. Complete the asset-authoring session

Files: `VFXEditorPanel`, a small editor document/model, `ContentBrowserPanel`,
asset/entity inspector routing, `EditorLayer` and existing command facilities.

- E1: New/Open/Save/Save As, editable effect name, path, dirty indicator and
  unsaved-change handling on switch/close/project changes. Route a VFX asset from
  Content Browser into this document without requiring a scene entity. Keep
  standalone asset editing and entity-instance editing visibly distinguishable.
- E2: select/add/delete/duplicate/rename/reorder emitters by stable ID; duplicate
  assigns new IDs and independent runtime state. Define which internal references
  are remapped. Add editor-only solo/visibility without changing authored enabled
  state. Validate deletion of a referenced emitter and show the affected links.
- E3: consistent module controls, conditional fields, reset/defaults, diagnostics
  and live preview updates. Separate module enable switches from collapse state;
  disabled modules retain their values and persist their enable state. Add
  emitter-list/module filtering and usable narrow/wide panel layout. Apply to
  entity copies the complete effect and preserves transform/unrelated components;
  editing an asset updates shared instances under an explicit reload policy.

Acceptance: create an effect without a scene, edit a secondary emitter, switch
emitters, save, close and reopen without data loss; invalid save leaves dirty
state; deleting/switching scenes leaves the document usable. Test all controls
that previously mutated only `m_Config`.

Suggested commits: `feat(vfx-editor): add effect document open save and dirty state`,
`feat(vfx-editor): manage stable emitter stacks and references`,
`feat(vfx-editor): unify module editing and scene apply workflows`.

### F. Texture, material and depth-aware particle shading

Files: resource pickers/cache adapters, `RHIParticleRenderer`, `Particle.hlsl`,
`Scene::RenderParticles`, preview submission and asset validation.

- F1: per-emitter searchable texture, normal-map and material slots with
  thumbnails, validated drag/drop, replacement, clear and missing-file feedback.
  Upload and bind those resources per draw in preview and scene. Reuse existing
  texture/color-space infrastructure without depending on UI from Engine/VFX.
- F2: define a particle-compatible subset of existing material assets: color/alpha,
  albedo map/UV, emission, supported transparency/cutout and existing particle
  lighting/normal inputs. Specify material-versus-emitter precedence: assigned
  material drives its supported fields; unassigned emitters use legacy config
  defaults. Show unsupported material/custom-shader fields clearly rather than
  accepting a path that has no effect. Full ForwardPBR/custom-program execution
  for particles is not required by this plan.
- F3: implement and verify the advertised Additive, Alpha Blend and Cutout paths.
  Preserve existing enum numeric meanings: current `Multiply = 2` must not be
  relabeled as premultiplied alpha or silently become Cutout. Append explicit
  values if needed, migrate labels/serialization and test the actual blend math.
  Bind a valid scene-depth input before enabling soft particles; define behavior
  for missing depth, preview ground, HDR and MSAA. A softness uniform alone is
  not an implementation of depth-intersection fading.

Acceptance: two emitters with different maps/materials produce distinct pixels;
replace/clear/disable/missing-file cases use correct initialized fallbacks.
Cutout has a working alpha threshold; blending and depth fading are verified
against opaque geometry on DX12/Vulkan. Document which fields/material types
the particle renderer supports.

Suggested commits: `feat(vfx): bind authored emitter textures in preview and scene`,
`feat(vfx): apply particle-compatible material properties`,
`feat(vfx): implement explicit cutout and depth-aware particle shading`.

### G. Undo/redo and complete curve/gradient authoring

Files: `CurveEditorWidget`, editor document/commands, serializer and CPU/UI tests.

- G1: edit commands store authored before/after state and stable IDs, not runtime
  pointers. Cover emitter operations, module toggles, assignments and numeric edits.
  A continuous drag is one undo transaction. Undo back to the saved revision
  clears dirty state; redo or divergent edits update it correctly. Reuse existing
  commands where suitable, without a repository-wide command-system rewrite.
- G2: preserve the four-point Bezier contract while completing point editing,
  numeric entry, presets and reset. Add independent color and alpha key tracks,
  add/delete/time/value editing and defined ordering for coincident/crossed keys.
  Selection belongs to each widget/document; do not share a static selected-key
  index across unrelated gradients. Gradient/curve changes preview live and
  remain valid after emitter switching, undo and save/reload.

Acceptance: edit a curve and alpha ramp, undo/redo, save/reopen and compare both
serialized values and sampled evaluations. Different emitters/widgets cannot move
or edit each other's selected key. Test dragging keys across their neighbors.

Suggested commits: `feat(vfx-editor): add undoable authoring transactions`,
`feat(vfx-editor): complete independent curve and color alpha key editing`.

### H. Integrated sub-emitter relationships and bounded events

Files: effect definitions/instances, `ParticleSystem`, component loader, panel
target picker, renderer traversal and serializer.

- H1: resolve target IDs after all emitters are created. Provide a target picker,
  compatible-name migration diagnostics, rename/delete handling and validation of
  self-links/cycles. Start by rejecting cyclic graphs. Each child is owned and
  updated once; avoid recursively owning the same instance and updating it again
  as an independent stack. Render triggered child particles as well as the parent.
- H2: implement Birth/Collision/Death, probability/count, color and velocity
  inheritance with defined local/world conversions. Enforce per-step event and
  spawn budgets so large bursts cannot recursively exhaust the process. Pending
  events do not contain pointers invalidated by pool compaction or emitter edits.

Acceptance: an effect authored and loaded through normal APIs, without a test
manually calling `AddChildSystem()`, produces visible child particles. Rename,
reorder and save/reopen preserve links; invalid graphs and deleted targets are
diagnosed; inheritance and budgets have deterministic CPU coverage.

Suggested commits: `feat(vfx): resolve authored sub-emitter graphs safely`,
`feat(vfx): implement bounded inherited particle event spawning`.

### I. Finish advertised mesh emission and collision workflows

Files: shape/runtime sampling, import/mesh adapters, collision services,
definitions/serializer and inspector controls.

- I1: MeshSurface references real mesh data, samples triangles proportional to
  area, and uses barycentric positions/normals with the authored transform.
  Exclude degenerate faces; handle missing/empty meshes with diagnostics and a
  documented fallback. Keep mesh resource loading out of the sampling math.
  Add a mesh picker and persist the reference; a sphere is not MeshSurface.
- I2: expose persistent preview planes/boxes and resolve scene physics raycast
  callbacks with safe lifetimes. Apply bounce/friction/kill/radius consistently;
  define whether an emitter uses preview colliders or the scene's collision
  service. Preserve existing force-field/noise/wind settings through save/reload.
  Unsupported collision geometry must be explicit, not a silent no-op.

Acceptance: particles originate on a known non-spherical mesh with unequal-area
triangles; degenerate/missing data is safe. Preview and scene collision fixtures
exercise bounce, kill and collision-triggered children. A saved collision module
restores its configured geometry, not only an Enabled flag.

Suggested commits: `feat(vfx): sample authored mesh surfaces by triangle area`,
`feat(vfx-editor): persist and bind collision geometry and physics services`.

### J. Reference effects, layered verification and honest closure

Files: focused tests, dedicated reference assets/scenes, CMake/test registration,
user guide and acceptance report.

- J1: add focused CPU suites for effect persistence, migration, selection/isolation,
  edit commands, playback/seeking, graph events and sampling. They must run without
  a device/window; temporary filesystem fixtures are allowed. Preserve Step 102
  as historical regression coverage and fix its misleading completion message.
- J2: add GPU readback tests for actual multi-emitter rendering, resources,
  sorting, transforms, texture clearing, material modes, soft depth, child draws,
  resizing, supported HDR/MSAA and resource teardown. Explicit backend selection
  must disable fallback and verify the actual active device. Reuse format-aware
  readback/capture helpers; never read RGBA16F using four bytes per pixel.
- J3: author dedicated Campfire, collision-Sparks and vehicle smoke/backfire
  references. Save/reopen and asset-backed/inline equivalents use matched
  camera/light/seed/time for comparisons. Record backend/device/configuration,
  failures and capture paths. Optional timing results distinguish simulation,
  CPU submission/wait and GPU measurements actually collected.
- J4: execute the UI matrix below, fix discovered failures in separate commits,
  update the VFX guide and reconcile all Phase 39 checkboxes only against evidence.

Suggested commits: `test(vfx): cover complete effect authoring and playback contracts`,
`test(vfx): verify multi-emitter rendering on explicit RHI backends`,
`test(vfx): add reproducible effect reference assets`,
`docs(vfx): record supported workflows and runtime acceptance`.

## 5. Validation matrix and execution rules

| Layer | Minimum cases | Completion evidence |
| --- | --- | --- |
| CPU assets | Multiple emitters, full module values/enables, resources, stable IDs, legacy migration, invalid input, failed save, scene references/inline copies | Real serializer/model assertions; no device/window |
| CPU ownership/editor model | Same selection, two previews, scene independence, secondary-emitter edits, duplicate/delete/reorder, undo clean markers, scene replacement | Distinct pools/state and unchanged unrelated definitions |
| CPU simulation | Independent RNG, fixed-step seek, duration/drain, loops, boundary/large-step bursts, budgets, graph inheritance, collision and mesh sampling | Explicit expected counts/positions/state with deterministic fixtures |
| GPU DX12 | At least two emitters in one submission, changed/reversed order, distinct resources, blend/cutout/depth, transforms, child draws, multiple batches, SDR/HDR/MSAA/resize | Actual shader compilation and pixel/readback assertions |
| GPU Vulkan | Same meaningful fixtures with requested backend and fallback disabled | Actual Vulkan device and pixel/readback assertions |
| UI document | New/open/double-click, unsaved switch/close, Save As, failed writes, undo/redo, scene apply/reload | Real editor interaction and reopened data |
| UI controls | Narrow/wide panel, secondary emitter, module changes, texture/material/mesh pickers, color/alpha/curve edits, graph targets, playback and scrub | Real preview changes without editing/resetting the scene |
| Lifecycle | Close/reopen, resize/minimize, delete selection, switch scene/project, Play/Stop, standalone load | No invalid access, double simulation, leaked preview ownership or stale bindings |

Existing commands are regression entry points, not proof of the new work:

```powershell
cmake --build build --config Release --target AxleTests --parallel 12
./build/Editor/Release/AxleTests.exe --step47
./build/Editor/Release/AxleTests.exe --step99
./build/Editor/Release/AxleTests.exe --step102
```

Run one step per process. Update existing tests when APIs change while preserving
their intended coverage. Check material regressions 101/104/105/106 when changing
shared scene/material/RHI code. A new VFX CPU target and explicit GPU options must
be implemented and registered before documenting their commands as available.

Compile the affected targets and run the focused checks before each code commit.
No new warnings; report pre-existing warnings such as LNK4098 separately rather
than claiming a warning-free build. Re-run broader checks when shared paths or
failures justify them. Do not add tests that only duplicate an implementation's
formula without exercising the production path.

Use three distinct statuses: implemented, CPU-tested and runtime/UI-accepted.
Do not infer GPU or editor acceptance from a CPU assertion, pool count, descriptor
handle, screenshot of a static panel or the existing Step 102 PASS message.

## 6. Milestones, task sharing and commit policy

| Gate | Required packages | Reviewable result |
| --- | --- | --- |
| M1: safe preview foundation | A plus B definition boundary | Selecting/editing preview cannot affect scene; no per-frame reset |
| M2: reusable effect core | B, C, D | Multi-emitter asset reopens and renders on DX12/Vulkan with deterministic playback |
| M3: usable authoring | E, F, G | Full asset workflow, reliable module/resource controls and undo/redo |
| M4: advertised modules complete | H, I | Authored child effects, actual mesh surface emission and usable collisions |
| M5: phase acceptance | J and all prior gates | Campfire workflow and backend/UI matrix pass with documented limits |

The atomic commit subjects above are suggestions, not a fixed number of commits.
Split a sub-step further when it has independent behavior or verification. Each
implementation commit carries its relevant tests and evidence; document-only
planning commits do not require rebuilding unrelated binaries. Keep local commits
within the user's authorization; pushing to a remote is a separate action.

If handing work to Gemini, start with B1/B2 and deterministic CPU fixtures under
a frozen schema contract; later consider C or G model helpers. Keep ownership,
renderer integration and physical DX12/Vulkan/UI acceptance with an operator who
can run them. Assign file ownership and dependencies before concurrent work;
avoid simultaneous edits to Components, SceneSerializer or VFXEditorPanel. A
CPU-only contributor records GPU/UI results as NOT RUN, even if test code exists.

No calendar estimate or completion percentage is asserted: renderer resource
isolation and legacy migration may reveal additional defects. M1 is the first
implementation task. Do not start Phase 40 on the assumption Phase 39 is closed.

## 7. Definition of done

- [ ] Create/open a multi-emitter VFX asset without a pre-existing scene entity.
- [ ] Author independent Flame/Smoke/Sparks stacks with real textures/materials.
- [ ] Edit secondary emitters, reorder/rename and undo/redo without losing state.
- [ ] Play/pause/step/restart/scrub produce valid deterministic preview state.
- [ ] Preview never shares/reset scene particle pools or RNG state.
- [ ] Save/reopen the effect and scene preserves all supported authoring data.
- [ ] Referenced/shared effects and Make Unique have explicit, tested behavior.
- [ ] Renderer draws distinct emitters/resources safely in one GPU submission.
- [ ] Sub-emitters, real MeshSurface and collision controls work through authored assets.
- [ ] DX12/Vulkan GPU acceptance and actual editor interaction are recorded.
- [ ] Supported limits and remaining out-of-scope features are documented.
- [ ] ROADMAP parent and child statuses match the evidence; no remaining required work.
