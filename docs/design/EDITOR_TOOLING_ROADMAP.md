# Editor Tooling Improvement Roadmap

Status: **proposed work**, reviewed 2026-11-01; Phase 39 VFX sequencing updated 2026-11-02. This consolidates durable ideas from older visual-editor and parity specifications. It is not a list of implemented capabilities or a fixed phase schedule.

Current material/UI behavior is documented in the [material guide](../guides/MATERIALS_AND_SHADERS.md) and [UI guide](../guides/UI.md). Address dependency and lifecycle issues in the [architecture assessment](../architecture/ASSESSMENT.md) alongside feature development.

## 1. Shared authoring foundations

Provide reusable preview camera/lighting controls, document dirty/save/reload behavior, undoable commands, asset pickers and structured diagnostics. Keep preview state separate from game state and keep editor targets out of runtime dependencies.

Acceptance: opening, editing, saving, reopening and undoing a representative asset behaves consistently across tools. Closing a preview releases resources safely. Runtime-only builds do not link editor panels.

## 2. Materials and shader authoring

Current foundations include material descriptors, inspection/preview and a wired HLSLStudioPanel. Future work should establish these contracts:

| Work | Required design | Acceptance evidence |
|---|---|---|
| Consistent live preview | Reusable viewport with orbit/zoom, mesh and environment selection | Changes are visible with controlled preview settings on selected backends |
| Reflection-driven controls | Stable shader-property IDs, types, defaults, ranges and storage mapping | Reordered/added properties round-trip without losing unrelated values |
| Safe shader reload | Source/include dependency tracking, invalidation and layout compatibility | Compile failure keeps a usable last-known-good pipeline; successful compatible updates reach the intended scene/preview |
| Packed texture inputs | Explicit ORM channels, color-space rules and fallback handling | Descriptor/UI/shader results agree with known channel test textures |
| Advanced surface models | Define schema, shading equations and capability fallback before adding controls | Clearcoat/anisotropy or other features have representative visual and persistence checks |
| Editor diagnostics | Structured file/line/stage messages, source navigation and include errors | Compiler errors navigate to the correct source location; success identifies the backend/stages actually compiled |

Comparisons with commercial tools are inspiration, not a claim of feature parity. Prioritize complete end-to-end behavior over a large collection of controls.

## 3. VFX authoring

Build on ParticleSystem, emitter stacks, curves/gradients and VFXEditorPanel. Improve multi-emitter selection/enable/ordering, deterministic preview reset, module discoverability and asset persistence. Define runtime ownership for child systems and sub-emitter triggers.

Phase 39 has been **reopened** after inspecting its actual editor/runtime paths.
The existing panel and Step 102 CPU checks do not establish a complete VFX asset
workflow. Follow the [detailed Phase 39 completion plan](VFX_PHASE_39_COMPLETION_PLAN.md)
for the A-J work packages, stable authored-definition/runtime-instance boundary,
atomic sub-steps and CPU/GPU/UI matrix. First isolate preview from scene state and
make unchanged selection idempotent; then complete multi-emitter persistence,
playback, real RHI rendering, authoring/resources/undo and advertised module paths.

Acceptance: preview controls and scene playback use compatible configuration; saved assets reproduce emitter behavior; births/collisions/deaths and sub-emitter recursion have explicit limits/lifetimes. Separate CPU simulation checks from backend draw checks.

## 4. UI authoring and presentation

Finish an explicit runtime UI submission path rather than equating CPU geometry counts with rendered output. Define upload/binding/batching, clipping, text/font resources and teardown. Make input order relative to layout/reload consistent.

Extend datasource scoping with a documented lifetime and naming model; avoid publishing multiple entities into colliding global keys. Keep one scene-level canvas entity with an internal document tree. Improve datasource/asset selection and bidirectional document editing without duplicating the same state in unrelated widgets.

Acceptance: two independent entities drive two canvases without cross-talk; resizing and DPI changes preserve layout/input behavior; document edits round-trip; a standalone render test proves actual visible canvas submission.

## 5. Animation authoring

Runtime skeleton/clip/pose/state-machine types are foundations for tooling, not evidence of a completed animation editor. Candidate tools include:

- Timeline/dopesheet with transport, frame/time scrub, bone channels and keyframe edit commands.
- Skeleton viewport overlay with joint/bone selection synchronized with the timeline.
- State/transition graph with parameter and crossfade inspection.
- Blend-space authoring, with 1D support verified before proposing 2D workflows as implemented.
- Animation event markers with explicit script callback timing and persistence.

Acceptance: an authored clip/state-machine asset survives save/load; timeline, bone selection and runtime evaluation agree; undo/redo works; playback and events have defined timing semantics. Do not document 2D blend spaces/events as completed merely because they appear in a roadmap.

## 6. Sequencing and verification

1. Stabilize common ownership, save/undo and preview interfaces.
2. Complete runtime/editor dependency separation and required rendering lifecycles.
3. Deliver one complete asset workflow at a time.
4. Add focused persistence, lifecycle and interaction checks for that workflow.
5. Validate visual behavior on declared backends and record the actual configuration/results.

Use the current test runner/source names when implementing tests. Historical phase-step numbers are not API contracts and should not be copied into new acceptance plans. No feature implementation, test-pass result or benchmark is supplied by this roadmap.
