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
they do not carry a second independent checkbox state. A parent group checkbox is
allowed only as a derived summary: it remains unchecked until every child leaf is
PASS, and it carries no evidence separate from its children. Close each leaf
immediately after its evidence passes, then update the derived parent summary.

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

- [ ] **M15-U1 / S1.1 — Freeze direct-light reference contracts.** Derived status: PASS only when all four children pass independent review.
  - [x] **M15-U1-01.1 — Freeze direct-light units.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Authored intensity is a finite dimensionless multiplier clamped to `[0, 100]`: directional `1 = 100,000 lx`, point `1 = 1,000` and spot `1 = 500` calibrated local scene units. Invalid/negative inputs produce zero. Regression covers both sides of the former 100/10 discontinuities and Debug/Release `RenderUnitTests` pass. Repair commit: `4af042b2`; review finding S1.1-R2 resolved.
  - [x] **M15-U1-01.2 — Freeze sun angular-radius policy.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Default `0.00462512` rad and clamp `[0, 0.25]` are present in the render-world/CPU/shader path; Debug/Release `RenderUnitTests` pass. This accepts the default and clamp contract, not authored-component controls or a GPU image comparison. Source commit: `7ff495a`.
  - [x] **M15-U1-01.3 — Freeze BRDF normalization.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **Evidence:** Diffuse `1/PI` and specular denominator factor `4` are used by sun, point and spot shader paths; the numeric CPU reference and Debug/Release `RenderUnitTests` pass. GPU image/energy validation remains in later U1 leaves. Source commit: `35905c6`.
  - [ ] **M15-U1-01.4 — Freeze exposure interaction.**
    - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
    - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
    - **Evidence:** The GPU frame histogram now has one writer and tone mapping samples its 1x1 adapted exposure. Native readback at fixed authored exposure confirms bright scenes darken and dark scenes lift on D3D12 and Vulkan in Debug and Release. Debug/Release `AxleEditor` builds pass. Repair commit: `c5b9f82f`; review finding S1.1-R1 is resolved in source. Editor visual evidence remains required before this leaf and S1.1 can be checked.
- [ ] **M15-U1-02.1 — Validate GGX visibility/normal-distribution compensation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U1-02.2 — Validate dielectric/metal F0 reference.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U1-03.1 — Validate directional-light units/shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U1-03.2 — Validate point-light units/shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U1-03.3 — Validate spot-light units/shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U1-04.1 — Run roughness/view/light-angle sweep.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U1-04.2 — Run zero/behind-surface finite cases.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U1-05 — Record direct-light fallback behavior for unsupported capability tiers.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

**Exit:** direct-light fixtures match the frozen numeric reference within documented tolerance; units, shadows, HDR response and fallback diagnostics are consistent across supported native backends.

### U2 / S2.1–S2.3 — Dynamic environment and authored environment policy

- [ ] **M15-U2-01.1 — Freeze procedural-sky convention.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-01.2 — Freeze environment-bake convention.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-01.3 — Freeze authored-cubemap color/orientation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-02.1 — Publish environment generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-02.2 — Define cache identity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-02.3 — Define invalidation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-02.4 — Define last-good fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-03.1 — Verify sky/direct-light coupling.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-03.2 — Verify environment rotation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-03.3 — Verify environment intensity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-03.4 — Verify missing/failed asset behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-04 — Keep local dynamic six-face capture and probe blending in U5; this Unit owns the shared environment input contract.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U2-05.1 — Run D3D12 environment readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-05.2 — Run Vulkan environment readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U2-05.3 — Run OpenGL-subset environment readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** procedural and authored environments produce the declared HDR/IBL inputs, cache and invalidation are transactional, and failed or unsupported environments select a named fallback without mutating authored data.

### U3 / S3.1 — RHI resource ownership, graph and measurement

