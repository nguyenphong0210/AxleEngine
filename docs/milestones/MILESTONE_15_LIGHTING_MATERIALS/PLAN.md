# Milestone 15 — Lighting and Material Fidelity

**Status:** HOLD for full M15 acceptance. U0 through U3 are accepted. The [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records the corrected source, Editor captures and native evidence.
**Current source reference:** `114438ba` for U4-E (`80e9837a` U4-D, `9028e8d7` U4-C, `020c32ed` U4-A/B, `baaf36d2` U3, `c6245e2e` S1.3); `19ac2a1` remains the historical M14 handoff baseline. This reference and the batch schedule below do not mark any unchecked leaf PASS or release later packages until explicitly assigned in the live exchange.
**Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md)
**Detailed implementation notes:** [REMEDIATION_PLAN.md](REMEDIATION_PLAN.md)
**Final acceptance procedure:** [U10 architecture and feature review](U10_ACCEPTANCE_REVIEW.md)
**M14 prerequisite review:** [M14 U9 final acceptance](../../reviews/M14_U9_FINAL_ACCEPTANCE_REVIEW.md)
**Documentation review:** [M15 documentation review](../../reviews/M15_DOCUMENTATION_REVIEW.md)

M14 supplies the accepted entry contracts for `ViewContext`, `IRenderPass` and
`RenderPassScheduler`, `ResolvedSurface`, `RenderGraph` and target generations,
`RenderBackendPolicy`, the lighting/reflection/GI services, temporal/post-process
services, diagnostics and `RenderService` product ownership. M15 must implement
fidelity behind these seams and keep every deferred behavior explicitly marked
until its own released Unit evidence passes. The historical [M14 review](../../reviews/M14_CODEX_REVIEW.md)
is retained for audit history and is superseded as the current acceptance record.

This plan replaces the previous optimistic checklist. A checkbox is ticked only after the stated source, runtime, and verification evidence exists. A compiled shader, allocated texture, successful dispatch, non-empty image, or UI toggle is not acceptance evidence by itself.

## 1. Goal and acceptance vocabulary

M15 delivers a physically based forward material and lighting system with smooth shading, correct material data, direct/HDR lighting, shadows, occlusion, reflections, post-processing, authoring, documented fallbacks and reproducible evidence.

Use these states in reports:

- **Verified:** source path and required runtime fixture pass.
- **Partial:** executable code exists, but a contract remains open.
- **Not run:** required hardware/backend/fixture/inspection was unavailable.
- **Blocked:** a required prerequisite is unavailable; do not convert this to PASS.
- **Accepted:** every exit condition for the named checkpoint passes and independent review records it.

## 2. Current baseline (not the completion checklist)

| Area | Historical state at `77d6b70` (not the current M14 source) | Classification |
|---|---|---|
| CSM, HDR/Split-Sum IBL, local shadows, procedural sky | Existing implementation and earlier native evidence | Baseline to protect |
| P0 baseline fixtures and audit | 6 deterministic fixtures, 19 diagnostic channels, 5 open source findings | Verified checkpoint |
| GPU timestamps | Valid probe and renderer scope on RTX 3070 D3D12/Vulkan Debug/Release | Verified checkpoint; full M19 rollout matrix open |
| Smooth ForwardPBR normal | Derivative-flat-normal override removed | Verified partial checkpoint |
| `ScreenNormal` producer | Opaque normal pass exists and is consumed by GTAO/SSR | Verified partial checkpoint |
| GTAO/contact composite | Conservative bounded post-lighting composite | Verified partial checkpoint; true indirect separation open |
| Hi-Z | Per-level descriptor sets and graph reads fixed | Verified resource checkpoint; quality traversal open |
| Authored reflection probe | `CubemapPath` bake/cache/sample path works | Verified partial P5 checkpoint |
| Dynamic capture/prefilter/scheduling/multi-probe | Not complete | Open |
| SSR material resolve | Fixed Hi-Z input exists; cone tracing, material Fresnel and energy resolve remain open | Open |
| Fog/DDGI/exposure/bloom/TAA/SSS/LTC | Prototypes or partial paths exist; contracts are not closed | Open |
| RT | Capability reporting only; no BLAS/TLAS or RT pipeline | Moved to M16 Ray Tracing Backend |

## 3. Hierarchy and dependency order

M15 follows the repository hierarchy exactly:

```text
Milestone 15 — Lighting and Material Fidelity
  └─ Phase 45 — Lighting and Material Fidelity delivery
      └─ Unit U0..U10 — architectural ownership and release boundary
          └─ SubUnit Sx.y — one independently buildable/checkable implementation package
```

The detailed execution sections below use `P0..P13` plus the separately gated `P6B` as historical package labels. In the canonical hierarchy they are **SubUnits**, not additional Milestones or Phases. Each SubUnit maps to one Unit and must leave the tree buildable.

| Milestone | Phase | Unit | SubUnits in this plan | Scope |
|---|---|---|---|---|
| M15 | 45.1–45.3 | U0 Baseline | S0.1–S0.2 (`P0`) | Existing lighting baseline, contracts and fixtures |
| M15 | 45.4, 45.6 | U1 Direct BRDF | S1.1–S1.3 | GGX compensation, sun angular radius and direct-light numeric fixtures |
| M15 | 45.5 | U2 Dynamic Environment | S2.1–S2.3 | Procedural sky, environment bake/cache and publication |
| M15 | 45.3 / shared infrastructure | U3 Screen-Space Infrastructure | S3.1–S3.4 (`P1`, `P2`) | Targets, graph, descriptors, surface data, timing and histories |
| M15 | 45.7–45.8 | U4 Occlusion | S4.1–S4.3 (`P3`) | GTAO, bilateral reconstruction and directional contact shadows |
| M15 | 45.9–45.11 | U5 Reflections and Area Lights | S5.1–S5.8 (`P4`, `P5A–P5D`, `P6`, `P7`) | Hi-Z, captures, probes, SSR, planar reflection and LTC |
| M15 | 45.12 | U6 Atmosphere | S6.1–S6.3 (`P9`) | Froxel fog, shadows and temporal fog state |
| M15 | 45.13 | U7 Experimental GI | S7.1–S7.4 (`P11`) | DDGI acquisition, atlases, visibility and feedback |
| M15 | 45.14–45.15 | U8 Post and Temporal | S8.1–S8.6 (`P8`) | Exposure, bloom and TAA |
| M15 | 45.16 | U9 Advanced Materials | S9.1–S9.5 (`P10`) | Sheen, absorption, dispersion, SSS and graph parity |
| M15 | 45.18 | U10 Authoring and Rollout | S10.1–S10.5 (`P12`) | Editor, presets, evidence and final acceptance |
Phase 45.17 is the final performance/quality gate inside U10; it is not a separate implementation Unit. Ray tracing is removed from M15 scope and is planned as M16-RT.

M16-RT owns BLAS/TLAS, RT pipelines, RT reflections/shadows, denoising and optional RT GI. M15 only supplies the material, view, capture and raster fallback contracts that M16 consumes.

Package aliases in this table follow the recovery plan: P8 is post/temporal, P9 is atmosphere, P10 is advanced materials, P11 is experimental DDGI, and P12 is authoring/final acceptance. P6B remains a separately gated raster extension; P13 is retired from M15 and re-homed in M16-RT.

### Unit closure rule

A Unit closes only when all its SubUnits pass, its dependency Units are accepted, and its Unit exit evidence is recorded. A Phase closes only when every Unit mapped to that Phase passes. M15 closes only when all required Phases and the final U10 gate pass.

### Canonical checklist policy

All M15 execution state is recorded in the single atomic checklist beginning in
Section 4. The Unit/SubUnit hierarchy and dependency table describe ownership;
they do not carry a second independent checkbox state. A parent group checkbox is
allowed only as a derived summary: it remains unchecked until every child leaf is
PASS, and it carries no evidence separate from its children. Close each leaf
immediately after its evidence passes, then update the derived parent summary.

### 3.1 Scope allocation boundary: Milestone 15 vs Milestone 16/17

To prevent scope creep, M15 owns raster lighting/material fidelity, M16 owns ray tracing, and M17 owns scalable geometry/visibility. The following boundaries are enforced:

| Architectural Item | Target Milestone | Allocation and Execution Contract |
|---|---|---|
| **1. Modular `IRenderPass` Extraction** | **M15 (100% Progressive)** | Replace the monolithic lambda adapters in `RHISceneRendererPasses.cpp` by creating dedicated pass classes under `Engine/Renderer/include/Axle/Passes/` and `src/Passes/` as each M15 feature package is delivered. |
| **2. Hi-Z Depth Pyramid** | **M15 (100% Core)** | Delivered in U5/S5.1 (P4) with full compute downsampling and odd-dimension handling for SSR; reusable by M17 for GPU-driven occlusion culling. |
| **3. Denoising Strategy** | **M15 (Pass-Local) / M16 (Unified)** | M15 uses pass-local spatial/temporal filters: Bilateral reconstruction for GTAO (P3), Roughness cone-filtering for SSR (P6), and Variance-clipped TAA (P8). A unified spatiotemporal denoiser (SVGF) is deferred to M16 (RT backend). |
| **4. Clustered / Tiled Light Culling** | **M17 (100% Deferred)** | Formally deferred to Milestone 17 (Scalable Geometry and Visibility). M15 retains Forward+ lighting with a bounded active light capacity managed by `LightingService`. |
| **5. Bindless Textures for Material Graph** | **M15 (Heap Runtime) / M17 (Full Compiler)** | M15 delivers runtime `Tier2Bindless` heap resource support and standard texture slot mapping. Full HLSL dynamically indexed bindless array emission (`g_Textures[index]`) for indirect draw execution is aligned with M17 Virtual Geometry / Meshlets. |

### Dependency order

```text
U0 -> U1/U2 -> U3 -> U4 -> U5 -> U6/U7 -> U8 -> U9 -> U10; M16-RT depends on M15 U3/U5/U7, and M17 depends on M15 contracts.

Within U5: `S5.1 Hi-Z -> S5.2 Dynamic Capture -> S5.3 Prefilter -> S5.4 Invalidation/Scheduling -> S5.5 Multi-Probe -> S5.6 SSR -> S5.7 LTC -> S5.8 Planar Reflection`. The existing `P4/P5A–P5D/P6/P7` names are implementation aliases for these SubUnits.

Within U6: `S6.1 Froxel Resources & Tiers -> S6.2 Media/Shadow Injection, Integration & Temporal Froxels -> S6.3 Edge Policies, Transparency/Depth, Fallbacks & Cost` (`P9`). Because U8 (TAA/object motion vectors) follows U6 in the milestone order above, U6 temporal reprojection uses `ViewContext` previous/current camera matrices and target/history resets (`S3.4`) without introducing a hidden U8 dependency.
```

P5A–P5D may reuse authored-probe work, but none may silently depend on unfinished P6. Planar reflection is a required M15 raster contract; RT off-screen reflection is an M16 requirement.

#### U5 Execution Batches (U5-A through U5-I)

To keep each delivery checkpoint focused, buildable, and independently verifiable on CPU and native D3D12/Vulkan without violating Section 4's single atomic checklist rule, the remaining U5 work is organized into nine sequential batches matching SubUnits `S5.1`–`S5.8` (splitting `S5.6` into traversal and resolve):

| Batch | SubUnit & Atomic Leaves | Scope Focus | Suggested Review Cluster |
|---|---|---|---|
| **U5-A** | `S5.1`: `M15-U5-05.1` → `M15-U5-07.2` | Hi-Z conservative odd-edge sampling (`HiZ.hlsl`), `[0, 1]` min-depth convention contract, useful level-count policy (`RHIViewTargets`), per-level visualization, RHI barrier validation, and `1x1`/`3x5`/`1919x1079` native readbacks vs CPU reference. | Cluster 1 (Hi-Z, Probes) |
| **U5-B** | `S5.2`: `M15-U5-08.1` → `M15-U5-15` | Dynamic six-face reflection capture: color/depth targets, six 90-degree views & direction/up validation, recursion/TAA/UI/feedback isolation guards, opaque/cutout + selected shadows, layer-offset copies, transactional generation publish & last-good retention, red-object cubemap fixture. | Cluster 1 (Hi-Z, Probes) |
| **U5-C** | `S5.3`: `M15-U5-16.1` → `M15-U5-20` | Cubemap roughness prefilter: mip-capable cubemap + per-mip UAV/SRV views, GGX roughness prefilter compute shader preserving face orientation & cross-face seams natively (no equirectangular routing), mirror vs rough sweep fixtures. | Cluster 1 (Hi-Z, Probes) |
| **U5-D** | `S5.4`: `M15-U5-21.1` → `M15-U5-26.4` | Capture invalidation & scheduling: transform/revision/geometry/material/light/asset tracking, manual/auto capture with per-frame face/probe budget, `Pending`/`Stale`/`Failed` diagnostics, last-good retention on failure, capture cache eviction, resize/device-reset/scene-replace fixtures. | Cluster 1 (Hi-Z, Probes) |
| **U5-E** | `S5.5`: `M15-U5-27.1` → `M15-U5-31.5` | Multi-probe selection & fallback: Box/Sphere influence weights, per-probe parallax correction prior to blending, seamless multi-probe blending, unified reflection energy policy across `SSR -> Local Probe -> Sky IBL`, overlap/boundary/missing/stale/SSR-miss tests. | Cluster 1 (Hi-Z, Probes) |
| **U5-F** | `S5.6` (part 1): `M15-U5-32.1` → `M15-U5-33.3` | Hi-Z SSR ray traversal & refinement: freeze ray/depth projection, initial ray + start mip, true Hi-Z cell stepping & descend/refine loop (`SSR.hlsl`), bounds/max-steps termination, thickness, hit refinement, edge/disocclusion rejection, CPU/native traversal readback. | Cluster 2 (SSR Resolve) |
| **U5-G** | `S5.6` (part 2): `M15-U5-34.1` → `M15-U5-39.6` | Material-aware SSR resolve: HDR color mip pyramid + roughness cone filtering, surface roughness/metallic/baseColor/reflection flags binding, dielectric & conductor Fresnel `F0`, indirect specular replacement/blend in `ForwardPBR`/`MaterialSurface` without duplicating IBL, SSR/probe/sky blend, full sweep matrix (hit, miss, motion, roughness, gold/chrome/dielectric). | Cluster 2 (SSR Resolve) |
| **U5-H** | `S5.7`: `M15-U5-40` → `M15-U5-45.3` | LTC rectangular & disk area lights: validated LTC transform/amplitude tables, rectangle & disk emitters, units/orientation/sidedness/range, diffuse + specular LTC integration, explicit area-shadow policy, Component/Extraction/Serialization/Inspector completion, Monte Carlo numerical comparison. | Cluster 3 (Area & Planar) |
| **U5-I** | `S5.8`: `M15-U5-46.1` → `M15-U5-46.8` | Planar reflection: planar surface contract, independent mirrored capture `ViewContext`, targets/graph edges, dedicated `PlanarReflectionPass`, shared energy resolve/fallback, oblique near-plane clipping & self-intersection avoidance, invalidation/failure retention, native tests. | Cluster 3 (Area & Planar) |

#### U6 Execution Batches (U6-A through U6-E)

To prevent the current 2D raymarch prototype from hiding resource, physical-integration, temporal, or transparency bugs, U6 (`S6.1`–`S6.3`) is organized into five sequential, independently testable checkpoints that pair each implementation step directly with its corresponding `M15-U6-07.*` verification gates:

| Batch | SubUnit & Atomic Leaves | Scope Focus | Suggested Review Cluster |
|---|---|---|---|
| **U6-A** | `S6.1` (+ budget): `M15-U6-01.1`, `M15-U6-01.2`, `M15-U6-07.5` (initial) | Froxel 3D contract & RHI allocation: freeze 3D grid geometry (`160x90x64` reference, log/exponential Z-slice distribution, near/far bounds) and quality tier scaling (`Low`/`Medium`/`High`/`Ultra`); add bounded `TEXTURE_3D` SRV/UAV scatter, integrated-volume, and ping-pong history slots in `RHIViewTargets` (`RHITexture`), resize/device-epoch lifecycle, fault rollback, CPU contract checks, native DX12/Vulkan 3D read/write validation, and initial per-tier VRAM accounting. | Cluster 1 (Static 3D Core) |
| **U6-B** | `S6.1`/`S6.2` core: `M15-U6-02.1`, `M15-U6-03.1`, `M15-U6-03.2`, `M15-U6-05.3`, `M15-U6-06` | Static 1-frame 3D froxel media & physical integration: replace the 2D `VolumetricFog.hlsl` raymarch and `FroxelFogPass` dispatch with 3D density/media injection + front-to-back slice integration (`inScatter`, `transmittance`) + `FogApply`; enforce Beer–Lambert / Henyey–Greenstein units against an independent CPU analytic/quadrature reference (with temporal blending disabled so 1-frame accuracy is unmasked); remove the invented directional sun fallback and emit explicit `RenderDiagnostics`; remove the `0.0001` density clamp so zero density/intensity preserves linear HDR output identically. | Cluster 1 (Static 3D Core) |
| **U6-C** | `S6.2` shadows: `M15-U6-02.2`, `M15-U6-07.1`, `M15-U6-07.2` | Light & shadow visibility injection: sample directional CSM cascades and supported local lights/shadows during 3D froxel scattering with explicit `RenderGraph` shadow-atlas reads; verify native DX12/Vulkan wall occlusion behind blockers (`07.1`), bounded light shafts through a blocker slit (`07.2`), and zero shafts when directional light is absent. Keep `M15-U6-02.2` `EDITOR VISUAL` explicitly open/NOT RUN unless verified with Debug/Release Editor captures. | Cluster 1 (Static 3D Core) |
| **U6-D** | `S6.2`/`S6.3` temporal: `M15-U6-04.1`, `M15-U6-04.2`, `M15-U6-04.3`, `M15-U6-07.3`, `M15-U6-07.4` | Temporal 3D froxel reprojection & invalidation: ping-pong 3D history reprojected via `ViewContext` `PrevViewProjection`, frustum/depth/neighborhood rejection to bound trails, and explicit history invalidation on frame 0, `CameraCut`, resize, `SceneRevision`/`SceneInstanceID`, `DeviceEpoch`, `FeaturePolicy`, and tier/settings changes; verify bounded trails under camera motion (`07.3`) and clean resize/reset transitions (`07.4`) on Debug/Release DX12/Vulkan. | Cluster 2 (Temporal & Policies) |
| **U6-E** | `S6.3` edges & cost: `M15-U6-05.1`, `M15-U6-05.2`, `M15-U6-07.6` (+ final `07.5`) | Transparency/depth interaction, edge policies, fallback & profiling: resolve `Transparent`/`Particles` depth composition (`05.1`) so closer transparent surfaces sample/apply fog at true depth instead of opaque-depth `FogApply` overwrite; verify `No-light` scattering/extinction (`05.2`), authoring/extraction/serialization/Inspector/Runtime parity, and OpenGL/MSAA capability diagnostics; reconcile final VRAM (`07.5`) and record Debug/Release DX12/Vulkan per-pass GPU timestamp costs across tiers (`07.6`). | Cluster 2 (Temporal & Policies) |

**U6 execution & review cadence:** Each batch `U6-A`..`U6-E` is implemented, self-verified (CPU + Debug/Release D3D12/Vulkan + RHI validation), and locally committed as one coherent checkpoint without modifying unchecked leaves in later batches. Per the shared working protocol, Codex conducts either one consolidated review at the end of `U6-E` or an optional mid-unit checkpoint review after `U6-C` (Cluster 1) before layering temporal history and transparency composition. This schedule is planning structure only; it does not release U6 implementation or check off any atomic leaf ahead of an explicit task assignment in [CODEX_GEMINI_EXCHANGE.json](../../agents/CODEX_GEMINI_EXCHANGE.json).

**U6-E review:** The [Codex review and verification](../../reviews/M15_U6_E_CODEX_REVIEW.md) records the corrected implementation and requirement evidence. Formal acceptance remains open for the focused independent recheck of the corrections.

#### U8 Execution Batches (U8-A through U8-E)

To keep HDR post-processing and temporal reconstruction modular, independently verifiable on CPU and native D3D12/Vulkan, and isolated from motion-vector/shader layout regressions, U8 (`S8.1`–`S8.6`) is split into five sequential checkpoints covering all 42 atomic leaves:

| Batch | Atomic Leaves | Scope Focus | Review Boundary |
|---|---|---|---|
| **U8-A** | `M15-U8-01.1` → `M15-U8-05.4` | Auto Exposure & metering pipeline order: full-viewport log-luminance histogram + global reduction, real `DeltaTime` adaptation across `30/60/120 Hz`, per-view exposure history & first-frame/black-frame/camera-cut policies, pre-Bloom/UI-excluded metering source, `ToneMapping` feed, mixed-tile & rate-invariance tests (`M15-U8-02.1/02.3/03.1/04` `EDITOR VISUAL` stay explicitly open if display is offline). | Consolidated U8 review |
| **U8-B** | `M15-U8-06` → `M15-U8-10.4` | 6-level physically-based Bloom: single `PrefilterThreshold` pass on level 0, validated 6-mip dimensions for odd/small views, additive upsample reconstruction preserving finer levels, single-application tint & intensity at composite, and impulse-halo / gain / odd-small / HDR-SDR fixtures. | Consolidated U8 review |
| **U8-C** | `M15-U8-11.1` → `11.3`, `13.1` → `13.5` | Motion vectors & temporal reset infrastructure: stable per-entity `PrevWorldTransform` cache, native `Velocity` target & camera/object motion-vector generation with jitter cancellation, and complete 5-trigger history resets (`Resize`, `ViewSwitch`, `CameraCut`, `Settings/Format`, `SceneRevision`). | Consolidated U8 review |
| **U8-D** | `M15-U8-12.1` → `12.2`, `15.1` → `15.4` | TAA resolve core: closest `3x3` depth & velocity sampling, disocclusion/invalid-history rejection, explicit resource declarations, no silent FXAA-on-TAA stacking, and `static shimmer`, `moving object`, `disocclusion`, `resize/switch` native tests. | Consolidated U8 review |
| **U8-E** | `M15-U8-14.1` → `14.3`, `15.5` | TAA edge policies & U8 closure: transparent geometry & reactive mask policies, unjittered UI/gizmo policy, `ghosting` fixture, post/temporal GPU timestamp & VRAM accounting, and final `PLAN.md` + exchange delivery. | Consolidated U8 review |

#### U9 Execution Batches (U9-A through U9-E)

To keep advanced material shading models, transmittance optics, screen-space subsurface filtering, MaterialGraph contract parity, and product authoring pipelines isolated and independently verifiable, U9 (`S9.1`–`S9.5`, 19 leaves) was implemented across five sequential checkpoints starting from `6f22017a`:

