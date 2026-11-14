# Milestone 15 — Lighting and Material Fidelity

**Status:** `HOLD - M14 architecture prerequisite is accepted; M15 feature implementation is not released.`
**Current source reference:** `19ac2a1` (M14 final handoff). No M15 feature is promoted by this reference.
**Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md)
**Detailed implementation notes:** [REMEDIATION_PLAN.md](REMEDIATION_PLAN.md)
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
| RT | Capability reporting only; no BLAS/TLAS or RT pipeline | Optional extension open |

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
| M15 | 45.9–45.11 | U5 Reflections and Area Lights | S5.1–S5.8 (`P4`, `P5A–P5D`, `P6`, `P7`) | Hi-Z, captures, probes, SSR and LTC |
| M15 | 45.12 | U6 Atmosphere | S6.1–S6.3 (`P9`) | Froxel fog, shadows and temporal fog state |
| M15 | 45.13 | U7 Experimental GI | S7.1–S7.4 (`P11`) | DDGI acquisition, atlases, visibility and feedback |
| M15 | 45.14–45.15 | U8 Post and Temporal | S8.1–S8.6 (`P8`) | Exposure, bloom and TAA |
| M15 | 45.16 | U9 Advanced Materials | S9.1–S9.5 (`P10`) | Sheen, absorption, dispersion, SSS and graph parity |
| M15 | 45.18 | U10 Authoring and Rollout | S10.1–S10.5 (`P12`) | Editor, presets, evidence and final acceptance |
| M15 | optional 45.19 extension | U11 Ray-Traced Extension | S11.1–S11.4 (`P13`) | BLAS/TLAS, RT reflections and RT-assisted GI |

Phase 45.17 is the final performance/quality gate inside U10; it is not a separate implementation Unit. U11 is optional and cannot be claimed as part of core M15 unless its SubUnits pass.

Package aliases in this table follow the recovery plan: P8 is post/temporal, P9 is atmosphere, P10 is advanced materials, P11 is experimental DDGI, and P12 is authoring/final acceptance. P6B and P13 remain separately gated extensions.

### Unit closure rule

A Unit closes only when all its SubUnits pass, its dependency Units are accepted, and its Unit exit evidence is recorded. A Phase closes only when every Unit mapped to that Phase passes. M15 closes only when all required Phases and the final U10 gate pass.

### Canonical checklist policy

All M15 execution state is recorded in the single atomic checklist beginning in
Section 4. The Unit/SubUnit hierarchy and dependency table describe ownership;
they do not carry a second checkbox state. Close each item immediately after
its evidence passes, then continue to the next dependency-ready item.

### 3.1 Scope allocation boundary: Milestone 15 vs Milestone 16

To prevent scope creep and maintain distinct milestones (M15 Fidelity vs M16 Scalability), the following architectural boundaries are strictly enforced:

| Architectural Item | Target Milestone | Allocation and Execution Contract |
|---|---|---|
| **1. Modular `IRenderPass` Extraction** | **M15 (100% Progressive)** | Replace the monolithic lambda adapters in `RHISceneRendererPasses.cpp` by creating dedicated pass classes under `Engine/Renderer/include/Axle/Passes/` and `src/Passes/` as each M15 feature package is delivered. |
| **2. Hi-Z Depth Pyramid** | **M15 (100% Core)** | Delivered in U5/S5.1 (P4) with full compute downsampling and odd-dimension handling for SSR; reused in M16 for GPU-driven occlusion culling. |
| **3. Denoising Strategy** | **M15 (Pass-Local) / M16 (Unified)** | M15 uses pass-local spatial/temporal filters: Bilateral reconstruction for GTAO (P3), Roughness cone-filtering for SSR (P6), and Variance-clipped TAA (P8). A unified spatiotemporal denoiser (SVGF) is deferred to M16 or U11 (RT extension). |
| **4. Clustered / Tiled Light Culling** | **M16 (100% Deferred)** | Formally deferred to Milestone 16 (Phase 46.3 — Scalable Lighting Pipeline). M15 retains Forward+ lighting with a bounded active light capacity managed by `LightingService`. |
| **5. Bindless Textures for Material Graph** | **M15 (Heap Runtime) / M16 (Full Compiler)** | M15 delivers runtime `Tier2Bindless` heap resource support and standard texture slot mapping. Full HLSL dynamically indexed bindless array emission (`g_Textures[index]`) for indirect draw execution is aligned with M16 Virtual Geometry / Meshlets. |