- [x] **M15-U3-01.1 — Validate timestamp probe description.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U3-01.2 — Require successful timestamp readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [x] **M15-U3-02 — Read renderer timestamps on all public view submission paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-03.1 — Check descriptor update helpers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-03.2 — Check storage update helpers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-03.3 — Check constant update helpers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-04 — Never modify a descriptor set after binding in the same submission.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-05.1 — Declare Hi-Z reads/writes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-05.2 — Declare normal/probe reads.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-05.3 — Declare history/exposure reads.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U3-06 — Add per-view target generation, completion and reset ownership.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-07.1 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-07.2 — Test feature toggle.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-07.3 — Test allocation failure/retry.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-07.4 — Test device epoch.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-07.5 — Test shutdown/reinit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-08 — Add asynchronous per-pass query/readback ring and invalid-measurement state.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-09.1 — Create pass headers under Passes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-09.2 — Create pass implementations under Passes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-10.1 — Extract GTAOPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-10.2 — Extract HiZGenerationPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-10.3 — Extract ScreenSpaceReflectionPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U3-10.4 — Extract FroxelFogPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: fog shafts obey blockers; zero density/no-light leaves the image unchanged.
- [ ] **M15-U3-10.5 — Extract PostProcessPass.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-11.1 — Forward orchestration through RenderPassScheduler.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-11.2 — Forward view state through ViewContext.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

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
- [ ] **M15-U3-15.1 — Verify inverse-transpose under nonuniform scale.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-15.2 — Verify mirrored-transform handedness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-15.3 — Verify normal-map transform.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-16 — Define alpha-cutout/depth/normal coverage and background validity.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-17.1 — Unify base color.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-17.2 — Unify metallic/roughness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U3-17.3 — Unify AO/emission.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-17.4 — Unify normal/tangent.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-17.5 — Unify clearcoat/anisotropy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-17.6 — Unify reflection inputs.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U3-18 — Verify sRGB/linear texture and packed ORM conventions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-19.1 — Round-trip asset to component.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-19.2 — Round-trip component to extraction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-19.3 — Round-trip extraction to GPU.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-19.4 — Round-trip save/reopen.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-20 — Add smooth-sphere normal-field and anti-faceting metric.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere has smooth shading with no triangle bands, faceting or invalid pixels.

**Exit:** smooth sphere has no triangle bands; built-in/graph materials match in supported scope; save/reopen preserves fields.

#### S3.3 — Material Graph, preview and runtime parity

- [ ] **M15-U3-21.1 — Unify built-in resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-21.2 — Unify Material Graph resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U3-21.3 — Unify Preview resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U3-21.4 — Unify Runtime resolver.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-22 — Record explicit unsupported feature masks and fallback diagnostics for graph/custom paths.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U3-23.1 — Compare lights.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-23.2 — Compare shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U3-23.3 — Compare IBL.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-23.4 — Compare output policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

#### S3.4 — Temporal, diagnostics and measurement integration

- [ ] **M15-U3-24.1 — Declare motion resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-24.2 — Declare depth resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-24.3 — Declare history resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-24.4 — Declare exposure resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U3-25.1 — Reset on target generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-25.2 — Reset on camera cut.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-25.3 — Reset on feature toggle.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-25.4 — Reset on device epoch.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-26.1 — Record timing validity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-26.2 — Record fallback reasons.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-26.3 — Record resource generations.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U3-26.4 — Report Unknown invalid measurements.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

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
- [ ] **M15-U4-03.1 — Move AO before lighting.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U4-03.2 — Expose true indirect target if needed.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-04.1 — Apply contact to directional contribution.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-04.2 — Avoid CSM/local double attenuation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-05.1 — Validate half-resolution sampling.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-05.2 — Validate bilateral reconstruction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-05.3 — Validate depth edges.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-05.4 — Validate odd resolutions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-06.1 — Test no-sun.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-06.2 — Test no-AO.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-06.3 — Test no-contact.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-06.4 — Test emissive.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-06.5 — Test mirror/unoccluded sphere.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
- [ ] **M15-U4-07.1 — Test normal-map behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U4-07.2 — Test animated/deformed mesh.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** occlusion is stable and finite; unoccluded specular/emission is preserved; no new faceting/halos.

### U5 / S5.1 — Hi-Z depth pyramid

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
- [ ] **M15-U5-05.1 — Validate min reduction reference.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-05.2 — Validate 1x1.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-05.3 — Validate 3x5.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-05.4 — Validate 1919x1079.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-06.1 — Freeze depth convention.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-06.2 — Define edge policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-06.3 — Define useful level count.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-07.1 — Add per-level visualization.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U5-07.2 — Validate barriers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** every level matches reference reduction and transitions correctly into SSR.

### U5 / S5.2–S5.5 — Reflection Captures and local probes

#### S5.2 — Dynamic six-face capture