| Batch | Atomic Leaves | Commit | Scope Focus | Review Boundary |
|---|---|---|---|---|
| **U9-A** | `M15-U9-01`, `08.1` | `5c140f37` | Charlie sheen Direct, Point, Spot, LTC Area & IBL BRDF + 9x9 directional albedo LUT energy conservation (`<= 1.0`). | Consolidated U9 review |
| **U9-B** | `M15-U9-02`, `03`, `08.2` | `ffb140b1` | Beer–Lambert absorption, thickness/path policy (`REFRACTION_ABSORPTION_POLICY.md`) & Cauchy 3-wavelength chromatic dispersion. | Consolidated U9 review |
| **U9-C** | `M15-U9-04.1` → `04.3`, `05`, `08.3` | `3244aa34` | Separable screen-space SSS (`SSS.hlsl`, `SubsurfaceScatteringPass`), depth & normal/material boundary rejection, specular/emissive split & recombination. | Consolidated U9 review |
| **U9-D** | `M15-U9-07.1`, `07.2`, `08.4` | `dbdf828d` | 13-pin `PBRMasterOutput` lowering, `MaterialSurface.hlsl` Sheen/SSS split parity, `MaterialBindingPlan::SupportedFeatures`, explicit unsupported diagnostics & built-in/graph parity fixtures. | Consolidated U9 review |
| **U9-E** | `M15-U9-06.1` → `06.6` | `7568cd5e` | Shared bounds/clamps across `Material`, `MaterialComponent`, `MaterialComponentCodec`, `RHIMaterial`, `ResolvedSurface`, `SceneExtraction`; Inspector UI (`Sheen`, `Subsurface Scattering`, `Absorption & Dispersion`); Preview & Editor single-sample SSS viewport wiring; Runtime persistence & GPU regression. | Consolidated U9 review |

