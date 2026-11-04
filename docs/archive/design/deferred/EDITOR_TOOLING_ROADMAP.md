# Editor Tooling Improvement Roadmap

> **Deferred reference:** outside the active architecture cleanup. Follow the
> [optimization roadmap](../../../ROADMAP.md); embedded prompts and old priorities
> are historical guidance, not a current assignment. Recheck APIs before resuming.

Status: **proposed work**, reviewed 2026-11-01; Phase 39 VFX sequencing updated 2026-11-02; Phase 40 animation handoff updated 2026-11-03. This consolidates durable ideas from older visual-editor and parity specifications. It is not a list of implemented capabilities or a fixed phase schedule.

Current material/UI behavior is documented in the [material guide](../../../guides/MATERIALS_AND_SHADERS.md) and [UI guide](../../../guides/UI.md). Address dependency and lifecycle issues in the [architecture assessment](../../../architecture/ASSESSMENT.md) alongside feature development.

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
The [Gemini coding guide](VFX_PHASE_39_GEMINI_IMPLEMENTATION_GUIDE.md) expands those
contracts into proposed C++ types, implementation flows, test fixtures and a
code/CPU-test handoff; physical GPU/UI acceptance remains an external check.

Acceptance: preview controls and scene playback use compatible configuration; saved assets reproduce emitter behavior; births/collisions/deaths and sub-emitter recursion have explicit limits/lifetimes. Separate CPU simulation checks from backend draw checks.

## 4. UI authoring and presentation

Finish an explicit runtime UI submission path rather than equating CPU geometry counts with rendered output. Define upload/binding/batching, clipping, text/font resources and teardown. Make input order relative to layout/reload consistent.

Extend datasource scoping with a documented lifetime and naming model; avoid publishing multiple entities into colliding global keys. Keep one scene-level canvas entity with an internal document tree. Improve datasource/asset selection and bidirectional document editing without duplicating the same state in unrelated widgets.

Acceptance: two independent entities drive two canvases without cross-talk; resizing and DPI changes preserve layout/input behavior; document edits round-trip; a standalone render test proves actual visible canvas submission.

## 5. Animation authoring

Runtime skeleton/clip/pose/state-machine types are foundations, not a completed
animation editor. Follow the [Phase 40 completion plan](ANIMATION_PHASE_40_COMPLETION_PLAN.md)
for the audited source gaps, A0-K3 packages and dependencies. The
[Gemini coding guide](ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md) specifies
ownership, asset formats, timing/conditions, 1D/2D blending, event delivery,
RHI skinning, dopesheet/graph commands and concrete CPU/GPU/UI fixtures.

First fix partial bind-pose sampling, topology and shared mutable animator state;
then complete durable assets and the missing DX12/Vulkan skeletal draw path before
building the timeline and graph workflow. The first reviewable slice is import ->
save/load -> independent two-state animator -> private RHI preview and scene.
Finish authoring, event/script integration, assembly and packaged Runtime paths
with separate implementation and external acceptance evidence.

Scope: single-layer Clip/1D/2D controllers, local TRS key editing, skeleton selection
and typed event markers. Root motion, foot IK, retargeting, layers/masks and full
commercial-engine parity are excluded. The baseline RHI renderer has no complete
shadow-map pass; record that renderer limitation rather than claiming skeletal RHI
shadow support. Phase 38.3/39 manual acceptance remains separately open.

Acceptance: authored clip/controller assets survive save/load and scene/assembly/
package reuse; timeline, bone selection and runtime poses agree; independent
instances, history, event timing and physical backend pixels have meaningful checks.
Passing CPU tests does not establish interactive or GPU acceptance.

## 6. Sequencing and verification

1. Stabilize common ownership, save/undo and preview interfaces.
2. Complete runtime/editor dependency separation and required rendering lifecycles.
3. Deliver one complete asset workflow at a time.
4. Add focused persistence, lifecycle and interaction checks for that workflow.
5. Validate visual behavior on declared backends and record the actual configuration/results.

Use the current test runner/source names when implementing tests. Historical phase-step numbers are not API contracts and should not be copied into new acceptance plans. No feature implementation, test-pass result or benchmark is supplied by this roadmap.
