# Milestone 15 — Lighting and Material Fidelity

**Status:** `HOLD — implementation is in progress; this document is the canonical execution and acceptance contract.`
**Current source reference:** `77d6b70` (authored reflection-probe checkpoint). Later uncommitted experiments are not part of the baseline.
**Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md)
**Detailed implementation notes:** [REMEDIATION_PLAN.md](REMEDIATION_PLAN.md)
**Independent review:** [M14_CODEX_REVIEW.md](../../reviews/M14_CODEX_REVIEW.md)

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

| Area | Current state at `77d6b70` | Classification |
|---|---|---|
| CSM, HDR/Split-Sum IBL, local shadows, procedural sky | Existing implementation and earlier native evidence | Baseline to protect |
| P0 baseline fixtures and audit | 6 deterministic fixtures, 19 diagnostic channels, 5 open source findings | Verified checkpoint |
| GPU timestamps | Valid probe and renderer scope on RTX 3070 D3D12/Vulkan Debug/Release | Verified checkpoint; full M18 matrix open |
| Smooth ForwardPBR normal | Derivative-flat-normal override removed | Verified partial checkpoint |
| `ScreenNormal` producer | Opaque normal pass exists and is consumed by GTAO/SSR | Verified partial checkpoint |
| GTAO/contact composite | Conservative bounded post-lighting composite | Verified partial checkpoint; true indirect separation open |
| Hi-Z | Per-level descriptor sets and graph reads fixed | Verified resource checkpoint; quality traversal open |
| Authored reflection probe | `CubemapPath` bake/cache/sample path works | Verified partial P5 checkpoint |
| Dynamic capture/prefilter/scheduling/multi-probe | Not complete | Open |
| SSR material resolve | Fixed Hi-Z input exists; cone tracing, material Fresnel and energy resolve remain open | Open |
| Fog/DDGI/exposure/bloom/TAA/SSS/LTC | Prototypes or partial paths exist; contracts are not closed | Open |
| RT | Capability reporting only; no BLAS/TLAS or RT pipeline | Optional extension open |

## 3. Hierarchy and dependency order

M15 follows the repository hierarchy exactly:

```text
Milestone 15 — Lighting and Material Fidelity
  └─ Phase 45 — Lighting and Material Fidelity delivery
      └─ Unit U0..U10 — architectural ownership and release boundary
          └─ SubUnit Sx.y — one independently buildable/checkable implementation package
```

The detailed execution sections below use `P0..P14` as historical package labels. In the canonical hierarchy they are **SubUnits**, not additional Milestones or Phases. Each SubUnit maps to one Unit and must leave the tree buildable.

| Milestone | Phase | Unit | SubUnits in this plan | Scope |
|---|---|---|---|---|
| M15 | 44.1–44.3 | U0 Baseline | S0.1–S0.2 (`P0`) | Existing lighting baseline, contracts and fixtures |
| M15 | 44.4, 44.6 | U1 Direct BRDF | S1.1–S1.3 | GGX compensation, sun angular radius and direct-light numeric fixtures |
| M15 | 44.5 | U2 Dynamic Environment | S2.1–S2.3 | Procedural sky, environment bake/cache and publication |
| M15 | shared infrastructure | U3 Screen-Space Infrastructure | S3.1–S3.4 (`P1`, `P2`) | Targets, graph, descriptors, surface data, timing and histories |
| M15 | 44.7–44.8 | U4 Occlusion | S4.1–S4.3 (`P3`) | GTAO, bilateral reconstruction and directional contact shadows |
| M15 | 44.9–44.11 | U5 Reflections and Area Lights | S5.1–S5.8 (`P4`, `P5A–P5D`, `P6`, `P7`) | Hi-Z, captures, probes, SSR and LTC |
| M15 | 44.12 | U6 Atmosphere | S6.1–S6.3 (`P8`) | Froxel fog, shadows and temporal fog state |
| M15 | 44.13 | U7 Experimental GI | S7.1–S7.4 (`P9`) | DDGI acquisition, atlases, visibility and feedback |
| M15 | 44.14–44.15 | U8 Post and Temporal | S8.1–S8.6 (`P10`) | Exposure, bloom and TAA |
| M15 | 44.16 | U9 Advanced Materials | S9.1–S9.5 (`P11`) | Sheen, absorption, dispersion, SSS and graph parity |
| M15 | 44.18 | U10 Authoring and Rollout | S10.1–S10.5 (`P12`, `P14`) | Editor, presets, evidence and final acceptance |
| M15 | optional 44.19 extension | U11 Ray-Traced Extension | S11.1–S11.4 (`P13`) | BLAS/TLAS, RT reflections and RT-assisted GI |

