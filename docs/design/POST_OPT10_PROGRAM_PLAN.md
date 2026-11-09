# Post-OPT-10 OPT-11..OPT-20 program plan

Updated: **2026-11-09**

This document splits Post-OPT-10 into independent top-level OPTs. It is a
planning document only: it does not close OPT-10, activate a task, or authorize
source implementation.

The common **Post-OPT-10 baseline gate** comes first: OPT-10 must be accepted or
explicitly deferred, the capability matrix and benchmark contract must exist,
and the RenderWorld/RenderService/RenderGraph ownership contracts must be
frozen. These are entry conditions, not a large feature OPT.

## Dependency graph

```mermaid
flowchart TD
    Gate[Post-OPT-10 baseline gate] --> O11[OPT-11 Render Thread & VRAM Aliasing]
    Gate --> O12[OPT-12 Shader and Material]
    O11 --> O13[OPT-13 Lighting & Post-Processing]
    O12 --> O13
    O11 --> O14[OPT-14 Nanite / GPU Geometry]
    O12 --> O14
    Gate --> O15[OPT-15 Logic LOD]
    Gate --> O16[OPT-16 Offline autoLOD]
    O13 --> O17[OPT-17 Animation]
    O12 --> O17
    O13 --> O18[OPT-18 VFX]
    O12 --> O18
    O17 --> O19[OPT-19 Authoring UI]
    O18 --> O19
    O12 --> O19
    O14 --> O20[OPT-20 Performance and rollout]
    O15 --> O20
    O16 --> O20
    O17 --> O20
    O18 --> O20
    O19 --> O20
```

Each OPT has its own base commit, implementation scope, tests, evidence and
acceptance record. A plan or a completed earlier OPT does not release the next
one.

## Post-OPT-10 baseline gate

Before OPT-11 begins:

- Record the final OPT-10 state and all unavailable gates.
- Re-audit current Renderer, RHI, Asset, Animation, VFX, Scene and Plugin APIs.
- Define fixed benchmark scenes, camera paths, hardware tiers, resolutions,
  quality presets, frame-time percentiles and image-diff thresholds.
- Freeze capability/fallback reporting for D3D12, Vulkan and OpenGL.
- Freeze RenderWorld snapshot ownership, RenderService lifetime and RenderGraph
  pass/resource contracts.

No performance number such as `120 FPS`, `3.5 ms`, `50% VRAM` or `0% dormant
CPU` is accepted without this baseline.

## OPT-11 — Render Thread, parallel command recording and VRAM aliasing

**Goal:** Decouple simulation from rendering, scale draw-call generation across
worker cores, and optimize transient GPU memory without violating RHI owner
thread, resource lifetime, or cache coherency rules.

Packages:

- **11.1 Ownership design:** specify device, resource, RenderService,
  presentation, shutdown and device-loss ownership.
- **11.2 Snapshot queue:** bounded `RenderWorld` lock-free queue, back-pressure,
  frame-drop policy and one-frame latency diagnostics.
- **11.3 Offscreen worker & presentation:** run a dedicated render worker and
  transfer resize, acquire, present and surface rebuild safely without
  cross-thread `CanSubmit` violations.
- **11.4 Parallel command recording:** leverage NRI independent multi-threaded
  command allocators and `enkiTS` worker jobs to record draw passes concurrently,
  merging into the primary command list prior to queue submission.
- **11.5 Transient VRAM aliasing:** integrate topological lifetime analysis into
  `RenderGraph`, allocate transient targets from shared hardware heaps
  (`ID3D12Heap` via `CreatePlacedResource` / Vulkan memory suballocation), and
  automatically emit aliasing memory barriers and cache invalidations.
- **11.6 Plugin/lifetime stress:** test plugin callbacks, resource leases,
  shutdown and restart while frames are queued and transient heaps are active.
- **11.7 Acceptance:** identical image output, bounded latency, clean shutdown,
  measured CPU overlap, verified VRAM reduction from aliased passes, and
  device-loss recovery.