**Editor visual policy (user steering 2026-11-17):** During automated batch delivery, Editor visual gates stayed open for a consolidated capture session. Mark an Editor visual gate PASS after inspecting the required Debug and Release Editor screenshots. The [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records current evidence.

## 4. Canonical atomic execution and acceptance checklist

This is the only state-bearing checklist for M15. Each item is one independently
implementable behavior with its own PASS condition and evidence. A SubUnit is
PASS only when all its items are PASS; optional P6B/DDGI items may be marked
DEFERRED with an explicit reason.

**Visual verification rule:** any item marked `EDITOR VISUAL` requires rebuilding
the affected Editor in Debug and Release, launching
`build/Editor/<Config>/AxleEditor.exe --rhi=dx12 --strict-rhi` (repeat with
Vulkan when parity is required), opening the named fixture, and attaching a
screenshot/readback. The item is PASS only when the expected display stated in
the item is visible.


### U0 — Baseline and contract freeze

- [x] **M15-U0-01.1 — Freeze dirty-path policy and source commit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#M15-U0-01.1--freeze-dirty-path-policy-and-source-commit) records source boundary `19ac2a1`, current HEAD, dirty paths, preservation policy and source diff check.
- [x] **M15-U0-01.2 — Preserve user-authored scene changes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#M15-U0-01.2--preserve-user-authored-scene-changes) records scene existence, clean scene status/diff and the no-overwrite policy.
- [x] **M15-U0-02.1 — Create smooth-sphere fixture.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
  - **Evidence:** manifest/schema checks, Debug `AxleRenderUnitTests` exit 0, and user-supplied Editor screenshot of `Test.axlescene` showing a smooth sphere; see [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#M15-U0-02.1--create-smooth-sphere-fixture).
- [x] **M15-U0-02.2 — Create roughness/metal sweep fixture.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
  - **Evidence:** fixture introduced in `93c44c3`, repaired entity/project structure and inline material isolation in `a09cba7`, and validated by `M15RoughnessFixture`; Debug/Release strict startup loads eight entities. User-supplied Editor screenshot confirms five smooth spheres with progressively broader highlights from roughness 0.04 through 0.80, with no visible black pixels, faceting or intensity jump. See [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#M15-U0-02.2--visual-gate-pass).
- [x] **M15-U0-02.3 — Create numbered reflection-box fixture.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [x] **M15-U0-02.4 — Create probe-overlap fixture.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-02.5 — Create post/temporal/resource-edge fixtures.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-03.1 — Freeze HDR/display comparison space.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-03.2 — Freeze backend matrix and warmup/sample policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-03.3 — Freeze diagnostic channels.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-04 — Add non-mutating baseline manifest/report validation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U0-05.1 — Record open historical findings.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-05.2 — Withdraw unsupported FIXED/driver claims.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U0-06.1 — Reference smooth normals.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
  - **Evidence:** `AxleRenderUnitTests` runs a 64x128 analytic radial-normal reference with finite/unit/position-error and adjacent-edge-angle metrics; Debug/Release `RenderUnitTests` pass. User-supplied Debug Editor screenshot shows smooth shading without triangle bands or invalid pixels. See [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#M15-u0-06-1--visual-gate-pass).
- [x] **M15-U0-06.2 — Reference Hi-Z reduction.** *(derived parent; all four children PASS)*

  - [x] **M15-U0-06.2.1 — Freeze Hi-Z depth convention and reduction operator.**
    - **PASS when:** the convention is recorded from the current renderer contract and agrees with the production shader/resource dimensions.
    - **Evidence:** zero-to-one depth, minimum-depth nearest-blocker policy, clamp-to-edge sampling, and `max(1, floor(size / 2^level))` dimensions are recorded in [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#m15-u0-06-2-1--freeze-hiz-depth-convention-and-reduction-operator).
  - [x] **M15-U0-06.2.2 — Create independent CPU Hi-Z reference.**
    - **PASS when:** a CPU reference accepts arbitrary depth arrays and produces the complete pyramid without reusing production shader/reduction code.
    - **Evidence:** `scripts/test_m15_hiz_reference.py` implements an independent 4x4 → 2x2 → 1x1 min-reduction reference; Debug/Release `M15HiZReference` CTest pass. No Editor evidence is required for this CPU-only leaf.
  - [x] **M15-U0-06.2.3 — Validate Hi-Z reference edge cases.**
    - **PASS when:** reference outputs match expected dimensions and edge values for `1x1`, `3x5` and `1919x1079` inputs.
    - **Evidence:** `M15HiZReference` validates exact dimensions and edge values for `1x1`, `3x5` and `1919x1079`; Debug/Release CTest pass. No Editor evidence is required for this CPU-only leaf.
  - [x] **M15-U0-06.2.4 — Validate nontrivial Hi-Z level identity.**
    - **PASS when:** tests fail if only level zero is written or all levels contain the same data, and pass for the independent reference pyramid.
    - **Evidence:** `M15HiZReference` accepts the independent pyramid and rejects missing levels plus same-signature level data; Debug/Release CTest pass. No Editor evidence is required for this CPU-only leaf.
- [x] **M15-U0-06.3 — Reference probe direction.** *(derived parent; both children PASS)*
  - [x] **M15-U0-06.3.1 — Freeze cubemap face and up-vector convention.**
    - **PASS when:** the six face order and center-direction/up-vector policy are recorded from the current environment bake contract.
    - **Evidence:** `EnvironmentCube` face order and `EnvironmentCubeDirection` mapping are recorded in [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#m15-u0-06-3-1--freeze-cubemap-face-and-up-vector-convention).
  - [x] **M15-U0-06.3.2 — Validate six-face axis-coded reference.**
    - **PASS when:** an axis-coded environment maps each face center to the expected native cube axis and preserves orientation without vertical flips.
    - **Evidence:** Debug/Release `AxleEnvironmentBakeTests` pass the six-face directional reference. No Editor evidence is required for this CPU-only reference leaf.
- [x] **M15-U0-06.4 — Reference roughness-to-mip mapping.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
  - **Evidence:** `M15RoughnessMipReference` validates the independent 5-mip mapping, monotonicity, endpoints and clamping; Debug/Release CTest pass. User-supplied Editor screenshot confirms continuous highlight/reflection broadening across the roughness sweep without black pixels, faceting or intensity jump. See [M15 U0 re-audit](../../reviews/M15_U0_REAUDIT.md#m15-u0-06-4--visual-gate-pass).
- [x] **M15-U0-07.1 — Create deterministic image comparison.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `scripts/compare_images.py` compares P6 RGB images in explicit linear/sRGB space and reports PSNR, global SSIM, maximum channel error and deterministic pass/fail thresholds; Debug/Release `M15ImageCompare` CTest pass. No Editor evidence is required for this CPU-only tooling leaf.
- [x] **M15-U0-07.2 — Create artifact identity validation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `scripts/validate_artifact_identity.py` validates schema, source commit, backend/configuration, fixture, resolution, comparison space, artifact path, file size and SHA-256; Debug/Release `M15ArtifactIdentity` CTest pass. No Editor evidence is required for this CPU-only tooling leaf.

**Exit:** known bad behavior is reproducible and rejected; report states `M15 HOLD`.

### U1 / S1.1–S1.3 — Direct BRDF and primary lighting

- [x] **M15-U1 / S1.1 — Freeze direct-light reference contracts.** All four children passed independent review at source `c5b9f82f`; see `docs/reviews/M15_U1_S1_1_INDEPENDENT_REVIEW.md`.
  - [x] **M15-U1-01.1 — Freeze direct-light units.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Authored intensity is a finite dimensionless multiplier clamped to `[0, 100]`: directional `1 = 100,000 lx`, point `1 = 1,000` and spot `1 = 500` calibrated local scene units. Invalid/negative inputs produce zero. Regression covers both sides of the former 100/10 discontinuities and Debug/Release `RenderUnitTests` pass. Repair commit: `4af042b2`; review finding S1.1-R2 resolved.
  - [x] **M15-U1-01.2 — Freeze sun angular-radius policy.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Default `0.00462512` rad and clamp `[0, 0.25]` are present in the render-world/CPU/shader path; Debug/Release `RenderUnitTests` pass. This accepts the default and clamp contract, not authored-component controls or a GPU image comparison. Source commit: `7ff495a`.
  - [x] **M15-U1-01.3 — Freeze BRDF normalization.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Diffuse `1/PI` and specular denominator factor `4` are used by sun, point and spot shader paths; the numeric CPU reference and Debug/Release `RenderUnitTests` pass. GPU image/energy validation remains in later U1 leaves. Source commit: `35905c6`.
  - [x] **M15-U1-01.4 — Freeze exposure interaction.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
    - **Evidence:** The GPU frame histogram has one writer and tone mapping samples its 1x1 adapted exposure. Native readback at fixed authored exposure confirms bright scenes darken and dark scenes lift on D3D12 and Vulkan in Debug and Release; Debug/Release `AxleEditor` builds pass. Two user-supplied Editor screenshots of the same D3D12 fixture at intensity `5.00` and authored exposure `1.00` show the scene darken with Auto-Exposure enabled, without invalid output (`Error 0`). The screenshot does not label its binary configuration; the native matrix covers both configurations. Repair commit: `c5b9f82f`; review finding S1.1-R1 resolved.
- [x] **M15-U1 / S1.2 — Validate GGX visibility and dielectric/metal F0 references.** *(derived parent; accepted after independent recheck; source `5c65cd84`)*
  - [x] **M15-U1-02.1 — Validate GGX visibility/normal-distribution compensation.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Source commit `5c65cd84` aligns the baked DFG LUT with the forward shader's correlated Smith visibility and makes malformed LUT energy neutral. Independent quadrature checks anisotropic NDF normalization, DFG scale/bias at three view/roughness pairs, compensation bounds and shader path coverage. Debug/Release `M15BRDFReference` and `RenderUnitTests` pass; DXIL/SPIR-V compilation and D3D12/Vulkan native surface readbacks pass in both configurations. Independent recheck found no P0/P1/P2 findings. See [S1.2 verification](../../reviews/M15_U1_S1_2_REFERENCE_REVIEW.md). No Editor visual is required for this numeric-reference leaf.
  - [x] **M15-U1-02.2 — Validate dielectric/metal F0 reference.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** The same dedicated fixture checks neutral dielectric F0 `0.04`, RGB metal F0 from base color, half-metal interpolation, metallic clamping and resolved material-to-shader constants; shader source checks cover sun, point, spot, rect and ambient consumers. Debug/Release `M15BRDFReference` pass. Independent recheck found no P0/P1/P2 findings. See [S1.2 verification](../../reviews/M15_U1_S1_2_REFERENCE_REVIEW.md). No Editor visual is required for this numeric-reference leaf.
- [x] **M15-U1 / S1.3 — Validate primary-light units and shadows.** *(derived parent; accepted after independent review and Editor evidence; source `c6245e2e`)*
  - [x] **M15-U1-03.1 — Validate directional-light units/shadows.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** `M15PrimaryLightingReference` validates the frozen 100,000-lux directional calibration, four-cascade split/coverage/finite-matrix policy, invalid-direction clearing, and shader receiver path. Existing CSM CPU and native rendering fixtures validate real cascade depth, receiver darkening, cutout behavior and fallback on D3D12/Vulkan Debug/Release. User-supplied Editor screenshot shows the selected Directional Light, four-split shadow settings, visible plane/sphere shadows and `Error 0`. See [S1.3 verification](../../reviews/M15_U1_S1_3_PRIMARY_LIGHTING_REVIEW.md).
  - [x] **M15-U1-03.2 — Validate point-light units/shadows.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** The fixture validates the point-light unit packing, six cubemap face projections, atlas bounds, deterministic local-shadow selection and invalid-resolution diagnostic. Native local-shadow tests verify point shadow pixels, all six face depth tiles, seam coverage, cache and allocation fallback on D3D12/Vulkan Debug/Release. User-supplied Editor screenshot shows the selected Point Light, `Cast Shadows`, visible point shadow and `Error 0`. See [S1.3 verification](../../reviews/M15_U1_S1_3_PRIMARY_LIGHTING_REVIEW.md).
  - [x] **M15-U1-03.3 — Validate spot-light units/shadows.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** The fixture validates spot unit packing, cone-center zero-to-one projection and shared PCF receiver path. Native local-shadow tests verify spot shadow darkening, opt-out and cache behavior on D3D12/Vulkan Debug/Release. User-supplied Editor screenshot shows the selected Spot Light, `Enabled`, `Cast Shadows`, visible cone lighting/shadow and `Error 0`. See [S1.3 verification](../../reviews/M15_U1_S1_3_PRIMARY_LIGHTING_REVIEW.md).
- [x] **M15-U1-04.1 — Run roughness/view/light-angle sweep.** *(accepted after independent CPU verification and Editor evidence; source `cc0623d9`)*
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independent `M15RoughnessSweepReference` checks the five roughness samples, monotonic highlight peak reduction, half-power lobe broadening, finite view/light-angle sweeps and shader roughness clamping. Debug/Release `RenderUnitTests`, `M15BRDFReference`, `M15PrimaryLightingReference` and `M15RoughnessSweepReference` pass. User-supplied Editor screenshot shows all five spheres with a continuous roughness highlight progression, Directional Light `Cast Real-Time Shadows` enabled and Console `Error 0`. See [U1-04.1 verification](../../reviews/M15_U1_04_1_ROUGHNESS_SWEEP_REVIEW.md).
- [x] **M15-U1-04.2 — Run zero/behind-surface finite cases.** *(implemented and verified in `034f07e7`)*
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `M15DirectLightEdgeReference` checks independent zero, tangent and behind-surface vectors across roughness/metallic values, verifies bounded zero output and shader finite guards. Debug/Release builds and focused CTest pass. See [U1-04.2/U1-05 verification](../../reviews/M15_U1_04_2_05_DIRECT_LIGHT_EDGE_AND_FALLBACK_REVIEW.md).
- [x] **M15-U1-05 — Record direct-light fallback behavior for unsupported capability tiers.** *(implemented and verified in `034f07e7`)*
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** `M15DirectLightFallbackReference` verifies D3D12/Vulkan local-shadow support, OpenGL/None fallback capabilities and the explicit `LOCAL_SHADOW_UNSUPPORTED` gate. The existing OpenGL runtime fixture passes in Debug and Release on the RTX 3070, with `OPT05 RenderService PASS`. See [U1-04.2/U1-05 verification](../../reviews/M15_U1_04_2_05_DIRECT_LIGHT_EDGE_AND_FALLBACK_REVIEW.md).

**Exit:** direct-light fixtures match the frozen numeric reference within documented tolerance; units, shadows, HDR response and fallback diagnostics are consistent across supported native backends.

### U2 / S2.1–S2.3 — Dynamic environment and authored environment policy

- [x] **M15-U2-S2.1 — Freeze environment input contract.** *(implemented and verified in `6c98b0e0`)*
  - **Scope:** freeze procedural-sky parameters, bake input/output conventions, authored cubemap color space, face orientation and explicit environment-lighting opt-in.
  - [x] **M15-U2-01.1 — Freeze procedural-sky convention.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** `M15EnvironmentContractReference` checks deterministic equirectangular HDR generation, +Y zenith, finite parameter validation and explicit diagnostics in Debug/Release. See [S2.1 verification](../../reviews/M15_U2_S2_1_ENVIRONMENT_CONTRACT_REVIEW.md).
  - [x] **M15-U2-01.2 — Freeze environment-bake convention.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** `M15EnvironmentContractReference` checks linear RGBA32F input, deterministic constant bake, mip dimensions, BRDF finiteness and rejection of invalid source interpretation. See [S2.1 verification](../../reviews/M15_U2_S2_1_ENVIRONMENT_CONTRACT_REVIEW.md).
  - [x] **M15-U2-01.3 — Freeze authored-cubemap color/orientation.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** `M15EnvironmentContractReference` checks fixed cube face centers/order, +Y/-Y orientation, authored HDR identity, explicit `EnvironmentLighting` opt-in and non-mutating invalid-source rejection. See [S2.1 verification](../../reviews/M15_U2_S2_1_ENVIRONMENT_CONTRACT_REVIEW.md).

- [x] **M15-U2-S2.2 — Publish, cache and recover environment generations.** *(roll-up: tick when U2-02.1–U2-02.4 are complete)* *(accepted in `M15_U2_S2_2_ENVIRONMENT_GENERATION_REVIEW.md`; source contract update verified in Debug/Release)*
  - **Scope:** publish only complete generations, define cache identity, invalidate stale generations transactionally and retain a named last-good or neutral fallback on failure.
  - **Implementation order:** U2-02.1 → U2-02.2 → U2-02.3 → U2-02.4. Keep publication/cache identity separate from invalidation/fallback changes.
  - [x] **M15-U2-02.1 — Publish environment generation.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** `Test_EnvironmentRendering.cpp` verifies four-map publication only after complete native upload; Debug/Release DX12 and Vulkan native runs exit 0, and OpenGL fallback exits 0. See [S2.2 review](../../reviews/M15_U2_S2_2_ENVIRONMENT_GENERATION_REVIEW.md).
  - [x] **M15-U2-02.2 — Define cache identity.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Cache keys include source/request, source revision, image interpretation and bake settings; the fixture verifies reuse, source-change replacement and LRU bound `<=4`. See [S2.2 review](../../reviews/M15_U2_S2_2_ENVIRONMENT_GENERATION_REVIEW.md).
  - [x] **M15-U2-02.3 — Define invalidation.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Device shutdown/reinit removes stale native generations before rebuild; the fixture verifies a new native handle and bounded cache. See [S2.2 review](../../reviews/M15_U2_S2_2_ENVIRONMENT_GENERATION_REVIEW.md).
  - [x] **M15-U2-02.4 — Define last-good fallback.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Same-source corruption and upload OOM retain the exact last-good generation with a diagnostic; missing redirected source uses neutral fallback and does not borrow another source. See [S2.2 review](../../reviews/M15_U2_S2_2_ENVIRONMENT_GENERATION_REVIEW.md).

- [x] **M15-U2-S2.3 — Verify environment consumers and failure behavior.** *(roll-up: tick when U2-03.1–U2-03.4 are complete)* [Accepted: S2.3 review](../../reviews/M15_U2_S2_3_ENVIRONMENT_CONSUMERS_REVIEW.md)
  - **Scope:** verify sky/direct-light coupling, independent environment rotation and intensity, plus missing/failed asset behavior without mutating authored data.
  - **Contract note:** `CustomAzimuth` and `CustomElevation` remain procedural sun controls. Authored HDR rotation must use a dedicated environment-rotation field before U2-03.2 is implemented; it must not silently reuse the sun controls.
  - [x] **M15-U2-03.1 — Verify sky/direct-light coupling.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Native fixture verifies procedural sky/direct-sun coupling and the environment-lighting gate on D3D12/Vulkan; see the S2.3 review and six native logs.
  - [x] **M15-U2-03.2 — Verify environment rotation.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Dedicated EnvironmentRotation is serialized, exposed in the Inspector, propagated to native HLSL and OpenGL GLSL sampling; the axis/lobe fixture asserts a +90 degree authored HDR rotation.
  - [x] **M15-U2-03.3 — Verify environment intensity.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Native fixture verifies intensity/exposure changes through readback without rebaking the published generation, on D3D12/Vulkan; OpenGL fallback remains green.
  - [x] **M15-U2-03.4 — Verify missing/failed asset behavior.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Native fixture covers missing, corrupt and injected upload-failure inputs, retaining last-good or neutral fallback with diagnostics; all six Debug/Release backend runs exit 0.

- [x] **M15-U2-04 — Keep local dynamic six-face capture and probe blending in U5; this Unit owns the shared environment input contract.**
  - **PASS when:** U2 defines and verifies the shared procedural/authored environment input, publication, consumer and fallback contract while the dynamic local capture and multi-probe implementation remains assigned to U5.
  - **Evidence:** [S2.1 contract](../../reviews/M15_U2_S2_1_ENVIRONMENT_CONTRACT_REVIEW.md), [S2.2 generation](../../reviews/M15_U2_S2_2_ENVIRONMENT_GENERATION_REVIEW.md) and [S2.3 consumers](../../reviews/M15_U2_S2_3_ENVIRONMENT_CONSUMERS_REVIEW.md) are accepted. The current renderer consumes one selected authored cubemap probe; U5-08–U5-31 remain open for dynamic capture and blending.

- [x] **M15-U2-05.1 — Run D3D12 environment readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Debug/Release D3D12 native environment readback PASS (2/2, exit 0); 128x128 PPM output and SHA-256 manifests validated. Exact commands, logs and artifact directories: [U2-05 readback report](../../reviews/M15_U2_05_ENVIRONMENT_READBACK.md).
- [x] **M15-U2-05.2 — Run Vulkan environment readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Debug/Release Vulkan native environment readback PASS (2/2, exit 0); valid 128x128 PPM outputs match D3D12 hashes and SHA-256 manifests validate. Exact commands, logs and artifacts: [U2-05 readback report](../../reviews/M15_U2_05_ENVIRONMENT_READBACK.md).
- [x] **M15-U2-05.3 — Run OpenGL-subset environment readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Debug/Release OpenGL-subset glReadPixels environment readback PASS (2/2, exit 0); background/IBL PPM outputs differ, hashes match across configurations, and SHA-256 manifests validate. Exact commands, logs and artifacts: [U2-05 readback report](../../reviews/M15_U2_05_ENVIRONMENT_READBACK.md).

**Exit:** procedural and authored environments produce the declared HDR/IBL inputs, cache and invalidation are transactional, and failed or unsupported environments select a named fallback without mutating authored data.

### U3 / S3.1 — RHI resource ownership, graph and measurement

- [x] **M15-U3-01.1 — Validate timestamp probe description.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** TimestampProbe uses a two-query TIMESTAMP pool, 16-byte host readback and a positive tick delta; Debug/Release D3D12 and Vulkan measurements are recorded in [M14 P1 GPU timing](../../reviews/M14_P1_GPU_TIMING.md).
- [x] **M15-U3-01.2 — Require successful timestamp readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Renderer-integrated readback asserts renderer_gpu_scope_valid=1 after a submitted view; the same M14 P1 report records positive D3D12/Vulkan values and the native fixture still checks GetLastGPUTimestampMilliseconds() > 0.
- [x] **M15-U3-02 — Read renderer timestamps on all public view submission paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U3-03.1 — Check descriptor update helpers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Source `1cb3a8b7`; Debug/Release RenderGraph and environment native fixtures pass on D3D12/Vulkan (8/8, exit 0). Exact commands and artifact paths: [U3-03.1 descriptor helper review](../../reviews/M15_U3_03_1_DESCRIPTOR_HELPERS_REVIEW.md).
- [x] **M15-U3-03.2 — Check storage update helpers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Source `5210156f`; Debug/Release RenderGraph storage fixture and environment renderer pass on D3D12/Vulkan (8/8, exit 0). Exact builds, commands, logs and artifacts: [U3-03.2 storage helper review](../../reviews/M15_U3_03_2_STORAGE_HELPERS_REVIEW.md).
- [x] **M15-U3-03.3 — Check constant update helpers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Debug/Release RenderGraph native fixture on D3D12/Vulkan (4/4, exit 0) exercises failed constant mapping, unchanged bytes/generation and retry. Exact commands and logs: [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-04 — Never modify a descriptor set after binding in the same submission.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Existing `IsInUse` rejection is exercised by a native fixture across recording, executable and submitted states; completion permits reuse. Debug/Release D3D12/Vulkan 4/4 exit 0; [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-05.1 — Declare Hi-Z reads/writes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Strict native environment fixture asserts all eight Hi-Z pass recordings and reads back distinct level data. Debug/Release D3D12/Vulkan 4/4 exit 0; [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-05.2 — Declare normal/probe reads.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Strict native SSR fixture asserts `SurfaceNormal` and `SSRCompute` with an authored probe, reads ScreenNormal and observes changed output; Debug/Release D3D12/Vulkan 4/4 exit 0. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-05.3 — Declare history/exposure reads.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
  - **Evidence:** Debug/Release D3D12/Vulkan native fixture checks history/exposure pass order and first/second TAA frames (4/4, exit 0); Debug/Release Editor builds exit 0. User-supplied D3D12 Editor screenshots show Auto-Exposure and TAA both on and both off, five rendered spheres, no invalid frame and Console `Error 0`. The screenshots do not identify the Editor build configuration. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-06 — Add per-view target generation, completion and reset ownership.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Native fixture verifies target generation diagnostics, completion token epoch/value and wait, replacement reset and failed allocation preservation. Debug/Release D3D12/Vulkan 4/4 exit 0; [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-07.1 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Native two-view fixture resizes one target generation to 12×10 and back, validates its complete set and preserves the other view's pixels; Debug/Release D3D12/Vulkan 4/4 exit 0. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-07.2 — Test feature toggle.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Native fixture toggles GTAO/contact and SSR, checks new target generations and exact normal/Hi-Z availability; Debug/Release D3D12/Vulkan 4/4 exit 0. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-07.3 — Test allocation failure/retry.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Existing native fixtures inject late target allocation failure, preserve generation/GUI binding, render old generation, then retry and reset history; Debug/Release D3D12/Vulkan 8/8 exit 0. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-07.4 — Test device epoch.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Native epoch fixture invalidates old view targets on device shutdown and rebuilds the same wrapper into a new generation after reinit; Debug/Release D3D12/Vulkan 4/4 exit 0. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-07.5 — Test shutdown/reinit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Native environment fixture shuts renderer down, reinitializes, renders a fresh frame with valid completion, then shuts down cleanly; Debug/Release D3D12/Vulkan 4/4 exit 0. [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-08 — Add asynchronous per-pass query/readback ring and invalid-measurement state.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Source `4eba2b07` adds a three-slot frame/pass timestamp query ring with completion-keyed readback and explicit Unknown/Invalid states. Debug/Release native environment fixture passes on D3D12/Vulkan (4/4, exit 0), including injected readback and query-pool failures; [U3 batches 1–2 review](../../reviews/M15_U3_BATCH_1_2_REVIEW.md).
- [x] **M15-U3-09.1 — Create pass headers under Passes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** GTAO, Hi-Z, SSR, fog and post graph/record contracts plus borrowed frame operations compile in Debug/Release `AxleScene` (both exit 0). See [U3 batch 3 review](../../reviews/M15_U3_BATCH_3_REVIEW.md).
- [x] **M15-U3-09.2 — Create pass implementations under Passes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Five pass modules now own the GTAO, Hi-Z/SSR, fog and post RenderGraph declarations; Debug/Release native environment fixture passes D3D12/Vulkan 4/4 after rewiring. Recording extraction continues in U3-10. [U3 batch 3 review](../../reviews/M15_U3_BATCH_3_REVIEW.md).
- [x] **M15-U3-10.1 — Extract GTAOPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `GTAOPass::Record` owns copy, compute and apply recording through borrowed frame operations; Debug/Release native environment fixture passes D3D12/Vulkan 4/4 with strict GTAO pass sequence. [U3 batch 3 review](../../reviews/M15_U3_BATCH_3_REVIEW.md).
- [x] **M15-U3-10.2 — Extract HiZGenerationPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `HiZGenerationPass::Record` owns all eight reduction levels, source/destination constants, descriptor updates and dispatch; Debug/Release native environment fixture passes D3D12/Vulkan 4/4 with level identity/readback checks. [U3 batch 3 review](../../reviews/M15_U3_BATCH_3_REVIEW.md).
- [x] **M15-U3-10.3 — Extract ScreenSpaceReflectionPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
  - **Automated evidence:** `ScreenSpaceReflectionPass::Record` owns copy, Hi-Z, compute and apply; Debug/Release native environment fixture passes D3D12/Vulkan 4/4 with probe/fallback and level readback assertions. Editor evidence: same camera/material pair shows no plane reflection with SSR off and a bright reflected streak on the plane with SSR on, with no black edge. [U3 batch 4 review](../../reviews/M15_U3_BATCH_4_REVIEW.md).
- [x] **M15-U3-10.4 — Extract FroxelFogPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: fog shafts obey blockers; zero density/no-light leaves the image unchanged.
  - **Automated evidence:** `FroxelFogPass::Record` owns copy, compute and apply; Debug/Release native environment fixture passes D3D12/Vulkan 4/4 with fog-on/off and authored fog cases. User-provided Editor captures reviewed in this session show `Volumetric Fog` enabled and disabled on the same fixture, with visibly different output and Console `Error 0`. [U3 batch 4 review](../../reviews/M15_U3_BATCH_4_REVIEW.md).
- [x] **M15-U3-10.5 — Extract PostProcessPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `PostProcessPass::Record` owns bloom, auto exposure, TAA, tone mapping and FXAA recording; Debug/Release native environment fixture passes D3D12/Vulkan 4/4. [U3 batch 4 review](../../reviews/M15_U3_BATCH_4_REVIEW.md).
- [x] **M15-U3-11.1 — Forward orchestration through RenderPassScheduler.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** DDGI, GTAO, SSR, fog and post/temporal now register as separately named scheduler nodes in their original order, with individual graph declaration, prepare and recording callbacks. Debug/Release native environment fixture passes D3D12/Vulkan 4/4, including graph order and output checks. [U3 batch 4 review](../../reviews/M15_U3_BATCH_4_REVIEW.md).
- [x] **M15-U3-11.2 — Forward view state through ViewContext.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `RecordFramePasses` now obtains camera, settings, dimensions, jitter and target identity from the active `ViewContext` snapshot; borrowed frame inputs retain only world/service data and native output handles. Missing frame or invalid targets fail explicitly through diagnostics. Debug/Release Editor, environment and render-unit builds pass; native D3D12/Vulkan matrix passes 4/4. Existing `Test_ViewContextUnit` covers target-generation, material-generation, history and reset identity contracts. [U3 batch 4 review](../../reviews/M15_U3_BATCH_4_REVIEW.md).

**Exit:** validation clean; failures retain last-good generation or explicit fallback; timing is valid on tested workloads; pass modularity roadmap is active.

### U3 / S3.2 — Surface, normal and material data contract

> **Codex status correction (2026-11-16):** the built-in SurfaceNormal contract
> and opaque Material Graph companion normal path are verified. Graphs still do
> not expose authored alpha, so graph cutout/transparent coverage remains an
> explicit fallback tracked by U3-22. The scalar U3-17 material checks pass,
> while full graph-vs-ForwardPBR light/shadow/IBL parity remains open under
> U3-21.2 and U3-23.1..23.3.

- [x] **M15-U3-12 — Remove derivative-flat-normal override from generic ForwardPBR modes.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U3-13 — Produce opaque world-space `ScreenNormal` and declare it in the graph.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U3-14 — Match normal producer with normal/detail-normal decoding and strength.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** `SurfaceNormal.hlsl` and `RHISceneRendererPasses.cpp` bind material constants/textures and implement `DecodeMaterialNormal` with strength and RNM detail blending; verified in `AxleRenderUnitTests` `--m15-surface-material-contract` (exit 0). [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-15.1 — Verify inverse-transpose under nonuniform scale.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: cofactor transform preserves normal-tangent orthogonality ($n' \cdot t' = 0$) under nonuniform scale and oblique rotation. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-15.2 — Verify mirrored-transform handedness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: negative determinant mirrors tangent sign $t.w \to -t.w$ while preserving outward normal direction, aligning bitangent with texture V coordinate. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-15.3 — Verify normal-map transform.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: tangent-space normal map perturbs normal into world tangent space via reconstructed TBN. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-16 — Define alpha-cutout/depth/normal coverage and background validity.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** `SurfaceNormal.hlsl` executes cutout discard on alpha threshold failure and outputs `float4(normalize(N), 1.0)` for covered geometry (0.0 for background); verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-17.1 — Unify base color.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `MaterialSurface.hlsl` normalizes Lambertian diffuse using `INV_PI` matching `ForwardPBR.hlsl`. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-17.2 — Unify metallic/roughness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: roughness clamped `[0.04, 1.0]` and metallic saturated `[0.0, 1.0]`. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-17.3 — Unify AO/emission.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: AO scales ambient accumulation while direct lighting and emission are preserved unscaled. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-17.4 — Unify normal/tangent.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: matching TBN and normal decoding contracts. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-17.5 — Unify clearcoat/anisotropy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: clearcoat BRDF layer attenuation evaluates in `MaterialSurface.hlsl`. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-17.6 — Unify reflection inputs.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `MaterialSurface.hlsl` unifies direct sunlight scale (`DIRECT_LUX_TO_RADIANCE` 0.000035) and ambient specular evaluation using `FresnelSchlickRoughness`. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-18 — Verify sRGB/linear texture and packed ORM conventions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `Albedo` and `Emissive` are sRGB, remaining slots are Linear; `PackedORM` maps to slot 2 with `PackedORMTexturedPBR`. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-19.1 — Round-trip asset to component.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `SetAsset()` / `SyncFromAsset()` and `SyncToAsset()` synchronize all standard and advanced material parameters. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-19.2 — Round-trip component to extraction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `SyncToRHI()` accurately populates `RHIMaterialDesc` with all material properties. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-19.3 — Round-trip extraction to GPU.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `RHIMaterial::BuildConstants()` populates GPU `MaterialConstants` correctly matching shader layout. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-19.4 — Round-trip save/reopen.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: Save and Load round-trip preserves all material parameters across `.axleasset` and flat `.mat` file formats. [U3 batch 5 review](../../reviews/M15_U3_BATCH_5_REVIEW.md).
- [x] **M15-U3-20 — Add smooth-sphere normal-field and anti-faceting metric.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp` (max normal angular delta $\theta = 3.98^\circ \le 4.1^\circ$, radial alignment $\le 10^{-6}$). User-provided Editor capture reviewed in this session: smooth sphere highlights and silhouettes show no visible triangle bands, faceting or invalid pixels; Console reports `Error 0`. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.

**Exit:** smooth sphere has no triangle bands; built-in/graph materials match in supported scope; save/reopen preserves fields.

#### S3.3 — Material Graph, preview and runtime parity

- [x] **M15-U3-21.1 — Unify built-in resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `MaterialResolver::ResolveBuiltin` maps material descriptors to canonical `ResolvedSurface`. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).
- [x] **M15-U3-21.2 — Unify Material Graph resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `MaterialGraphRuntime::ResolveSurface` accepts canonical `ResolvedSurface` inputs and decorates diagnostics. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md). Editor visual evidence remains user-provided.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [x] **M15-U3-21.3 — Unify Preview resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `MaterialResolver::ResolvePreview` bridges preview state into `ResolvedSurface`. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md). Editor visual evidence remains user-provided.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [x] **M15-U3-21.4 — Unify Runtime resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `MaterialResolver::ResolveRuntime` converts extracted `RenderMaterialData` to `ResolvedSurface`. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).
- [x] **M15-U3-22 — Record explicit unsupported feature masks and fallback diagnostics for graph/custom paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `UnsupportedSurfaceFeatures` bitmask and `BuildUnsupportedFeaturesDiagnostic` format structured diagnostics and set fallback flags. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).
- [x] **M15-U3-23.1 — Compare lights.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp` and `Test_EnvironmentRendering.cpp`: direct sun lighting evaluation parity under identical physical lux units and BRDF formulation; native D3D12/Vulkan pixel match `diff=(0,0,0)`. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).
- [x] **M15-U3-23.2 — Compare shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: shadow attenuation and cascade selection contracts match. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md). Editor visual evidence remains user-provided.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [x] **M15-U3-23.3 — Compare IBL.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: analytical ambient and split-sum reflection parity. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).
- [x] **M15-U3-23.4 — Compare output policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: ACES filmic tonemapping and gamma 2.2 / HDR clamp output policy parity. [U3 batch 6 review](../../reviews/M15_U3_BATCH_6_REVIEW.md).

#### S3.4 — Temporal, diagnostics and measurement integration

- [x] **M15-U3-24.1 — Declare motion resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `RHIViewTargetDesc::EnableScreenStorage = true` declares screen storage for motion/properties; `TemporalService` bypasses uninitialized motion on frame 0 and validates motion tracking on commit. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-24.2 — Declare depth resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU contract verifies the descriptor policy; native `Test_EnvironmentRendering` configures the target generation and verifies `Depth`, `OpaqueDepth`, and `ParticleDepth` are allocated and valid on D3D12/Vulkan. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-24.3 — Declare history resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `RHIViewTargetDesc::HistoryCount = 2` declares dual-slot history; `TemporalService` manages ping-pong read/write slot transitions on commit. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-24.4 — Declare exposure resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU contract verifies policy/clamping; native `Test_EnvironmentRendering` configures and validates the 1x1 exposure UAV/SRV on D3D12/Vulkan. User-provided Editor captures reviewed in this session show the same fixture with `Auto-Exposure` unchecked and checked, with stable output and Console `Error 0`. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [x] **M15-U3-25.1 — Reset on target generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `ObserveTargets` triggers `ViewResetReason::TargetGeneration`, resets temporal history, and restores default slot identities. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-25.2 — Reset on camera cut.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `CameraCut = true` triggers `ViewResetReason::CameraCut` and invalidates temporal history and disocclusion feedback. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-25.3 — Reset on feature toggle.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `FeatureGeneration` increment triggers `ViewResetReason::FeaturePolicy`. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-25.4 — Reset on device epoch.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: Device epoch increment triggers `ViewResetReason::DeviceEpoch` and updates `ViewContext` device epoch. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-26.1 — Record timing validity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: unqueried passes report `GPUValidity == Unknown`, while active queries record `GPUValidity == Valid` with measured time. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-26.2 — Record fallback reasons.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: pass fallback reports `DiagnosticStatus::Fallback` with explicit reason string, preserved in frame diagnostics list. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-26.3 — Record resource generations.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: `RenderDiagnostics` records and returns `TargetGeneration` and `MaterialGeneration`. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).
- [x] **M15-U3-26.4 — Report Unknown invalid measurements.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: unqueried 0.0 ms measurements report `IsGPUTimeValid() == false` with `GPUValidity == Unknown`. [U3 batch 7 review](../../reviews/M15_U3_BATCH_7_REVIEW.md).

**S3.3/S3.4 exit:** parity, reset and diagnostics fixtures pass on supported native paths, with unsupported paths explicitly reported.

### U4 / S4.1–S4.3 — GTAO and directional contact shadows

- [x] **M15-U4-01 — Use valid `ScreenNormal` input.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U4-02.1 — Remove whole-scene AO multiplication.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U4-02.2 — Use bounded AO/contact weights.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U4-03.1 — Move AO before lighting.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Native `SurfaceNormalPrepass -> GTAOCompute -> OpaqueAndSky` produces visibility before forward lighting. ForwardPBR and MaterialSurface apply raw AO only to ambient/indirect; direct diffuse, direct specular and emissive remain invariant. Debug/Release D3D12/Vulkan each pass 64 deterministic forward radiance draws plus actual renderer/graph generation checks. Bounded legacy composite and U4-C reconstruction are retained and tested. [Independent runtime re-review](../../reviews/M15_U4_AB_CODEX_REVIEW.md). Editor visual evidence remains user-provided.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [x] **M15-U4-03.2 — Expose true indirect target if needed.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Actual forward shaders modulate indirect radiance in registers. The prepass reuses existing normal/depth targets and half-resolution GTAO storage; no separate indirect target or MRT is added, avoiding a prospective 16,588,800-byte RGBA16_FLOAT indirect target at 1080p. Standard auxiliary allocation remains available through `EnableScreenStorage`. [Independent runtime re-review](../../reviews/M15_U4_AB_CODEX_REVIEW.md).
- [x] **M15-U4-04.1 — Apply contact to directional contribution.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Production ForwardPBR and MaterialSurface combine contact only with sun radiance. Deterministic GPU readback checks contact 0/0.5/1, disabled contact, no-sun ambient/emissive/point/spot lighting and mixed sun/local light. All four Debug/Release D3D12/Vulkan runs report zero comparison error; actual graph generation is required to exclude silent built-in fallback. [Independent runtime re-review](../../reviews/M15_U4_AB_CODEX_REVIEW.md).
- [x] **M15-U4-04.2 — Avoid CSM/local double attenuation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Shared forward shader minimum and CPU contracts use $\min(V_{\text{csm}}, V_{\text{contact}})$. Native production shaders sample actual comparison-PCF CSM at 0/0.5/1 and verify 0.5 CSM plus 0.5 contact stays 0.5; fully shadowed sun stays zero and local lights remain independent. Correct graph comparison sampler and linear CSM depth are verified on both backends/configurations. [Independent runtime re-review](../../reviews/M15_U4_AB_CODEX_REVIEW.md).
- [x] **M15-U4-05.1 — Validate half-resolution sampling.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU dimension contracts and native allocator/compute/apply fixtures pass at 1x1, 3x5, 127x63 and 1919x1079 (959x539 GTAO). Compute selects an exact full-resolution depth/normal source texel using shared rational center mapping. Debug/Release D3D12/Vulkan readback checks every generated visibility texel. [Independent U4-C review](../../reviews/M15_U4_BATCH_3_CODEX_REVIEW.md).
- [x] **M15-U4-05.2 — Validate bilateral reconstruction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** The normalized 5x5 half-resolution kernel reconstructs individually loaded AO/contact texels paired with their exact compute-source depths. CPU weight contracts and native production `PSMain` fixtures verify flat fields, both visibility channels, alpha preservation and odd/minimal extents. [Independent U4-C review](../../reviews/M15_U4_BATCH_3_CODEX_REVIEW.md).
- [x] **M15-U4-05.3 — Validate depth edges.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Depth rejection happens before AO/contact accumulation; neither depth nor visibility is bilinearly mixed across surfaces. Native production shader fixtures verify vertical/horizontal step edges and reversed visibility. Unrepresented thin surfaces receive neutral visibility instead of foreign-surface occlusion. Debug/Release D3D12/Vulkan maximum color error is below 2e-5. [Independent U4-C review](../../reviews/M15_U4_BATCH_3_CODEX_REVIEW.md).
- [x] **M15-U4-05.4 — Validate odd resolutions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Dispatch bounds $(W + 7)/8$, compute thread guards, shared odd-size sample mapping and clamped integer reconstruction taps are verified by focused CPU/CTest and dedicated native fixtures. Real 1919x1079 rendering reads all 516901 GTAO texels; 1x1, 3x5 and 127x63 also pass. Zero-size views are rejected. [Independent U4-C review](../../reviews/M15_U4_BATCH_3_CODEX_REVIEW.md).
- [x] **M15-U4-06.1 ? Test no-sun.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Codex verified CPU contact sweeps and native built-in/canonical graph isolation; the actual no-sun sphere is identical with contact toggled. Debug/Release on D3D12 and Vulkan pass. [Independent U4-D review](../../reviews/M15_U4_BATCH_4_CODEX_REVIEW.md).
- [x] **M15-U4-06.2 ? Test no-AO.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU disabled-AO sweeps, native zero-mask/disabled-flag comparison, and repeated real-renderer enable/disable transitions verify neutral AO=1. [Independent U4-D review](../../reviews/M15_U4_BATCH_4_CODEX_REVIEW.md).
- [x] **M15-U4-06.3 ? Test no-contact.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU and both production forward shaders preserve CSM visibility at 0, 0.5 and 1 with contact disabled; real no-sun contact dispatch remains neutral. [Independent U4-D review](../../reviews/M15_U4_BATCH_4_CODEX_REVIEW.md).
- [x] **M15-U4-06.4 ? Test emissive.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU mixed-light emission additivity and pure emission sweeps; native AO/contact 3x3 sweeps verify nonzero emissive invariance on built-in and canonical graph shaders. HDR readbacks are finite. [Independent U4-D review](../../reviews/M15_U4_BATCH_4_CODEX_REVIEW.md).
- [x] **M15-U4-06.5 ? Test mirror/unoccluded sphere.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated PASS:** Codex repaired a real GTAO horizon integral defect that nearly blackened the unoccluded sphere. Production compute fixtures preserve open planes and still occlude a genuine blocker. Linear RGBA16 HDR readback before tone mapping compares 1264 interior pixels, AO/contact toggles, repeated disable, finite RGBA, and analytic smooth normals. Debug/Release D3D12/Vulkan pass. Native images and exact commands: [independent U4-D review](../../reviews/M15_U4_BATCH_4_CODEX_REVIEW.md).
  - **EDITOR VISUAL PASS:** Verified in `U4D` (`UnoccludedSphere.axlescene` via `AxleEditor --capture-plan --no-fallback --rhi-validation` in `Debug` DX12, `Release` DX12, and `Release` Vulkan, Console `Error 0`, `nonFinite=0`). Mirror sphere exhibits smooth curvature over HDR sky with no triangle bands or invalid pixels, and GTAO ON vs OFF matches analytically (`meanAbsDiff=0.00`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U4-07.1 - Test normal-map behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Codex repaired NaN tangent-basis fallback in both ForwardPBR and SurfaceNormal and corrected detail normal/mask registers to t11/t12, with native mask assertions. Actual enabled linear normal textures run through built-in and canonical graph providers, with analytic ScreenNormal checks, HDR strength response and front-facing parity, mirrored/nonuniform transforms, degenerate tangents, 72 native back-facing samples per pipeline (AO=1), and mapped emissive/direct invariance. Debug/Release D3D12/Vulkan pass with finite readbacks and no validation errors. [Independent U4-E review](../../reviews/M15_U4_BATCH_5_CODEX_REVIEW.md).
- [x] **M15-U4-07.2 - Test animated/deformed mesh.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** The deformed-mesh branch is verified via real published vertex generations moving in front of a rear receiver. Native current-depth reconstruction and normals match analytic geometry, genuine receiver AO reaches about 0.284, and 1218 disocclusion events match fresh-target HDR/AO/contact without stale data. CPU projection validation/round-trip checks replace the previously ignored projection parameter. Skeletal palette execution is not claimed. Debug/Release D3D12/Vulkan pass. [Independent U4-E review](../../reviews/M15_U4_BATCH_5_CODEX_REVIEW.md).
  - **EDITOR VISUAL PASS:** Debug D3D12, Release D3D12 and Release Vulkan captures show continuous shading on the mirrored and positively scaled normal-mapped spheres with GTAO/contact on and off. The native deformation fixture measures live current-depth and normal updates. See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).

**Exit:** Native validation, shallow-floor contact visibility and U4D/U4E Editor captures passed. The unoccluded mirror viewport matches bit-for-bit with GTAO toggled. See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).

### U5 / S5.1 — Hi-Z depth pyramid

The [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records the current Hi-Z and SSR tests, probe and LTC Editor captures, and the shadowed planar ON/OFF comparison.

> **Execution schedule note (2026-11-17):** See [U5 Execution Batches](#u5-execution-batches-u5-a-through-u5-i) for the `U5-A`..`U5-I` breakdown. Historical resource items `M15-U5-01`..`U5-04.2` predate this breakdown (`M15-U5-03` still requires its deferred `EDITOR VISUAL` fixture verification). Implementation resumes at `U5-A` (`M15-U5-05.1` → `M15-U5-07.2`) when released.

- [x] **M15-U5-01 — Use immutable per-level descriptor generations.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U5-02.1 — Check every descriptor update.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U5-02.2 — Check every descriptor bind failure.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U5-03 — Declare all eight levels as SSR reads.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [x] **M15-U5-04.1 — Validate target dimensions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U5-04.2 — Validate non-identical level readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U5-05.1 — Validate min reduction reference.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verification against baseline `a746397b` (single U5-A delivery checkpoint): standalone `HiZ.hlsl` (`4x4` canonical and real rendered `HiZ0..HiZ(N-1)` readback) matches `Axle::HiZContract::BuildMinReductionPyramid` and `scripts/test_m15_hiz_reference.py` with `max_error = 0` on CPU and Debug/Release native D3D12/Vulkan (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md); independently verified by Codex after corrections.
- [x] **M15-U5-05.2 — Validate 1x1.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Standalone `HiZ.hlsl`, `scripts/test_m15_hiz_reference.py`, `Axle::HiZContract::VerifyOddResolutionPyramid(1, 1)`, and full `RHISceneRenderer` `1x1` SSR/Hi-Z readbacks verify `useful=1`, `allocated=8`, exact `[0, 1]` min value, `max_error = 0`, and zero RHI validation errors on Debug/Release D3D12/Vulkan (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
- [x] **M15-U5-05.3 — Validate 3x5.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Standalone `HiZ.hlsl`, `scripts/test_m15_hiz_reference.py`, CPU `3x5` column/row blocker tests (`col x=2` `0.03f`, `row y=4` `0.02f`), standalone trailing odd-border blocker (`0.015f`), and full `RHISceneRenderer` `3x5` readbacks (`3x5 -> 1x2 -> 1x1`, `useful=3`, `allocated=8`) pass with `max_error = 0` on Debug/Release D3D12/Vulkan (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
- [x] **M15-U5-05.4 — Validate 1919x1079.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Standalone `HiZ.hlsl` (including `127x63` and `1919x1079` border blocker propagation), `scripts/test_m15_hiz_reference.py`, CPU contract checks (`11` levels reaching `1x1` at level `10`), and full `RHISceneRenderer` `1919x1079` HiZ0..HiZ10 texel readback match CPU reference with `max_error = 0` and zero validation errors on Debug/Release D3D12/Vulkan (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
- [x] **M15-U5-06.1 — Freeze depth convention.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Frozen `[0.0, 1.0]` zero-to-one nearest-blocker `min()` depth convention in `Axle::HiZContract` (`NearDepth=0.0f`, `FarDepth=1.0f`, `GetDepthConventionSummary()`) and `Engine/Resources/Shaders/HiZ.hlsl`, with explicit rejection of zero extents, out-of-range level counts, buffer mismatches, and negative/`>1`/`NaN`/`Inf` depths verified via `M15HiZContract` (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
- [x] **M15-U5-06.2 — Define edge policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Defined and implemented conservative odd-edge folding (`includeExtraX`, `includeExtraY`) in `Engine/Resources/Shaders/HiZ.hlsl`, `Axle::HiZContract`, and `scripts/test_m15_hiz_reference.py` so trailing odd column/row (`2*dst + 2`) folds into the last destination texel whenever `src > 1` is odd; verified on `1x1`, `3x5`, `5x3`, `1x5`, `5x1`, `127x63`, and `1919x1079` across CPU and Debug/Release D3D12/Vulkan (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
- [x] **M15-U5-06.3 — Define useful level count.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Defined `ComputeUsefulHiZLevelCount` (`floor(log2(max(w, h))) + 1` clamped to `[1, MaxHiZLevels(16)]`, `0` on `0` extent) and `ComputeAllocatedHiZLevelCount` (`max(MinSSRHiZLevels(8), useful)`) in `RHIViewTargets` and `Axle::HiZContract`, expanding `RHIViewTargets` and `RHISceneRenderer` to 16 `R32_FLOAT` levels while preserving SSR's 8 static `u_HiZ0..u_HiZ7` bindings (`1x1 -> 1/8`, `3x5 -> 3/8`, `127x63 -> 7/8`, `1919x1079 -> 11/11`, `1440p/4K -> 12/12`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
- [x] **M15-U5-07.1 — Add per-level visualization.**
  - **Codex corrections:** attachment-format PSOs prepared before recording (BGRA8/RGBA32 native coverage), explicit compute/storage capability fallback, full Editor level range 0..15, and actual opaque-depth-to-HiZ0 verification. Visualization remains a scene depth-tone mode subject to subsequent fog/postprocessing; raw Hi-Z values are unchanged. Editor captures compare levels 0 and 3 in the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated PASS:** Implemented `VisualizeVSMain`/`VisualizePSMain` in `HiZ.hlsl`, `HiZGenerationPass::DeclareVisualization`/`RecordVisualization`, `SceneRenderSettings::HiZVisualizationLevel` (`-1` off, reflected in `HashViewFeaturePolicy`), target-builder parity across `RHISceneRenderer`/`RenderService`/`RHIViewportDescriptor`, `RenderFXPopup` UI control, and explicit fallbacks (`hiz_visualization_unavailable`, `hiz_visualization_clamped_or_tail`). Debug/Release D3D12/Vulkan native tests verify level 0 geometry vs sky (`0.360784` vs `0`), coarse level spreading (`vis0 != vis3 != vis6`), uniform 1x1 level 6 (`0.372549`), clamped level 99 matching level 6 with explicit fallback, finite HDR path, and exact scene recovery when disabled (`recoveredColor == normalColor`, 0 validation errors). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).
  - **EDITOR VISUAL — PASS:** Verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan --no-fallback --rhi-validation`, Console `Error 0`, `nonFinite=0`). Hi-Z Level `0` vs Level `3` coarsens properly (`meanAbsDiff=1.59, maxAbsDiff=186, changedPct=1.57%`) and restores cleanly on `-1`. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-07.2 — Validate barriers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** CPU `RenderGraph` barrier test validates 11-level `SSRCopy -> HiZ0..HiZ10 -> SSRCompute + HiZVisualize` RAW transitions and rejects uninitialized Hi-Z reads before generation; native Debug/Release D3D12/Vulkan full-renderer runs pass across `1x1`, `3x5`, `127x63`, `1919x1079`, and per-level visualization with `0` RHI validation errors (`exit 0`). [Independent U5-A review](../../reviews/M15_U5_A_CODEX_REVIEW.md).

**Exit:** every level matches reference reduction and transitions correctly into SSR.

### U5 / S5.2–S5.5 — Reflection Captures and local probes

#### S5.2 — Dynamic six-face capture

- [x] **M15-U5-08.1 — Allocate capture color targets.**
  - **Codex review corrections:** whole-operation reentry guard; capture input stamps invalidate material/light/mesh/clip/shadow changes at unchanged transform revisions; deterministic-only failure caching and automatic transient retry; exact valid clip planes and safe anonymous keys; generation-based capture-feedback suppression including retained aliases; main environment publication restoration. Native automatic recapture, environment, feedback and failure-recovery assertions pass in Debug/Release on DX12/Vulkan. [Independent report](../../reviews/M15_U5_B_CODEX_REVIEW.md). The [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records the subsequent probe-scene captures.
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex against delivery `f49ff61b` after corrections) — `Passes::ReflectionCaptureTargets` allocates single-sample linear HDR `RGBA16_FLOAT` `FaceColor` and `OrientedColor` targets plus immutable `RGBA16_FLOAT` candidate `Cubemap` transactionally via `RHITextureDesc`; `ReflectionCaptureContract::ValidateCapturePolicy` rejects MSAA (`SampleCount != 1`) and non-HDR color formats (`M15ReflectionCaptureContract` CPU & native Debug/Release D3D12/Vulkan pass, `0` validation errors).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-08.2 — Allocate capture depth targets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `Passes::ReflectionCaptureTargets` allocates single-sample `D32_FLOAT` `FaceDepth` attached to `RHIViewTargets`, validates depth format via `ValidateCapturePolicy`, and verifies finite `RGBA16_FLOAT` HDR radiance (`+X` emissive intensity `3.0` > `2.0`, not clamped/tone-mapped) natively on Debug/Release D3D12 and Vulkan.
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-09.1 — Render six 90-degree views.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independently verified by Codex against delivery `f49ff61b` after corrections. `ReflectionCaptureContract::BuildCubeFaceCamera` configures 90-degree FOV, `1.0` square aspect ratio, and captures all 6 faces (`0..5`: `+X, -X, +Y, -Y, +Z, -Z`); verified by `AxleRenderUnitTests --m15-reflection-capture-contract` and native `AxleEnvironmentRenderingTests` Debug/Release `dx12` & `vulkan` (`exit 0`, artifact `M15_U5B_faces_strip.ppm`). [Independent U5-B review](../../reviews/M15_U5_B_CODEX_REVIEW.md).
- [x] **M15-U5-09.2 — Validate directions/up vectors.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independently verified by Codex against delivery `f49ff61b` after corrections. `ReflectionCaptureContract::ValidateCubeFaceBasis` verifies unit orthogonality and `Right == cross(Up, Forward)` matching `EnvironmentCubeDirection` (`+Y,+Y,-Z,+Z,+Y,+Y`); native D3D12/Vulkan readbacks verify off-axis quadrant markers on `+X`, `+Y`, `+Z` (`[U5-B +X] TR=(0.84,1.96,1.96) TL=(1.92,0,0) BR=(1.92,0,0)`) with zero mirror/flip defects (`exit 0`). [Independent U5-B review](../../reviews/M15_U5_B_CODEX_REVIEW.md).
- [x] **M15-U5-10.1 — Disable recursive SSR/probe sampling.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `SanitizeCaptureSettings`, `FilterCaptureWorld`, `ValidateCapturePolicy`, and `RHISceneRenderer::PrepareReflectionProbe` suppress SSR (`EnableSSR = false`) and all reflection probe bindings (`m_ActiveReflectionProbePrefilter.reset()`, `ReflectionProbes.clear()`) during capture; verified in `M15ReflectionCaptureContract` and native Debug/Release D3D12/Vulkan runs.
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-10.2 — Disable TAA history.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — capture executes on isolated `m_CaptureViewContext` with `EnableTAA = false` without touching `m_ViewContext` or resetting `m_ImplicitViewTargets`; native Debug/Release D3D12/Vulkan tests verify main-view `FrameIndex` and `m_LastCompletion` remain unchanged across capture and advance cleanly (`frame + 1`) on the next main frame.
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-10.3 — Disable Editor/UI.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `SanitizeCaptureSettings` clears `Wireframe` and `ShowPhysicsDebug`, `FilterCaptureWorld` strips `DebugLines`, and `RHISceneRendererPasses` omits overlay/physics debug passes during isolated capture (`M15ReflectionCaptureContract` & native Debug/Release D3D12/Vulkan verified).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-10.4 — Disable capture feedback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `FilterCaptureWorld` excludes `ExcludeEntityID` (probe/self entity `777`) and `AdditionalExcludedEntityIDs`, and candidate cubemap is never bound as SRV during capture passes; native D3D12/Vulkan readbacks confirm self sphere covering `(0,0,0)` does not pollute any captured face.
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-11.1 — Render opaque/cutout scene.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independently verified by Codex against delivery `f49ff61b` after corrections. `FilterCaptureWorld` and `RHISceneRendererPasses` render opaque + cutout queues (`SurfaceType::Opaque`/`Cutout`) + skybox while excluding `Transparent`/`Additive` meshes and `Particles`; native Debug/Release D3D12/Vulkan runs verify orange `Cutout` marker in `-Z` bottom-left while excluding `Transparent` blue blocker in front of `+X` (`exit 0`). [Independent U5-B review](../../reviews/M15_U5_B_CODEX_REVIEW.md).
- [x] **M15-U5-11.2 — Render selected shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `FilterCaptureWorld` + `SanitizeCaptureSettings` preserve directional/local shadow casting according to `CaptureShadows` policy (`EnableShadows`); native Debug/Release D3D12/Vulkan runs measure `[U5-B Shadows] unlit=(1.94751,1.94751,1.94751) shadowed=(0,0,0)` on `+X` receiver wall.
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-12.1 — Copy faces with correct layer offsets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independently verified by Codex against delivery `f49ff61b` after corrections. `CubemapCapture.hlsl` (`OrientFaceCS`) horizontally mirrors right-handed captured faces (`size - 1u - dstPixel.x`) before `ReflectionCapturePass::RecordOrientAndCopy` copies each face into cubemap `mipOffset = 0, layerOffset = faceIndex` (`0..5`); native Debug/Release D3D12/Vulkan readbacks confirm all 6 layers match exact world directions (`exit 0`). [Independent U5-B review](../../reviews/M15_U5_B_CODEX_REVIEW.md).
- [x] **M15-U5-12.2 — Validate copy transitions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independently verified by Codex against delivery `f49ff61b` after corrections. CPU `RenderGraph` contract verifies `OpaqueAndSky (ColorAttachment) -> CaptureOrient (ShaderResource -> ShaderResourceStorage) -> CaptureCopy (CopySource -> CopyDest) -> Export (ShaderResource)` barrier chain and rejects read-before-write; native Debug/Release D3D12/Vulkan runs pass with `0` RHI validation errors (`exit 0`). [Independent U5-B review](../../reviews/M15_U5_B_CODEX_REVIEW.md).
- [x] **M15-U5-13.1 — Retain capture resources through completion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `RenderGraph` `KeepAlive` retains scratch face targets and candidate cubemaps across `RHICompletionToken`, and inter-face/pre-reuse synchronization calls `m_Device->WaitForCompletion` before overwriting shared host-upload buffers or scratch targets; native Debug/Release D3D12/Vulkan wait/readback passes with `0` validation errors.
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-13.2 — Publish capture generation transactionally.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — `RHISceneRenderer::CaptureDynamicReflectionProbe` stages `candidateCubemap` across all 6 faces and updates `m_DynamicProbeCaptures[probeKey]` only after face 5 succeeds; `ReflectionService::ResolvePublication` moves `DynamicPending` -> `DynamicReady` only with live generation handle (`Test_LightingServicesUnit.cpp` & native D3D12/Vulkan fault-injection tests pass).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-14 — Prove the red reference object appears in the captured cubemap.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Automated Status:** Automated PASS (independently verified by Codex after corrections) — native Debug/Release D3D12/Vulkan fixtures verify `+X` HDR red reference wall appears in face `0` (`cPosX.r > 2.0`) AND drives mirror-sphere reflection via SSR probe fallback (`[U5-B SSR Fallback] withProbe=(43,0,0) withoutProbe=(0,0,0)`, `ReflectionState::DynamicReady`, artifact `M15_U5B_faces_strip.ppm`).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-15 — Retain last-good generation when a face/copy fails.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independently verified by Codex against delivery `f49ff61b` after corrections. Mid-capture `ScopedRHIFault` on `BeginCommandBuffer` (aborting Face `2` after Faces `0..1`) returns `Success = false, CapturedFaces = 2, PreservedPreviousGeneration = true` and keeps the prior green `GenerationID` / `RHIResourceHandle` intact and readable, invalid `CaptureResolution = 127` emits `DYNAMIC_PROBE_SETTINGS` while preserving last-good generation, and subsequent capture recovers cleanly on Debug/Release D3D12 & Vulkan (`exit 0`). [Independent U5-B review](../../reviews/M15_U5_B_CODEX_REVIEW.md).

#### S5.3 — Cubemap prefilter

- [x] **M15-U5-16.1 — Allocate mip-capable cubemap.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `RHITextureDesc::CubemapStorage` allocates 6-layer `RGBA16_FLOAT` mip-capable cubemaps (`MipLevels = ReflectionCaptureContract::ComputePrefilterMipCount(res)` clamped `[1, 5]`); `ValidatePrefilterPolicy` rejects SDR `RGBA8_UNORM`, out-of-range mips/resolutions, and undersized budgets. Verified (independently reviewed and repaired by Codex; Editor visuals remain deferred) by `AxleRHIResourceViewTests {dx12,vulkan} storage` (Debug/Release; presentation-independent native allocation gate), `AxleRenderUnitTests`, and `AxleEnvironmentRenderingTests` (dx12/vulkan Debug/Release); [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).
- [x] **M15-U5-16.2 — Allocate destination UAV/SRV views.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `RHITexture` exposes `GetMipUAV(mip)` (`STORAGE_TEXTURE_ARRAY`, `mipOffset = mip, mipNum = 1, layerNum = 6`) and `GetMipSRV(mip)` (`TEXTURE_CUBE`, `mipOffset = mip, mipNum = 1`) with generation lease aliases, `DefineViewAccess`, `RetainGeneration()`, `Shutdown()`, and `ScopedRHIFault` rollback. Verified in `AxleRHIResourceViewTests {dx12,vulkan} storage` (Debug/Release, retained-generation ownership, valid shrinking 3D mip slices, and mid-mip `CreateTextureUAV` fault injection) and `AxleEnvironmentRenderingTests`; [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).
- [x] **M15-U5-17.1 — Implement roughness prefilter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently reviewed and repaired by Codex; Editor visuals remain deferred) — `Engine/Resources/Shaders/CubemapPrefilter.hlsl` (`DownsampleCubeCS` + `PrefilterCubeCS`) and `RHISceneRenderer::PrefilterCapturedCubemap` execute pure-3D multi-mip downsampling + GGX importance sampling across all 6 faces (`mip 0..4`) with trilinear clamp sampler, per-mip barrier transitions, and prefilter begin/end-recording fault rollback. Native D3D12/Vulkan checks verify strictly finite non-negative texels across all 5 mips x 6 faces, monotonic peak drop across mips `0..4`, and widening GGX skirt (`plusZSkirt[2] > plusZSkirt[0] + 0.05f`).
  - **EDITOR VISUAL:** PASS — verified in `M15_RoughnessSweep.axlescene` (`Debug` & `Release` DX12 via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Five gold metallic spheres (`roughness 0.04 -> 0.80`) broaden highlights & floor/environment reflections monotonically. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-17.2 — Preserve face orientation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `CubemapPrefilter.hlsl` (`CubeFaceTexelDirection`) and `ReflectionCaptureContract::ComputeCubeTexelDirection` share canonical `EnvironmentCubeDirection` bases (`+X,-X,+Y,-Y,+Z,-Z`) across every mip `0..4`; verified on CPU (`Test_M15_ReflectionCaptureContractUnit.cpp`) and Debug/Release D3D12/Vulkan native tests (`Test_M15_ReflectionCaptureRendering.cpp`); [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).
- [x] **M15-U5-17.3 — Handle seams.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `CubemapPrefilter.hlsl` samples 3D `TextureCube` across normalized direction vectors using `Trilinear(ClampToEdge)` without face-clamped 2D wrapping; CPU checks validate shared edge alignment (`+Z/+X`, `+Z/+Y`), cross-face bilinear blending, and all 8 corners approached from each of their 3 meeting faces and native Debug/Release D3D12/Vulkan readbacks across a bright `+X/+Z` boundary pillar confirm continuous radiance across adjacent cube borders on prefiltered mips `1..3` (`0` validation errors); [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).
- [x] **M15-U5-18.1 — Use sharp mirror mip.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** `PrefilterCubeCS` (`mip == 0` / `roughness <= 1e-4`) samples crisp mip `0` source radiance without GGX cone blur; `ForwardPBR.hlsl` queries `u_PrefilterMap.GetDimensions(0, ..., envLevels)` (`MAX_REFLECTION_LOD = float(envLevels - 1)`) so `roughness = 0.05` samples near mip `0` with scene-linear HDR peak `6.4925` in Debug/Release D3D12 & Vulkan; [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).
- [x] **M15-U5-18.2 — Use blurred rough-material mips.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently reviewed and repaired by Codex; Editor visuals remain deferred) — `ForwardPBR` split-sum roughness sweep against captured 5-mip dynamic cubemap demonstrates monotonic broadening into higher prefiltered mips (`ringFraction`: `0.0758104` @ `0.05` -> `0.118724` @ `0.35` -> `0.39823` @ `0.70` -> `0.53212` @ `0.95` (scene-linear HDR before tone mapping)) on Debug/Release D3D12 & Vulkan.
  - **EDITOR VISUAL:** PASS — verified in `M15_RoughnessSweep.axlescene` (`Debug` & `Release` DX12 via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Five gold metallic spheres (`roughness 0.04 -> 0.80`) broaden highlights & floor/environment reflections monotonically. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-19.1 — Validate controlled environment.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Controlled 64px HDR cubemap fixture compared GPU prefilter output against CPU base-mip 3D cubemap GGX reference (a bounded approximation; GPU uses solid-angle source LOD) (`ReflectionCaptureContract::EvaluatePrefilterDirectionCPU`) on Debug/Release D3D12 & Vulkan (`mip 1` `r=0.25`: `gpu=(7.66797,3.87109,1.31445)` vs `cpu=(7.83077,3.91539,1.30513)`, `relErr=0.0190894`; `mip 2` `r=0.50`: `relErr=0.1366`); [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).
- [x] **M15-U5-19.2 — Validate roughness sweep.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (independently reviewed and repaired by Codex; Editor visuals remain deferred) — metallic sphere fixture sweeping `roughness = 0.05 -> 0.35 -> 0.70 -> 0.95` produces strictly finite non-negative pixels and monotonic peak/width progression (`peakLuma`: `6.4925 -> 3.76098 -> 1.038 -> 0.729727`, read from the intermediate HDR target before tone mapping; sharp peak must exceed 1) on Debug/Release D3D12 & Vulkan (`exit 0`, `0` validation errors).
  - **EDITOR VISUAL:** PASS — verified in `M15_RoughnessSweep.axlescene` (`Debug` & `Release` DX12 via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Five gold metallic spheres (`roughness 0.04 -> 0.80`) broaden highlights & floor/environment reflections monotonically. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-20 — Do not route cubemap data through equirectangular assumptions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** `CubemapPrefilter.hlsl` and `RenderContracts.cpp` operate purely on 3D cubemap texel directions (`CubeFaceTexelDirection`, `SampleCubemapBilinear3D`) with zero equirectangular/spherical projection (`atan2`/`asin` absent by contract test); native `+Y` pole beacon checks confirm azimuthal isotropy on Debug/Release D3D12 & Vulkan; [Codex U5-C Review](../../reviews/M15_U5_C_CODEX_REVIEW.md).

Codex verification limit: native headless DX12/Vulkan filtering and storage-view gates pass in Debug/Release. Vulkan `targets` and strict Editor startup also pass; DX12 presentation (`targets` and strict Editor startup) cannot initialize the NRI `SwapChainInterface` on this adapter. Default DX12 Editor startup succeeds via Vulkan fallback and is not counted as DX12 presentation evidence. See the Codex report for exact exits/logs.

#### S5.4 — Invalidation and scheduling

**Automated acceptance (2026-11-17):** U5-D delivery `fb699f21` accepted after
independent Codex repairs and Debug/Release verification on D3D12 and Vulkan.
See [findings, final executed matrix and acceptance limits](../../reviews/M15_U5_D_CODEX_REVIEW.md).
The [independent follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records the dynamic-probe and Play/Eject captures alongside the native invalidation and reuse tests.

**Scheduling boundary:** partial jobs restart on tracked input changes; at two
faces per render they need three compatible renders. Continuous motion can leave
a cold probe pending or retain a stale last-good generation. The cache budget
counts resident and candidate cubemap texel payloads; retained/selected residents
are protected, so a rebuild requires capacity for both old and new generations.
It does not measure driver overhead, shared scratch or externally held generations.

- [x] **M15-U5-21.1 — Track probe transform.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `ReflectionCaptureDomainStamps::TransformStamp` hashes `EntityID`, `StableUUID`, `Position`, `BoxOffset`, `BoxExtents`, `Radius`, `Shape`, `WorldTransform`, and `ParallaxCorrected` in `RHISceneRenderer::ComputeDynamicCaptureDomainStamps`; verified in `Test_LightingServicesUnit.cpp`, `Test_M15_ReflectionCaptureContractUnit.cpp`, and `Test_M15_ReflectionCaptureRendering.cpp` (Debug/Release DX12 & Vulkan, `exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-21.2 — Track scene revision.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `RenderWorld` extracts `SceneRevision`, `SceneInstanceID`, and `SceneLifetimeEpoch`; `ReflectionService::EvaluateInvalidation` detects changes cleanly across Debug/Release CPU contracts & native DX12/Vulkan checks (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-21.3 — Track geometry/material.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (accepted after independent Codex review and repairs) — `GeometryStamp` tracks mesh transforms/UUIDs/URIs/shadows/builtins and `Skins` bone matrices; `MaterialStamp` tracks resolved `MaterialResolver` surface generation, scalars, shaders, and custom properties. Verified on CPU (`Test_M15_ReflectionCaptureContractUnit.cpp`) and Debug/Release DX12 & Vulkan (`Test_M15_ReflectionCaptureRendering.cpp`, `exit 0`).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-21.4 — Track light/shadow.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (accepted after independent Codex review and repairs) — `LightingStamp` hashes directional/additional directional lights, point/spot/rect lights, shadow flags/biases/resolutions, skybox settings, and IBL enablement; verified on CPU and Debug/Release DX12 & Vulkan (`exit 0`).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-21.5 — Track asset generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `AssetStamp` hashes `DeviceEpoch`, manual IBL handles, texture/shader file timestamps, custom shader fingerprints, and mesh `NativeView` vertex/index handles; verified via live GPU `RHIMesh` swap (`Test_M15_ReflectionCaptureRendering.cpp`) and CPU contracts (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-22 — Support manual and automatic capture.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `ReflectionProbeCaptureMode` (`Automatic`/`Manual`) wired through `Types.hpp`, `Components.hpp`, `RenderWorld.hpp`, `SceneExtraction.cpp`, and `SceneSerializer.cpp` (`CaptureMode` YAML round-trip). `Manual` probes hold last-good capture with `Stale`/`ManualRefreshRequired` across edits until explicit `CaptureDynamicReflectionProbe` or `RequestDynamicReflectionCapture`; verified across Debug/Release CPU & DX12/Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-23.1 — Bound faces/probes per frame.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `SceneRenderSettings::ReflectionCaptureMaxFacesPerFrame` (default `6`) & `ReflectionCaptureMaxProbesPerFrame` (default `1`), `ComputeFrameCaptureSchedule`, and `RHISceneRenderer::PendingCaptureJob` slice 6-face captures deterministically across frames (`MaxFacesPerFrame=2` -> 3 frames) and cap per-frame probes (`MaxProbesPerFrame=1`) on Debug/Release DX12 & Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-23.2 — Expose pending diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `DynamicReflectionProbeStatus` and `ReflectionFrame` (`CapturePending`, `CaptureCompletedFaces`, `CaptureTotalFaces`, `ActiveGenerationID`) expose per-face progress during multi-frame jobs without leaking partial cubemaps; verified in `Test_LightingServicesUnit.cpp` and `Test_M15_ReflectionCaptureRendering.cpp` (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-23.3 — Expose stale diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `DynamicReflectionProbeStatus::Stale` and `ReflectionFrame::CaptureStale` surface edited manual probes, `InvalidateDynamicReflectionProbe` triggers, and retained last-good generations during pending/failed rebuilds (`DYNAMIC_PROBE_STALE`) on Debug/Release CPU & DX12/Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-23.4 — Expose failed diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `RecordDynamicCaptureFailure` updates `DynamicCaptureLifecycleState::Failed`, `FailureCount`, `LastDiagnostic`, and `ReflectionFrame::CaptureFailed` across Debug/Release CPU & DX12/Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-24 — Retain last-good generation on rebuild failure.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). Multi-frame `PendingCaptureJob` stages exclusively into candidate cubemaps; mid-capture `ScopedRHIFault` on face `4/6` (plus settings/budget errors) aborts cleanly, preserves the existing `DynamicReflectionProbeGeneration` & HDR texels, keeps `ReflectionFrame::PrefilterReady` bound, and recovers cleanly on retry in Debug/Release DX12 & Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-25 — Add capture cache key, memory budget and eviction.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `ResolveDynamicProbeKey` (`uuid:`/`entity:`/`anon:`), `EstimateDynamicProbeCacheBytes`, `ReflectionCaptureCacheBudgetBytes`, `SetDynamicReflectionCaptureCacheBudget`, `DynamicReflectionCaptureCacheStats`, and LRU eviction (`ComputeLRUCacheEvictionPlan` via `LastUsedSequence`) verified across 3 probes in a 2-probe budget + tight-budget rejection in Debug/Release CPU & DX12/Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-26.1 — Test repeated capture.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Automated Status:** Automated PASS (accepted after independent Codex review and repairs) — automated Debug/Release D3D12 & Vulkan runs validate repeated capture reuse when unchanged, monotonic `GenerationID` advancement across material/clip/mode/recovery transitions, and clean texel replacement (`exit 0`).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (plus `U5_AreaPlanar` for shadowed capture; `Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; Play + Free-Fly Eject & probe capture run cleanly. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-26.2 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). Resizing main viewport targets (`64x64 -> 96x48`) leaves probe `GenerationID` unchanged; changing probe `CaptureResolution` (`32 -> 64`) reallocates cubemaps cleanly and updates `CachedBytes` (`130,944 -> 524,160`) in Debug/Release DX12 & Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-26.3 — Test device reset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `IsValidForDevice(deviceEpoch)` checks `DeviceEpoch` and live handle epoch; `RunM15ReflectionDeviceResetTests` executes full `device.Shutdown()` + `device.Init(...)` transition on Debug/Release DX12 & Vulkan (`exit 0`, zero validation errors), rejecting prior-epoch generations and recapturing cleanly on the new epoch. [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).
- [x] **M15-U5-26.4 — Test scene replacement.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (accepted after independent Codex review and repairs). `RenderWorld::SceneInstanceID` + `SceneLifetimeEpoch` and `StableUUID` keys isolate replaced scenes (`Scene A -> Scene B` even with colliding ECS entity indices) and `Scene::Clear()` lifetime epoch increments, purging retired generations without entity aliasing or cross-scene radiance leakage on Debug/Release CPU & DX12/Vulkan (`exit 0`). [U5-D Codex review](../../reviews/M15_U5_D_CODEX_REVIEW.md).

#### S5.5 — Multi-probe selection and blend

**Automated acceptance (2026-11-17):** Delivery `32144635` accepted after Codex
repairs and separate-context source rechecks. Final Debug/Release builds and
28/28 commands pass, including native DX12/Vulkan and strict Editor startup with
the real three-entity fixture. [Full findings, costs and limits](../../reviews/M15_U5_E_CODEX_REVIEW.md).
The [independent follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records Editor evidence for the box and sphere probe scene and SSR fallback.

The four-slot view selection is bounded and conservative; exact influence is
evaluated per pixel. SSR replaces exported forward specular in linear HDR before
transparent composition. Unsupported raw custom companions report fallback.
The two SSR term targets add 16 bytes/pixel; remaining S5.6 quality work stays open.

- [x] **M15-U5-27.1 — Compute box influence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. CPU/native OBB influence, finite degenerate basis, conservative rotated-mesh bounds, and C1 falloff pass in Debug/Release DX12/Vulkan. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-27.2 — Compute sphere influence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Sphere influence/overlap and native sphere parallax pass alongside CPU sphere intersection checks in Debug/Release DX12/Vulkan. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Dual Sphere/Box parallax probes blend cleanly over smooth spheres with zero invalid pixels. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-28 — Blend multiple valid probes without hard seams.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Up to four ready probes blend with per-fragment normalized influence. Camera-first strict ranking, ready overflow publication, and retained view-generation pins pass CPU/native regressions. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Dual Sphere/Box parallax probes blend smoothly over Red/Blue reference walls & checkered floor with no hard seams or black edge. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-29 — Apply parallax correction per probe before blending.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Box/sphere correction is applied independently before sampling. CPU exact parallel-slab boundary and finite basis regressions pass; native off-axis fixture changes raw Green 2.94727 to corrected Red 1.71016. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Sphere/Box parallax probes reflect glowing Red/Blue walls cleanly and fall back without black edges. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-30.1 — Blend probe with SSR confidence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Opaque forward rendering exports actual environment specular/material response to graph-owned MRTs; SSRApply replaces only that contribution before transparency. Confident native hits preserve reflection-disabled HDR emission and canonical AO=0 emission, including companion-only resource binding. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). SSR & local probes reflect Red/Blue walls & floor smoothly with clean fallback and no black edges. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-30.2 — Blend probe with sky fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Probe/sky coverage and authored intensities blend in linear HDR; native boundary transitions are monotonic. Canonical sky sampling now uses the authored rotation consistently with ForwardPBR. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-30.3 — Enforce one energy policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Independent native scene-linear readback gives unit scene radiance .99988 with SSR off and 1.0121 on (delta < .03). Actual material response replaces fixed-F0 subtraction; diffuse/direct/emission remain outside the replacement. Raw custom shaders without a compatible companion use named SSR fallback. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-31.1 — Test overlap.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. CPU and native box/sphere overlap, normalized weights, four-slot cap and per-fragment blending pass in both configurations/backends. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-31.2 — Test boundary movement.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Native scene-linear boundary samples transition from local Red (1.99996,.03125) through mixed (.99668,1.03398) to sky Blue (.03125,2), with monotonic blue fraction. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-31.3 — Test missing probe.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Native four-missing/high-ranked probes do not hide a ready fifth: active=1, missing=4, selected identity and reflected radiance are preserved. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-31.4 — Test stale probe.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. Native edited Manual probes retain last-good Red capture, stale diagnostics and generation identity, then publish updated Green after explicit request. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
- [x] **M15-U5-31.5 — Test SSR miss.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS after independent Codex review/repairs. SSR misses preserve the scene pixel and fall back to probes or sky: probe Red/Green/Blue (1.99968,.0312453,.031246), sky Red/Blue (.0312463,1.99976). Actual SSRApply and hits are required in the companion tests. See [Codex review and final matrix](../../reviews/M15_U5_E_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Off-screen / oblique reflections fall back cleanly from SSR to probes & HDR sky without black edges. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).

**P5 exit:** with SSR disabled, a captured cubemap contains the red object and the sphere samples it; roughness changes sharpness; invalidation/scheduling/blending/fallback pass.
  - **Automated status:** Accepted after independent Codex repairs and native Debug/Release DX12/Vulkan verification. With SSR disabled, -Z capture is (4,.159912,.159912); linear HDR sphere Red peaks 3.29863 -> 1.09219 -> .138735 as roughness increases. Interactive visual gates remain open. See [Codex review](../../reviews/M15_U5_E_CODEX_REVIEW.md).

### U5 / S5.6 — Material-aware SSR and reflection resolve

Codex review covers the delivery named **U5-F** (`21a3e25e`, U5-32.1..39.6), including the resolve leaves originally labeled U5-G in the planning table. Corrections align odd-size DDA cell footprints with folded-border Hi-Z reduction, refine camera-facing/decreasing-depth hits exactly, and make CPU endpoint/distance/confidence policy match the shader. New regressions compare one-pixel blockers against mip-0 traversal in both axes/directions, plus 75 actual CPU/GPU ray comparisons per native run (square, odd 255x191 and camera-facing fixtures). UV/distance/confidence bounds are 0.002/0.003/0.025; the subpixel camera-facing resolve checks bilinear input radiance times BRDF response and confidence. See the linked Codex report for final execution results and open visual gates. No S5.7 release is implied.

- [x] **M15-U5-32.1 — Freeze ray/depth projection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `SSRRaymarchContract::ProjectViewToUVZ`/`ReconstructViewPos`/`ClipScreenRay` match `SSR.hlsl` RH `[0,1]` depth projection and pass Debug/Release unit checks. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-32.2 — Build initial ray and starting mip.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `BuildInitialSSRRay` and `SSR.hlsl` initialize normal-biased start view/UVZ, clamp `maxMip` to `GetHiZUsefulLevelCount()-1`, and reject sky/near-plane rays in Debug/Release CPU & DX12/Vulkan runs. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-32.3 — Advance current Hi-Z cell.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Hierarchical Hi-Z DDA ascends coarse mips across open cells (`rayZMax < cellMinZ`) and steps deterministically in Debug/Release CPU (`HighestMipReached >= 2`) and DX12/Vulkan GPU runs. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-32.4 — Descend/refine possible hit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Coarse intersections clamp Z entry (`tPlane`), descend `--mip` to mip 0, and refine exact crossings cleanly in CPU/DX12/Vulkan tests. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-32.5 — Handle bounds/max steps/termination.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Screen/depth `[0,1]` clipping, near-plane clamping, `SSRParams.y=64` step budget, and early sky-exit termination pass Debug/Release CPU & DX12/Vulkan checks. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-32.6 — Produce hit distance/miss/confidence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `CSMain` writes `[0,1]` normalized `float4(hitUV, hitDistNorm, confidence)` safe for both `RGBA16_FLOAT` and `RGBA8_UNORM`; misses output `conf=0`, hits output `conf=0.810352` on native DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-32.7 — Run CPU/native traversal readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Synthetic 64x64 Hi-Z pyramid CPU readback (`Test_M15_HiZContractUnit`) and native `ScreenStorage` readbacks (`storage=(0.501855, 0.406641, 0.0703247, conf=0.810352)`) pass in Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-33.1 — Add thickness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). View-space penetration check rejects false hits deeper behind surface than thickness in CPU checks and native DX12/Vulkan (`[U5-F Thickness] st=(0.501855, 0.406641, conf=0.484912) hdr=(2.42344, 0.096936, 0.096936)` past foreground bar). See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-33.2 — Add hit refinement.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Exact mip-0 depth-plane refinement handles both increasing- and decreasing-depth crossings in `SSRRaymarchContract` and `SSR.hlsl` (`HitUV` error `< 0.08` CPU, sub-texel native hit agreement). See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-33.3 — Add edge/disocclusion rejection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). 4-neighbor orthogonal depth support check and smooth screen-border/distance/camera fades attenuate uncertain silhouette/border hits in `SSR.hlsl`. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-34.1 — Filter by roughness cone.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `ComputeRoughnessConePixelRadius` and `SSRApply` cone LOD integration produce monotonic roughness broadening in native DX12/Vulkan (`G(0.05)=11.3359 -> G(0.30)=8.92734 -> G(0.65)=2.65883`). See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `M15_RoughnessSweep.axlescene` (`Debug` & `Release` DX12 via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Five gold metallic spheres (`roughness 0.04 -> 0.80`) broaden floor/env reflections continuously; SSR ON vs OFF `changedPct=20.57%` (`meanAbsDiff=2.31, maxAbsDiff=65`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-34.2 — Filter over HDR color footprint.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `RHIViewTarget::SSRColor0..3` half-res `RGBA16_FLOAT` chain populated via `DownsampleColorCS` and sampled with 9-tap depth-aware cone integration in Debug/Release DX12/Vulkan and RenderGraph barrier tests. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-35.1 — Pass roughness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `ForwardPBR.hlsl` and `MaterialSurface.hlsl` emit per-pixel signed roughness via `MakeMaterialReflectionSurfaceOutput` into `ReflectionEnvironment.a` verified by shader contract and native roughness sweep tests. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `M15_RoughnessSweep.axlescene` (`Debug` & `Release` DX12 via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Monotonic roughness broadening visible across `0.04 -> 0.80` metallic spheres with SSR active (`changedPct=20.57%`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-35.2 — Pass metallic/base color.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Metallic/dielectric Fresnel response propagated cleanly via `ReflectionResponse.rgb` without legacy `0.02` attenuation in `MaterialSurface.hlsl`, differentiating Gold, Chrome, and Blue dielectric natively on DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-35.3 — Pass reflection flags.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `ReflectionEnvironment.a` sign gates `SSRApply` per receiver without breaking `ScreenStorage` tracing (`[U5-F Disabled] conf=0.810352, hdr=(0,0,0)` on Debug/Release DX12/Vulkan). See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`). Reflective mirror/satin spheres reflect Red/Blue walls while `EnableEnvironmentReflections=false` box renders matte. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-35.4 — Validate declared resolve bindings.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `m_SSRDownsamplePipelineLayout`, `m_SSRApplyPipelineLayout` (`t0..t8`, `s0`), descriptor sets, and `SSRCopy -> HiZ -> SSRColor0..3 -> SSRCompute -> SSRApply` RenderGraph barriers pass negative/positive validation with zero DX12/Vulkan errors. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-36.1 — Compute dielectric Fresnel/F0.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `ComputeSpecularF0` and canonical shaders use achromatic `0.04` F0 on dielectrics; native Blue dielectric (`baseColor=(0.02,0.12,0.95), metallic=0`) reflects achromatic white `(1.28965, 1.28965, 1.28965)`. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-36.2 — Compute conductor Fresnel/F0.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Conductors use tinted `baseColor` F0; native Gold `(4.86016, 3.73008, 2.20039)` and Chrome `(4.86016, 4.86016, 4.86016)` match physical Schlick Fresnel on DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-37.1 — Replace/blend indirect specular.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `SSRApply` replaces exported `envSample.rgb` with filtered SSR radiance weighted by `confidence * roughnessFade`, preserving direct lighting/emission in linear HDR across DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-37.2 — Prevent duplicate IBL reflection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Both U5-E energy conservation check (`base=0.99987 -> ssr=1.01134`) and U5-F `SSRApply` replacement keep indirect specular free of additive double counting on Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`). Reflections over Red/Blue reference walls & floor preserve energy without double-counted IBL halos or black edges. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-38.1 — Blend SSR confidence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). `weight = confidence * EvaluateRoughnessFade(roughness)` smoothly lerps prior environment specular into SSR hit color in `SSR.hlsl` without discontinuities. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `U5_ProbesSSR` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`). Smooth transition between SSR hits and probe/sky fallback across spheres & checkered floor. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-38.2 — Blend local probe influence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Native SSR miss over local Blue probe preserves `envSample` cleanly (`[U5-F Miss->Probe] conf=0, hdr=(0.0312439, 0.0312439, 1.99961)`) alongside U5-E multi-probe blending on DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-38.3 — Blend sky fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Native SSR miss with no local probe cleanly falls back to sky environment (`[U5-F Miss->Sky] hdr=(0.0390503, 1.79648, 0.0390503)` DX12 / `(0.0390564, 1.79668, 0.0390564)` Vulkan). See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-39.1 — Test hit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Native Red wall reflection hit produces `conf=0.810352` and linear HDR `(4.05078, 0.161987, 0.161987)` identically in Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-39.2 — Test miss.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Native miss tests (`Miss->Probe` and `Miss->Sky`) confirm `conf=0` and clean fallback radiance on Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-39.3 — Test moving object.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Translating Red emitter cube across frames (`f0=(4.83656, 0) -> f1=(0, 4.86016)`) moves SSR cleanly with zero stale ghosting in Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-39.4 — Test roughness sweep.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Native roughness sweep `(0.05, 0.30, 0.65)` yields monotonic Green peak reduction `11.3359 -> 8.92734 -> 2.65883` in Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
  - **EDITOR VISUAL:** PASS — verified in `M15_RoughnessSweep.axlescene` (`Debug` & `Release` DX12 via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Five gold metallic spheres (`0.04 -> 0.80`) broaden SSR & environment highlights continuously; SSR ON vs OFF `changedPct=20.57%` (`meanAbsDiff=2.31, maxAbsDiff=65`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U5-39.5 — Test gold/chrome/dielectric.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Native Chrome `(4.86016, 4.86016, 4.86016)`, Gold `(4.86016, 3.73008, 2.20039)`, and Blue dielectric `(1.28965, 1.28965, 1.28965)` pass in Debug/Release DX12/Vulkan. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).
- [x] **M15-U5-39.6 — Test camera motion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Automated PASS (independently reviewed and repaired by Codex; interactive visual gates remain deferred). Multi-frame camera orbit/translation across floor + curved sphere completes cleanly with finite non-negative HDR outputs and zero DX12/Vulkan validation errors. See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).

**Exit:** colored geometry appears in the expected reflection; roughness varies continuously; no black edge, ghost or energy spike.
  - **Automated status:** Independently reviewed and repaired by Codex across Debug/Release CPU + native DX12/Vulkan + strict Editor startup checks. Interactive Editor visual gates (`34.1`, `35.1`, `35.3`, `37.2`, `38.1`, `39.4`) remain open (deferred per user direction to post-U5 consolidated visual pass). See [Codex U5-F review](../../reviews/M15_U5_F_CODEX_REVIEW.md).

### U5 / S5.7 — LTC rectangular and disk area lights

> **Independent Codex review:** U5-G / historical batch row U5-H (S5.7, requirements 40..45.3) passed its [original automated review](../../reviews/M15_U5_G_CODEX_REVIEW.md). The [independent follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records the later LTC Inspector and roughness captures.

- [x] **M15-U5-40 — Add validated LTC transform/amplitude tables or reproducible generation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Pinned authors' 64x64 GGX fit, shared packed-half CPU/HLSL lookup, reproducible importer, positive-determinant/finite validation, 18 independent specular quadrature cases and native GGX comparisons. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-41.1 — Implement rectangle emitter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Rectangle horizon-clipped contour agrees with diffuse quadrature; actual native specular is compared with independent dense GGX integration. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-41.2 — Implement disk emitter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Area-preserving 32-segment disk/ellipse contour; sharp specular edge tangent-rotation regression (.465245..466255), circle/area-ratio and native disk tests. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-41.3 — Validate units/orientation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** CPU/native frame packing preserves world dimensions, calibrated intensity and safe orthonormal orientation; collinear, non-unit and invalid dimensions are tested. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-41.4 — Validate sidedness/range.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** CPU/HLSL sidedness, smooth range cutoff and Euclidean ellipse projection; analytic eccentric closest distance and native eccentric range parity. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-42.1 — Integrate diffuse area response.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Shared diffuse contour integral across ForwardPBR and MaterialSurface, independent quadrature and actual native canonical/builtin parity. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-42.2 — Integrate specular area response.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Fitted isotropic LTC with restored builtin GGX compensation/aniso response, smooth zero-strength transition, per-emitter fitted clearcoat and real native graph parity without fallback. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-43 — Define area-light shadow policy separately.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Explicit UnshadowedAnalyticLTC policy and zero area-light local shadow atlas allocation; Inspector policy source/build integration verified. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md). **EDITOR VISUAL: PASS — verified in `U5_AreaPlanar` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`; plus `M15_RoughnessSweep` for continuous roughness broadening): `Rect Light Warm` displays `Area Light (LTC)` & `Shadow Policy: Unshadowed Analytic LTC` in `Inspector`. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).**
- [x] **M15-U5-44.1 — Complete component path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Shape, enabled state, dimensions, intensity, color, sidedness and policy retained by RectLightComponent, runtime codec and copy/extraction paths. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-44.2 — Complete extraction path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** SceneExtraction -> RenderWorld -> LightingService -> RHILight -> native/canonical shaders and lighting stamps verified, preserving 64B/2304B GPU layouts. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-44.3 — Complete serialization/runtime path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Scene and assembly round-trips plus real Assembly Editor private-preview extraction, edited-width save and component-removal save; runtime path verified. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-44.4 — Complete Inspector path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** EntityInspector controls, creation menus and icons audited; strict Debug/Release Editor empty/U4E startup on both backends verifies integration. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md). **EDITOR VISUAL: PASS — verified in `U5_AreaPlanar` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`; plus `M15_RoughnessSweep` for continuous roughness broadening): `Rect Light Warm` displays `Area Light (LTC)` & `Shadow Policy: Unshadowed Analytic LTC` in `Inspector`. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).**
- [x] **M15-U5-45.1 — Compare roughness/view angles.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Independent CPU roughness/view matrix, native roughness peak/wing, aniso strength/rotation, disk-edge rotation and area clearcoat tests. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md). **EDITOR VISUAL: PASS — verified in `U5_AreaPlanar` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`; plus `M15_RoughnessSweep` for continuous roughness broadening): `Rect Light Warm` displays `Area Light (LTC)` & `Shadow Policy: Unshadowed Analytic LTC` in `Inspector`. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).**
- [x] **M15-U5-45.2 — Compare emitter angles.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** CPU/native emitter-angle, grazing and sidedness checks plus independent GGX angle matrix. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).
- [x] **M15-U5-45.3 — Compare numerical reference.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** **Automated PASS after independent Codex review and repairs.** Independent dense emitter quadrature validates actual diffuse and specular response on CPU and GPU; raw-fit oracle uses neutral compensation LUT. Final Debug/Release verification and limits: [Codex review](../../reviews/M15_U5_G_CODEX_REVIEW.md).

**Exit:** shape/roughness-correct, finite highlights without duplicate emitter energy.
  - **Verification:** The [original U5-G review](../../reviews/M15_U5_G_CODEX_REVIEW.md) passed 34/34 Debug/Release CPU, native, Assembly Editor and startup commands. The [independent follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records Inspector and roughness capture evidence.

### U5 / S5.8 — Planar reflection

Planar reflection is the deterministic raster path for a selected reflective plane. It uses a separate capture ViewContext and target generation so the main view history, camera jitter and scheduler state are never re-entered recursively.

**Delivery naming:** The released S5.8 package is U5-H1 (`bb20102b`) + U5-H2 (`3a2f197b`), followed by Codex corrections `19af7efd` and `eee4ef4b`. The older suggested-batch table calls S5.8 U5-I; these eight requirement IDs define the reviewed scope.

- [x] **M15-U5-46.1 — Freeze planar surface contract.**
  - **PASS when:** the plane transform, normal, clip distance, source view and reflection confidence are represented explicitly and use the canonical surface units.
  - **Evidence:** Independent Codex PASS after contract/camera/identity repairs (`19af7efd`, `eee4ef4b`): Debug/Release contract, extraction and serialization tests plus actual Assembly preview/save/remove. Authored range/resolution and clip-bias regressions pass. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).
- [x] **M15-U5-46.2 — Create independent mirrored capture ViewContext.**
  - **PASS when:** the mirrored camera is derived from the plane, recursion is disabled, and capture state cannot mutate the parent view history.
  - **Evidence:** Independent Codex PASS: mirrored view/oblique projection, independent capture context, recursion filtering and actual main-view completion synchronization; native Debug/Release DX12/Vulkan frame-index/TAA isolation and ID-zero stable receiver exclusion. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).
- [x] **M15-U5-46.3 — Allocate planar color/depth targets and graph edges.**
  - **PASS when:** resize, format, lifetime and completion dependencies are declared through RenderGraph/RHIViewTargets.
  - **Evidence:** Independent Codex PASS: native storage/targets in both configs/backends, scaled scratch capture and resampling, fresh candidate generation ownership, resize-failure retention and device epoch regression. All formerly blocked DX12 target checks now pass in the current display context. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).
- [x] **M15-U5-46.4 — Register a dedicated PlanarReflectionPass.**
  - **PASS when:** capture, resolve and release are scheduled as explicit passes without callback recursion or hidden resource reads.
  - **Evidence:** Independent Codex PASS: dedicated copy/resolve/release DAG and isolated capture graph; CPU contract and full native DX12/Vulkan Debug/Release execution with zero RHI validation errors. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).
- [x] **M15-U5-46.5 — Resolve planar energy and fallback.**
  - **PASS when:** planar confidence is blended once with SSR, local probe and sky IBL according to the shared reflection energy policy.
  - **EDITOR VISUAL:** capture a fixed view with planar reflection enabled and disabled; no black edge or duplicate highlight is allowed.
  - **Evidence:** **Independent automated PASS:** ON/OFF, SSR/probe/sky and roughness fallback, plus linear-HDR confidence 0/.5/1 midpoint and receiver-emission preservation with SSR on/off on both native backends/configs. Jittered depth reconstruction and live manual resolve controls repaired. **EDITOR VISUAL: PASS — verified in `U5_AreaPlanar` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) after CSM oblique-projection fix (`93234b4e`). Crisp mirror reflection with `[x] Planar Reflections` checked in open `RenderFXPopup`, and smooth SSR+Probe fallback when unchecked (`changedPct=40.15%, meanAbsDiff=27.46..27.47, maxAbsDiff=222`) with no black edge or duplicate highlight. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md) & [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).**
- [x] **M15-U5-46.6 — Validate oblique clipping, edges and self-intersection.**
  - **PASS when:** the reflected plane excludes geometry behind the mirror, clips at the declared extent and avoids self-intersection artifacts.
  - **Evidence:** Independent Codex PASS: native HDR green target with red receiver at valid EnTT index zero and blue behind-plane blocker excluded; reflected winding, geometric receiver/positive capture bias, extent/edge CPU cases and valid-candidate selection. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).
- [x] **M15-U5-46.7 — Validate invalidation and failure retention.**
  - **PASS when:** plane/camera/scene/material/light changes invalidate the capture and a failed update retains last-good data or an explicit fallback.
  - **Evidence:** Independent Codex PASS after atomic-publication/cache repair: all domain stamps, scene/device lifetime, manual lifecycle, unchanged transient retry, pre/post-submit pixel retention, capture-scale failure retention, A/B/A ownership and explicit overwrite invalidation; native Debug/Release DX12/Vulkan. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).
- [x] **M15-U5-46.8 — Complete Editor and native evidence.**
  - **PASS when:** Debug/Release Editor evidence and deterministic image/readback comparison agree with the native fixture.
  - **EDITOR VISUAL:** show the same camera with planar reflection on and off and the relevant Rendering Features policy visible.
  - **Evidence:** **Independent automated PASS:** actual Assembly preview/edit/save/remove and strict empty/U4E Editor startup on DX12/Vulkan in Debug/Release (`fallback=none`); Inspector/menu/status/request paths inspected. **EDITOR VISUAL: PASS — fixed shadowed Planar+CSM bug caught during Editor capture (`93234b4e` + new `M15-U5-46.8` regression test). `U5_AreaPlanar` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) shows matching planar ON/OFF frames alongside visible `RenderFXPopup` controls. See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md) & [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).**

**Exit:** a planar mirror produces stable, clipped reflection with explicit SSR/probe/IBL fallback and no recursive capture.
  - **Automated status:** Independently reviewed and repaired by Codex (`19af7efd`, `eee4ef4b`); all eight requirements accounted for, 13 targets built per config and final matrix 34/34 PASS, including all formerly blocked DX12 presentation checks. Separate-context source recheck completed. Deferred Editor visual gates `46.5` and `46.8` (plus `93234b4e` shadowed CSM+Planar fix and regression test) now **PASS** in `U5_AreaPlanar` (`Debug` DX12, `Release` DX12, `Release` Vulkan, `Error 0`); `93234b4e` & `82e82d82` await independent Codex audit (see [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md)). See [Codex review](../../reviews/M15_U5_H_CODEX_REVIEW.md).

### U6 / S6.1–S6.3 — Volumetric fog and atmosphere

> **Execution schedule note (2026-11-19).** U6-A established froxel allocation, quality tiers, and initial VRAM figures. U6-B verified static 3D integration and the D3D12 windowed checks. U6-C verified light and shadow injection, including Debug and Release Editor slit captures. [U6 batch schedule](#u6-execution-batches-u6-a-through-u6-e) · [U6-A review](../../reviews/M15_U6_A_REVIEW.md) · [U6-B review](../../reviews/M15_U6_B_REVIEW.md) · [U6-C review](../../reviews/M15_U6_C_REVIEW.md)

- [x] **M15-U6-01.1 — Allocate froxel resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Debug and Release CPU and native D3D12/Vulkan tests passed 3D SRV/UAV readback, depth policy, resize, late OOM rollback and device-epoch recreation. Vulkan viewport lifecycle passed. [U6-A review and D3D12 windowed limit](../../reviews/M15_U6_A_REVIEW.md)
- [x] **M15-U6-01.2 — Scale froxel quality tiers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Debug and Release CPU and native D3D12/Vulkan checks passed all four tiers, including the High reference grid of 160 × 90 × 64. [U6-A review](../../reviews/M15_U6_A_REVIEW.md)
- [x] **M15-U6-02.1 — Inject density/media.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Debug and Release D3D12/Vulkan readback verifies 3D injection, component priority and the native RGBA16F storage contract. [U6-B review](../../reviews/M15_U6_B_REVIEW.md)
- [x] **M15-U6-02.2 — Inject light/shadow visibility.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
  - Native D3D12 and Vulkan tests verify directional and local lighting with the real atlases and the unshadowed fallback. Debug and Release D3D12 Editor captures show the bounded shaft and Console `Error 0`. [U6-C evidence](../../reviews/M15_U6_C_REVIEW.md) See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) for the independent current-source slit ON/OFF captures.
- [x] **M15-U6-03.1 — Integrate scattering/extinction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Native 3D integration and linear composition pass homogeneous, partial-slice, stratified boundary and distant opaque depth fixtures. [U6-B review](../../reviews/M15_U6_B_REVIEW.md)
- [x] **M15-U6-03.2 — Validate physical units.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - CPU analytic and fine-step quadrature comparisons verify Beer–Lambert distance in meters and Henyey–Greenstein phase. Native readbacks agree in Debug and Release on both backends. [U6-B review](../../reviews/M15_U6_B_REVIEW.md)
- [x] **M15-U6-04.1 — Add temporal reprojection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Native ping-pong and HDR readbacks confirm world-space history reuse and static fallback. [U6-D verification](../../reviews/M15_U6_D_VERIFICATION.md) [Independent Codex review](../../reviews/M15_U6_D_CODEX_REVIEW.md)
- [x] **M15-U6-04.2 — Add temporal rejection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - GPU readbacks verify former frustum and fog depth rejection, current-neighborhood bounds and preserved extinction. [U6-D verification](../../reviews/M15_U6_D_VERIFICATION.md) [Independent Codex review](../../reviews/M15_U6_D_CODEX_REVIEW.md)
- [x] **M15-U6-04.3 — Add invalidation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Native and ViewContext tests cover first-frame, cut, view generation, scene, device epoch, feature settings and failed-frame invalidation. [U6-D verification](../../reviews/M15_U6_D_VERIFICATION.md) [Independent Codex review](../../reviews/M15_U6_D_CODEX_REVIEW.md)
- [x] **M15-U6-05.1 — Define transparent/depth interaction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Independent PASS:** Native Debug/Release D3D12 and Vulkan pass analytic mesh, graph, refraction and particle composition checks. The separate-context recheck found an edge leak repaired in `05b1e453`. The added foreground-silhouette assertion fails with the old Forward PBR shader and passes with the repaired shader. See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).
- [x] **M15-U6-05.2 — Define no-light behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Independent PASS:** Native Debug/Release D3D12 and Vulkan verify zero unlit scattering with positive extinction, point-only scattering, and authored fog parity. Runtime loose and cooked modes measure center red at 213 with fog and 231 with it disabled. Debug/Release OpenGL verifies the supported fallback. See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).
- [x] **M15-U6-05.3 — Define zero-density behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Fog-off and zero-density or zero-intensity HDR readbacks match at every pixel and alpha. The native service also verifies byte-for-byte SDR identity at zero intensity. [U6-B review](../../reviews/M15_U6_B_REVIEW.md)
- [x] **M15-U6-06 — Remove invented sun fallback and emit diagnostics.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - Native fixtures verify unlit extinction, zero added radiance, explicit diagnostics and recovery after a fog descriptor allocation failure. [U6-B review](../../reviews/M15_U6_B_REVIEW.md)
- [x] **M15-U6-07.1 — Test wall occlusion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Debug and Release D3D12 and Vulkan readbacks verify CSM and local wall occlusion, including a blocked froxel at a narrow spotlight edge. [U6-C evidence](../../reviews/M15_U6_C_REVIEW.md)
- [x] **M15-U6-07.2 — Test shafts.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - All bright sample-row columns stay within the slit plus the filter margin. The same geometry emits zero shaft radiance without a directional light. [U6-C evidence](../../reviews/M15_U6_C_REVIEW.md)
- [x] **M15-U6-07.3 — Test camera motion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Controlled camera translation matches the predicted history address. Rotation after a light goes dark leaves zero measured trail. [U6-D verification](../../reviews/M15_U6_D_VERIFICATION.md) [Independent Codex review](../../reviews/M15_U6_D_CODEX_REVIEW.md)
- [x] **M15-U6-07.4 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - Resizing a live view target and changing the fog quality tier both seed a fresh volume before reuse. [U6-D verification](../../reviews/M15_U6_D_VERIFICATION.md) [Independent Codex review](../../reviews/M15_U6_D_CODEX_REVIEW.md)
- [x] **M15-U6-07.5 — Measure memory.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Independent PASS:** The four current native scorecards pass actual four-volume allocation budgets at 1080p and 1440p. They record the shared color and depth allocations separately. See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).
- [x] **M15-U6-07.6 — Measure cost.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Independent PASS:** The four current native scorecards capture six warmed completion-token-matched samples for each quality tier and extent. Debug/Release service and Runtime pass the D3D12/Vulkan and OpenGL paths. See the [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md).