Phase 45.17 is the final performance/quality gate inside U10; it is not a separate implementation Unit. U11 is optional and cannot be claimed as part of core M15 unless its SubUnits pass.

### Unit closure rule

A Unit closes only when all its SubUnits pass, its dependency Units are accepted, and its Unit exit evidence is recorded. A Phase closes only when every Unit mapped to that Phase passes. M15 closes only when all required Phases and the final U10 gate pass.

### WorkItem granularity rule

High-risk SubUnits are split into WorkItems below. A WorkItem is the smallest planned delivery that should normally have one focused change and one focused verification. WorkItems inherit their parent SubUnit's dependencies and do not create a new roadmap phase.

| Unit | SubUnit | WorkItems |
|---|---|---|
| U3 | S3.1 Resource/graph | W3.1.1 target generation; W3.1.2 descriptor lifetime; W3.1.3 graph declarations; W3.1.4 failure/retry; W3.1.5 timing ring |
| U3 | S3.2 Surface/material | W3.2.1 normal producer; W3.2.2 normal-map parity; W3.2.3 material packing; W3.2.4 graph parity; W3.2.5 smooth-sphere metric |
| U5 | S5.1 Hi-Z | W5.1.1 level allocation; W5.1.2 reduction; W5.1.3 barriers; W5.1.4 odd-size reference; W5.1.5 debug readback |
| U5 | S5.2 Dynamic Capture | W5.2.1 capture formats; W5.2.2 six camera faces; W5.2.3 recursive suppression; W5.2.4 face copy/lifetime; W5.2.5 red-object fixture |
| U5 | S5.3 Prefilter | W5.3.1 mip allocation; W5.3.2 cubemap orientation; W5.3.3 roughness convolution; W5.3.4 seam handling; W5.3.5 roughness sweep |
| U5 | S5.4 Scheduling | W5.4.1 invalidation keys; W5.4.2 manual capture; W5.4.3 automatic budget; W5.4.4 last-good generation; W5.4.5 cache eviction |
| U5 | S5.5 Multi-Probe | W5.5.1 influence weights; W5.5.2 parallax direction; W5.5.3 multi-probe blend; W5.5.4 stale/missing fallback; W5.5.5 seam fixture |
| U5 | S5.6 SSR | W5.6.1 Hi-Z cell traversal; W5.6.2 thickness/refinement; W5.6.3 cone filtering; W5.6.4 material Fresnel; W5.6.5 energy resolve |
| U7 | S7.1–S7.4 DDGI | W7.1.1 radiance acquisition; W7.1.2 atlas packing; W7.1.3 visibility; W7.1.4 temporal scheduling; W7.1.5 ForwardPBR feedback; W7.1.6 leak/bounce fixture |
| U8 | S8.1–S8.6 Post | W8.1.1 global histogram; W8.1.2 exposure consumer; W8.2.1 bloom extraction; W8.2.2 bloom reconstruction; W8.3.1 motion vectors; W8.3.2 history reset; W8.3.3 disocclusion; W8.3.4 transparent/UI policy |
| U9 | S9.1–S9.5 Materials | W9.1 sheen; W9.2 absorption; W9.3 dispersion; W9.4 SSS diffusion; W9.5 graph/asset parity |
| U10 | S10.1–S10.5 Authoring | W10.1 material Inspector; W10.2 graph/preview; W10.3 probe gizmo/capture UI; W10.4 serialization/runtime; W10.5 presets/evidence |
| U11 | S11.1–S11.4 RT | W11.1 capability policy; W11.2 BLAS; W11.3 TLAS/update; W11.4 RT shaders; W11.5 denoise; W11.6 fallback/fixture |

