# Milestone 15 — Lighting and Material Fidelity

**Status:** `HOLD for full M15 acceptance. The user has separately released U0, S1.1, S1.2 and S1.3; S1.3 is accepted after independent review and Editor evidence.`
**Current source reference:** `c6245e2e` for S1.3 implementation; `19ac2a1` remains the historical M14 handoff baseline. This reference does not promote later M15 work.
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
```

P5A–P5D may reuse authored-probe work, but none may silently depend on unfinished P6. Planar reflection is a required M15 raster contract; RT off-screen reflection is an M16 requirement.

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
  - **Evidence:** Verified in `Test_M15_OcclusionContractUnit.cpp`: pre-lighting occlusion contract guarantees $L_o = L_{\text{ambient}} \cdot ao + (L_d + L_s) \cdot cs + L_e$, where ambient/indirect is modulated strictly while direct diffuse, specular highlights, and emissive remain strictly invariant. Preserves U4-01/U4-02 bounded post-composite fallback passes. Native D3D12 and Vulkan tests pass. [U4 batch 1 review](../../reviews/M15_U4_BATCH_1_REVIEW.md). Editor visual evidence remains user-provided.
  - **EDITOR VISUAL:** build Debug/Release Editor, open the applicable fixture and capture the result. Expected: the declared behavior matches the frozen reference without invalid output.
- [x] **M15-U4-03.2 — Expose true indirect target if needed.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_OcclusionContractUnit.cpp`: architectural decision documented that a separate physical indirect target is NOT required for forward shading under the pre-lighting contract ($L_{\text{ambient}}$ modulated directly in registers during forward pass, saving ~16.6 MB VRAM at 1080p and eliminating MRT write bandwidth). Auxiliary target allocation via `RHIViewTargetDesc::EnableScreenStorage` is available for decoupled passes. [U4 batch 1 review](../../reviews/M15_U4_BATCH_1_REVIEW.md).
- [x] **M15-U4-04.1 — Apply contact to directional contribution.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_OcclusionContractUnit.cpp`: contact shadow visibility modulates strictly the matching directional sun contribution; local lights (point, spot), ambient, and emissive remain 100% invariant under contact shadow variations. When directional sun is absent (`HasSun=false`), contact shadows have zero effect on scene radiance. When contact shadows are disabled (`EnableContactShadows=false`), normal direct lighting is preserved. Native D3D12/Vulkan tests pass. [U4 batch 2 review](../../reviews/M15_U4_BATCH_2_REVIEW.md).
- [x] **M15-U4-04.2 — Avoid CSM/local double attenuation.**
  - **PASS when:** the behavior is implemented independently, its dedicated fixture/assertion passes, and failure/fallback is explicit.
  - **Evidence:** Verified in `Test_M15_OcclusionContractUnit.cpp`: `CombineDirectionalVisibility` uses bounded minimum formulation $\min(V_{\text{csm}}, V_{\text{contact}})$, preventing double attenuation. Fully shadowed CSM regions ($V_{\text{csm}}=0$) remain 0.0 with no redundant contact darkening; penumbra regions ($0.5, 0.5$) combine to $0.5$ avoiding $0.25$ double-dip attenuation. Local point and spot lights are isolated from directional contact shadows. [U4 batch 2 review](../../reviews/M15_U4_BATCH_2_REVIEW.md).
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
  - **EDITOR VISUAL:** build Debug/Release Editor and capture the dynamic six-face fixture. Expected: the red reference object appears at the correct reflected location; misses fall back smoothly with no black edge.
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

### U5 / S5.8 — Planar reflection

Planar reflection is the deterministic raster path for a selected reflective plane. It uses a separate capture ViewContext and target generation so the main view history, camera jitter and scheduler state are never re-entered recursively.

- [ ] **M15-U5-46.1 — Freeze planar surface contract.**
  - **PASS when:** the plane transform, normal, clip distance, source view and reflection confidence are represented explicitly and use the canonical surface units.
  - **Evidence:** record source commit, exact build/configuration, command/exit code and artifact/readback path.
- [ ] **M15-U5-46.2 — Create independent mirrored capture ViewContext.**
  - **PASS when:** the mirrored camera is derived from the plane, recursion is disabled, and capture state cannot mutate the parent view history.
  - **Evidence:** record source commit and a focused capture-context test.
- [ ] **M15-U5-46.3 — Allocate planar color/depth targets and graph edges.**
  - **PASS when:** resize, format, lifetime and completion dependencies are declared through RenderGraph/RHIViewTargets.
  - **Evidence:** record graph validation, resize and device-reset results.
- [ ] **M15-U5-46.4 — Register a dedicated PlanarReflectionPass.**
  - **PASS when:** capture, resolve and release are scheduled as explicit passes without callback recursion or hidden resource reads.
  - **Evidence:** record pass registration and scheduler execution logs.
- [ ] **M15-U5-46.5 — Resolve planar energy and fallback.**
  - **PASS when:** planar confidence is blended once with SSR, local probe and sky IBL according to the shared reflection energy policy.
  - **EDITOR VISUAL:** capture a fixed view with planar reflection enabled and disabled; no black edge or duplicate highlight is allowed.
- [ ] **M15-U5-46.6 — Validate oblique clipping, edges and self-intersection.**
  - **PASS when:** the reflected plane excludes geometry behind the mirror, clips at the declared extent and avoids self-reflection artifacts.
  - **Evidence:** record edge, grazing-angle and self-intersection fixtures.
- [ ] **M15-U5-46.7 — Validate invalidation and failure retention.**
  - **PASS when:** plane/camera/scene/material/light changes invalidate the capture and a failed update retains last-good data or an explicit fallback.
  - **Evidence:** record generation and failure-path tests.
- [ ] **M15-U5-46.8 — Complete Editor and native evidence.**
  - **PASS when:** Debug/Release Editor evidence and deterministic image/readback comparison agree with the native fixture.
  - **EDITOR VISUAL:** show the same camera with planar reflection on and off and the relevant Rendering Features policy visible.

**Exit:** a planar mirror produces stable, clipped reflection with explicit SSR/probe/IBL fallback and no recursive capture.

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

### M16 handoff — Ray Tracing Backend

M15 does not contain a ray-tracing Unit. The former U11/P13 checklist is moved to [Milestone 16 Ray Tracing Backend](../MILESTONE_16_RAY_TRACING/PLAN.md). M15 is complete for this boundary when its raster contracts are explicit: `ResolvedSurface`, `ViewContext`, capture targets, reflection confidence/energy policy, denoising history invalidation and raster fallback. M16 must consume those contracts without introducing a second material interpretation.

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
P6B/DDGI items are PASS or explicitly DEFERRED, and the final acceptance
items record Debug/Release builds, native backend evidence, Editor visual review,
parity artifacts, soak/performance measurements and an independent review. A
compile, non-empty image or parent heading never substitutes for an item PASS.

**Status:** HOLD — this checklist is the execution contract; no M15 implementation is released by this document.
