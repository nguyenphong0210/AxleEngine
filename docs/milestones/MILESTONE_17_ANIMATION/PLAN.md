# Milestone 17 — Skeletal Animation Studio and Runtime Plan

**Status** — Proposed. Release an explicit checkpoint or range after M16 core acceptance (`RT-08D`).
**Phase 46** — Asset/evaluator foundation and native rendering (46.1), Studio and gameplay runtime (46.2), product acceptance (46.3).
**Architecture** — [Asset, evaluator, Scene, render and Studio contracts](ARCHITECTURE.md).
**Handoffs** — [M20 packaging](../MILESTONE_20_COOKER/PLAN.md), [M21 skeletal LOD](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md), [M22 animation URO](../MILESTONE_22_SIMULATION_SCALE/PLAN.md).

## 1. Product outcome and baseline

A developer can import a skinned FBX or glTF character, edit a clip and controller, assign them in a scene or assembly, preview the result and run the saved character in a detached packaged Runtime. Two actors sharing a rig and compiled controller can have different poses, parameter values and attachments in the same frame. The visible pose determines depth, shadow, motion and entity picking. The reference character locomotes over a step while a masked upper-body reload emits a gameplay event and keeps a prop aligned with the hand socket. The fixture also demonstrates 1D/2D blending, sync markers, additive layers, foot IK and opt-in root motion.

The source baseline includes CPU importer and asset validation, a controller evaluator, transactional document classes, render extraction and OpenGL skinning. D3D12 and Vulkan currently report skeletal rendering as unsupported. The historical [Phase 40 plan](../../archive/design/deferred/ANIMATION_PHASE_40_COMPLETION_PLAN.md) and [foundation corrections](../../archive/design/reports/ANIMATION_FOUNDATION_FINAL_CORRECTIONS.md) identify relevant existing fixtures and source. Reproduce the CPU baseline and record the accepted M15/M16 renderer topology before assigning current status.