#### WorkItem tracking checklist

The following gates are intentionally separate from the larger SubUnit checkboxes. Tick a WorkItem only when its parent SubUnit's source and runtime evidence exists.

- [ ] **U3/W3.1:** target generations, descriptor lifetime, graph declarations, failure/retry and timing ring each have focused tests.
- [ ] **U3/W3.2:** surface normal, normal maps, material packing, graph parity and smooth-sphere metric pass.
- [ ] **U5/W5.1:** Hi-Z allocation/reduction/barriers/odd-size/reference/debug checks pass.
- [ ] **U5/W5.2:** six-face capture formats, orientations, recursive suppression, copies/lifetime and red-object fixture pass.
- [ ] **U5/W5.3:** cubemap mips, orientation/seams, roughness convolution and sweep pass.
- [ ] **U5/W5.4:** invalidation, manual/automatic scheduling, budget, last-good generation and eviction pass.
- [ ] **U5/W5.5:** influence/parallax/multi-probe/fallback/seam checks pass.
- [ ] **U5/W5.6:** traversal, thickness/refinement, cone filtering, Fresnel and energy resolve pass.
- [ ] **U7/W7.1:** DDGI acquisition, atlas, visibility, schedule, ForwardPBR feedback and leak/bounce fixture pass.
- [ ] **U8/W8.1:** global histogram and exposure consumer pass frame-rate/adaptation checks.
- [ ] **U8/W8.2:** bloom extraction/reconstruction pass impulse/gain checks.
- [ ] **U8/W8.3:** motion vectors, reset, disocclusion and transparent/UI policy pass temporal checks.
- [ ] **U9/W9:** sheen, absorption, dispersion, SSS and graph/asset parity each pass their material fixture.
- [ ] **U10/W10:** Inspector, graph/preview, probe UI, serialization/runtime and presets/evidence each pass authoring round trips.
- [ ] **U11/W11:** capability policy, BLAS/TLAS, RT shaders, denoise and fallback pass optional RT acceptance or are explicitly deferred.

### Dependency order

```text
U0 -> U1/U2 -> U3 -> U4 -> U5 -> U6/U7 -> U8 -> U9 -> U10; U11 is optional and depends on U3/U5/U7.

Within U5: `S5.1 Hi-Z -> S5.2 Dynamic Capture -> S5.3 Prefilter -> S5.4 Invalidation/Scheduling -> S5.5 Multi-Probe -> S5.6 SSR -> S5.7 LTC`. The existing `P4/P5A–P5D/P6/P7` names are implementation aliases for these SubUnits.
```

P5A–P5D may reuse authored-probe work, but none may silently depend on unfinished P6. P13 is optional for raster M15 acceptance and mandatory only if exact dynamic off-screen reflections are product requirements.

## 4. U0 / S0.1–S0.2 — Contract, fixtures and baseline normalization

- [x] Record dirty-path policy and preserve user scene changes.
- [x] Add deterministic sphere, reflection, roughness, post, temporal and resource-edge fixtures.
- [x] Define HDR/display comparison space, backend matrix, warmup/sample frames and diagnostic channels.
- [x] Add non-mutating baseline manifest/report validation.
- [x] Record open findings and withdraw unsupported historical FIXED/driver claims.
- [ ] Add CPU/reference checks for smooth normals, Hi-Z reduction, probe direction and roughness-to-mip mapping.
- [ ] Add image-difference tooling with frozen thresholds and artifact identity.

**Exit:** known bad behavior is reproducible and rejected; report states `M15 HOLD`.

## 5. U3 / S3.1 — RHI resource ownership, graph and measurement