### Dependency order

```text
U0 -> U1/U2 -> U3 -> U4 -> U5 -> U6/U7 -> U8 -> U9 -> U10; U11 is optional and depends on U3/U5/U7.

Within U5: `S5.1 Hi-Z -> S5.2 Dynamic Capture -> S5.3 Prefilter -> S5.4 Invalidation/Scheduling -> S5.5 Multi-Probe -> S5.6 SSR -> S5.7 LTC`. The existing `P4/P5A–P5D/P6/P7` names are implementation aliases for these SubUnits.
```

P5A–P5D may reuse authored-probe work, but none may silently depend on unfinished P6. P13 is optional for raster M15 acceptance and mandatory only if exact dynamic off-screen reflections are product requirements.

## 4. Canonical atomic execution and acceptance checklist

This is the only state-bearing checklist for M15. Each item is one independently
implementable behavior with its own PASS condition and evidence. A SubUnit is
PASS only when all its items are PASS; optional U11/P6B/DDGI items may be marked
DEFERRED with an explicit reason.

**Visual verification rule:** any item marked `EDITOR VISUAL` requires rebuilding
the affected Editor in Debug and Release, launching
`build/Editor/<Config>/AxleEditor.exe --rhi=dx12 --strict-rhi` (repeat with
Vulkan when parity is required), opening the named fixture, and attaching a
screenshot/readback. The item is PASS only when the expected display stated in
the item is visible.


### U0 — Baseline and contract freeze

- [x] **M15-U0-01 — Record dirty-path policy and preserve user scene changes.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U0-02 — Add deterministic sphere, reflection, roughness, post, temporal and resource-edge fixtures.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [x] **M15-U0-03 — Define HDR/display comparison space, backend matrix, warmup/sample frames and diagnostic channels.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U0-04 — Add non-mutating baseline manifest/report validation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U0-05 — Record open findings and withdraw unsupported historical FIXED/driver claims.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U0-06 — Add CPU/reference checks for smooth normals, Hi-Z reduction, probe direction and roughness-to-mip mapping.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U0-07 — Add image-difference tooling with frozen thresholds and artifact identity.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** known bad behavior is reproducible and rejected; report states `M15 HOLD`.

### U1 / S1.1–S1.3 — Direct BRDF and primary lighting

- [ ] **M15-U1-01 — Freeze direct-light units, sun angular-radius policy, BRDF normalization and exposure interaction.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.
- [ ] **M15-U1-02 — Validate GGX visibility/normal-distribution compensation and dielectric/metal F0 behavior against a numeric reference.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U1-03 — Verify directional, point and spot lights share the same `LightingService` units, shadow visibility and material inputs.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U1-04 — Add roughness/view/light-angle sweeps and finite zero/behind-surface cases.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: high roughness broadens the highlight/reflection continuously; low roughness stays sharp.
- [ ] **M15-U1-05 — Record direct-light fallback behavior for unsupported capability tiers.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** direct-light fixtures match the frozen numeric reference within documented tolerance; units, shadows, HDR response and fallback diagnostics are consistent across supported native backends.

### U2 / S2.1–S2.3 — Dynamic environment and authored environment policy

- [ ] **M15-U2-01 — Freeze procedural-sky, environment-bake and authored-cubemap color-space/orientation policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U2-02 — Define environment generation/publication, cache identity, invalidation and last-good fallback through `ReflectionService`.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U2-03 — Verify sky/direct-light coupling, environment rotation/intensity and missing/failed asset behavior.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U2-04 — Keep local dynamic six-face capture and probe blending in U5; this Unit owns the shared environment input contract.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U2-05 — Add deterministic sky/bake/asset-failure readbacks for D3D12, Vulkan and the documented OpenGL subset.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** procedural and authored environments produce the declared HDR/IBL inputs, cache and invalidation are transactional, and failed or unsupported environments select a named fallback without mutating authored data.