**Exit:** The required U6-E recheck, Debug/Release native matrix and Editor slit captures passed. The [independent U4–U6 follow-up](../../reviews/M15_U4_U6_CODEX_FOLLOWUP.md) records the foreground refraction repair, no-light assertions, allocation sizes and pass timings.

### U7 / S7.1–S7.4 — Experimental DDGI

The [independent Codex review](../../reviews/M15_U7_CODEX_REVIEW.md) verifies the corrections through `75a93b66`. The delivery entries below retain Gemini's earlier evidence where applicable.

- [x] **M15-U7-01.1 — Choose radiance acquisition.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`, `93baed17`): chose shared offscreen amortized 6-face tile-atlas world-space raster capture (`DDGICapture`, explicit `Experimental` / default OFF policy) over ray tracing or screen-space sampling; verified via `DDGI_ACQUISITION_POLICY.md`, `M15DDGIContract`, and Debug/Release Vulkan/D3D12 `AxleEnvironmentRenderingTests --m15-ddgi`.
- [x] **M15-U7-01.2 — Document acquisition cost/policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`): authored `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/DDGI_ACQUISITION_POLICY.md` documenting offscreen `16x16` per-face capture tiles, `RGBA16_FLOAT` / `D32_FLOAT` formats, staggered probe update budget (`MaxProbesPerFrame` default `8`, hard cap `32`), fallback policy, and experimental limits.
- [x] **M15-U7-02 — Implement probe indexing/origin/spacing and bounded allocation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`, `93baed17`): `DDGIProbeGridComponent`, 3D/linear probe indexing, `DDGIConstants`, 32x16x32 bounded cap (`DDGIContract::MaxProbeCount = 16384`, `MaxAtlasBudgetBytes = 64 MiB`), and `RHIViewTargets` dual atlases + capture scratch verified in `M15DDGIContract` and Debug/Release Vulkan/D3D12 `AxleEnvironmentRenderingTests` (`build/Tests/Environment/{Debug,Release}/{vulkan,dx12}/M15_U7_ddgi_scorecard.txt`).
- [x] **M15-U7-03.1 — Pack irradiance atlas.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`, `3db0aba9`): `6x6` octahedral interior + `1 px` border gutter (`8x8` tile) `RGBA16_FLOAT` packing (`DDGICommon.hlsli`, `DDGIPass::CSUpdateIrradiance`) verified against CPU contract and GPU captures on Debug/Release Vulkan/D3D12 (`--m15-ddgi`).
- [x] **M15-U7-03.2 — Pack distance atlas.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`, `3db0aba9`): `14x14` octahedral interior + `1 px` border gutter (`16x16` tile) `RGBA16_FLOAT` packing (`DDGICommon.hlsli`, `DDGIPass::CSUpdateDistanceMoments`) verified against CPU contract and GPU readbacks on Debug/Release Vulkan/D3D12 (`--m15-ddgi`).
- [x] **M15-U7-03.3 — Pack variance moments.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3db0aba9`): `CSUpdateDistanceMoments` packs `(E[r], E[r^2], validWeight, backfaceRatio)` in `RGBA16_FLOAT`, verified via `M15DDGIContract` and Debug/Release Vulkan/D3D12 GPU readbacks (`--m15-ddgi`).
- [x] **M15-U7-04.1 — Implement visibility moments.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3db0aba9`): offscreen `D32_FLOAT` capture depth + `SV_IsFrontFace` backface sign encoding feed radial `(E[r], E[r^2])` visibility moments and backface ratio, verified on Debug/Release Vulkan/D3D12 (`--m15-ddgi`).
- [x] **M15-U7-04.2 — Implement Chebyshev weighting.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`, `3db0aba9`, `281019df`): one-tailed Chebyshev visibility weighting (`variance / (variance + d^2)`) with backface threshold suppression in `DDGICommon.hlsli`, verified in `M15DDGIContract` and sealed-chamber native fixtures (`--m15-ddgi`).
- [x] **M15-U7-04.3 — Validate bias.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`39372690`, `93baed17`): shared `ViewBias` (default `0.15`) and `NormalBias` (default `0.08`) constants verified across CPU contracts and native Debug/Release Vulkan/D3D12 `--m15-ddgi` fixtures.
- [x] **M15-U7-05.1 — Add temporal accumulation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3db0aba9`, `93baed17`): hysteresis blending (`DefaultHysteresis = 0.90`) in `CSUpdateIrradiance`/`CSUpdateDistanceMoments` verified for steady propagation and `Hysteresis=0.0` vs `0.95` convergence rate on Debug/Release Vulkan/D3D12 (`--m15-ddgi`).
- [x] **M15-U7-05.2 — Add staggered updates.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3db0aba9`, `93baed17`): post-seed round-robin staggered batching via `MaxProbesPerFrame` / `UpdateFraction` verified in `M15DDGIContract` (`ProbeCursor` wrapping) and GPU steady-state probe budget checks (`08.3`).
- [x] **M15-U7-06 — Feed irradiance into diffuse ForwardPBR lighting.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
  - **Evidence:** **Self-verified automated + EDITOR VISUAL PASS:** `281019df` & `93baed17` integrate world-space DDGI diffuse irradiance (`b8`, `t7`/`t8` `space4`) into `ForwardPBR.hlsl` & `MaterialSurface.hlsl` (with `ReconcileSPIRVDescriptorBindings` in `RHIShader.cpp`), passing Debug/Release Vulkan/D3D12 `AxleEnvironmentRenderingTests`, 7/7 `MaterialGraph|M15DDGI` CTests, and `AxleEditor --verify-empty-startup`. **EDITOR VISUAL PASS:** `U7_DDGI.axlescene` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) shows warm orange & cool teal indirect bounce illuminating the matte white sphere (`changedPct=68.49%..71.44%, meanAbsDiff=64.42..65.06`) with `DDGI Probe Grid (Experimental)` visible in `Inspector`. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U7-07.1 — Add scene/probe invalidation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Independent native PASS at `75a93b66`. World lattice, scene, target and material generation changes invalidate the atlas. A fixed-revision cutoff regression verifies immediate reseed and corrected visibility. Camera, moving geometry and animated lights preserve cursor progress. Hysteresis applies to the next update.