- [ ] **M15-U5-08.1 — Allocate capture color targets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-08.2 — Allocate capture depth targets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-09.1 — Render six 90-degree views.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-09.2 — Validate directions/up vectors.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-10.1 — Disable recursive SSR/probe sampling.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-10.2 — Disable TAA history.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: motion reduces shimmer without persistent ghosts or UI jitter.
- [ ] **M15-U5-10.3 — Disable Editor/UI.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U5-10.4 — Disable capture feedback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-11.1 — Render opaque/cutout scene.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-11.2 — Render selected shadows.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U5-12.1 — Copy faces with correct layer offsets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-12.2 — Validate copy transitions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-13.1 — Retain capture resources through completion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-13.2 — Publish capture generation transactionally.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-14 — Prove the red reference object appears in the captured cubemap.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-15 — Retain last-good generation when a face/copy fails.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

#### S5.3 — Cubemap prefilter

- [ ] **M15-U5-16.1 — Allocate mip-capable cubemap.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-16.2 — Allocate destination UAV/SRV views.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-17.1 — Implement roughness prefilter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U5-17.2 — Preserve face orientation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-17.3 — Handle seams.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-18.1 — Use sharp mirror mip.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-18.2 — Use blurred rough-material mips.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U5-19.1 — Validate controlled environment.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-19.2 — Validate roughness sweep.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U5-20 — Do not route cubemap data through equirectangular assumptions.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.

#### S5.4 — Invalidation and scheduling

- [ ] **M15-U5-21.1 — Track probe transform.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-21.2 — Track scene revision.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-21.3 — Track geometry/material.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U5-21.4 — Track light/shadow.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U5-21.5 — Track asset generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-22 — Support manual and automatic capture.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-23.1 — Bound faces/probes per frame.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-23.2 — Expose pending diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-23.3 — Expose stale diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-23.4 — Expose failed diagnostics.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-24 — Retain last-good generation on rebuild failure.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-25 — Add capture cache key, memory budget and eviction.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-26.1 — Test repeated capture.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-26.2 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-26.3 — Test device reset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-26.4 — Test scene replacement.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

#### S5.5 — Multi-probe selection and blend

- [ ] **M15-U5-27.1 — Compute box influence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-27.2 — Compute sphere influence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
- [ ] **M15-U5-28 — Blend multiple valid probes without hard seams.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-29 — Apply parallax correction per probe before blending.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the numbered/red object appears at the expected reflected location; misses fall back smoothly with no black edge.
- [ ] **M15-U5-30.1 — Blend probe with SSR confidence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-30.2 — Blend probe with sky fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-30.3 — Enforce one energy policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-31.1 — Test overlap.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-31.2 — Test boundary movement.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-31.3 — Test missing probe.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-31.4 — Test stale probe.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-31.5 — Test SSR miss.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.

**P5 exit:** with SSR disabled, a captured cubemap contains the red object and the sphere samples it; roughness changes sharpness; invalidation/scheduling/blending/fallback pass.

### U5 / S5.6 — Material-aware SSR and reflection resolve

- [ ] **M15-U5-32.1 — Freeze ray/depth projection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-32.2 — Build initial ray and starting mip.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-32.3 — Advance current Hi-Z cell.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-32.4 — Descend/refine possible hit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-32.5 — Handle bounds/max steps/termination.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-32.6 — Produce hit distance/miss/confidence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-32.7 — Run CPU/native traversal readback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-33.1 — Add thickness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-33.2 — Add hit refinement.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-33.3 — Add edge/disocclusion rejection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-34.1 — Filter by roughness cone.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U5-34.2 — Filter over HDR color footprint.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-35.1 — Pass roughness.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U5-35.2 — Pass metallic/base color.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-35.3 — Pass reflection flags.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-35.4 — Validate declared resolve bindings.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-36.1 — Compute dielectric Fresnel/F0.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-36.2 — Compute conductor Fresnel/F0.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-37.1 — Replace/blend indirect specular.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-37.2 — Prevent duplicate IBL reflection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-38.1 — Blend SSR confidence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U5-38.2 — Blend local probe influence.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-38.3 — Blend sky fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-39.1 — Test hit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-39.2 — Test miss.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-39.3 — Test moving object.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-39.4 — Test roughness sweep.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U5-39.5 — Test gold/chrome/dielectric.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-39.6 — Test camera motion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** colored geometry appears in the expected reflection; roughness varies continuously; no black edge, ghost or energy spike.

### U5 / S5.7 — LTC rectangular and disk area lights

