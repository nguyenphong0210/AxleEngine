# AxleEngine Post-OPT-10 Master Roadmap

Status: planning and acceptance index. This document is the single roadmap
source for the Post-OPT-10 rendering, geometry, simulation and product work.
It does not release implementation by itself. A user instruction or an explicit
task-card release is still required before source changes begin.

## How the names fit together

`Phase` is a long architectural chapter. It groups related systems and explains
the order in which they should be built.

`OPT` is a release boundary. It owns a concrete scope, dependencies, commits,
tests, review and acceptance state. An OPT can contain several `Cxx-yy`
packages.

`Cxx-yy` is the smallest normal implementation/review unit. It should normally
have one coherent local commit, focused tests and a separate evidence record.

This distinction keeps the roadmap readable without losing the package IDs used
by task cards, commits and test reports.

## Rules for every package

- A planning entry does not authorize implementation.
- Record implementation, independent review and acceptance separately.
- Rebuild the executable that the verification command runs.
- Test D3D12 and Vulkan on the native path; keep OpenGL behavior explicit as a
  compatibility or unsupported-feature policy.
- Preserve authored values on unsupported paths and report diagnostics.
- Do not claim a performance target until the fixed benchmark scene, camera,
  hardware, resolution and frame-time method are recorded.
- Use one commit per coherent package and do not stage unrelated scene/editor
  changes.

## Phase A — Rendering and material foundations

### OPT-11 — Render ownership, transient resources and rollout

Purpose: establish safe RenderService/RHI ownership, bounded frame snapshots,
transient allocation and measured rollout behavior.

Packages: C11-01 ownership, C11-02 snapshot queue, C11-03 worker/presentation,
C11-04 parallel recording, C11-05 transient VRAM aliasing, C11-06 lifetime
stress, C11-07 acceptance.

Current state: qualified foundation and synchronous product behavior accepted;
full asynchronous/parallel product rollout remains deferred. See the detailed
OPT-11 delivery and acceptance reports.

### OPT-12 — Shader and material pipeline

Purpose: versioned material graph data, compiler/layout/resource semantics and
backend policy.

Packages: C12-01 material document/IR, C12-02 DXC graph compiler, C12-03
reflection/layout/reload, C12-04 resource semantics, C12-05 bounded Slang
evaluation, C12-06 explicit OpenGL graph policy.

Current state: C12-01 through C12-06 complete within their bounded scopes;
full graph UI and automatic product graph authoring remain separate work.

## Phase B — Lighting and material fidelity

### OPT-13 — Lighting, environment and post-processing

Purpose: make authored materials and environments read correctly across the
modern native renderer while preserving OpenGL compatibility.

Accepted packages:

- C13-01 directional CSM/PCF contract and native implementation.
- C13-02 HDR environment/IBL ownership and fallback semantics.
- Released local-light extension: bounded Point/Spot shadows on D3D12/Vulkan,
  explicit OpenGL policy, Sandbox metal/rubber/glass materials.
- Sandbox viewport correction: sphere winding, grounded fixture placement and
  HDR starter environment selection.

Future packages, not released by the current OPT-13 acceptance:

- C13-03 GTAO/SSAO, contact shadows and bounded SSR.
- C13-04 froxel volumetric fog and sun shafts.
- C13-05 DDGI probes with capability fallback.
- C13-06 bloom, auto-exposure and color grading.
- C13-07 final quality/budget acceptance.

Material-fidelity order for the next release should be:

1. Multi-scatter GGX/Kulla-Conty energy compensation.
2. GTAO and short-range contact shadows.
3. Dynamic procedural-sky IBL rebuilt only when sky parameters change.
4. Reflection probes with parallax correction.
5. Hi-Z SSR after depth/normal/history contracts exist.
6. LTC area lights, then TAA and cinematic post-processing.
7. SSS, sheen and volumetric Beer-Lambert glass.

## Phase C — Scalable geometry and visibility

### OPT-14 — Meshlets, GPU culling and virtual geometry

Purpose: reduce CPU draw submission and scale dense static geometry through
capability-gated GPU visibility and meshlet rendering.

Packages: C14-01 GPU instance data, C14-02 Hi-Z/frustum/cone culling and indirect
draws, C14-03 deterministic meshlet asset schema, C14-04 optional cluster DAG,
C14-05 Task/Mesh Shader path, C14-06 indexed fallback, C14-07 scale/parity
acceptance.

Recommended order: finish C14-01 and an indexed GPU-culling fallback before
attempting hardware mesh shaders. Dense-scene scale is a measured target, not a
promise.

### OPT-16 — Offline autoLOD

Purpose: deterministic meshoptimizer-based LOD generation for skinned,
transparent, collision and unsupported-mesh fallbacks. Continuous meshlets stay
owned by OPT-14.

Packages: source/settings schema, simplifier wrapper, deterministic LOD output,
cooker/cache invalidation, runtime selection and special-mesh policy.

## Phase D — Simulation scale

### OPT-15 — Logic and simulation LOD

Purpose: reduce CPU cost for large worlds while preserving authoritative
simulation semantics.

Packages: public tick identity, significance tiers, deterministic staggered
scheduler, spatial index, dormancy/wake protocol, physics fast path, character
and animation throttling, and acceptance stress.

## Phase E — Runtime content systems

### OPT-17 — Animation completion

CPU correctness, authored skeleton/clip/controller resources, instance isolation,
native skeletal geometry/draw, picking/shadows and acceptance.

### OPT-18 — VFX completion

Deterministic effect data, GPU/CPU rendering parity, material/lighting
integration, preview isolation and runtime acceptance.

### OPT-19 — Authoring UI and tooling

Material graph UI, reflection/probe/lighting authoring, geometry diagnostics,
animation/VFX tools and persistence/reload behavior.

## Phase F — Product performance and rollout

### OPT-20 — Performance, parity and default rollout

Purpose: consolidate measured frame-time, memory, image parity, fallback,
device-loss and product-startup evidence. Only this phase may turn opt-in
experiments into defaults, and only when the benchmark contract passes.

## Current boundary

OPT-13 local shadows, HDR/IBL starter environment and the reviewed Sandbox
viewport correction are complete within the released scope. VSM, C13-03 and
later lighting packages are deferred. OPT-14 and later OPTs remain proposals
until the user releases a concrete package.

Detailed execution plans and delivery reports remain evidence documents; they do
not create a second roadmap or advance a package automatically.