### U3 / S3.1 — RHI resource ownership, graph and measurement

- [x] **M15-U3-01 — Make timestamp probe descriptions valid and require successful readback.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U3-02 — Read renderer timestamps on all public view submission paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-03 — Add checked update helpers for every M15 descriptor/storage/constant update.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-04 — Never modify a descriptor set after binding in the same submission.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-05 — Declare every actual graph read/write, including Hi-Z, normal, probe, history and exposure resources.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U3-06 — Add per-view target generation, completion and reset ownership.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-07 — Test resize, feature toggle, allocation failure/retry, device epoch and shutdown/reinit.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-08 — Add asynchronous per-pass query/readback ring and invalid-measurement state.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-09 — Adopt progressive modular `IRenderPass` extraction under `Engine/Renderer/include/Axle/Passes/` and `src/Passes/`.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-10 — Replace legacy lambda adapters in `RHISceneRendererPasses.cpp` incrementally with dedicated pass classes (`GTAOPass` in P3, `HiZGenerationPass` in P4, `ScreenSpaceReflectionPass` in P6, `FroxelFogPass` in P9, `PostProcessPass` in P8).**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: fog shafts obey blockers; zero density/no-light leaves the image unchanged.
- [ ] **M15-U3-11 — Confine `RHISceneRenderer` to pure pass orchestration via `RenderPassScheduler` and `ViewContext` forwarding.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** validation clean; failures retain last-good generation or explicit fallback; timing is valid on tested workloads; pass modularity roadmap is active.

### U3 / S3.2 — Surface, normal and material data contract

- [x] **M15-U3-12 — Remove derivative-flat-normal override from generic ForwardPBR modes.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U3-13 — Produce opaque world-space `ScreenNormal` and declare it in the graph.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-14 — Match normal producer with normal/detail-normal decoding and strength.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-15 — Verify inverse-transpose normals under non-uniform scale and mirrored transforms.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-16 — Define alpha-cutout/depth/normal coverage and background validity.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-17 — Unify built-in and Material Graph base color, metallic, roughness, AO, emission, normal, clearcoat and reflection inputs.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U3-18 — Verify sRGB/linear texture and packed ORM conventions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-19 — Complete asset -> component -> extraction -> GPU round-trip tests.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-20 — Add smooth-sphere normal-field and anti-faceting metric.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.

**Exit:** smooth sphere has no triangle bands; built-in/graph materials match in supported scope; save/reopen preserves fields.

#### S3.3 — Material Graph, preview and runtime parity

- [ ] **M15-U3-21 — Resolve built-in, Material Graph, Preview and Runtime materials through the same supported `ResolvedSurface` fields and defaults.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen, and matches Runtime/Preview.
- [ ] **M15-U3-22 — Record explicit unsupported feature masks and fallback diagnostics for graph/custom paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-23 — Compare equivalent built-in/graph fixtures under identical lights, shadows, IBL and output policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.

#### S3.4 — Temporal, diagnostics and measurement integration

- [ ] **M15-U3-24 — Bind motion/depth/history/exposure resources through the declared graph contract.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: motion reduces shimmer without persistent ghosts, UI jitter or black gaps.
- [ ] **M15-U3-25 — Verify target-generation, camera-cut, feature-toggle and device-epoch resets before temporal consumers import history.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-26 — Record per-pass timing validity, fallback reasons and resource generations; invalid measurements remain `Unknown`.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**S3.3/S3.4 exit:** parity, reset and diagnostics fixtures pass on supported native paths, with unsupported paths explicitly reported.

### U4 / S4.1–S4.3 — GTAO and directional contact shadows