- [ ] **M15-U5-40 — Add validated LTC transform/amplitude tables or reproducible generation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U5-41.1 — Implement rectangle emitter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-41.2 — Implement disk emitter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-41.3 — Validate units/orientation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-41.4 — Validate sidedness/range.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-42.1 — Integrate diffuse area response.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-42.2 — Integrate specular area response.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-43 — Define area-light shadow policy separately.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U5-44.1 — Complete component path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-44.2 — Complete extraction path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-44.3 — Complete serialization/runtime path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-44.4 — Complete Inspector path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U5-45.1 — Compare roughness/view angles.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U5-45.2 — Compare emitter angles.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U5-45.3 — Compare numerical reference.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** shape/roughness-correct, finite highlights without duplicate emitter energy.

### U6 / S6.1–S6.3 — Volumetric fog and atmosphere

- [ ] **M15-U6-01.1 — Allocate froxel resources.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-01.2 — Scale froxel quality tiers.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-02.1 — Inject density/media.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-02.2 — Inject light/shadow visibility.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U6-03.1 — Integrate scattering/extinction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-03.2 — Validate physical units.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-04.1 — Add temporal reprojection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-04.2 — Add temporal rejection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-04.3 — Add invalidation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-05.1 — Define transparent/depth interaction.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-05.2 — Define no-light behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-05.3 — Define zero-density behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-06 — Remove invented sun fallback and emit diagnostics.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07.1 — Test wall occlusion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07.2 — Test shafts.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07.3 — Test camera motion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07.4 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07.5 — Measure memory.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U6-07.6 — Measure cost.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** fog respects blockers/lights, zero density preserves the image, trails are bounded, cost is reported.

### U7 / S7.1–S7.4 — Experimental DDGI

- [ ] **M15-U7-01.1 — Choose radiance acquisition.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-01.2 — Document acquisition cost/policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-02 — Implement probe indexing/origin/spacing and bounded allocation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U7-03.1 — Pack irradiance atlas.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-03.2 — Pack distance atlas.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-03.3 — Pack variance moments.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-04.1 — Implement visibility moments.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-04.2 — Implement Chebyshev weighting.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-04.3 — Validate bias.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-05.1 — Add temporal accumulation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-05.2 — Add staggered updates.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-06 — Feed irradiance into diffuse ForwardPBR lighting.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U7-07.1 — Add scene/probe invalidation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-07.2 — Add no-valid-probe fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-07.3 — Add memory/update budgets.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-08.1 — Test indirect bounce.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-08.2 — Test wall leakage.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-08.3 — Test grid settings.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U7-08.4 — Test disabled fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** bounce follows world probes rather than camera pixels; walls suppress leakage; experimental status/cost explicit.

### U8 / S8.1–S8.6 — HDR post and temporal pipeline

### Exposure

- [ ] **M15-U8-01.1 — Build log-luminance histogram.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-01.2 — Reduce histogram globally.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-02.1 — Adapt previous exposure.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U8-02.2 — Use real delta time.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-02.3 — Keep exposure per-view.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U8-03.1 — Feed exposure to tonemapping.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U8-03.2 — Define first-frame behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-03.3 — Define black-frame behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-03.4 — Define camera-cut behavior.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-04 — Exclude UI/bloom from metering by policy.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: a bounded multiscale halo appears without repeated tint/gain.
- [ ] **M15-U8-05.1 — Test mixed tiles.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-05.2 — Test 30 Hz.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-05.3 — Test 60 Hz.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-05.4 — Test 120 Hz.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

### Bloom

- [ ] **M15-U8-06 — Threshold HDR source once.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-07.1 — Build six levels.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-07.2 — Validate level dimensions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-08.1 — Reconstruct additively.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-08.2 — Preserve finer contributions.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-09 — Apply intensity/tint once.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U8-10.1 — Test impulse halo.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-10.2 — Test gain.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-10.3 — Test odd/small resolution.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-10.4 — Test HDR/SDR.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

### TAA