- [x] Make timestamp probe descriptions valid and require successful readback.
- [x] Read renderer timestamps on all public view submission paths.
- [ ] Add checked update helpers for every M15 descriptor/storage/constant update.
- [ ] Never modify a descriptor set after binding in the same submission.
- [ ] Declare every actual graph read/write, including Hi-Z, normal, probe, history and exposure resources.
- [ ] Add per-view target generation, completion and reset ownership.
- [ ] Test resize, feature toggle, allocation failure/retry, device epoch and shutdown/reinit.
- [ ] Add asynchronous per-pass query/readback ring and invalid-measurement state.

**Exit:** validation clean; failures retain last-good generation or explicit fallback; timing is valid on tested workloads.

## 6. U3 / S3.2 — Surface, normal and material data contract

- [x] Remove derivative-flat-normal override from generic ForwardPBR modes.
- [x] Produce opaque world-space `ScreenNormal` and declare it in the graph.
- [ ] Match normal producer with normal/detail-normal decoding and strength.
- [ ] Verify inverse-transpose normals under non-uniform scale and mirrored transforms.
- [ ] Define alpha-cutout/depth/normal coverage and background validity.
- [ ] Unify built-in and Material Graph base color, metallic, roughness, AO, emission, normal, clearcoat and reflection inputs.
- [ ] Verify sRGB/linear texture and packed ORM conventions.
- [ ] Complete asset -> component -> extraction -> GPU round-trip tests.
- [ ] Add smooth-sphere normal-field and anti-faceting metric.

**Exit:** smooth sphere has no triangle bands; built-in/graph materials match in supported scope; save/reopen preserves fields.

## 7. U4 / S4.1–S4.3 — GTAO and directional contact shadows

- [x] Use valid `ScreenNormal` input.
- [x] Remove full-scene `AO * contact` multiplication and use bounded independent weights.
- [ ] Move AO before lighting or expose a true indirect-light target.
- [ ] Apply contact visibility only to its directional-light contribution and avoid CSM/local double attenuation.
- [ ] Validate half-resolution sampling, bilateral reconstruction, depth edges and odd resolutions.
- [ ] Test no-sun, no-AO, no-contact, emissive, mirror and unoccluded-sphere fixtures.
- [ ] Test normal-map and animated/deformed mesh behavior.

**Exit:** occlusion is stable and finite; unoccluded specular/emission is preserved; no new faceting/halos.

## 8. U5 / S5.1 — Hi-Z depth pyramid

- [x] Use immutable per-level descriptor generations.
- [x] Check every update and bind failure.
- [x] Declare all eight levels as SSR reads.
- [x] Validate dimensions and non-identical readback for all levels.
- [ ] Validate min reduction against CPU reference for odd dimensions and 1x1/3x5/1919x1079.
- [ ] Define depth convention, edge policy and useful level count.
- [ ] Add per-level visualization and barrier validation.

**Exit:** every level matches reference reduction and transitions correctly into SSR.

## 9. U5 / S5.2–S5.5 — Reflection Captures and local probes

### S5.2 — Dynamic six-face capture

- [ ] Allocate capture color/depth targets with matching pipeline formats.
- [ ] Render six 90-degree views with correct directions/up vectors.
- [ ] Disable recursive SSR/probe sampling, TAA history, Editor/UI and feedback during capture.
- [ ] Render opaque/cutout scene and selected shadows under the capture policy.
- [ ] Copy each face into the cubemap with correct layer offsets and transitions.
- [ ] Retain resources through completion and publish generation transactionally.
- [ ] Prove the red reference object appears in the captured cubemap.
- [ ] Retain last-good generation when a face/copy fails.

### S5.3 — Cubemap prefilter

- [ ] Allocate mip-capable cubemap/destination UAV/SRV views.
- [ ] Implement cubemap-aware roughness prefilter preserving face orientation/seams.
- [ ] Use sharp mip for mirror materials and progressively blurred mips for rough materials.
- [ ] Validate output with controlled environment/roughness sweep.
- [ ] Do not route cubemap data through equirectangular assumptions.

### S5.4 — Invalidation and scheduling

- [ ] Track probe transform, scene revision, geometry/material, light/shadow and asset generations.
- [ ] Support manual and automatic capture.
- [ ] Bound faces/probes per frame and expose pending/stale/failed diagnostics.
- [ ] Retain last-good generation on rebuild failure.
- [ ] Add capture cache key, memory budget and eviction.
- [ ] Test repeated capture, resize, device reset and scene replacement.