- [x] **M15-U4-01 — Use valid `ScreenNormal` input.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U4-02 — Remove full-scene `AO * contact` multiplication and use bounded independent weights.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U4-03 — Move AO before lighting or expose a true indirect-light target.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U4-04 — Apply contact visibility only to its directional-light contribution and avoid CSM/local double attenuation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U4-05 — Validate half-resolution sampling, bilateral reconstruction, depth edges and odd resolutions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U4-06 — Test no-sun, no-AO, no-contact, emissive, mirror and unoccluded-sphere fixtures.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.
- [ ] **M15-U4-07 — Test normal-map and animated/deformed mesh behavior.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** occlusion is stable and finite; unoccluded specular/emission is preserved; no new faceting/halos.

### U5 / S5.1 — Hi-Z depth pyramid

- [x] **M15-U5-01 — Use immutable per-level descriptor generations.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U5-02 — Check every update and bind failure.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [x] **M15-U5-03 — Declare all eight levels as SSR reads.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [x] **M15-U5-04 — Validate dimensions and non-identical readback for all levels.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-05 — Validate min reduction against CPU reference for odd dimensions and 1x1/3x5/1919x1079.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-06 — Define depth convention, edge policy and useful level count.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-07 — Add per-level visualization and barrier validation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior is visible in the named fixture and matches the frozen reference.

**Exit:** every level matches reference reduction and transitions correctly into SSR.

### U5 / S5.2–S5.5 — Reflection Captures and local probes

#### S5.2 — Dynamic six-face capture

- [ ] **M15-U5-08 — Allocate capture color/depth targets with matching pipeline formats.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-09 — Render six 90-degree views with correct directions/up vectors.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-10 — Disable recursive SSR/probe sampling, TAA history, Editor/UI and feedback during capture.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: motion reduces shimmer without persistent ghosts, UI jitter or black gaps.
- [ ] **M15-U5-11 — Render opaque/cutout scene and selected shadows under the capture policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U5-12 — Copy each face into the cubemap with correct layer offsets and transitions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-13 — Retain resources through completion and publish generation transactionally.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-14 — Prove the red reference object appears in the captured cubemap.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-15 — Retain last-good generation when a face/copy fails.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

#### S5.3 — Cubemap prefilter

- [ ] **M15-U5-16 — Allocate mip-capable cubemap/destination UAV/SRV views.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-17 — Implement cubemap-aware roughness prefilter preserving face orientation/seams.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: high roughness broadens the highlight/reflection continuously; low roughness stays sharp.
- [ ] **M15-U5-18 — Use sharp mip for mirror materials and progressively blurred mips for rough materials.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U5-19 — Validate output with controlled environment/roughness sweep.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: high roughness broadens the highlight/reflection continuously; low roughness stays sharp.
- [ ] **M15-U5-20 — Do not route cubemap data through equirectangular assumptions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

#### S5.4 — Invalidation and scheduling

- [ ] **M15-U5-21 — Track probe transform, scene revision, geometry/material, light/shadow and asset generations.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-22 — Support manual and automatic capture.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-23 — Bound faces/probes per frame and expose pending/stale/failed diagnostics.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-24 — Retain last-good generation on rebuild failure.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-25 — Add capture cache key, memory budget and eviction.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-26 — Test repeated capture, resize, device reset and scene replacement.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

#### S5.5 — Multi-probe selection and blend

- [ ] **M15-U5-27 — Compute continuous box/sphere influence weights.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.
- [ ] **M15-U5-28 — Blend multiple valid probes without hard seams.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-29 — Apply parallax correction per probe before blending.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-30 — Blend probe with SSR confidence and sky IBL under one energy policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-31 — Test overlap, boundary movement, missing/stale probe and SSR miss.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.

**P5 exit:** with SSR disabled, a captured cubemap contains the red object and the sphere samples it; roughness changes sharpness; invalidation/scheduling/blending/fallback pass.

### U5 / S5.6 — Material-aware SSR and reflection resolve