The seven parent requirements `M17-01` through `M17-07` cover the full product. Each numbered child has one execution owner. M17 proves early and final export through the existing package path and publishes dependency edges to M20. M21 owns derived geometry LOD and meshlets. M22 owns significance-based pose cadence and bone LOD. The [architecture scope](ARCHITECTURE.md#scope-after-m17) describes later animation extensions.

## 2. Execution order

Each numbered checkpoint is a separate implementation, verification and commit boundary. The table lists direct technical prerequisites. Complete checkpoints in table order within the released range. Native tests must run before checkpoints that depend on their results. Batch evidence is self-verification. C1.3 is the consolidated independent review boundary.

| Checkpoint | Prerequisites | Requirements | Deliverable |
| --- | --- | --- | --- |
| **M17-A1.1** | Accepted M16 core | 01.1, 01.2 | Source and fixture baseline, asset validation and analytic CPU reference. |
| **M17-A1.2** | A1.1 | 01.3, 01.4 | Production FBX/glTF import and reimport with staged publication and rollback. |
| **M17-A1.3** | A1.2 | 01.5, 01.6 | Shared Scene/assembly binding, clone/rebind semantics and dependency edges. |
| **M17-A1.4** | A1.3 | 01.7, 04.6 | Device-free Animation boundary, shared basic motion program, isolated playback state and fixed-tick pose publication. |
| **M17-A2.1** | A1.4 | 02.1, 02.2 | Native vertex layout/upload, immutable draw packet and deformed bounds. |
| **M17-A2.2** | A2.1 | 02.3, 02.4 | Frame-owned palette, shader math and first real two-actor DX12/Vulkan draw. |
| **M17-A3.1** | A2.2 | 02.5, 02.6 | Main and Preview geometry, materials, depth/normals and shadow passes. |
| **M17-A3.2** | A3.1 | 02.7, 02.8 | Presentation interpolation, motion history and presented-frame entity picking. |
| **M17-A3.3** | A3.2 | 02.9, 02.10 | Native captures, RT coverage diagnostics, fallback and GPU lifetime. |
| **M17-A4.1** | A3.1 | 07.6 | Early detached Runtime and VFS smoke fixture using the native two-actor path. |
| **M17-B1.1** | A3.1 | 03.1, 03.2 | Clip document host, real Editor entry points and private Preview transport. |
| **M17-B1.2** | B1.1 | 03.3, 03.4 | Editable Dope Sheet, transactions and document lifecycle. |
| **M17-B1.3** | B1.2, A3.2 | 03.5 | Posed bone overlay, track reveal and input focus. |
| **M17-B2.1** | A1.4 | 04.1, 04.2 | Typed controller instances, deterministic transitions and 1D evaluation in the shared program. |
| **M17-B2.2** | B2.1 | 04.3, 04.7 | 2D topology and interpolation, sync markers and phase continuity. |
| **M17-B2.3** | A4.1, B1.3, B2.2 | 04.4, 04.5 | Visual controller and blend editors, live Preview and assignment. |
| **M17-B3.1** | A1.3, B2.3 | 05.1, 05.2 | Socket authoring, attachment identity, transform resolution and persistence. |
| **M17-B3.2** | B3.1, B2.2 | 05.3, 05.4 | Ordered masked additive layers in the real evaluator and Editor. |
| **M17-B3.3** | B3.2 | 05.5 | Masked override and one-shot action authoring, runtime commands and arbitration. |
| **M17-B3.4** | B3.3 | 05.6 | Gameplay marker authoring and fixed-tick event delivery. |
| **M17-B4.1** | B3.3, B2.1 | 06.1, 06.2 | Root motion extraction and tick-tagged consumption by the movement owner. |
| **M17-B4.2** | B4.1, B3.1, B3.4 | 06.3, 06.4 | Scene-backed two-bone IK and final pose, attachment and event ordering. |
| **M17-C1.1** | A3.3, A4.1, B4.2 | 07.1, 07.2 | Reopened gameplay fixture and detached package with source inputs unavailable. |
| **M17-C1.2** | C1.1 | 07.3, 07.4 | Integrated CPU, native GPU, interactive Editor, Runtime and measurement results. |
| **M17-C1.3** | C1.2 | 07.5 | Independent requirement audit, fixes and final rechecks. |

Phase 46.1 owns A1–A4, Phase 46.2 owns B1–B4, and Phase 46.3 owns C1. A2.2 demonstrates the first native two-actor draw. A3.1 joins the draw to the main scene and Preview. A4.1 proves that the same path reaches a detached package before Studio expansion.

## 3. Atomic checklist

### M17-01 · Import, codecs and scene binding

- [ ] **M17-01.1 · Reproduce the entry baseline.** Record source/build identity, available authored fixtures, import and extraction callers, native pass/picking seams and supported routes. Run the CPU suite and its failure probe. Record readback metric units and freeze the expected tolerances before candidate scoring.
- [ ] **M17-01.2 · Freeze data and sampling invariants.** Assert parent-before-child ordering, stable joint and material slot identities, rig signature, explicit signed bind TRS, inverse-bind composition and sparse-channel bind fallback. Use an analytic rig for exact key boundaries, held tracks and finite quaternion/cubic results. Reject malformed ranges, identities, influences and incompatible rigs before publication.
- [ ] **M17-01.3 · Wire production imports.** Import procedural, FBX and glTF examples through the project import route. Verify converted/reflected basis, skin and helper ancestry, strongest-four influence normalization and stable material slots. Cover glTF STEP/LINEAR/CUBICSPLINE and FBX STEP/STEP-next and sampled bake. Report malformed or unsupported skin selections.
- [ ] **M17-01.4 · Publish complete generations and reimport.** Save and read back native mesh/geometry/rig/clip generations and verify references and numeric round trips. Confirm unchanged reimport and supported rename/reorder retain identities. Reject ambiguous remaps. Verify discovery and export select the same committed generation. Exercise truncated binary, missing external glTF buffer, collapsed source times, stale jobs and simulated write failure. Confirm the prior generation remains usable.
- [ ] **M17-01.5 · Share binding across real callers.** Use the same mesh/controller and material/parameter overrides in Scene, direct assembly instantiation and assembly editor. Test shared and detached assets, a declared inline fallback, a valid mesh with bad controller and a missing mesh. Verify diagnostic context includes the owning reference.
- [ ] **M17-01.6 · Isolate instances and enumerate dependencies.** Duplicate an actor and create an Edit-to-Play copy. Verify independent active states, clocks and triggers and explicit reset on stop/rebind. Enumerate typed owner-to-target edges for geometry, rig, clips/controller and materials. Resolve an authored binding using its native geometry with raw FBX/glTF unavailable.
- [ ] **M17-01.7 · Establish the CPU module and Scene publication boundary.** Build and run the evaluation path without Renderer, RHI, Scene, physics or Editor dependencies in the Animation library. Move OpenGL geometry ownership to the render boundary and keep the existing compatibility route. Observe one committed pose per executed tick, two isolated actors, stale job result rejection and a binding swap that retains the last ready pose and resources until the new generation commits.

### M17-02 · Native skinning and pass integration

- [ ] **M17-02.1 · Validate the native mesh.** Convert existing CPU geometry into the reflected skinned vertex stream and indexed submeshes. Assert offsets/stride, unsigned joint attribute, four weight lanes and slot/range parity. Verify one shared mesh generation and replacement after device epoch change.
- [ ] **M17-02.2 · Validate the extracted packet and bounds.** Compare extracted entity/instance, mesh/rig/pose and material identities, caster flags and conservative bounds. Demonstrate a deformed silhouette outside the bind AABB remains visible. Exercise the conservative route for assets with unproven bounds.
- [ ] **M17-02.3 · Verify palette ABI and lifetimes.** Match C++ packing and DXIL/SPIR-V reflection. Verify nonzero base offsets, including beyond element 128, first/last legal joints, 128 accepted and 129 rejected, current and previous slices, bounded failure and two overlapping submissions. Force replacement before the old submission retires.
- [ ] **M17-02.4 · Prove native deformation.** Compare CPU vertices and tangent frames with native captured results for bind pose, translation, rotation and signed/nonuniform scale. Draw two different poses sharing one mesh in a native submission on each backend. Inspect per-instance coverage and fail if the harness renders an empty or static substitute.
- [ ] **M17-02.5 · Draw with production materials.** Use native LOD0 in main viewport and Preview. Verify textured submeshes and normal orientation, opaque/cutout/transparent route and unsupported material diagnostics. Verify authored slots after save/reopen.
- [ ] **M17-02.6 · Match surface and shadow coverage.** Read surface depth/normals and shadow depth for a moving actor, including a cutout, cascaded and local shadows, an off-camera caster and disabled shadows. Compare the affected silhouettes and expected alpha holes.
- [ ] **M17-02.7 · Match current and historical pose.** Compare an intermediate presentation pose and entity transform against consecutive simulation commits. Test a stationary camera with moving bones, camera motion with static pose, skipped render frames and same-size independent views. Read vectors and validity from compatible submitted samples. Verify rebind, seek, cut, teleport and device reset invalidate only incompatible history.
- [ ] **M17-02.8 · Match presented-frame picking.** Select overlapping independently posed actors, including animated pixels outside bind silhouettes and alpha-cutout holes. Verify stale/resize/scene and reused-ID cases. Show picking and capture leave animator time unchanged.
- [ ] **M17-02.9 · Verify raster captures and RT coverage.** Observe moved skeletal silhouettes in supported planar/probe captures and track pose ownership across scheduled faces. Test RT Off, ray shadows/reflections when eligible and raster fallback for unavailable deforming coverage. Document other capture policies, including DDGI.
- [ ] **M17-02.10 · Verify capability and recovery.** Regress supported OpenGL color/shadows. On both native backends exercise invalid packets, palette/descriptor pressure, failed recording/submission, replacement, teardown, Preview resize and epoch change. Trace diagnostics and retained leases until completion.

### M17-03 · Clip Studio and Preview

- [ ] **M17-03.1 · Connect Editor entry points.** Open from Content Browser and the Inspector, or create an editable copy. Assign a compatible rig/clip and report incompatible selection. Verify a dirty clip survives unrelated scene selection. Preserve imported source bytes.
- [ ] **M17-03.2 · Isolate Preview and transport.** Use a private scene, view and animator. Test Play/Pause, Step, loop, seek and camera interaction. Compare samples to the CPU reference and verify the live/Edit/Play animator time, state and transform remain independent.
- [ ] **M17-03.3 · Author keys and inspect values.** Exercise per-joint TRS tracks, key/range and box selection, snapped/free moves, insert/value edit/delete and copy/paste. Test time collisions, invalid quaternion or incompatible paste. Preserve unrelated keys and marker values. Save, reopen and resample the changed clip.
- [ ] **M17-03.4 · Verify history and dirty lifecycle.** Check one undo command per drag, branch-after-save, selection and no-op rules. Test failed save/read, Reload, Save As and save/discard/cancel on close or project switch. Verify failed candidates retain valid Preview and working states.
- [ ] **M17-03.5 · Synchronize hierarchy, overlay and selection.** Pick a moving joint and reveal its track. Exercise zero-length bones, near clipping, resize, overlay priority and input conflicts with camera/gizmos. Verify cleanup and reopen.

### M17-04 · Animator Controller and blend spaces

- [ ] **M17-04.1 · Compile typed definitions and isolated instances.** Persist state/edge IDs, entry, Float/Int/Bool/Trigger defaults and typed overrides. Reopen and rename by ID. Exercise unknown type, missing clip, rig mismatch and invalid conditions. Verify Scene and Preview have independent runtime values.
- [ ] **M17-04.2 · Verify transitions and 1D.** Execute endpoints and changing-duration samples, stable priority ties, grouped typed conditions, atomic Trigger consumption, exit and loop crossings, zero-duration transitions and fade completion. Cover large steps, pause/reset and bounded work using an expected state/phase trace. Regress existing typed parameter callers.
- [ ] **M17-04.3 · Implement and verify 2D.** Round-trip stable point IDs, X/Y coordinates, typed parameters and clip references. Assert barycentric interior weights, edge/exterior projection, single-point and collinear cases, duplicate rejection, stable ties and continuous finite normalized poses. Verify phase through parameter changes, blend duration changes and crossfades.
- [ ] **M17-04.4 · Author visual state and blend graphs.** Add/delete/rename/connect states and edges, author conditions and entry, edit 1D/2D points, save layout and inspect live state/fade/weights. Prove the UI edits feed the production evaluator. A failed compile reports the field and keeps the previous usable Preview.
- [ ] **M17-04.5 · Verify controller assignment and reuse.** Assign, reopen, duplicate and Make Unique through the Editor. Preserve compatible authored overrides by ID, reset declared transient state and verify behavior and referenced clips after export.
- [ ] **M17-04.6 · Compile a shared motion program and isolate per-instance evaluation.** Run a minimal single-state clip program through the reusable CPU evaluator. Verify separate sampling and progression, shared immutable compiled data, isolated instance state and bounded reusable pose scratch. Reject invalid motion dependencies. Confirm layout-only edits preserve the compiled semantic revision.
- [ ] **M17-04.7 · Author and evaluate sync markers.** Preserve stable marker identities through save, reopen, reimport and compatible editing. Trace phase alignment through Blend Space duration changes, marker boundaries and wrap. Verify deterministic normalized-phase fallback for clips without compatible markers. Sampling sync markers leaves the gameplay queue untouched.

### M17-05 · Sockets, layers, actions and gameplay events

- [ ] **M17-05.1 · Persist attachments.** Author socket ID/name, joint and local transform, and attach a prop with an offset. Round-trip Scene/assembly. Verify duplicate labels remain addressable, missing joints diagnose and cycles or cross-scene references are rejected.
- [ ] **M17-05.2 · Update attachments from final joint transforms.** Compare prop transforms on two actor instances before and after pose/world-transform changes. Verify scene teardown, mount/unmount and unresolved socket behavior. Observe updated attachment origins before particle consumption.
- [ ] **M17-05.3 · Author and evaluate additive layers.** Persist ordered layers, stable joint masks, reference pose and weight. Verify TRS deltas, zero and full weights, overlapping layers and masked-out joints during locomotion. Invalid masks or reference poses report field-level diagnostics.
- [ ] **M17-05.4 · Verify Editor/Runtime parity.** Author and preview a layer, undo/redo, reopen and compare pose in Scene and Runtime. Verify another instance with the same definition retains independent layer state.
- [ ] **M17-05.5 · Author masked override and one-shot actions.** Persist a named action on a masked layer. Play, interrupt, reject, cancel and complete it through the runtime command boundary. Verify playback-handle and generation checks, finite fades, local TRS composition and uninterrupted locomotion beneath the action. Round-trip the definition and inspect action state in the private Preview.
- [ ] **M17-05.6 · Author and dispatch gameplay markers.** Persist marker ID, time, type and payload. Compare committed occurrences across explicit start, terminal keys, loops, crossfade and opted-in layers. Verify ordering, bounded overflow, no duplicate emission for repeated views, and generation invalidation on pause/rebind/stop as specified by the event contract. Seek and Preview must not dispatch to the active game Scene.

### M17-06 · Root motion and procedural foot IK

- [ ] **M17-06.1 · Extract opt-in motion.** Compare known translation/yaw deltas with one and multiple loops and during crossfade. Check in-place defaults, base motion authority, pose compensation, overlay isolation and invalid/unavailable data behavior.
- [ ] **M17-06.2 · Route motion through the movement owner.** Emit at post-physics tick N and consume once in FixedControls N+1. Verify input precedence, collisions, deterministic fixed-step behavior and invalidation on pause, reset, teleport, rebind and stop. Prove neither render nor local skeleton transforms apply the same displacement again.
- [ ] **M17-06.3 · Solve foot contacts with Scene physics.** Author a valid two-bone chain and contact settings. Use real Scene/Physics queries and verify level ground, step/slope, unreachable target, near-straight pose, airborne/miss and missing service. Measure contact/ankle error and bend direction, keep finite fallback poses and confirm physics body movement stays with its owner.
- [ ] **M17-06.4 · Verify final-pose ordering.** Trace override/additive composition, root compensation, IK, pose commit, sockets, event delivery and particle origins for independently playing actors. Verify a visible attachment follows the same interpolated presentation sample as the actor. Compare render/sample results against that trace and demonstrate repeated views do not reapply simulation.

### M17-07 · Packaging, product and final review

- [ ] **M17-07.1 · Produce the reference scene and assembly.** Save, reopen and instantiate the reference character with controller, socket prop, additive motion and IK. Replay locomotion with its masked reload and gameplay event, plus an opt-in root motion segment. Compare authored definitions, runtime binding, attachment pose and fixed-tick trace.
- [ ] **M17-07.2 · Export and run without source inputs.** Cook approved asset versions with existing export. Trace required dependencies through mounted VFS and run detached Runtime with strict native backend. Test missing-required and optional fallback outcomes with disposable copies. Hand M20 the edge list and reproducible packaged fixture.
- [ ] **M17-07.3 · Verify the integrated matrix.** Execute CPU, native GPU, product, interactive UI and OpenGL gates in Section 4. Include Play/Stop, project and scene changes, reimport/failed reload, resize and supported feature-off paths.
- [ ] **M17-07.4 · Measure and hand off.** Record evaluation and palette cost, bytes, allocation, throughput and native raster cost at the frozen fixture scales. Provide M21 the mesh/pose/bound/coverage/picking reference and M22 the tick and pose invariants. Publish the tested support and fallback matrix.
- [ ] **M17-07.5 · Close on independent evidence.** Map every requirement to production entry point and verified result on final source, distinguish self-verification and independent evidence, resolve blocking findings and recheck fixes under the [shared protocol](../../agents/WORKING_PROTOCOL.md). Record unavailable checks as `NOT RUN` and keep their gates open.
- [ ] **M17-07.6 · Prove detached Runtime early.** Export the native two-actor scene with a minimal compiled controller and native rig, clip and geometry products. Mount and run it outside the source checkout with FBX/glTF unavailable. Inspect animation from the strict DX12 and Vulkan routes. Save this fixture for regression during Studio and gameplay work.

## 4. Verification and evidence

### 4.1 Required fixtures

| Fixture | Measurable output |
| --- | --- |
| Analytic rig | Root and child joints with known bind/sample transforms and unkeyed components. CPU vertices and tangent frames, pose tolerance and exact state/phase event order. |
| Real sources and malformed inputs | Reuse the [FBX/glTF fixtures](../../../Tests/Fixtures/Animation) for basis, helper, signed scale, supported interpolation, precision and rejected data. Add a minimal Editor import/reimport source with transitive buffers and multiple material submeshes. |
| Native two-actor scene | Shared mesh with independently posed actors, textures, occlusion, shadows and planar/probe coverage. Capture deformed silhouette, depth/normals, ID and motion. Choose numeric comparison points away from ambiguous edge pixels. |
| Reference character and action | 1D/2D control and marker trace, masked reload over locomotion, one gameplay occurrence, socket prop, obstructed root locomotion and a foot on a step. Save the project and replay in Preview, Editor Play, loose Runtime and detached packaged Runtime. Preview exposes the effects as local diagnostics for authoring. |
| Stress and failure variants | Workloads of 1, 32 and 128 actors, a 128-joint fixture and a 129-joint rejection. Include overlapping frames, stale results, failed upload/read, rig replacement and device resets. Publish measured budgets and thresholds before final candidate scoring. |

### 4.2 Execution matrix

| Gate | Required evidence |
| --- | --- |
| CPU | Build/run `AxleAnimationUnitTests` in Debug and Release, including the failure probe. Rebuild applicable Scene/shared phase, serialization and product targets. Use actual production calls and always-active assertions. |
| Native physical GPU | D3D12 and Vulkan in Debug and Release with strict backend and validation. Register a selectable skeletal suite by A2.2 for layout, native deformation, render passes and resource lifetime. Archive adapter/driver, effective routes, pixel/readback artifacts and error counters. Setup-only, fallback backend or empty view cannot pass. |
| Product and compatibility | Editor scene and Preview, Play/Stop, Runtime loose and detached packaged on both native backends. Record supported OpenGL color/shadow behavior separately. Launch packaged Runtime outside the checkout with only published artifacts. Prove an animated render and verify the raw source inputs are absent. |
| Interactive Editor | Exercise import, Content Browser/Inspector, clips, controller, layers, actions and markers, bone selection, focus, save/reopen, error/discard and resize. Use the [Editor visual capture procedure](../../agents/EDITOR_VISUAL_CAPTURE.md) and record actual interactions and output. |
| Comparison policy | Compare IDs, dependency edges and discrete outcomes exactly. For meter-scale analytic fixtures, start with position error ≤ 0.0001 m, unit-normal angular error ≤ 0.1°, normalized blend weight sum error ≤ 0.00001 and interior motion error ≤ 0.25 output pixels. Record and freeze any fixture-specific tolerance change before candidate scoring. Compare images in a declared space at the same LOD0, pose, camera, light, exposure, target format, AA and warmup. Check silhouette and alpha coverage separately. |
| Lifetime and scale | Verify bounded descriptor, palette and mesh-generation use over repeated Preview close/reload, overlapping frames and at least 1,000 integrated stress frames per native backend. Record workload/counters, CPU/GPU timing percentiles and cold upload/compile versus steady state. |

Run the current CPU baseline from a configured multi-configuration test build.

```powershell
cmake --build build --config Debug --target AxleAnimationUnitTests
ctest --test-dir build -C Debug -R '^Animation(UnitTests|HarnessFailure)$' --output-on-failure
cmake --build build --config Release --target AxleAnimationUnitTests
ctest --test-dir build -C Release -R '^Animation(UnitTests|HarnessFailure)$' --output-on-failure
```

Use the [Runtime export verifier](../../../scripts/verify_runtime_export.py) for the established detached-launch pattern. Record exact commands for the new skeletal suite and package case as the owning checkpoints register them.

### 4.3 Handoff and closure

Record base/head, requirement IDs, production entry points, exact commands, configurations/exit codes, artifact locations, effective routes and open checks for each committed checkpoint. Use `PASS`, `MISSING`, `FAIL` and `NOT RUN` in the final `docs/reviews/` requirement table. Close M17 when all mandatory leaves pass on final source with independent review, correction rechecks and actual native, interactive and packaged product evidence.
