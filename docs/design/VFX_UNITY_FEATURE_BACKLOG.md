# VFX feature expansion backlog

Updated: 2026-11-03. Status: **planned; no implementation claimed**.

This is the deferred feature backlog requested after the neutral VFX Editor
delivery. The parent checklists are Phase 39.1 and Phase 39.2 in
[ROADMAP.md](../../ROADMAP.md). Existing behavior is described in the
[authoring guide](VFX_EDITOR_AUTHORING_GUIDE.md); Phase 39 interactive/OpenGL
acceptance remains tracked independently in its
[acceptance report](VFX_PHASE_39_REVIEW_AND_ACCEPTANCE_REPORT.md).

Feature categories were checked against Unity's official
[Particle System modules](https://docs.unity.com/en-us/engine/6000.0/manual/visual-effects/particle-systems/particle-system-modules)
and [Visual Effect Graph documentation](https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/index.html).
The packages and dependencies below are Axle implementation proposals, not
claims that these features already exist or that Unity's implementation must be
copied. Phase numbers group VFX work; they do not change the scheduled scope of
Phase 40 animation work.

## Current boundary

Axle already has time-based emission/bursts, sampled initial scalar ranges,
scalar lifetime curves, color/alpha gradients, basic shapes and OBJ surface
sampling, curl noise, authored forces/collision geometry, inherited sub-emitter
events, texture-grid animation and three billboard orientations. These paths
must be extended rather than replaced by a second particle system.

No automatic example effect is part of a new document. Future modules follow
the same empty-effect policy, neutral defaults and explicit enable states.
Reference effects belong in test/art assets and must not become editor logic.

## Phase 39.1: CPU particle authoring and rendering expansion

All packages start unchecked. P1 is the first implementation wave; P2 follows
the required foundations and measured runtime needs. Each package should have
its own implementation/test commit and its own acceptance evidence.

| ID / priority | Gap or partial feature | Planned implementation and decisive acceptance |
| --- | --- | --- |
| A0 / P1 | Shared parameter modes are incomplete | Add reusable Constant / Two Constants / Curve / Two Curves and color/gradient modes, with validation, units and deterministic per-particle sampling. Migrate existing ranges/curves without changing old effect output. Verify codec/history round trips and independent instance seeds before using the types in new modules. |
| A1 / P1 | Moving-emitter emission and initialization | Add rate over distance, start delay and bounded prewarm. Track previous world transforms, interpolate spawn positions along movement and define teleport/reset behavior. Test stationary/moving emitters, equivalent movement subdivisions, pause/seek, delayed bursts and bounded/cancelable prewarm. |
| A2 / P1 | Main timing, spaces and lifecycle options | Add simulation speed, scaled/unscaled time selection, custom simulation transform, explicit scaling modes, stop actions and optional ring-buffer policies. Define frame ownership and Stop-versus-drain semantics. Verify copy/reload/scene teardown, parent transforms and no accumulated pause/cull catch-up spikes. |
| A3 / P1 | Motion modules are currently scalar/basic | Add per-axis velocity/force over lifetime, orbital/radial motion, velocity limits/damping, emitter-motion inheritance and lifetime by emitter speed. Keep emitter-motion inheritance distinct from existing sub-emitter inherited velocity. Verify local/world/custom space, sampled versus continuously inherited motion, gravity/drag composition and deterministic seek. |
| A4 / P1 | Scalar size/rotation and lifetime-only appearance | Add 3D start size/rotation, per-axis lifetime size/rotation, and color/size/rotation by speed. Specify normalized speed ranges and composition order with lifetime curves and Start Color. Verify zero-speed/range edge cases, legacy scalar migration, renderer packing, bounds and live updates. |
| B1 / P1 | Trails and ribbons are absent | Add bounded per-particle trail history and authored ribbons, width/color curves, minimum vertex distance, UV modes, lifetime/particle-death behavior and world/local attachment. Use stable particle handles with generations rather than dense pool indices, which can change on kill. Verify fast motion, pool reuse, sorting, resource retirement and real DX12/Vulkan pixels. |
| B2 / P1 | Particle output is billboard-only | Add instanced mesh particles with imported mesh/material selection, submeshes, 3D orientation/scale, independent draw bindings and correct bounds/sorting. Keep mesh-particle output separate from Mesh Surface emission. Verify different meshes/materials in one frame, save/reopen, missing assets, HDR/MSAA and scene/preview equivalence. |
| B3 / P2 | Billboard/renderer controls are limited | Add horizontal/vertical alignment, configurable stretch factors, pivot/alignment controls, particle sort modes/bias and min/max screen size. Define lighting/shadow support against actual RHI capabilities. Verify degenerate view/velocity axes, alpha ordering across emitters and accurate frame bounds; unsupported paths need explicit behavior. |
| C1 / P2 | Custom particle data/shader contract is absent | Add named custom attributes and explicit vertex/instance streams, then a particle-compatible custom shader interface. Version CPU/GPU layouts and validate reflected signatures, rather than treating mesh-PBR shaders as compatible. Test stream offsets, type/default validation, simultaneous materials and last-good shader behavior. |
| C2 / P2 | Physics exists, but trigger/gameplay delivery is incomplete | Extend collision filters, quality/radius modes and scene collider authoring; add separate trigger volumes with enter/inside/exit/outside events. Expose bounded, lifetime-safe gameplay callbacks. Preserve existing floor/planes/boxes and sub-emitter collision events. Test high-speed crossings, entity destruction, event budgets and scene service unbinding. |
| C3 / P2 | Per-particle lights are absent | Add light assignment, ratio and color/range/intensity modulation under an explicit per-effect and scene-wide light budget. Define stable selection when over budget. Test light-buffer capacity, flicker under pool reuse and measurable scene/preview pixel differences. |
| C4 / P2 | Shape/noise/external-force authoring is partial | Extend shape arc/radius-thickness/distribution/direction controls, texture-driven emission masks and richer mesh sources. Add per-axis noise/remapping and shared scene force-field volumes/filtering. Reuse existing curl noise, force evaluators and mesh sampling. Skinned-mesh emission depends on validated Phase 40 pose/skinning data. |
| C5 / P2 | Flipbook and sub-emitter options are partial | Add FPS/speed/lifetime sheet timing, row/sprite selection and deterministic start-frame randomization. Extend event types, inheritance flags and sub-emitter probability/count modes with bounded routing. Test frame limits/UVs, inherited lifetime/size/rotation and graph validation after rename/duplicate/delete. |
| D1 / P1, extended in P2 | Performance controls and diagnostics are limited | Establish profiler baselines first, then add correct bounds/frustum/distance culling, explicit culling simulation policies, quality tiers/LOD, global particle/event/trail/light budgets and reusable effect-instance pooling. Report CPU time, draw count, upload size and overdraw where measurable. Verify budget exhaustion, resumed simulation and independent reused instances. |
| D2 / P2 | Advanced authoring/runtime integration is incomplete | Add an exposed effect-parameter API with per-instance overrides, validation/diagnostics, reusable module settings and multiselection editing. Integrate sequencing/animation-event activation only after the relevant animation APIs are available. Verify override persistence, source-versus-instance changes, undo boundaries and packaged Runtime dependencies. |
| E / every wave | Acceptance must follow each feature | Extend CPU, production GPU, editor and packaged Runtime fixtures; verify real textures/meshes/materials, missing resources, failure paths and supported backend behavior. Expand soft-depth/MSAA integration where compatible depth data becomes available. Close a package only with its own evidence and documented limits. |

### Implementation order and dependencies

1. Record D1 baselines and finish the outstanding Phase 39 acceptance items.
2. Implement A0, then A1/A2, then A3/A4. Migrate existing data before enabling
   the new authoring controls. A1 is particularly useful for moving vehicle effects.
3. Deliver B1 and B2 as complete asset -> editor -> simulation -> renderer slices;
   B1 needs stable particle identity, while B2 needs A4's 3D data/layout contract.
4. Complete D1's runtime budgets/pooling as particle, trail and mesh workloads
   become measurable. B3 and C1-C5 follow their corresponding runtime/RHI services.
   Reuse the reflection/layout contract established in C1 for later GPU outputs.
5. Add D2 after override ownership and animation-event dependencies are explicit.
   Run E at each wave; it is not a final cleanup batch.

The first useful delivery is neutral moving-emitter authoring with rich
parameters, followed by trails and mesh output. Feature breadth must not defer
asset persistence, actual rendering or UI behavior to an unspecified later pass.

## Phase 39.2: GPU simulation and visual graph

This is a separate planned expansion. Begin after profiling identifies effects
that need GPU simulation, required RHI compute/resource services are verified,
and its maintenance/platform budget is agreed. Phase 39.1 completion does not
depend on a node editor.

- [ ] **G0 - Feasibility and contract**: Record target particle counts/frame cost,
  supported backends, compute/indirect-draw capabilities, data layout, bounds and
  event limitations. Define which authored modules can run on CPU/GPU and what
  happens on unsupported hardware. Keep existing CPU assets working.
- [ ] **G1 - GPU runtime**: Implement spawn/update/compaction, bounded allocation,
  buffer ownership/barriers, sorting, indirect drawing and safe resize/teardown.
  Validate numerical behavior against bounded CPU reference cases and use
  backend-specific tolerances; do not promise bitwise CPU/GPU random equivalence.
- [ ] **G2 - Graph document and compiler**: Persist stable node/port IDs and typed
  parameters; define spawn/initialize/update/output contexts, validation,
  compilation diagnostics and last-good execution. Add subgraphs only with
  explicit dependency/cycle and versioning rules.
- [ ] **G3 - Graph authoring and integration**: Add searchable nodes, connections,
  undo/redo, reusable subgraphs, exposed instance parameters, bounded event
  routing, preview and asset/scene/packaged Runtime workflows.
- [ ] **G4 - Acceptance**: Physical DX12/Vulkan tests, stress captures, resource
  lifetime and failure tests, CPU fallback/unsupported-path verification,
  interactive authoring checks and measured performance reports.

## Completion rule for every package

Use one authored model across inspector, assets, runtime and shaders. New
properties require persistence/migration, enable-state behavior, undo/redo,
copy/duplicate behavior and independent instances. Rendering features require
actual rendered evidence, not just populated structs or compiled shaders.
Do not add inactive placeholder controls, silently substitute a preset, or mark
an unavailable GPU/UI test as passed. The evidence report must distinguish
automated CPU results, physical GPU results and native interaction checks.