Out of scope: mesh shaders and DDGI (owned by OPT-14 and OPT-13).

## OPT-12 — Shader and Material pipeline

**Goal:** Establish a versioned material representation before optional Slang or
node-editor work.

Packages:

- **12.1 Material IR:** stable node/value IDs, schema version, validation and
  serialization.
- **12.2 Existing compiler backend:** lower a minimal graph to current HLSL,
  DXIL and SPIR-V compilation.
- **12.3 Reflection/layout:** descriptor compatibility, pipeline cache keys and
  last-known-good reload.
- **12.4 Resource semantics:** texture color space, sampler, fallback and
  authored dependency diagnostics.
- **12.5 Optional Slang evaluation:** prototype Slang as a backend; it must not
  be required by existing materials.
- **12.6 OpenGL policy:** define a separate lowering path or explicit unsupported
  feature diagnostic; never claim HLSL works on OpenGL automatically.

Acceptance requires cross-backend shader/layout tests, invalid-graph rollback and
stable authored asset round-trips.

## OPT-13 — Performance-First Lighting, Volumetric Atmosphere, DDGI and Post-Processing

**Goal:** Deliver a complete high-fidelity lighting stack within a strict
3.5ms–5.0ms GPU budget on mainstream hardware, featuring Cascaded Shadows,
Screen-Space Ambient/Reflections, Froxel Volumetric Fog, Dynamic Diffuse GI
probes with capability fallback, and Film-Grade Post-Processing.

Packages:

- **13.1 Cascade contract & implementation:** camera-space split, texel
  stabilization, depth bias, 3–4 cascades with PCF 5x5 sampling across D3D12
  and Vulkan, preserving the OpenGL compatibility fallback.
- **13.2 HDR Environment & IBL:** authored HDR skyboxes, pre-filtered
  radiance/irradiance cubemaps, split-sum 2D BRDF LUT integration, and diffuse
  ambient fallback.
- **13.3 Screen-space ambient & reflections:** depth/normal history,
  half-resolution GTAO/SSAO compute pass with bilateral depth-aware
  reconstruction (75% ALU savings), and bounded SSR ray-marching with temporal
  invalidation.
- **13.4 Volumetric Fog & God Rays:** low-resolution Frustum Voxel Grid
  (*Froxel* $160 \times 90 \times 64$) with temporal reprojection, phase
  function scattering, and directional sun shaft integration.
- **13.5 Dynamic Diffuse Global Illumination (DDGI Probes):** 3D axis-aligned
  probe grid volume ($32 \times 16 \times 32$), dual octahedral texture
  atlases (irradiance $8 \times 8$ and distance/variance $16 \times 16$ with
  Chebyshev visibility to eliminate light leaking), temporal 25% staggered ray
  updates, hardware ray-tracing via NRI with seamless fallback to IBL/ambient
  probe when RT is unavailable.
- **13.6 Film-Grade Post-Processing:** ACES Filmic Tone Mapping, PBR eye
  adaptation (auto-exposure compute pass with luminance histogram), thresholded
  bloom with dual-filtering blur, and color grading LUTs.
- **13.7 Acceptance & Budget verification:** backend readbacks, resize/device-failure
  tests, quality thresholds, verified lighting budget ($\le 5.0\text{ ms}$ at
  1080p on target hardware), and fallback parity on non-RT/OpenGL devices.

## OPT-14 — Nanite-style virtual geometry and GPU culling

**Goal:** Enable rendering of dense scenes (100k+ instances) through
continuous GPU-driven meshlet clustering, cluster DAG hierarchy, and hardware
task/mesh shaders on D3D12/Vulkan, with strict capability-gated indexed
fallback.

Packages:

- **14.1 GPU instance data:** bounds, transforms, material IDs and generation
  ownership in unified structured buffers.