### S5.5 — Multi-probe selection and blend

- [ ] Compute continuous box/sphere influence weights.
- [ ] Blend multiple valid probes without hard seams.
- [ ] Apply parallax correction per probe before blending.
- [ ] Blend probe with SSR confidence and sky IBL under one energy policy.
- [ ] Test overlap, boundary movement, missing/stale probe and SSR miss.

**P5 exit:** with SSR disabled, a captured cubemap contains the red object and the sphere samples it; roughness changes sharpness; invalidation/scheduling/blending/fallback pass.

## 10. U5 / S5.6 — Material-aware SSR and reflection resolve

- [ ] Advance through Hi-Z cells hierarchically.
- [ ] Add thickness, hit refinement and edge/disocclusion rejection.
- [ ] Add roughness cone/footprint filtering over HDR color mips.
- [ ] Pass roughness, metallic/base color and reflection flags into resolve.
- [ ] Compute Fresnel/F0 for dielectric and conductor materials.
- [ ] Replace/blend indirect specular instead of blindly adding a second reflection over IBL.
- [ ] Blend SSR, local probe and sky fallback by confidence/influence.
- [ ] Test hit, miss, moving object, roughness sweep, gold/chrome/dielectric and camera motion.

**Exit:** colored geometry appears in the expected reflection; roughness varies continuously; no black edge, ghost or energy spike.

## 11. U5 / S5.7 — LTC rectangular and disk area lights

- [ ] Add validated LTC transform/amplitude tables or reproducible generation.
- [ ] Implement rectangle/disk emitters with units, orientation, sidedness and range.
- [ ] Integrate diffuse/specular area response consistently.
- [ ] Define area-light shadow policy separately.
- [ ] Complete component/extraction/serialization/runtime/Inspector paths.
- [ ] Compare against numerical reference across roughness/view/emitter angles.

**Exit:** shape/roughness-correct, finite highlights without duplicate emitter energy.

## 12. U6 / S6.1–S6.3 — Volumetric fog and atmosphere

- [ ] Allocate bounded 3D froxels with documented quality scaling (`160×90×64` reference).
- [ ] Inject density/media and supported light/shadow visibility.
- [ ] Integrate scattering/extinction with defined units.
- [ ] Add temporal reprojection/rejection and invalidation.
- [ ] Define transparent/depth interaction and no-light/zero-density behavior.
- [ ] Remove invented sun fallback and emit diagnostics.
- [ ] Test wall occlusion, shafts, camera motion, resize, memory and cost.

**Exit:** fog respects blockers/lights, zero density preserves the image, trails are bounded, cost is reported.

## 13. U7 / S7.1–S7.4 — Experimental DDGI

- [ ] Choose/document spatial radiance acquisition (shared tracing or bounded raster probe capture).
- [ ] Implement probe indexing/origin/spacing and bounded allocation.
- [ ] Implement octahedral irradiance and distance/variance atlases.
- [ ] Implement visibility moments/Chebyshev weighting and bias.
- [ ] Implement temporal accumulation and staggered updates.
- [ ] Feed irradiance into diffuse ForwardPBR lighting.
- [ ] Add scene/probe invalidation, no-valid-probe fallback and budgets.
- [ ] Test indirect bounce, wall leakage, grid settings and disabled fallback.

**Exit:** bounce follows world probes rather than camera pixels; walls suppress leakage; experimental status/cost explicit.

## 14. U8 / S8.1–S8.6 — HDR post and temporal pipeline

### Exposure

- [ ] Build one global log-luminance histogram and reduction.
- [ ] Adapt previous exposure with real delta time and per-view state.
- [ ] Feed adapted exposure into tonemapping; define first frame/black frame/camera cut.
- [ ] Exclude UI/bloom from metering by policy.
- [ ] Test mixed tiles, 30/60/120 Hz and exposure limits.

### Bloom

