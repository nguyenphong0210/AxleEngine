# Milestone 18 — Visual FX Studio and GPU Particles Plan

**Status** — Proposed. Release implementation by checkpoint after M17 acceptance.

**Phase 47** — CPU foundation and production rendering (47.1), native GPU simulation and graph (47.2), product acceptance (47.3).

**Architecture** — [VFX runtime, assets, rendering and authoring contracts](ARCHITECTURE.md).

**Dependencies and handoffs** — [M17 animation events and sockets](../MILESTONE_17_ANIMATION/ARCHITECTURE.md#4-simulation-physics-and-presentation) · [M20 cooker](../MILESTONE_20_COOKER/PLAN.md) · [M21 indirect geometry](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md).

## 1. Product outcome

An artist can build a multi-emitter effect in VFX Studio, preview and scrub it, save a native asset, reuse it in a Scene or Assembly and ship it in a detached Runtime package. Two instances can share a definition while owning different transforms, parameters, seeds and playback state. Gameplay can play an effect at a world transform or an M17 socket, send an event, adjust exposed parameters and stop or release the instance. An operator can see the effective simulation route, culling, capacity use and dropped work.

D3D12 and Vulkan deliver CPU-simulated billboards, ribbons and instanced meshes, and the GPU billboard tier defined in Architecture. The CPU tier handles authoritative scene physics and gameplay events. A single effect can use CPU and GPU emitters with explicit supported interactions. Publish the OpenGL CPU billboard compatibility tier and the behavior of unsupported requests.

## 2. Starting point and refactor boundary

- Reuse `VFXEditorDocument`, the effect codec and atomic identity writer, CPU pool and emitter math, scene physics queries, private preview, RHI billboards, flipbook sampling and native readback harness. Reproduce current baseline results. The [Phase 39 review](../../archive/design/reports/VFX_PHASE_39_REVIEW_AND_ACCEPTANCE_REPORT.md) and [feature backlog](../../archive/design/deferred/VFX_UNITY_FEATURE_BACKLOG.md) provide historical evidence and fixture ideas.
- A1 owns the cutover from parallel primary-emitter and stack state to one authored binding and one instance owner. Align Scene, Editor Play, the Edit viewport, the private preview and Runtime on one fixed-step evaluator. Give renderer extraction its own read-only output contract in B2.
- Audit Content Browser, Studio, Inspector, plugin authoring, Scene and Assembly codecs, Registry, VFS and Runtime export. New effects use native `.axleasset` with `ParticleEmitter` kind and embedded UUID. Preserve supported current effect content and record each entry point's disposition. Historical-format migration tooling retains its own release scope.
- Start motion modules after the A1 cutover. Start GPU particle work after the native indirect fixture passes. Finish the graph on top of executable CPU and GPU routes so that the graph's support matrix reflects tested behavior.

## 3. Execution checkpoints

| Checkpoint | Requirements | Result and checkpoint gate |
| --- | --- | --- |
| **M18-A1 · Asset and instance foundation** | 01 | Reproduce CPU and native baselines, then cut over identity, VFS, Scene/Assembly and shared tick. Prove detached reference resolution and instance isolation. |
| **M18-A2 · Parameters and motion** | 02, 03 | Add reusable sampling and motion modules. Reopen the authored effect and compare fixed-seed and fixed-step traces. |
| **M18-B1 · Collision, gameplay and events** | 04 | Use scene-owned query and event services, typed instance commands and generation-safe handles. Prove M17 attachment/event ordering and test teardown and overflow. |
| **M18-B2 · Native particle outputs** | 05, 06 | Publish compact draw data and deliver billboard, ribbon and mesh output. Verify per-view sorting, material behavior and flipbook sampling on both native backends. |
| **M18-B3 · Scalability and diagnostics** | 10 | Add bounds, quality tiers, budgets and pooled transient instances. Exercise overload, culling transitions and telemetry with the CPU reference. Extend the same policy to GPU routes in C2. |
| **M18-C1 · Shared native indirect foundation** | 07 | Verify GPU-authored indexed arguments and count modes, barriers, overlap and readback on D3D12/Vulkan. Deliver a VFX-independent fixture and contract for M21. |
| **M18-C2 · GPU simulation** | 08 | Deliver the GPU billboard tier with bounded simulation, transparent sorting and indirect submission. Verify the route, numeric invariants and native lifecycle. |
| **M18-C3 · Visual Effect Graph** | 09 | Author and publish a real multi-emitter graph via typed nodes and shared effect compilation. Verify asynchronous revisions, diagnostics, persistent inputs and route selection. |
| **M18-D1 · Product acceptance** | 11 | Run the complete authored fixture, backend and performance matrices and complete consolidated final review. |

A1–B3 implement Phase 47.1. C1–C3 implement Phase 47.2. D1 closes Phase 47.3.

## 4. Requirement and acceptance checklist

### M18-01 · Asset, document and instance foundation

- [ ] **01.1 · Baseline and entry points.** Record present source and build identity, reproduce the dedicated CPU and native billboard suites, and list supported authoring/load paths and what A1 will consolidate. Record current failures and their resolution.
- [ ] **01.2 · Native identity and dependencies.** Preserve the embedded UUID on save and move. Verify a distinct UUID on Save As to a new file, and deterministic diagnostics for missing, malformed or conflicting identity. Account for root-relative raw resources and typed native references. Run the VFS loader with loose and mounted files and enumerate transitive texture, normal map, material and mesh dependencies, including child effects where supported. Include both Mesh Surface source and render mesh references.
- [ ] **01.3 · Scene and Assembly bindings.** Persist the reference or inline definition with typed overrides using the shared codec. Verify Scene and Assembly save/reopen and instantiation, rename/move by UUID, missing reference diagnostics and detached package loading. In the packaged reference fixture, the mounted asset must supply the definition. Make that fixture fail if the referenced package entry is removed.
- [ ] **01.4 · Authoring and instance isolation.** Verify dirty guards, undo/redo transactions, Reference, Make Unique, Apply and Save As. Use one fixed-step transport contract in Studio, Edit, Play and Runtime. Verify two instances sharing an asset, Play cloning, repeated views, close/reopen and preview seek/edit isolation of scene clocks, particle pools, seeds and event queues.

### M18-02 · Parameter modes and exposed values

- [ ] **02.1 · Deterministic parameter sampling.** Support Constant, Two Constants, Curve, Two Curves and color/alpha gradients. Validate domain, units and ranges. Round-trip disabled settings. Compare per-particle fixed-seed samples and distributions when changing an unrelated module, reordering emitters or reusing pool slots.
- [ ] **02.2 · Instance overrides.** Define stable IDs and types for exposed bool, integer, float, vector and color inputs. Exercise defaults, independent shared-asset overrides, authoring and game-side updates, reset-to-default, renames and invalid overrides. Verify retained compatible overrides and diagnostics for incompatible changes on revision replacement.

### M18-03 · Emitter motion

- [ ] **03.1 · Time and motion modules.** Deliver rate over distance, start delay, bounded prewarm, per-axis velocity/force curves, orbital motion and emitter velocity inheritance. Cover stationary/moving/teleport cases, loop seams, local/world transforms and the difference between emitter and sub-emitter inheritance.
- [ ] **03.2 · Transport and replay.** Compare the same fixed-tick inputs across Runtime and Preview with matching collision inputs. Exercise play, pause, resume, stop-and-drain, stop-and-clear, reset and bounded forward/backward seek, including backlog and repeat seek.

### M18-04 · Collision, sub-emitters and gameplay

- [ ] **04.1 · Physics modes.** Exercise world floor, authored planes/boxes and real scene geometry, including local-space transforms. Verify mode isolation for the primary and additional emitters, collision query limits and behavior when the service is unavailable.
- [ ] **04.2 · Event routing.** Validate stable emitter IDs and reject cycles or invalid targets. Verify Birth, Collision and Death payloads, inherited values and ordered dispatch. Exercise simultaneous events, deterministic queue/spawn/depth limits and overflow counters, generation invalidation, teardown and preview-local traces.
- [ ] **04.3 · Gameplay integration.** Exercise spawn at a world transform or a bound socket, typed parameter overrides, event injection, lifecycle commands and safe event subscription. Consume a committed M17 marker once at the final socket transform. Verify several views, stale handles, pooled reuse, entity removal and scene shutdown.

### M18-05 · Ribbon output

- [ ] **05.1 · Native ribbon geometry.** Generate camera-facing strips from bounded history keyed by generation-safe particle identity. Verify continuity, conservative bounds, local/world transforms, stretch/wrap UVs, and expiry and reuse on reset/death. Include native readback with visible coverage at joins and turns.

### M18-06 · Billboard, mesh and flipbook output

- [ ] **06.1 · Draw contracts.** Preserve billboard orientation and Mesh Surface emission. Deliver indexed mesh instancing with orientation, scale, bounds and material overrides. Verify reflected layouts and resource retirement after the last referencing frame.
- [ ] **06.2 · Material and view behavior.** Verify lit/unlit variants, declared blends, texture/normal map, soft depth and effective fallbacks. Exercise overlap across emitters with equal depths, slot reuse, two cameras and the declared order relative to other scene transparencies. Check the applicable fog and temporal/reactive behavior.
- [ ] **06.3 · Flipbook.** Prove adjacent-frame linear-space interpolation on native shaders using a controlled atlas with known colors. Cover wrap/clamp, final frame, disabled interpolation and atlas boundaries.

### M18-07 · Shared native compute-to-indirect contract

- [ ] **07.1 · ABI and submission.** Verify indexed argument layout, stride, offsets, count caps and zero/overflow cases on D3D12 and Vulkan. Test supported count buffers and the capability-gated fixed-count path with safe unused arguments. Compare native argument/count readback and pixel coverage to a known reference.
- [ ] **07.2 · Synchronization and ownership.** Verify declared compute-write to indirect-read transitions, validation, multiple in-flight frames, cross-frame reuse and submission retirement. Deliver the backend-neutral contract, capability decisions and independent fixture to M21.

### M18-08 · GPU particle tier

- [ ] **08.1 · Simulation and render.** Execute the minimum GPU billboard tier on both native backends. Verify spawn capacity and drops, live/dead reuse, compaction and safe indices, alpha sorting and indirect counts. Exercise empty/full pools, sort padding, multiple views and reset.
- [ ] **08.2 · Lifecycle and routes.** Exercise CPU and GPU emitters in one effect, capacity policy, explicit route selection and failure, preview rewind/recompile while the Scene keeps running, and resize/shutdown with outstanding frames. Validate device-epoch retirement and verify that shipping dispatch/submission needs no synchronous readback.

### M18-09 · Visual Effect Graph

- [ ] **09.1 · Authoring.** Persist stable node and pin IDs, topology, typed values, exposed inputs, resources and editor layout. Author the minimum GPU billboard tier through the graph. Verify undo/redo and reload, and reject invalid types, cycles, stage violations and unsupported features with node-level diagnostics.
- [ ] **09.2 · Compilation and publication.** Use the shared effect program for module authoring and graph authoring. Compile immutable revisions asynchronously. Exercise stale completion, close/reopen, valid → invalid → valid edits and the retained last ready generation. Verify that supported CPU and GPU mappings and unsupported routes agree with the published tier.

### M18-10 · Scalability, pooling and diagnostics

- [ ] **10.1 · Policy.** Author conservative bounds and quality tiers. Exercise frustum/distance culling, offscreen return, per-instance/per-scene limits for particles and transient work, priority ordering and stable overflow. Reuse pooled short-lived instances with clean generations. Verify authoritative gameplay behavior across camera and quality changes.
- [ ] **10.2 · Observability.** Surface requested/effective route, unsupported reason, bounds and cull decisions, live/spawn/drop/drawn counts, event/query overflow and timing. Verify diagnostics in Studio and the running Scene/Runtime, with the same meanings for CPU and GPU.

### M18-11 · Product and release verification

- [ ] **11.1 · Authored fixtures.** Build the three effects in the fixture matrix through Studio and commit their versioned authoring inputs, resource files and replay/capture instructions. Exercise reopen, Scene/Assembly instantiation, Editor Play, Runtime loose and detached package. Compare rendered output and report resolved asset UUIDs, revisions and effective routes.
- [ ] **11.2 · Robustness and capability coverage.** Run the native, compatibility, lifecycle and error cases in the evidence matrix. Prove missing dependency or unsupported-route failures with negative fixtures, and verify export includes the transitive inputs and runtime programs the tested routes use.
- [ ] **11.3 · Performance and final review.** Calibrate and freeze workload limits, configurations, hardware tiers and tolerances before final runs. Record actual timing, memory and overflow telemetry. Complete a requirement-to-evidence table in `docs/reviews/` with independent dispositions for all M18 requirements.

## 5. Fixture and evidence matrix

| Fixture | Production workflow and evidence |
| --- | --- |
| **Smoke and fire** | Multi-emitter effect with a GPU billboard graph, a CPU emitter, alpha/additive output, atlas interpolation and two instances with different overrides. Exercise compile failure recovery, declared CPU compatibility and detached export. |
| **Moving trail** | A moving emitter with rate over distance and a native ribbon. Cover local/world space, teleport, restart, offscreen return, changing camera and UV wrap/stretch. Compare CPU trace and native output. |
| **Impact and debris** | Committed M17 marker at a posed socket starts the effect. CPU collision creates visible instanced debris and a sub-emitter. Verify one gameplay occurrence, ordering/budgets and two instances with independent lifetimes. |

| Gate | Evidence |
| --- | --- |
| **Dedicated baseline** | Configure with `AXLE_ENABLE_VFX_GPU_TESTS=ON`. Build `AxleVFXUnitTests`, `AxleVFXGPUTests`, `AxleEditor` and `AxleRuntime` in Debug and Release. Confirm CTest registers the native cases and propagates harness failures. Run the command below for each configuration, then run the additional M18 gates. |
| **Native and compatibility** | Strict physical D3D12 and Vulkan with validation in Debug/Release. Include Studio preview, main and game views, applicable captures, Runtime loose/detached, and OpenGL on the declared CPU tier. Inspect actual Editor captures for authoring, visual output and fallback diagnostics. |
| **Simulation and failure** | Compare CPU event/count/seed traces for matching fixed inputs. Compare GPU count/index invariants, sort order, bounded statistical results and image tolerances. Include missing assets, compile failure, disabled capabilities, resource retirement and at least two frames in flight. |
| **Workload budgets** | Measure many small instances, one dense GPU effect, alpha overdraw and an event/collision-heavy case. Record hardware and driver, resolution, views, quality tier, counts and capture interval. Measure CPU tick/extraction/submission, GPU simulation/sort/render and frame `p50/p95/p99`, peak memory, upload bytes and budget drops. Record cold compilation apart from warmed runs. Freeze the acceptance limits and image tolerances before the measured acceptance run. |

```powershell
ctest --test-dir build -C Debug -R '^(VFXUnitTests|VFXGPU_D3D12|VFXGPU_Vulkan)$' --output-on-failure
ctest --test-dir build -C Release -R '^(VFXUnitTests|VFXGPU_D3D12|VFXGPU_Vulkan)$' --output-on-failure
```

Record source/build/backend identities, commands and exit codes, warnings, traces, captures and requirement dispositions. Required checks without execution evidence remain **NOT RUN**.

M18 hands M20 a detached multi-emitter package fixture and its typed dependency and runtime artifact inventory. M18 hands M21 the shared indirect fixture, native capability policy and ABI/ownership evidence.
