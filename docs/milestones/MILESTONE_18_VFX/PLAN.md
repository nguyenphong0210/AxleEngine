# Milestone 18 — Visual FX Studio and GPU Particles Plan

**Status** — Proposed. Release implementation by checkpoint after M17 acceptance.
**Phase 47** — CPU authoring and draw modes (47.1), GPU foundation and graph (47.2), product acceptance (47.3).
**Architecture** — [Effect data, simulation and render paths](ARCHITECTURE.md).
**Handoffs** — [M20 packaging](../MILESTONE_20_COOKER/PLAN.md), [M21 compute and indirect geometry](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md).

## 1. Starting point and deliverable

Reuse `VFXEditorDocument`, `ParticleEffectSerializer`, CPU particle pools, scene physics queries, RHI billboard drawing and the private VFX preview. The [Phase 39 review](../../archive/design/reports/VFX_PHASE_39_REVIEW_AND_ACCEPTANCE_REPORT.md) and its [feature backlog](../../archive/design/deferred/VFX_UNITY_FEATURE_BACKLOG.md) supply historical evidence and fixture ideas. Reproduce relevant current CPU and native checks. Deliver authoring and runtime playback for the same multi-emitter effect, including new CPU modules and draw modes, along with an explicitly supported GPU graph subset.

Use native `.axleasset` with `ParticleEmitter` kind for new effects. Align Editor dialogs, registry discovery, embedded identity, scene/assembly bindings and cooker enumeration. Preserve supported current asset content and report the disposition of each existing entry point. Asset migration tooling has its own release scope.

## 2. Execution and acceptance checklist

| Checkpoint | Implementation and independently reproducible gate |
| --- | --- |
| **M18-A1 · Asset and Editor baseline** | Verify project discovery, document lifecycle, isolation and fixed-step CPU playback on current source. Align save/open and dependency enumeration. |
| **M18-A2 · Parameters and motion** | Add reusable parameter modes and moving emitter modules. Verify deterministic persisted playback, scrubbing and equivalent fixed-step traces. |
| **M18-B1 · Events and collision** | Bind real scene physics, stable sub-emitter references and bounded gameplay event delivery. Test event limits and instance teardown. |
| **M18-B2 · Ribbons, mesh and flipbook** | Render true ribbons and instanced meshes, preserving true billboards and existing Mesh Surface emission. Verify linear flipbook blending and stable sorting. |
| **M18-C1 · Shared native indirect foundation** | Verify compute-written indexed arguments/counts, barriers, ownership, overlap and readback on D3D12/Vulkan. Deliver the backend-neutral contract and fixtures for M21. |
| **M18-C2 · GPU simulation** | Implement bounded spawn, update, compaction, optional bitonic sort and indirect submission on the supported capability tier. Read back counts and output invariants. |
| **M18-C3 · Visual Effect Graph** | Author a supported GPU subset through a visual graph. Validate typed edges, compile asynchronously, keep the last valid generation and test explicit CPU/failure routes. |
| **M18-D1 · Product and acceptance** | Verify complete Editor → saved asset → Scene → Runtime workflows, including packaging, performance and fallbacks. Complete consolidated final review. |

A1–B2 implement Phase 47.1, C1–C3 implement Phase 47.2, and D1 closes Phase 47.3.

- [ ] **M18-01 · Documents and preview (A1).** Verify standalone multi-emitter persistence, dirty guards, transactions, reference vs Make Unique, save/reopen and dependency tracing. Assert preview seeks and edits leave scene clocks, pools and RNG state intact.
- [ ] **M18-02 · Parameter modes (A2).** Support Constant, Two Constants, Curve, Two Curves and color/alpha gradients with deterministic sampling. Validate units, domains and references. Round-trip inactive settings and compare fixed-seed distributions when changing an unrelated module.
- [ ] **M18-03 · Emitter motion (A2).** Implement rate over distance, start delay, bounded prewarm, per-axis velocity/force curves, orbital motion and emitter velocity inheritance. Cover stationary/moving/teleport cases, loop boundaries, pause/seek and local/world transforms.
- [ ] **M18-04 · Collision and events (B1).** Test world floor and authored colliders, scene geometry queries, birth/collision/death links, cycle rejection, entity destruction and deterministic event/spawn budget limits. Expose a bounded runtime gameplay event path with safe listener lifetime.
- [ ] **M18-05 · Ribbon draw (B2).** Bound per-particle history, retain generation-safe identity, support stretch/wrap UVs and retirement on reset/death. Verify pool reuse, bounds, transforms and native readback.
- [ ] **M18-06 · Mesh particles and flipbook (B2).** Verify indexed mesh instancing, material overrides, orientation, bounds and transparent sort. Verify interpolation between neighboring flipbook frames and boundary behavior on the actual shader.
- [ ] **M18-07 · Compute-to-indirect reference (C1).** Verify native argument/count layout, capacity and zero cases, unsupported count fallback, graph barriers, completion and multiple in-flight frames. Keep a small indexed fixture usable by M21 independently of the particle graph.
- [ ] **M18-08 · GPU simulation (C2).** Verify spawn capacity and overflow counters, bounded compaction, reuse/reset, bitonic sorting for eligible transparent draws, indirect counts, frame/device retirement and a declared CPU or unsupported route. Reproduce one GPU effect on each native backend.
- [ ] **M18-09 · Visual Effect Graph and product (C3–D1).** Persist graph topology and typed input values, reject invalid or unsupported nodes, map diagnostics to nodes, retain the last valid active generation and verify the declared GPU and CPU feature mappings. Exercise authored asset identity and transitive resources through current export. Freeze support and workload budgets before acceptance runs.

## 3. Evidence and exit

Rebuild `AxleVFXUnitTests`, `AxleVFXGPUTests`, `AxleEditor` and `AxleRuntime` in Debug and Release. Use real shaders, mesh/materials/texture resources, bounded scenes, native readback and GPU validation. Test Editor operations and isolated preview across D3D12/Vulkan, loose and detached package execution, with OpenGL on its declared CPU subset. Record fixed-step CPU event traces, GPU count and range invariants, effective route diagnostics and comparable render measurements. Use equivalent image tolerances and statistical comparisons as appropriate for GPU particles.

For the existing CTest baseline, configure with `AXLE_ENABLE_VFX_GPU_TESTS=ON`, build the VFX test targets, then run `ctest --test-dir build -C Debug -R '^(VFXUnitTests|VFXGPU_D3D12|VFXGPU_Vulkan)$' --output-on-failure`. Repeat in Release. Confirm both GPU cases were registered and executed on the requested physical backend.

Record harness failure propagation, warnings, source/build/backend identity, visual evidence and requirement dispositions in `docs/reviews/`. M18 hands M20 an exported multi-emitter effect including the dependencies it actually uses and hands M21 the native indirect fixture and capability decisions.
