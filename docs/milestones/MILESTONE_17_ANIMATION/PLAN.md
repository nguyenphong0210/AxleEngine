# Milestone 17 — Skeletal Animation Studio and Runtime Plan

**Status** — Proposed. Release implementation by checkpoint after M16 core acceptance (`RT-08D`).
**Phase 46** — Assets and rendering (46.1), studio and runtime features (46.2), product acceptance (46.3).
**Architecture** — [Animation data, evaluation and rendering](ARCHITECTURE.md).
**Handoffs** — [M20 packaging](../MILESTONE_20_COOKER/PLAN.md), [M21 skeletal LOD](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md), [M22 animation URO](../MILESTONE_22_SIMULATION_SCALE/PLAN.md).

## 1. Starting point and deliverable

Reuse the FBX/glTF importers, skeletal codecs, validated meshes, controller evaluator, 1D blend space and transactional clip/controller models in [`Engine/Animation`](../../../Engine/Animation). Historical [foundation corrections](../../archive/design/reports/ANIMATION_FOUNDATION_FINAL_CORRECTIONS.md) and the [Phase 40 plan](../../archive/design/deferred/ANIMATION_PHASE_40_COMPLETION_PLAN.md) identify earlier fixtures and open integration work. Reproduce the CPU baseline on the released source. The modern render service currently reports skeletal skinning as unsupported. Build the native draw path before putting a studio preview on top of it.

Deliver one character authored from source through standalone Runtime, including clip editing, a visual controller, 1D/2D blending, sockets, two-bone foot IK and masked additive layers. Keep skeletal assets cookable through existing export at each relevant step. An opt-in root motion channel has a separate simulation contract in B4 so it can be tested independently from authored pose playback.

## 2. Execution and acceptance checklist

| Checkpoint | Implementation and independently reproducible gate |
| --- | --- |
| **M17-A1 · Asset contract** | Import one procedural rig and real FBX and glTF clips through the production Editor import path. Validate hierarchy, mesh weights, inverse binds, sampling and text/binary round trips. Reject malformed and unsupported combinations before publication. |
| **M17-A2 · Native skinning** | Upload immutable skeletal mesh generations and isolated 128-joint palette ranges. Compare native deformation to a CPU reference and exercise two animated instances in one submission on D3D12 and Vulkan. |
| **M17-A3 · Pass integration** | Wire depth, shadow, motion, picking and supported raster capture consumers to the same pose generation. Record effective paths and the M16 ray tracing policy for skeletal content. |
| **M17-B1 · Clip Studio** | Wire real clip documents into the Editor, a private preview, timeline/dope sheet, joint overlay, complete save/discard flow and undo/redo. |
| **M17-B2 · Controller Studio** | Connect typed parameters, transitions and visual state graph to the production evaluator. Deliver 2D blending and exercise both 1D and 2D phase continuity. |
| **M17-B3 · Attachments and pose layers** | Author sockets and standardized bone masks. Verify attachments and additive reference-pose behavior with independently playing instances. |
| **M17-B4 · IK and root motion** | Solve foot contacts with existing physics queries and implement explicit root motion authority. Test step crossings, miss/reset behavior, fixed-tick determinism and disabled behavior. |
| **M17-C1 · Product and acceptance** | Save/reopen character scene and assembly, run loose and detached packaged Runtime, compare results across native backends and exercise compatibility and failure routes. Complete one consolidated review on the final source. |

A1–A3 implement Phase 46.1, B1–B4 implement Phase 46.2, and C1 closes Phase 46.3.

- [ ] **M17-01 · Rig and clip import (A1).** Keep topological joint order and bind TRS for unkeyed components. Cover FBX STEP/linear/sampled curves, glTF STEP/LINEAR/CUBICSPLINE, helper nodes, signed transforms and unsupported multi-skin selection. Import errors retain the previous generation. Validate scene/assembly dependency extraction for skeletal geometry, rig, clips and controller.
- [ ] **M17-02 · Render path (A2–A3).** Check different poses of shared geometry, bind pose, unsupported palette size, new and previous pose, multiple views, shadow silhouette and entity ID under animated motion. Force overlap and resource replacement so completion-based retirement is observable. Compare the same authored scene against existing OpenGL support where applicable.
- [ ] **M17-03 · Clip editing (B1).** Create or open an editable clip, select joints/keys, scrub, insert and edit local TRS, box-select, copy/paste/delete, undo/redo and save/reopen. Group drags into transactions. Failed read/save, declined discard and preview close preserve the appropriate working/committed state. Preview playback never advances the edited scene.
- [ ] **M17-04 · Controller and blend spaces (B2).** Create states and transitions, edit Float/Int/Bool/Trigger parameters and typed, persisted conditions. Verify priorities, exit phase, crossfades, trigger consumption and save/reopen. Test 1D endpoint/duplicate rules and 2D points, edges, exterior projection and degenerate cases with finite normalized weights and phase continuity under parameter changes.
- [ ] **M17-05 · Sockets and layering (B3).** Attach a prop by stable joint/socket identity with local transform. Round-trip overrides and indicate missing joints. Blend local-space additive deltas against a declared reference pose using a persisted mask. Confirm masked actions preserve base locomotion on excluded joints.
- [ ] **M17-06 · Foot IK and root motion (B4).** Apply an analytical two-bone solve using fixed-tick physics ground queries. Cover sloped surfaces, unreachable contacts, airborne/reset behavior and blended weight. Verify default in-place playback, opted-in loop delta extraction and a single application of movement via the locomotion owner.
- [ ] **M17-07 · Product integration (C1).** Ship the rig/clip/controller/attachments through existing `Data.axlepak` export and trace authored references for M20. Exercise Preview, Editor scene, Play/Stop and loose/packaged Runtime, invalid inputs, supported fallback and device/resource teardown.

## 3. Evidence and exit

Rebuild `AxleAnimationUnitTests`, relevant renderer and picker suites, `AxleEditor` and `AxleRuntime` in Debug and Release. Extend the existing fixture suites and use actual native readback for the skeletal path, including the animated pick ID and independent palettes. Use a fixed tick trace for discrete pose/evaluator checks and a recorded numeric tolerance for floating-point samples. Capture the same camera, clip time and render settings for equivalent raster comparisons.

For the existing CPU baseline, build `AxleAnimationUnitTests` and run `ctest --test-dir build -C Debug -R '^Animation(UnitTests|HarnessFailure)$' --output-on-failure`. Repeat in Release. The new native skinning fixture must have its own selectable suite with strict backend and failure propagation.

Record strict physical D3D12 and Vulkan visual evidence and the OpenGL compatibility result. Include a windowed pass through import, clip/controller editing, undo/redo, save/reopen and dirty/invalid document handling. A Preview render and a separate Runtime rendered frame both require actual execution evidence. Record exit codes, source/backend/build identity, artifacts, declared limitations and dispositions for every requirement in `docs/reviews/` before closing M17.