- [ ] **M15-U8-11.1 — Generate camera motion vectors.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-11.2 — Generate object motion vectors.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-11.3 — Apply consistent jitter.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-12.1 — Reject disoccluded history.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-12.2 — Reject invalid history.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-13.1 — Reset on resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-13.2 — Reset on view switch.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-13.3 — Reset on camera cut.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-13.4 — Reset on settings/format.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-13.5 — Reset on scene generation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-14.1 — Define transparent policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-14.2 — Define reactive policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-14.3 — Define unjittered UI policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-15.1 — Test static shimmer.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-15.2 — Test moving object.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-15.3 — Test disocclusion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-15.4 — Test resize.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U8-15.5 — Test ghosting.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

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
- [ ] **M15-U9-04.1 — Implement separable diffusion.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-04.2 — Reject by depth.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-04.3 — Reject by normal/material boundary.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
- [ ] **M15-U9-05 — Preserve specular while diffusing intended diffuse data.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **Evidence:** record source commit, configuration, exact command, exit code and artifact/readback path; unavailable hardware or fixtures stay NOT RUN/BLOCKED.
- [ ] **M15-U9-06.1 — Complete native asset path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-06.2 — Complete component path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-06.3 — Complete GPU path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-06.4 — Complete Inspector path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U9-06.5 — Complete Preview path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U9-06.6 — Complete Runtime path.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-07.1 — Extend graph outputs/lowering.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-07.2 — Declare unsupported fields explicitly.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-08.1 — Test sheen.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-08.2 — Test absorption.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-08.3 — Test SSS.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U9-08.4 — Test built-in/graph parity.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** models are bounded, authorable, serializable and pass controlled fixtures.

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

### U11 / S11.1–S11.4 — Optional ray-traced extension

- [ ] **M15-U11-01.1 — Freeze backend policy.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-01.2 — Freeze capability fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-02.1 — Build BLAS.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-02.2 — Refit BLAS.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-03.1 — Build TLAS.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-03.2 — Update TLAS on scene/geometry change.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-04.1 — Add RT pipeline.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-04.2 — Add ray-generation/miss/closest-hit shaders.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-05 — Match ForwardPBR material/lighting evaluation.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected material/light response is visible with no abrupt energy jump or invalid pixels.
- [ ] **M15-U11-06.1 — Add denoising.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-06.2 — Add temporal rejection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-07 — Share geometry data with DDGI.**
  - **PASS when:** the requirement is implemented exactly as written, its dedicated source/runtime fixture passes, and failure/fallback behavior is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: colored indirect bounce follows world probes and walls suppress leakage.
- [ ] **M15-U11-08.1 — Test off-screen reflection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U11-08.2 — Test self-intersection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U11-08.3 — Test fallback.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**Exit:** off-screen reflection works within documented limits, or P13 is explicitly deferred.

### U10 / S10.5 — Final acceptance and rollout

- [ ] **M15-U10-08.1 — Rebuild Editor.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the edited value is visible, persists after reopen and matches Runtime/Preview.
- [ ] **M15-U10-08.2 — Rebuild Runtime.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-08.3 — Rebuild tests.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-09.1 — Run D3D12 fixtures.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-09.2 — Run Vulkan fixtures.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-10.1 — Run OpenGL fixtures.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-10.2 — Run unsupported-capability fixtures.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-11.1 — Inspect smooth sphere.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the sphere is smooth with no triangle bands or invalid pixels.
- [ ] **M15-U10-11.2 — Inspect roughness sweep.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: roughness broadens highlights/reflections continuously.
- [ ] **M15-U10-11.3 — Inspect red-object reflection.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U10-12.1 — Run resize/minimize/restore.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-12.2 — Run view switch/device reinit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-12.3 — Run 1000-frame soak.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-13.1 — Capture parity artifacts.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the expected reflected object/location is visible and fallback is smooth with no black edge.
- [ ] **M15-U10-13.2 — Report thresholds.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-14.1 — Measure p50/p95/p99/max timings.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-14.2 — Measure memory.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-14.3 — Record 1080p/1440p workload.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-15.1 — Report named lighting passes.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [ ] **M15-U10-15.2 — Report named GPU/preset.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-15.3 — Separate total frame time.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-16.1 — Record GPU/driver/OS.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-16.2 — Record backend/commit.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-17.1 — Run independent source review.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-17.2 — Run correction recheck.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-18.1 — Attach commit to each completed item.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-18.2 — Attach evidence to each completed item.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-19.1 — Promote PASS features.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.
- [ ] **M15-U10-19.2 — Keep failed/deferred features out of defaults.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path; unavailable prerequisites stay NOT RUN/BLOCKED.

**M15 final acceptance:** core items are accepted, optional items are accepted or explicitly deferred, and no required check is silently waived.

## 5. M15 completion rule

M15 is accepted only when all required U0-U10 atomic items are PASS, optional
U11/P6B/DDGI items are PASS or explicitly DEFERRED, and the final acceptance
items record Debug/Release builds, native backend evidence, Editor visual review,
parity artifacts, soak/performance measurements and an independent review. A
compile, non-empty image or parent heading never substitutes for an item PASS.

**Status:** HOLD — this checklist is the execution contract; no M15 implementation is released by this document.