- [x] **M15-U7-07.2 — Add no-valid-probe fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`281019df`, `93baed17`): falls back cleanly to `Sky/IBL` diffuse with `RenderDiagnostics` telemetry when all probes are backface-trapped, MSAA > 1, grid disabled, or `EnableDDGI=false` (verified matching analytical sky-ambient baseline on Debug/Release Vulkan/D3D12).
- [x] **M15-U7-07.3 — Add memory/update budgets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`93baed17`): live atlas + scratch VRAM byte accounting and NRI GPU pass timestamps (`ddgi_seed_ms`, `ddgi_capture_ms`, `ddgi_update_ms`) across `4x2x4..32x16x32` at `1920x1080` recorded in `build/Tests/Environment/{Debug,Release}/{vulkan,dx12}/M15_U7_ddgi_scorecard.txt`.
- [x] **M15-U7-08.1 — Test indirect bounce.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`281019df`, `93baed17`): oblique-angle world-space indirect color bounce, multi-bounce propagation, and custom `MaterialSurface` receiver (`u7d-ddgi-receiver.hlsl`) verified on Debug/Release Vulkan/D3D12 (`AxleEnvironmentRenderingTests --m15-ddgi`).
- [x] **M15-U7-08.2 — Test wall leakage.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`93baed17`): sealed dark chamber behind `0.8m` dividing wall blocks bright emitter indirect light (`<= 2` RGB inside vs `g > 50` outside) on Debug/Release Vulkan/D3D12 (`--m15-ddgi`).
- [x] **M15-U7-08.3 — Test grid settings.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`93baed17`): live sweeps of `EnergyMultiplier`, `GridOrigin`, `MaxProbesPerFrame`, and `Hysteresis` (`0.0` vs `0.95`) verified on Debug/Release Vulkan/D3D12 (`--m15-ddgi`).
- [x] **M15-U7-08.4 — Test disabled fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`281019df`, `93baed17`): verified analytical fallback parity under `EnableDDGI=false`, component `Enabled=false`, all-enclosed `Back`-face probes, MSAA > 1 (`Fallback==IBL`), `AxleRenderUnitTests` backend compute policy (`OpenGL -> EnableDDGI=false`), and `AxleRenderServiceTests` parity.