- [ ] **M15-U5-32 — Advance through Hi-Z cells hierarchically.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-33 — Add thickness, hit refinement and edge/disocclusion rejection.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-34 — Add roughness cone/footprint filtering over HDR color mips.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: high roughness broadens the highlight/reflection continuously; low roughness stays sharp.
- [ ] **M15-U5-35 — Pass roughness, metallic/base color and reflection flags into resolve.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-36 — Compute Fresnel/F0 for dielectric and conductor materials.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U5-37 — Replace/blend indirect specular instead of blindly adding a second reflection over IBL.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-38 — Blend SSR, local probe and sky fallback by confidence/influence.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-39 — Test hit, miss, moving object, roughness sweep, gold/chrome/dielectric and camera motion.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: motion reduces shimmer without persistent ghosts, UI jitter or black gaps.

**Exit:** colored geometry appears in the expected reflection; roughness varies continuously; no black edge, ghost or energy spike.

### U5 / S5.7 — LTC rectangular and disk area lights

- [ ] **M15-U5-40 — Add validated LTC transform/amplitude tables or reproducible generation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-41 — Implement rectangle/disk emitters with units, orientation, sidedness and range.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-42 — Integrate diffuse/specular area response consistently.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-43 — Define area-light shadow policy separately.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U5-44 — Complete component/extraction/serialization/runtime/Inspector paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen, and matches Runtime/Preview.
- [ ] **M15-U5-45 — Compare against numerical reference across roughness/view/emitter angles.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: high roughness broadens the highlight/reflection continuously; low roughness stays sharp.

**Exit:** shape/roughness-correct, finite highlights without duplicate emitter energy.

### U6 / S6.1–S6.3 — Volumetric fog and atmosphere

- [ ] **M15-U6-01 — Allocate bounded 3D froxels with documented quality scaling (`160×90×64` reference).**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U6-02 — Inject density/media and supported light/shadow visibility.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U6-03 — Integrate scattering/extinction with defined units.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U6-04 — Add temporal reprojection/rejection and invalidation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U6-05 — Define transparent/depth interaction and no-light/zero-density behavior.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U6-06 — Remove invented sun fallback and emit diagnostics.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07 — Test wall occlusion, shafts, camera motion, resize, memory and cost.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** fog respects blockers/lights, zero density preserves the image, trails are bounded, cost is reported.

### U7 / S7.1–S7.4 — Experimental DDGI

- [ ] **M15-U7-01 — Choose/document spatial radiance acquisition (shared tracing or bounded raster probe capture).**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U7-02 — Implement probe indexing/origin/spacing and bounded allocation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U7-03 — Implement octahedral irradiance and distance/variance atlases.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U7-04 — Implement visibility moments/Chebyshev weighting and bias.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U7-05 — Implement temporal accumulation and staggered updates.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U7-06 — Feed irradiance into diffuse ForwardPBR lighting.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U7-07 — Add scene/probe invalidation, no-valid-probe fallback and budgets.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U7-08 — Test indirect bounce, wall leakage, grid settings and disabled fallback.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** bounce follows world probes rather than camera pixels; walls suppress leakage; experimental status/cost explicit.

### U8 / S8.1–S8.6 — HDR post and temporal pipeline

### Exposure

- [ ] **M15-U8-01 — Build one global log-luminance histogram and reduction.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-02 — Adapt previous exposure with real delta time and per-view state.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U8-03 — Feed adapted exposure into tonemapping; define first frame/black frame/camera cut.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U8-04 — Exclude UI/bloom from metering by policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: a bounded multiscale halo appears without repeated tint/gain.
- [ ] **M15-U8-05 — Test mixed tiles, 30/60/120 Hz and exposure limits.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.

### Bloom

- [ ] **M15-U8-06 — Threshold HDR source once.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-07 — Build six downsample levels with correct dimensions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-08 — Reconstruct additively without discarding finer contributions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-09 — Apply intensity/tint once.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-10 — Test impulse halo, gain, odd/small resolution and HDR/SDR.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

### TAA

- [ ] **M15-U8-11 — Generate camera/object motion vectors with consistent jitter.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-12 — Reject disoccluded/invalid history.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-13 — Reset on resize, view switch, camera cut, settings/format/scene generation change.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-14 — Define transparent/reactive/UI policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-15 — Test static shimmer, moving object, disocclusion, resize and ghosting.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** exposure, bloom and TAA have behavior tests and no stale-history/gain artifacts.