- **14.2 GPU-driven culling (Hi-Z):** compute shader frustum culling, cone
  backface culling, two-phase occlusion culling against a Hierarchical-Z depth
  pyramid, and indirect draw argument generation (`ExecuteIndirect` /
  `vkCmdDrawIndexedIndirect`).
- **14.3 Meshlet asset generation & schema:** cluster generation via
  `meshoptimizer` (`meshopt_buildMeshlets`, $\le 64$ vertices, $\le 126$
  primitives), cluster bounding spheres/cones, source hashing, and
  deterministic debug cluster coloring.
- **14.4 Cluster DAG hierarchy:** recursive cluster grouping into a Directed
  Acyclic Graph with locked shared boundary loops to guarantee watertight,
  100% crack-free continuous LOD transitions.
- **14.5 Hardware Task & Mesh Shader pipeline:** Task (Amplification) Shaders
  evaluating projected screen-space error per cluster for dynamic continuous
  LOD selection and frustum/cone culling; Mesh Shaders emitting primitives
  directly to rasterizer on D3D12 and Vulkan via NRI.
- **14.6 Capability gate & indexed fallback:** robust feature detection with
  seamless, automatic fallback to offline discrete LOD indexed draws on devices
  without native mesh shader support.
- **14.7 Scale verification & acceptance:** benchmark 100k+ instances, verify
  zero crack artifacts on cluster boundaries, ensure identical visibility/image
  output against indexed fallback, and validate dynamic device recovery.

## OPT-15 — Logic LOD and simulation scaling

**Goal:** Reduce CPU simulation cost while preserving authoritative semantics.

Packages:

- **15.1 Tick identity:** public monotonic tick index in `SimulationClock`.
- **15.2 Significance policy:** tier data, distance/visibility inputs and
  hysteresis.
- **15.3 Deterministic scheduler:** modulo staggering, bounded work and stable
  entity ordering.
- **15.4 Spatial index:** scene-owned cells, migration and query tests.
- **15.5 Dormancy:** explicit sleep/wake events, persistence and excluded-view
  rules.
- **15.6 Interpolation:** visual interpolation for skipped logic ticks without
  changing fixed-step authority.
- **15.7 Physics policy:** sleeping/interest hints first; body deactivation only
  after contacts, joints, triggers, characters and wake-up are specified.
- **15.8 Plugin API:** read-only significance/tier queries for project plugins.
- **15.9 Animation URO:** update intervals and cached pose invalidation only after
  event, root-motion and VFX semantics are covered.

The first accepted slice keeps Jolt bodies active. “0% dormant CPU” is a measured
target, not a design assumption.

## OPT-16 — Offline autoLOD

**Goal:** Generate deterministic discrete LOD data at the AssetCooker boundary.

Packages:

- **16.1 Derived schema:** source hash, settings, tool version and provenance.
- **16.2 Simplifier wrapper:** integrate meshoptimizer behind a narrow Engine
  wrapper with license and version records.
- **16.3 Static LOD generation:** deterministic ratios, bounds and material groups.
- **16.4 Cooker/cache:** invalidation, package inclusion and failed-artifact
  rejection.
- **16.5 Runtime selection:** screen-space policy, original-mesh fallback and
  diagnostics.
- **16.6 Special meshes:** explicit skinned, transparent, collision and
  unsupported-mesh behavior.

Acceptance requires clean-checkout reproducibility, source-change invalidation
and visual/runtime fallback tests. Continuous meshlets belong to OPT-14.

## OPT-17 — Animation completion

**Goal:** Complete Animation in data, runtime bridge and acceptance layers.

Packages:

- **17.1 Foundation audit:** reclassify existing A0–A3/B1/B2/G1 evidence against
  current source.
- **17.2 CPU correctness:** skeleton topology, bind fallback, sampling, blending,
  events, root motion and malformed input.
- **17.3 Authored resources:** versioned skeleton/clip/controller/mesh definitions,
  atomic save and failed-save preservation.