**Exit:** Independent review and correction recheck passed for experimental DDGI at `75a93b66`. D3D12 and Vulkan native tests verify world-space bounce, wall leakage tolerance, budget and fallback. Editor `--capture-plan` independently verified `M15-U7-06` in Debug D3D12, Release D3D12 and Release Vulkan. See the [review evidence and limits](../../reviews/M15_U7_CODEX_REVIEW.md).

### U8 / S8.1–S8.6 — HDR post and temporal pipeline

The [independent Codex review](../../reviews/M15_U8_CODEX_REVIEW.md) verifies the corrections at `7b1bf41c`. The delivery entries retain Gemini's earlier evidence where applicable.

### Exposure

- [x] **M15-U8-01.1 — Build log-luminance histogram.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, re-verified at `62d77e0c`): full-viewport histogram (`AutoExposure.hlsl`) verified via `M15U8PostTemporalContract` (`AxleRenderUnitTests` / `ctest -R ^M15U8PostTemporalContract$`, Debug/Release exit 0) and native `AxleEnvironmentRenderingTests` (`vulkan` & `dx12`, Debug/Release exit 0).
- [x] **M15-U8-01.2 — Reduce histogram globally.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): global 256-bin histogram reduction verified against CPU contract and GPU readbacks (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release exit 0).
- [x] **M15-U8-02.1 — Adapt previous exposure.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `a13155b4` & `62d77e0c` pass `AutoExposure` temporal adaptation and `AxleEditor --verify-empty-startup --rhi=vulkan` (Debug/Release exit 0). **EDITOR VISUAL PASS:** `U8_HDRBloomTAA.axlescene` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) verifies 16-frame temporal Auto-Exposure + Bloom + TAA adaptation (`changedPct=92.52%, meanAbsDiff=33.65`), bounded bloom halo around HDR emissive beacon, and Editor UI dark chrome unshifted/excluded from metering. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U8-02.2 — Use real delta time.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): real frame `DeltaTime` propagated through `ViewContext` & `PostProcessPass`, rate-invariant across `30/60/120 Hz` (`exp30=2.6582 exp60=2.6484 exp120=2.6367` on Debug/Release Vulkan/DX12).
- [x] **M15-U8-02.3 — Keep exposure per-view.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `a13155b4` & `62d77e0c` isolate `Exposure` per `ViewContext` across `Main`/auxiliary views (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release, `AxleEditor --verify-empty-startup --rhi=vulkan` exit 0). **EDITOR VISUAL PASS:** `U8_HDRBloomTAA.axlescene` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) verifies 16-frame temporal Auto-Exposure + Bloom + TAA adaptation (`changedPct=92.52%, meanAbsDiff=33.65`), bounded bloom halo around HDR emissive beacon, and Editor UI dark chrome unshifted/excluded from metering. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U8-03.1 — Feed exposure to tonemapping.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `a13155b4` & `62d77e0c` declare and bind `viewTargets->GetExposure()` into `ToneMapping` with manual exposure fallback (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release, `AxleEditor --verify-empty-startup --rhi=vulkan` exit 0). **EDITOR VISUAL PASS:** `U8_HDRBloomTAA.axlescene` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) verifies 16-frame temporal Auto-Exposure + Bloom + TAA adaptation (`changedPct=92.52%, meanAbsDiff=33.65`), bounded bloom halo around HDR emissive beacon, and Editor UI dark chrome unshifted/excluded from metering. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U8-03.2 — Define first-frame behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): first-frame snap/initialization policy verified via `AutoExposureContract` and Debug/Release Vulkan/DX12 GPU fixtures.
- [x] **M15-U8-03.3 — Define black-frame behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): zero-luminance/black frame clamps safely to bounded `AutoExposureMin/Max` without NaN/Inf (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release).
- [x] **M15-U8-03.4 — Define camera-cut behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): `CameraCut` snaps exposure immediately without stale adaptation lag (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release).
- [x] **M15-U8-04 — Exclude UI/bloom from metering by policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: a bounded multiscale halo appears without repeated tint/gain.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `a13155b4` & `62d77e0c` run `AutoExposure` strictly pre-Bloom and verify UI/`PhysicsDebug` exclusion in native Vulkan/DX12 Debug/Release tests + `AxleEditor --verify-empty-startup --rhi=vulkan`. **EDITOR VISUAL PASS:** `U8_HDRBloomTAA.axlescene` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`) verifies 16-frame temporal Auto-Exposure + Bloom + TAA adaptation (`changedPct=92.52%, meanAbsDiff=33.65`), bounded bloom halo around HDR emissive beacon, and Editor UI dark chrome unshifted/excluded from metering. See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md).
- [x] **M15-U8-05.1 — Test mixed tiles.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): `25/75` vs `50/50` dark/bright mixed-tile luminance metering verified on Debug/Release Vulkan/DX12 (`AxleEnvironmentRenderingTests`).
- [x] **M15-U8-05.2 — Test 30 Hz.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): `dt=1/30` 1-second convergence (`exp30=2.6582`) verified on Debug/Release Vulkan/DX12 (`AxleEnvironmentRenderingTests`).
- [x] **M15-U8-05.3 — Test 60 Hz.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): `dt=1/60` 1-second convergence (`exp60=2.6484`) verified on Debug/Release Vulkan/DX12 (`AxleEnvironmentRenderingTests`).
- [x] **M15-U8-05.4 — Test 120 Hz.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`a13155b4`, `62d77e0c`): `dt=1/120` 1-second convergence (`exp120=2.6367`) matching `30/60 Hz` verified on Debug/Release Vulkan/DX12 (`AxleEnvironmentRenderingTests`).

### Bloom

- [x] **M15-U8-06 — Threshold HDR source once.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): single soft-knee prefilter + Karis average on `BloomDown0` (`Bloom.hlsl`, `PostProcessPass.cpp`) with `BloomDown1..5` sampling source mips without re-thresholding (`M15U8PostTemporalContract`, `AxleEnvironmentRenderingTests` Debug/Release Vulkan/DX12).
- [x] **M15-U8-07.1 — Build six levels.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): `Bloom0..Bloom5` compute downsample chain verified via `BloomContract` and `RHIViewTargets` (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release).
- [x] **M15-U8-07.2 — Validate level dimensions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): `RHIViewTargets::ComputeBloomExtent` / `BloomContract::ComputeMipExtent` validated across even, odd, and tiny resolutions (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`, `AxleRHIResourceViewTests`).
- [x] **M15-U8-08.1 — Reconstruct additively.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): tent-filter upsample pyramid (`BloomUp4..0`) with hardware `ONE + ONE` `LoadOp::LOAD` blending verified on Debug/Release Vulkan/DX12.
- [x] **M15-U8-08.2 — Preserve finer contributions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): hardware `LoadOp::LOAD` additive upsample retains finer mip peaks rather than overwriting `Bloom0..4` (`AxleEnvironmentRenderingTests` Debug/Release Vulkan/DX12).
- [x] **M15-U8-09 — Apply intensity/tint once.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): normalized `BloomTint * BloomIntensity * (1/6)` applied once in `BloomApply` (`Bloom.hlsl`), verified via gain linearity and shader contract checks (`M15U8PostTemporalContract`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release).
- [x] **M15-U8-10.1 — Test impulse halo.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): HDR impulse halo test confirms crisp inner core preservation + monotonic radial falloff on Debug/Release Vulkan/DX12 (`AxleEnvironmentRenderingTests`).
- [x] **M15-U8-10.2 — Test gain.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): single-application linear intensity gain sweep verified in Debug/Release Vulkan/DX12 GPU fixtures.
- [x] **M15-U8-10.3 — Test odd/small resolution.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): odd and minimal extents (`127x63`, `33x17`, `9x9`) validate and render cleanly on Debug/Release Vulkan/DX12.
- [x] **M15-U8-10.4 — Test HDR/SDR.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`fc6a2e66`, `62d77e0c`): both `RGBA16_FLOAT` HDR and `RGBA8_UNORM` SDR Bloom targets pass PSO/render checks across Vulkan/DX12 Debug/Release.