- [ ] Threshold HDR source once.
- [ ] Build six downsample levels with correct dimensions.
- [ ] Reconstruct additively without discarding finer contributions.
- [ ] Apply intensity/tint once.
- [ ] Test impulse halo, gain, odd/small resolution and HDR/SDR.

### TAA

- [ ] Generate camera/object motion vectors with consistent jitter.
- [ ] Reject disoccluded/invalid history.
- [ ] Reset on resize, view switch, camera cut, settings/format/scene generation change.
- [ ] Define transparent/reactive/UI policy.
- [ ] Test static shimmer, moving object, disocclusion, resize and ghosting.

**Exit:** exposure, bloom and TAA have behavior tests and no stale-history/gain artifacts.

## 15. U9 / S9.1–S9.5 — Advanced material models

- [ ] Charlie sheen direct/IBL response with energy checks.
- [ ] Beer–Lambert absorption with documented thickness/path policy.
- [ ] Cauchy/wavelength dispersion within documented scope.
- [ ] Screen-space separable Gaussian SSS with depth/normal/material rejection.
- [ ] Preserve specular while diffusing intended diffuse data.
- [ ] Complete native asset/component/GPU/Inspector/Preview/Runtime paths.
- [ ] Extend Material Graph outputs/lowering or explicitly defer unsupported fields.
- [ ] Test sheen, absorption, SSS and built-in/graph parity.

**Exit:** models are bounded, authorable, serializable and pass controlled fixtures.

## 16. U10 / S10.1–S10.4 — Editor and authoring integration

- [ ] Add controls for all accepted material fields with clamps/tooltips/defaults.
- [ ] Add probe placement, shape, capture, scheduling and stale/failed diagnostics.
- [ ] Add Preview controls matching Scene environment/exposure/AA/reflection policy.
- [ ] Complete scene/assembly/runtime codec round trips.
- [ ] Add Low/Medium/High/Ultra presets with explicit budgets.
- [ ] Diagnose unsupported OpenGL/capability paths with valid fallback.
- [ ] Test Editor create/edit/save/reopen and Runtime parity.

**Exit:** every accepted runtime feature is authorable and survives save/reload.

## 17. U11 / S11.1–S11.4 — Optional ray-traced extension

- [ ] Freeze backend/capability policy and fallback.
- [ ] Build/refit BLAS for supported geometry.
- [ ] Build/update TLAS per scene/geometry changes.
- [ ] Add RT pipeline and ray-generation/miss/closest-hit shaders.
- [ ] Match ForwardPBR material/lighting evaluation.
- [ ] Add denoising and temporal rejection.
- [ ] Share geometry data with DDGI.
- [ ] Test off-screen dynamic reflection, self-intersection and fallback.

**Exit:** off-screen reflection works within documented limits, or P13 is explicitly deferred.

## 18. U10 / S10.5 — Final acceptance and rollout

- [ ] Rebuild affected Editor/Runtime/tests Debug and Release.
- [ ] Run D3D12/Vulkan physical-GPU fixtures with validation.
- [ ] Run OpenGL and unsupported-capability fallback fixtures.
- [ ] Inspect windowed Editor output for smooth sphere, roughness sweep and red-object reflection.
- [ ] Run resize/minimize/restore, view switch, device reinit and >=1,000-frame soak.
- [ ] Capture deterministic parity artifacts and report thresholds.
- [ ] Run 1080p/1440p Release benchmark with p50/p95/p99/max timings and memory.
- [ ] Report named lighting budget on named GPU/preset.
- [ ] Record hardware/driver/OS/backend/commit for measurements.
- [ ] Complete independent source review and correction recheck.
- [ ] Update every completed checkbox with commit and evidence.
- [ ] Promote only features whose exit conditions pass.

**M15 final acceptance:** core items are accepted, optional items are accepted or explicitly deferred, and no required check is silently waived.

## 19. Work tracking

Each checkpoint leaves the tree buildable/testable. Use one coherent commit per completed checkpoint, record exact commands/configurations/exit codes, preserve unrelated user changes, and never stage the exchange file or scene with implementation work.