### U9 / S9.1–S9.5 — Advanced material models

- [ ] **M15-U9-01 — Charlie sheen direct/IBL response with energy checks.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U9-02 — Beer–Lambert absorption with documented thickness/path policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U9-03 — Cauchy/wavelength dispersion within documented scope.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U9-04 — Screen-space separable Gaussian SSS with depth/normal/material rejection.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.
- [ ] **M15-U9-05 — Preserve specular while diffusing intended diffuse data.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U9-06 — Complete native asset/component/GPU/Inspector/Preview/Runtime paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen, and matches Runtime/Preview.
- [ ] **M15-U9-07 — Extend Material Graph outputs/lowering or explicitly defer unsupported fields.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U9-08 — Test sheen, absorption, SSS and built-in/graph parity.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** models are bounded, authorable, serializable and pass controlled fixtures.

### U10 / S10.1–S10.4 — Editor and authoring integration

- [ ] **M15-U10-01 — Add controls for all accepted material fields with clamps/tooltips/defaults.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U10-02 — Add probe placement, shape, capture, scheduling and stale/failed diagnostics.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U10-03 — Add Preview controls matching Scene environment/exposure/AA/reflection policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U10-04 — Complete scene/assembly/runtime codec round trips.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-05 — Add Low/Medium/High/Ultra presets with explicit budgets.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-06 — Diagnose unsupported OpenGL/capability paths with valid fallback.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-07 — Test Editor create/edit/save/reopen and Runtime parity.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen, and matches Runtime/Preview.

**Exit:** every accepted runtime feature is authorable and survives save/reload.

### U11 / S11.1–S11.4 — Optional ray-traced extension

- [ ] **M15-U11-01 — Freeze backend/capability policy and fallback.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U11-02 — Build/refit BLAS for supported geometry.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U11-03 — Build/update TLAS per scene/geometry changes.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U11-04 — Add RT pipeline and ray-generation/miss/closest-hit shaders.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U11-05 — Match ForwardPBR material/lighting evaluation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U11-06 — Add denoising and temporal rejection.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U11-07 — Share geometry data with DDGI.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: colored indirect bounce follows world probes and walls suppress leakage.
- [ ] **M15-U11-08 — Test off-screen dynamic reflection, self-intersection and fallback.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.

**Exit:** off-screen reflection works within documented limits, or P13 is explicitly deferred.

### U10 / S10.5 — Final acceptance and rollout

- [ ] **M15-U10-08 — Rebuild affected Editor/Runtime/tests Debug and Release.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen, and matches Runtime/Preview.
- [ ] **M15-U10-09 — Run D3D12/Vulkan physical-GPU fixtures with validation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-10 — Run OpenGL and unsupported-capability fallback fixtures.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-11 — Inspect windowed Editor output for smooth sphere, roughness sweep and red-object reflection.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U10-12 — Run resize/minimize/restore, view switch, device reinit and >=1,000-frame soak.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-13 — Capture deterministic parity artifacts and report thresholds.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-14 — Run 1080p/1440p Release benchmark with p50/p95/p99/max timings and memory.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-15 — Report named lighting budget on named GPU/preset.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U10-16 — Record hardware/driver/OS/backend/commit for measurements.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-17 — Complete independent source review and correction recheck.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-18 — Update every completed checkbox with commit and evidence.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U10-19 — Promote only features whose exit conditions pass.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**M15 final acceptance:** core items are accepted, optional items are accepted or explicitly deferred, and no required check is silently waived.

## 5. M15 completion rule

M15 is accepted only when all required U0-U10 atomic items are PASS, optional
U11/P6B/DDGI items are PASS or explicitly DEFERRED, and the final acceptance
items record Debug/Release builds, native backend evidence, Editor visual review,
parity artifacts, soak/performance measurements and an independent review. A
compile, non-empty image or parent heading never substitutes for an item PASS.

**Status:** HOLD — this checklist is the execution contract; no M15 implementation is released by this document.