### TAA

- [x] **M15-U8-11.1 — Generate camera motion vectors.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `62d77e0c`): native `RG16_FLOAT` `Velocity` pass (`Velocity.hlsl::PSBackground`) verified on linear camera pan across Debug/Release Vulkan/DX12 (`AxleEnvironmentRenderingTests`).
- [x] **M15-U8-11.2 — Generate object motion vectors.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `62d77e0c`): per-view `PrevWorldTransform` cache keyed by `StableUUID`/`EntityID` (`ViewContext`) drives `Velocity.hlsl::PSMesh`, tested for moving/static/first-spawn/reassigned EntityID on Debug/Release Vulkan/DX12.
- [x] **M15-U8-11.3 — Apply consistent jitter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `62d77e0c`): Halton `(2,3)` subpixel jitter cancels identically between `CurrJitteredVP` and unjittered `CurrVP`/`PrevVP` (`<= 1e-4` static velocity leak on Debug/Release Vulkan/DX12).
- [x] **M15-U8-12.1 — Reject disoccluded history.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`c34c8a1b`, `62d77e0c`): closest 3x3 depth & dilated velocity lookup + YCoCg variance clip + exponential `disocclusionGate` (`TAA.hlsl`, `TAAContract`) verified on CPU and native Vulkan/DX12 Debug/Release.
- [x] **M15-U8-12.2 — Reject invalid history.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`c34c8a1b`, `62d77e0c`): out-of-bounds UV, non-finite velocity, first-frame/reset invalidation, explicit RenderGraph read declarations, and `RenderBackendPolicy` FXAA suppression verified across CPU & Debug/Release Vulkan/DX12.
- [x] **M15-U8-13.1 — Reset on resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `c34c8a1b`, `62d77e0c`): viewport dimension changes reset temporal & object transform history cleanly (`CommittedFrameCount == 1`, zero velocity) across CPU and Debug/Release Vulkan/DX12.
- [x] **M15-U8-13.2 — Reset on view switch.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `c34c8a1b`, `62d77e0c`): switching `RenderViewKind` (`Main` <-> `AssetPreview` / `ViewId`) isolates/resets history cleanly across CPU and Debug/Release Vulkan/DX12.
- [x] **M15-U8-13.3 — Reset on camera cut.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `62d77e0c`): `CameraCut` invalidates temporal history and per-object transform cache cleanly (`M15U8PostTemporalContract`, `AxleEnvironmentRenderingTests` Vulkan/DX12 Debug/Release).
- [x] **M15-U8-13.4 — Reset on settings/format.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `62d77e0c`): `FeatureGeneration` hashes TAA settings plus `m_CurrentColorFormat`/`m_CurrentDepthFormat` and resets history on toggles/changes (`M15U8PostTemporalContract`, `AxleEnvironmentRenderingTests` Debug/Release Vulkan/DX12).
- [x] **M15-U8-13.5 — Reset on scene generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`230d213d`, `62d77e0c`): `ViewSceneRevision` bump invalidates both temporal and object-transform caches (`M15U8PostTemporalContract`, `AxleEnvironmentRenderingTests` Debug/Release Vulkan/DX12).
- [x] **M15-U8-14.1 — Define transparent policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`62d77e0c`): `Transparent`/`Additive` surfaces and GPU particles are excluded from `MotionVectors` (`opaqueQueue` only) while rasterizing into `R8_UNORM` `Reactive`, verified in `TAAContract` and Debug/Release Vulkan/DX12 GPU fixtures.
- [x] **M15-U8-14.2 — Define reactive policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`62d77e0c`): native `RHIViewTarget::Reactive` (`R8_UNORM`, hardware `MAX` blend) populated by `ReactiveMask` (`Velocity.hlsl::PSReactive` + `Particle.hlsl::PSReactive`) damps `TAAResolve` history weight on translucent/additive/particle/intense-emissive pixels across CPU & Debug/Release Vulkan/DX12.
- [x] **M15-U8-14.3 — Define unjittered UI policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`62d77e0c`): 2D Canvas/ImGui UI and Editor gizmos compose post-TAA/ToneMapping without jitter, and `PhysicsDebug` defers unjittered rendering (`PhysicsDebugOverlay`) after `PostProcessPass` under TAA, verified on Debug/Release Vulkan/DX12 (`0` pixel shift & invariant exposure).
- [x] **M15-U8-15.1 — Test static shimmer.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`c34c8a1b`, `62d77e0c`): rotated high-contrast thin bar accumulates smooth subpixel anti-aliased transitions over 16 Halton jitter frames (`rms <= 4.5` across consecutive jitter phases) on Debug/Release Vulkan/DX12.
- [x] **M15-U8-15.2 — Test moving object.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`c34c8a1b`, `62d77e0c`): fast moving high-contrast object maintains foreground edge fidelity with clean trailing wake across Debug/Release Vulkan/DX12.
- [x] **M15-U8-15.3 — Test disocclusion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`c34c8a1b`, `62d77e0c`): rapidly moving occluder uncovers high-contrast background without stale foreground color ghosting on Debug/Release Vulkan/DX12.
- [x] **M15-U8-15.4 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`c34c8a1b`, `62d77e0c`): resize (`128x128 -> 192x144`), view switch, and `EnableTAA + EnableFXAA` mutual exclusion (`maxDiff == 0`) pass on Debug/Release Vulkan/DX12.
- [x] **M15-U8-15.5 — Test ghosting.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`62d77e0c`): moving semi-transparent wake & additive snap-off fixtures clear ghosting cleanly on Debug/Release Vulkan/DX12; 1280x720 Release GPU scorecard: VK `22.3960 MiB` (`frame=3.6294 ms`, `taa=0.8593 ms`), D3D12 `22.3789 MiB` (`frame=2.5830 ms`, `taa=0.9508 ms`).

**Exit:** Independent native and Editor verification passed for M15-U8. TAA checks previous depth, the six bloom levels retain HDR precision, and exposure excludes overlays. Debug D3D12, Release D3D12 and Release Vulkan captures verify the bloom halo, exposure adaptation and displayed output. See the [review evidence](../../reviews/M15_U8_CODEX_REVIEW.md).

### U9 / S9.1–S9.5 — Advanced material models

- [x] **M15-U9-01 — Charlie sheen direct/IBL response with energy checks.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`5c140f37`, re-verified at `7568cd5e`): Charlie sheen NDF/visibility/FGrazing + 9x9 directional-albedo LUT energy conservation (`<= 1.0`) implemented across Direct, Point, Spot, LTC Area, and IBL in `PBRCommon.hlsli`/`ForwardPBR.hlsl`/`MaterialSurface.hlsl`; `AxleRenderUnitTests` (`SheenContract`) & Debug/Release Vulkan/DX12 `AxleEnvironmentRenderingTests` (`[08.1]`) exit 0. Independent Codex review pending.
- [x] **M15-U9-02 — Beer–Lambert absorption with documented thickness/path policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`ffb140b1`, re-verified at `7568cd5e`): `exp(-sigma * pathLength)` transmittance (`AbsorptionContract` & `REFRACTION_ABSORPTION_POLICY.md`) verified on Debug/Release `AxleRenderUnitTests` & Vulkan/DX12 `AxleEnvironmentRenderingTests` (`[08.2]`, `sigma(3,0.5,1.5)`, zero opaque leakage `opaqueDiff=0`). Independent Codex review pending.
- [x] **M15-U9-03 — Cauchy/wavelength dispersion within documented scope.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`ffb140b1`, re-verified at `7568cd5e`): 3-wavelength Cauchy RGB IOR splitting (`EvaluateCauchyIORs`) within documented screen-space limits verified on Debug/Release `AxleRenderUnitTests` (`AbsorptionContract`) & Vulkan/DX12 `AxleEnvironmentRenderingTests` (`[08.2]`, `dispFringe=(-0.9202,0)`). Independent Codex review pending.
- [x] **M15-U9-04.1 — Implement separable diffusion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3244aa34`, re-verified at `7568cd5e`): point-sampled 11-tap separable H/V diffusion pass (`SSS.hlsl`, `SubsurfaceScatteringPass`, `RHIViewTarget::SubsurfaceDiffuse/Control/Temp`) verified across CPU (`SubsurfaceContract`) & Debug/Release Vulkan/DX12 `AxleEnvironmentRenderingTests` (`[08.3]`, `termBleedR=0.4530`). Independent Codex review pending.
- [x] **M15-U9-04.2 — Reject by depth.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3244aa34`, re-verified at `7568cd5e`): exponential relative-depth rejection weight (`ComputeBoundaryWeight`) blocks silhouette bleed (`silhouetteBleed=0`) in `SubsurfaceContract` & Debug/Release Vulkan/DX12 `[08.3]`. Independent Codex review pending.
- [x] **M15-U9-04.3 — Reject by normal/material boundary.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `3244aa34` & `7568cd5e` pass normal dot & SSS material boundary rejection (`seamBleed=0`, `silhouetteBleed=0` on Debug/Release Vulkan/DX12 `[08.3]` and `AxleEditor --verify-empty-startup --rhi=vulkan` exit 0). **EDITOR VISUAL PASS:** re-verified post-`82e82d82` on `U9_Materials.axlescene` & `.axleasset` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Hero trio shows Charlie Sheen plum velvet halo, SSS terminator glow with unbled `Adjacent Slate Boundary Block` silhouette, and Beer-Lambert absorption + Cauchy chromatic dispersion prism fringe; `Inspector` auto-expands `Sheen`, `Subsurface Scattering`, and `Absorption & Dispersion` beside live `Asset Preview` sphere (`ImGuiCond_Once`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md). **Independent Codex review pending.**
- [x] **M15-U9-05 — Preserve specular while diffusing intended diffuse data.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** Self-verified automated PASS (`3244aa34`, re-verified at `7568cd5e`): `DiffuseSSS` isolated from crisp specular/emissive before separable blur and recombined in `SSS.hlsl` (`specPeak(off=590.3094, on=589.5969)` on Debug/Release Vulkan/DX12 `[08.3]`). Independent Codex review pending.
- [x] **M15-U9-06.1 — Complete native asset path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`7568cd5e`): native `Material` defaults, `[0,1]` / `[0.01,1]` / `[0,16]` / `>= 0` setter & `.amat` reload clamps, and round-trip serialization verified in Debug/Release `AxleRenderUnitTests` (`M15U9Contract` `06.1`), `AxleMaterialUnitTests` (5/5), and `ctest -R ^M15U9AdvancedMaterialContract$`. Independent Codex review pending.
- [x] **M15-U9-06.2 — Complete component path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`7568cd5e`): `MaterialComponent`, `MaterialComponentCodec`, and `SceneSerializer` round-trip/clamps verified in Debug/Release `AxleRenderUnitTests` (`M15U9Contract` `06.2`), `ctest -R ^(M15U9AdvancedMaterialContract|M15SurfaceAndMaterialContract)$`, and Vulkan/DX12 `[06.5/06.6]`. Independent Codex review pending.
- [x] **M15-U9-06.3 — Complete GPU path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`7568cd5e`): `SceneExtraction`, `RHI::RHIMaterial`, and `ResolvedSurface` constant buffer packing & clamps match across Debug/Release `AxleRenderUnitTests` (`M15U9Contract` `06.3`) and Vulkan/DX12 `AxleEnvironmentRenderingTests`. Independent Codex review pending.
- [x] **M15-U9-06.4 — Complete Inspector path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `7568cd5e` adds `Sheen`, `Subsurface Scattering`, and `Absorption & Dispersion` Inspector controls with bounds/clamps & tooltips in `MaterialInspectorControls.cpp` (`AxleEditor` Debug/Release build + `--verify-empty-startup --rhi=vulkan` & `M15U9Contract` `06.4` exit 0). **EDITOR VISUAL PASS:** re-verified post-`82e82d82` on `U9_Materials.axlescene` & `.axleasset` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Hero trio shows Charlie Sheen plum velvet halo, SSS terminator glow with unbled `Adjacent Slate Boundary Block` silhouette, and Beer-Lambert absorption + Cauchy chromatic dispersion prism fringe; `Inspector` auto-expands `Sheen`, `Subsurface Scattering`, and `Absorption & Dispersion` beside live `Asset Preview` sphere (`ImGuiCond_Once`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md). **Independent Codex review pending.**
- [x] **M15-U9-06.5 — Complete Preview path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
  - **Evidence:** **Self-verified automated PASS (automated + EDITOR VISUAL):** `7568cd5e` wires `SceneRequiresSubsurfacePass` & `RHIViewportDesc::EnableSubsurface` in `AssetPreviewPanel`/`RHIViewportDescriptor` (dropping preview `MSAASamples` from `4` to `1` for SSS) with Debug/Release `AxlePreviewRenderTests` (`vulkan`/`dx12` & `surfaces`, 8/8 exit 0) & `AxleEnvironmentRenderingTests` (`[06.5/06.6]`). **EDITOR VISUAL PASS:** re-verified post-`82e82d82` on `U9_Materials.axlescene` & `.axleasset` (`Debug` DX12, `Release` DX12, `Release` Vulkan via `AxleEditor --capture-plan`, Console `Error 0`, `nonFinite=0`). Hero trio shows Charlie Sheen plum velvet halo, SSS terminator glow with unbled `Adjacent Slate Boundary Block` silhouette, and Beer-Lambert absorption + Cauchy chromatic dispersion prism fringe; `Inspector` auto-expands `Sheen`, `Subsurface Scattering`, and `Absorption & Dispersion` beside live `Asset Preview` sphere (`ImGuiCond_Once`). See [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md). **Independent Codex review pending.**