- **17.4 Instance isolation:** copy/duplicate clocks, triggers, transitions and
  independent runtime state.
- **17.5 RHI geometry:** backend-neutral skeletal conversion and palette leases.
- **17.6 RHI draw:** D3D12 and Vulkan skinned PBR/depth variants, resize and
  teardown.
- **17.7 Picking/shadows:** pose-consistent picking and explicit shadow limits.
- **17.8 Acceptance:** CPU, GPU, private preview and native UI evidence kept as
  separate states.

OPT-17 starts after OPT-13's render contracts. It does not create a private
renderer or depend on VFX merely to reuse preview behavior.

## OPT-18 — VFX completion

**Goal:** Complete VFX data, deterministic runtime and native rendering layers.

Packages:

- **18.1 Evidence audit:** reclassify Phase 39 reports and current panel/runtime
  behavior.
- **18.2 Effect assets:** versioned multi-emitter definitions, stable IDs and
  resource references.
- **18.3 Runtime instances:** independent RNG, playback, duration, looping,
  scrubbing and bounded events.
- **18.4 Child systems:** bounded sub-emitter relationships, collision policy and
  missing-resource diagnostics.
- **18.5 Scene persistence:** scene/assembly round-trip without GPU handles or
  ImGui dependencies.
- **18.6 RHI submission:** per-emitter buffers, descriptors and resource leases.
- **18.7 Backend coverage:** D3D12/Vulkan multi-emitter readback, depth/blend,
  resize and device-loss tests.
- **18.8 Acceptance:** CPU, GPU, preview and native UI evidence separated.

OPT-18 follows the shared render/material contracts. CPU work can be prepared
alongside OPT-17, but the full VFX phase is not closed until native GPU and UI
gates pass.

## OPT-19 — authoring UI and integration

**Goal:** Connect accepted runtime/data contracts to user workflows.

Packages:

- **19.1 Animation documents:** open/save/dirty/history and private preview.
- **19.2 Animation tools:** graph, dopesheet, events, selection, picking and
  input routing.
- **19.3 VFX documents:** open/save/dirty/history and isolated preview.
- **19.4 VFX tools:** emitter order, curves/gradients, resource pickers and
  multi-emitter controls.
- **19.5 Material editor:** graph editing, compile diagnostics and last-good
  program behavior.
- **19.6 Real UI matrix:** narrow/wide layout, restart, failed save, undo,
  selection changes, close/reopen and project reload.

No CPU count or panel existence is accepted as GPU/UI proof. UI must use the
existing RenderService, PluginSession and resource ownership contracts.

## OPT-20 — performance, rollout and closure

**Goal:** Decide which Post-OPT-10 features are production-ready.

Packages:

- **20.1 Benchmark matrix:** fixed scenes, backends, hardware tiers and presets.
- **20.2 Image quality:** golden images, tolerance policy and fallback parity.
- **20.3 Performance:** CPU/GPU frame time, memory, submissions and percentile
  reports.
- **20.4 Reliability:** resize, device loss, restart, export and plugin selection.
- **20.5 Cutover:** remove only measured temporary adapters and update docs.
- **20.6 Acceptance:** close OPT-11..OPT-19 independently; keep unsupported or
  unmeasured features deferred.

## VFX and Animation order

The correct relationship is not “finish one entire phase before touching the
other.” Use this split:

1. Prepare CPU/data work for OPT-17 and OPT-18 after the baseline gate.
2. Complete OPT-13 lighting/resource contracts.
3. Complete Animation runtime bridge (OPT-17) before VFX runtime bridge (OPT-18)
   in the shared checkout because skinning/picking is a direct renderer contract.
4. Complete both runtime bridges before OPT-19 authoring UI.
5. Keep historical order explicit: outstanding VFX acceptance is reviewed first,
   then Animation authoring, then VFX feature expansion.

## Status vocabulary

Every report uses **planned**, **implemented**, **verified** and **accepted** as
separate states. This document marks every OPT as **planned only**.