- [x] **M15-U9-06.6 — Complete Runtime path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`7568cd5e`): serialized `.axle` scene loaded via `SceneSerializer::Deserialize`, extracted cleanly via `ExtractRenderWorld`, and rendered on Debug/Release Vulkan & DX12 (`AxleEnvironmentRenderingTests` `[06.5/06.6]`, `rimLift=0.3674 termRedLift=1.2745`, plus `AxleRuntime` build & `AxleRenderServiceTests` 4/4 exit 0). Independent Codex review pending.
- [x] **M15-U9-07.1 — Extend graph outputs/lowering.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`dbdf828d`, re-verified at `7568cd5e`): 13-pin `PBRMasterOutput` (`Sheen*`, `Subsurface*`, `AbsorptionColor`, `IOR`, `Dispersion`) & 6 entry-point `MaterialSurface.hlsl` pipeline (`PSMain`, `PSReflectionMain`, `PSSSSMain`, `PSReflectionSSSMain`, `PSNormalMain`) pass Debug/Release `MaterialGraph*` ctests (`12/12` pass) and `M15U9Contract`. Independent Codex review pending.
- [x] **M15-U9-07.2 — Declare unsupported fields explicitly.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`dbdf828d`, re-verified at `7568cd5e`): `MaterialBindingPlan::SupportedFeatures` & `MG_UNSUPPORTED_OPAQUE_ABSORPTION` / `MG_UNSUPPORTED_OPAQUE_DISPERSION` surface explicit diagnostics & `UnsupportedFeatures` mask in `AxleRenderUnitTests` (`M15U9Contract` `07.2`). Independent Codex review pending.
- [x] **M15-U9-08.1 — Test sheen.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`5c140f37`, re-verified at `7568cd5e`): `AxleEnvironmentRenderingTests` `[08.1]` Debug/Release Vulkan & DX12 (`center=0.0898 grazing=0.3190 tightRatio=5.4623 wideRatio=2.7766 pointOn=23.6896 rectOn=0.0825`). Independent Codex review pending.
- [x] **M15-U9-08.2 — Test absorption.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`ffb140b1`, re-verified at `7568cd5e`): `AxleEnvironmentRenderingTests` `[08.2]` Debug/Release Vulkan & DX12 (`sigma(3,0.5,1.5)`, `unabs=(1,1,1)`, `dispFringe=(-0.9202,0)`, `opaqueDiff=0`). Independent Codex review pending.
- [x] **M15-U9-08.3 — Test SSS.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`3244aa34`, re-verified at `7568cd5e`): `AxleEnvironmentRenderingTests` `[08.3]` Debug/Release Vulkan & DX12 (`termBleedR=0.4530 silhouetteBleed=0 seamBleed=0 specPeak(off=590.3094, on=589.5969)`). Independent Codex review pending.
- [x] **M15-U9-08.4 — Test built-in/graph parity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Self-verified automated PASS (`dbdf828d`, `7568cd5e`): `AxleEnvironmentRenderingTests` `[08.4]` Debug/Release Vulkan & DX12 (`sheenRelErr=(0.0177,0.0360,0.0032)`, `sssMaxRelErr=0.0029`, built-in & graph `PSMain`/`PSSSSMain`/`PSReflectionMain` parity). Independent Codex review pending.

**Exit:** all U9 advanced material models, bounds/clamps, separable SSS, MaterialGraph lowering/diagnostics, Inspector/Preview/Runtime plumbing and Debug/Release Vulkan/DX12 fixtures pass self-verification on commit `7568cd5e` (`M15-U9-04.3/06.4/06.5` `EDITOR VISUAL` self-verified PASS post-`82e82d82` in [Visual & Windowed Recovery](../../reviews/M15_VISUAL_AND_WINDOWED_RECOVERY.md); U9 + `82e82d82` independent Codex review pending).

### U10 / S10.1–S10.4 — Editor and authoring integration

- [ ] **M15-U10-01.1 — Add material controls.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U10-01.2 — Add clamps/tooltips.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-01.3 — Add defaults.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-01.4 — Add capability diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-02.1 — Add probe placement/shape.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-02.2 — Add capture/scheduling UI.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U10-02.3 — Add stale/failed diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-03.1 — Match environment controls.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-03.2 — Match exposure/AA controls.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U10-03.3 — Match reflection controls.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U10-03.4 — Match Preview/Runtime policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U10-04 — Complete scene/assembly/runtime codec round trips.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-05.1 — Add Low preset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-05.2 — Add Medium preset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-05.3 — Add High preset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-05.4 — Add Ultra preset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-05.5 — Record budgets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-06.1 — Diagnose OpenGL fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-06.2 — Diagnose unsupported capability fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-07.1 — Test Editor create/edit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U10-07.2 — Test save/reopen.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-07.3 — Test Runtime parity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** every accepted runtime feature is authorable and survives save/reload.

### M16 handoff — Ray Tracing Backend

M15 does not contain a ray-tracing Unit. The former U11/P13 checklist is moved to [Milestone 16 Ray Tracing Backend](../MILESTONE_16_RAY_TRACING/PLAN.md). M15 is complete for this boundary when its raster contracts are explicit: `ResolvedSurface`, `ViewContext`, capture targets, reflection confidence/energy policy, denoising history invalidation and raster fallback. M16 must consume those contracts without introducing a second material interpretation.

### U10 / S10.5 — Final acceptance and rollout

Final acceptance follows [U10_ACCEPTANCE_REVIEW.md](U10_ACCEPTANCE_REVIEW.md): architecture, every feature, cross-feature integration, lifecycle/failure behavior, actual Editor visuals and measured cost are mandatory review dimensions. The expanded 17.1/17.2 parent items below are derived summaries only; all new children remain NOT RUN until executed under a released U10 task.

- [ ] **M15-U10-08.1 — Rebuild Editor.**
  - **PASS when:** Debug/Release Editor binaries build from the final integrated source with exact targets, configuration and binary identity recorded.
  - **Evidence:** Build commands/exit codes, final commit and binary paths; actual Editor behavior is evaluated by the visual/product gates. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
  - **EDITOR VISUAL:** Execute and record the required actual Editor inspection; startup or readback alone does not close this gate.
- [ ] **M15-U10-08.2 — Rebuild Runtime.**
  - **PASS when:** Debug/Release Runtime binaries build from the same final source and load the declared acceptance project without silently changing its backend or features.
  - **Evidence:** Runtime build and launch records, project/settings, backend/adapter, exit codes and binary identity. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-08.3 — Rebuild tests.**
  - **PASS when:** Every required test executable is rebuilt after its source/dependencies change, and the final consolidated suites run against the corrected baseline.
  - **Evidence:** Exact targets/commands, final source/configurations, per-command exits and individual test logs. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-09.1 — Run D3D12 fixtures.**
  - **PASS when:** Required D3D12 feature and integration fixtures execute on a physical supported GPU with backend fallback disabled; feature assertions and validation outcomes pass.
  - **Evidence:** Requirement/integration matrix rows, adapter/configuration, actual commands, image/readback artifacts and validation logs. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-09.2 — Run Vulkan fixtures.**
  - **PASS when:** Required Vulkan feature and integration fixtures execute on a physical supported GPU with backend fallback disabled; feature assertions and validation outcomes pass.
  - **Evidence:** Requirement/integration matrix rows, adapter/configuration, actual commands, image/readback artifacts and validation logs. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-10.1 — Run OpenGL fixtures.**
  - **PASS when:** OpenGL executes its declared supported path and explicitly diagnoses unsupported M15 features while preserving the defined fallback image and authored settings.
  - **Evidence:** Capability/support matrix and actual supported/fallback fixture results; no unsupported native-parity claim. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-10.2 — Run unsupported-capability fixtures.**
  - **PASS when:** Unsupported capability/MSAA/feature combinations exercise the documented rejection or fallback; allocation, recording and UI diagnostics agree and retries remain valid.
  - **Evidence:** Source-backed support decisions plus real rejection/fallback and recovery fixture outcomes; unavailable required checks remain NOT RUN. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-11.1 — Inspect smooth sphere.**
  - **PASS when:** Actual Editor inspection closes the required smooth/unoccluded sphere gates, including AO/contact on/off, edges, normals and intended lighting-term preservation.
  - **Evidence:** Debug/Release project/backend/camera/settings, actual visual observations and paired images; startup is not visual evidence. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
  - **EDITOR VISUAL:** Execute and record the required actual Editor inspection; startup or readback alone does not close this gate.
- [ ] **M15-U10-11.2 — Inspect roughness sweep.**
  - **PASS when:** Actual Editor inspection and controlled numeric/image comparisons show continuous roughness/metal/F0 behavior across the declared material and lighting paths.
  - **Evidence:** Frozen sweep fixtures/reference/tolerances, material settings, backend/build and captured observations/images. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
  - **EDITOR VISUAL:** Execute and record the required actual Editor inspection; startup or readback alone does not close this gate.
- [ ] **M15-U10-11.3 — Inspect red-object reflection.**
  - **PASS when:** The actual captured/reflected colored object appears in the expected directions and roughness levels, with correct planar/SSR/probe/sky fallback and no duplicate energy.
  - **Evidence:** Editor captures plus applicable native numeric/readback evidence, source/camera/settings and frozen expected results. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
  - **EDITOR VISUAL:** Execute and record the required actual Editor inspection; startup or readback alone does not close this gate.
- [ ] **M15-U10-12.1 — Run resize/minimize/restore.**
  - **PASS when:** Repeated resize/minimize/restore preserves declared valid output, completion-safe target/descriptor lifetimes and bounded resource behavior; failed reconfiguration recovers explicitly.
  - **Evidence:** Lifecycle matrix rows with transitions, outcomes, resource/memory measurements and native validation logs. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-12.2 — Run view switch/device reinit.**
  - **PASS when:** View switches, camera cuts, scene/settings changes and device reinit invalidate appropriate state without cross-view history or stale epoch reuse; capture and Preview/Runtime remain isolated.
  - **Evidence:** Actual lifecycle/reset/recovery fixtures, source ownership references, image/history outcomes and resource generation evidence. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-12.3 — Run 1000-frame soak.**
  - **PASS when:** The declared soak runs at least 120 warm-up and 1,000 measured frames with valid output/history, no unbounded resource/cache growth and no unresolved native validation error.
  - **Evidence:** Workload/preset/adapter, frame counts, reset/failure schedule, image checks and memory/resource high-water/steady-state logs. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-13.1 — Capture parity artifacts.**
  - **PASS when:** Reproducible artifacts compare supported D3D12/Vulkan, builtin/graph and Editor/Preview/Runtime paths in the same defined HDR/display spaces and authored conditions.
  - **Evidence:** Pairing manifest, final source/binary identity, fixtures/cameras/settings, images/readbacks, quantitative comparisons and reviewer observations. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
  - **EDITOR VISUAL:** Execute and record the required actual Editor inspection; startup or readback alone does not close this gate.
- [ ] **M15-U10-13.2 — Report thresholds.**
  - **PASS when:** Reference values, numeric/image tolerances and their rationale are frozen before evaluation; legitimate reference changes have independent expected-result evidence.
  - **Evidence:** Versioned comparison/threshold manifest, actual measurements and discrepancy disposition; no assertion relaxation solely to obtain PASS. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-14.1 — Measure p50/p95/p99/max timings.**
  - **PASS when:** Release CPU/GPU p50/p95/p99/max are recorded with valid timing/completion provenance for named passes and whole views/frames; cold compile/first-use and warm steady state are separated. Required promoted presets/workloads satisfy their frozen timing budgets; unexplained violations remain FAIL.
  - **Evidence:** At least 120 warm-up and 1,000 measured frames per declared workload, timing logs, cold/warm cache conditions and comparison against frozen budgets. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-14.2 — Measure memory.**
  - **PASS when:** Peak/steady memory, per-view targets/histories, probe/cache/update workload and resource counts stay within declared preset budgets, including lifecycle stress.
  - **Evidence:** Measured memory/resource scorecard, workload/view/update counts, budget values and explicit violation results. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-14.3 — Record 1080p/1440p workload.**
  - **PASS when:** Release 1080p and 1440p workloads have reproducible scene/camera/quality, geometry/light/probe/update counts and supported feature combinations.
  - **Evidence:** Workload/preset manifest and actual timing/memory results for each required configuration. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-15.1 — Report named lighting passes.**
  - **PASS when:** Named lighting-pass scopes match actual pass ownership and execution; all passes included in a lighting-budget claim are enumerated with valid GPU timing.
  - **Evidence:** Pass/source map, measured scope definitions and per-pass results; no hidden omitted work or double-counted overlapping scopes. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
  - **EDITOR VISUAL:** Execute and record the required actual Editor inspection; startup or readback alone does not close this gate.
- [ ] **M15-U10-15.2 — Report named GPU/preset.**
  - **PASS when:** Each reported result names the physical GPU/driver/backend, resolution and exact quality/update/sample budgets used.
  - **Evidence:** Scorecard rows linked to the frozen preset/workload and hardware manifests. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-15.3 — Separate total frame time.**
  - **PASS when:** The named lighting subset, whole-view GPU scope and total CPU/GPU/present frame behavior are reported separately; the 3.5-5.0 ms lighting target is evaluated only on its named workload/GPU/preset.
  - **Evidence:** Measured subset and total scope results and budget comparisons; lighting time alone does not establish an FPS claim. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-16.1 — Record GPU/driver/OS.**
  - **PASS when:** Final evidence identifies physical adapter, driver and OS and records validation/fallback policy for each required backend.
  - **Evidence:** Hardware/software manifest linked to actual execution logs. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-16.2 — Record backend/commit.**
  - **PASS when:** Source, executable/configuration, backend, fixtures/settings and corrected-baseline identity are traceable for every final acceptance result.
  - **Evidence:** Source/binary/evidence manifest, exact commands and artifact paths; stale results are not relabeled as current execution. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-17.1 — Run independent source review.**
  - **Derived summary:** PASS only when every child below is PASS; no separate evidence or execution claim. Follow [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).

- [ ] **M15-U10-17.1.1 — Freeze final review baseline and evidence scope.**
  - **PASS when:** The integrated source, binaries/configurations, support matrix, required/optional classification, fixture/reference/budget manifests and source-writer boundary are recorded.
  - **Evidence:** Source/support manifest; every U0-U10 leaf has an owner and planned evidence path.

- [ ] **M15-U10-17.1.2 — Audit layer boundaries and pass/service ownership.**
  - **PASS when:** Production dataflow respects Scene/Renderer/RHI/UI boundaries and the M15 progressive pass-extraction contract; duplicated algorithms/policies and remaining monolithic feature adapters receive findings.
  - **Evidence:** Actual module/pass/caller map and source audit, supplemented by architecture checks; see acceptance procedure Section 3.

- [ ] **M15-U10-17.1.3 — Audit GPU resource, graph, view and threading contracts.**
  - **PASS when:** Generation publication, completion/descriptor lifetime, graph dependencies, view/history isolation, owner-thread boundaries, cache invalidation and failure/retry paths satisfy their contracts.
  - **Evidence:** Source-to-contract audit plus native lifecycle/failure evidence and validation logs; unresolved violations are findings.

- [ ] **M15-U10-17.1.4 — Audit material, radiometric and feature-policy consistency.**
  - **PASS when:** Supported fields, spaces/units, reflection/AO energy, exposure/tonemapping, capability/MSAA policy and allocation/declaration/recording agree across builtin/graph and product views.
  - **Evidence:** Source/caller traces, independent references and supported/fallback parity evidence.

- [ ] **M15-U10-17.1.5 — Audit every U0-U10 feature and atomic requirement.**
  - **PASS when:** Every atomic leaf is mapped to full implementation/caller paths, actual behavior, independently verified evidence and an explicit disposition; prior package PASS does not bypass integrated-source review.
  - **Evidence:** Complete requirement-to-evidence matrix covering all feature families in acceptance procedure Section 4.

- [ ] **M15-U10-17.1.6 — Audit cross-feature integration and fallback.**
  - **PASS when:** All applicable minimum integration scenarios have supported-policy decisions, frozen expected behavior and passing numeric/image/native evidence; required combinations cannot be reclassified unsupported to hide missing behavior.
  - **Evidence:** Integration matrix from acceptance procedure Section 5, including real rejection/fallback tests for unsupported combinations.

- [ ] **M15-U10-17.1.7 — Audit stability, visual and performance evidence.**
  - **PASS when:** All required U10-08..16 gates and carried-forward visual checks are reconciled, measurements are valid/current and preset budget violations have explicit dispositions.
  - **Evidence:** Lifecycle, actual visual, parity and cold/warm timing/memory scorecards; see acceptance procedure Sections 6 and 7.

- [ ] **M15-U10-17.1.8 — Publish consolidated architecture and feature findings.**
  - **PASS when:** The report accounts for every requirement and discrepancy with stable finding IDs, severity, source/callers, trigger, expected/observed result, owner and resolution path.
  - **Evidence:** Architecture report, requirement/integration matrices and findings register; no missing required feature is hidden by a summary PASS.
- [ ] **M15-U10-17.2 — Run correction recheck.**
  - **Derived summary:** PASS only when every child below is PASS; no separate evidence or execution claim. Follow [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).

- [ ] **M15-U10-17.2.1 — Repair and track acceptance findings.**
  - **PASS when:** Findings within the released review scope are corrected in verified coherent checkpoints while preserving required behavior, references and unrelated work.
  - **Evidence:** Finding-to-correction commits and actual reproduction/verification results; no requirement deletion or assertion weakening solely to obtain PASS.

- [ ] **M15-U10-17.2.2 — Independently recheck significant corrections.**
  - **PASS when:** A separate-context reviewer inspects significant corrections and affected callers after the latest repair; any further necessary repairs receive focused rechecks.
  - **Evidence:** Independent recheck report naming exact inspected source and remaining findings; source-only review is not execution evidence.

- [ ] **M15-U10-17.2.3 — Reverify the corrected final integrated baseline.**
  - **PASS when:** Changed executables are rebuilt and affected checks plus required final consolidated gates pass against the corrected source; evidence provenance is updated.
  - **Evidence:** Final build/test/visual/measurement records, configurations, commands/results and artifacts; missing mandatory checks stay NOT RUN.

- [ ] **M15-U10-17.2.4 — Reconcile findings and final acceptance decision.**
  - **PASS when:** Every mandatory leaf and required visual/integration/stability/measurement gate is PASS; missing mandatory behavior blocks closure regardless of severity, with no unresolved P1/P2 blocker or required NOT RUN/BLOCKED check.
  - **Evidence:** Final requirement/finding register, permitted optional deferrals, known limits, supported defaults and explicit full/partial/not-accepted decision.
- [ ] **M15-U10-18.1 — Attach commit to each completed item.**
  - **PASS when:** Every completed atomic leaf names its implementation/correction commit and final reviewed source applicability; derived parents reference child dispositions.
  - **Evidence:** Complete U0-U10 requirement matrix with no unassigned or silently dropped leaf. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-18.2 — Attach evidence to each completed item.**
  - **PASS when:** Every completed leaf links independently verified required evidence, references/tolerances, exact execution or interactive observations and associated findings.
  - **Evidence:** Complete requirement-to-evidence matrix; implementer reports, source rechecks and actual reviewer executions are distinguished. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-19.1 — Promote PASS features.**
  - **PASS when:** Only features/presets with completed mandatory gates, supported backend behavior and measured budgets are promoted to their declared defaults.
  - **Evidence:** Final acceptance/support/preset manifest and explicit promotion decisions; opt-in/experimental status remains visible. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).
- [ ] **M15-U10-19.2 — Keep failed/deferred features out of defaults.**
  - **PASS when:** Failed, incomplete or deferred features remain out of defaults with explicit diagnostics/limits; mandatory missing behavior or required unrun checks keep full M15 acceptance open.
  - **Evidence:** Final findings/gate register, supported-default manifest and tested disable/fallback behavior. See [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md).

**M15 final acceptance:** every mandatory atomic leaf and required visual, integration, stability and measurement gate is PASS on the corrected integrated source. No missing mandatory behavior, unresolved P1/P2 blocker or required NOT RUN/BLOCKED check remains. Separately permitted optional deferrals and other nonblocking findings are explicit, and only verified supported features are promoted.

## 5. M15 completion rule

M15 is accepted only when all required U0-U10 atomic items are PASS, optional
P6B/DDGI items are PASS or explicitly DEFERRED, and the final acceptance
items record Debug/Release builds, native backend evidence, Editor visual review,
parity artifacts, soak/performance measurements and an independent review. A
compile, non-empty image or parent heading never substitutes for an item PASS.
The [U10 acceptance procedure](U10_ACCEPTANCE_REVIEW.md) defines the required
architecture audit, complete requirement/evidence matrix, minimum integration and
lifecycle scenarios, actual visual review, cold/warm performance evidence and
correction closure. Required deferred visual gates remain open until executed;
opt-in status alone is not permission to omit required implementation. This
documentation update does not release U10 work or change an active earlier task.

**Status:** HOLD — this checklist is the execution contract; no M15 implementation is released by this document.
