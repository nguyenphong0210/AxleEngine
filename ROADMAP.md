# AxleEngine Roadmap

This is the canonical execution roadmap. Work is ordered by Phase; each Phase owns exactly one `PLAN.md` and one `ARCHITECTURE.md` under `docs/phases/`.

---

## 1. Master Phase Directory

| Phase | Name | Scope & Status | Documents |
| :---: | :--- | :--- | :---: |
| **00** | **Architecture Foundation** | **Completed:** 41 Historical Phases (Milestones 1-10) + OPT-00..OPT-09 accepted.<br>**Open:** OPT-10 final release acceptance gates. | [Plan](docs/phases/PHASE_00_FOUNDATION/PLAN.md) · [Architecture](docs/phases/PHASE_00_FOUNDATION/ARCHITECTURE.md) |
| **01** | **Render & Material Foundation** | **Completed:** OPT-11 (Render Thread, VRAM Aliasing) & OPT-12 (DXC Material Pipeline).<br>**Deferred:** Asynchronous product rollout. | [Plan](docs/phases/PHASE_01_RENDER_FOUNDATION/PLAN.md) · [Architecture](docs/phases/PHASE_01_RENDER_FOUNDATION/ARCHITECTURE.md) |
| **02** | **Lighting & Material Fidelity** | **Completed:** OPT-13 Baseline (CSM, HDR/IBL, Local Shadows, Viewport Grounding).<br>**Queued:** Multi-Bounce GGX, GTAO, Froxel Fog, DDGI Probes, LTC, TAA. | [Plan](docs/phases/PHASE_02_LIGHTING_MATERIALS/PLAN.md) · [Architecture](docs/phases/PHASE_02_LIGHTING_MATERIALS/ARCHITECTURE.md) |
| **03** | **Geometry & Visibility** | **Proposed:** OPT-16 (Offline autoLOD via `meshoptimizer`) and OPT-14 (Nanite-style Virtual Geometry & Hardware Task/Mesh Shaders). | [Plan](docs/phases/PHASE_03_GEOMETRY_VISIBILITY/PLAN.md) · [Architecture](docs/phases/PHASE_03_GEOMETRY_VISIBILITY/ARCHITECTURE.md) |
| **04** | **Simulation Scale** | **Proposed:** OPT-15 (Logic & Simulation LOD, Significance Tiers, Staggered Scheduler, Spatial Dormancy Bubble, Jolt Physics Fast-Path). | [Plan](docs/phases/PHASE_04_SIMULATION_SCALE/PLAN.md) · [Architecture](docs/phases/PHASE_04_SIMULATION_SCALE/ARCHITECTURE.md) |
| **05** | **Content & Authoring Tools** | **Proposed:** OPT-17 (Animation Studio), OPT-18 (VFX Studio & GPU Particles), OPT-19 (Material Graph UI), OPT-Tools (Cooker & Pak Exporter). | [Plan](docs/phases/PHASE_05_CONTENT_TOOLING/PLAN.md) · [Architecture](docs/phases/PHASE_05_CONTENT_TOOLING/ARCHITECTURE.md) |
| **06** | **Performance & Rollout** | **Proposed:** OPT-20 (Standardized Benchmark Matrix, Multi-Backend Parity, Stress/Device Recovery, Master Production Cutover). | [Plan](docs/phases/PHASE_06_ROLLOUT/PLAN.md) · [Architecture](docs/phases/PHASE_06_ROLLOUT/ARCHITECTURE.md) |

---

## 2. Complete Traceability: What Has Been Done vs. What Remains

### Part A: What Has Been Done (Completed `[x]`)
- [x] **41 Historical Development Phases (Milestones 1 - 10)**: Core UI, Jolt Physics, Scene Graph, Assimp Asset Pipeline, JSON Scene Serializer, Assemblies/Prefabs, C++ Scripting Reflection & Hot-Reload DLL, enkiTS Multithreaded Job System, Component-Driven Architecture, Standalone `AxleRuntime.exe`, Asset Cooker & VFS, PBR Decoupled Materials, AAA Soft Shadows, HDR IBL Skybox, miniaudio 3D Spatial Audio, Modular Plugins (Spline, Terrain), Skeletal Animation Evaluation, CPU Particle System, NVIDIA NRI Modern RHI (D3D12/Vulkan), Forward+ Multi-Backend Viewport, Declarative Canvas2D UI, Unified `.axleasset` Architecture, Visual Material Studio & HLSL Dynamic Reflection.
- [x] **OPT-00**: Vehicle Retirement & Legacy Data Preservation.
- [x] **OPT-01**: Layer Architecture Validation (`scripts/check_architecture.py`) & Zero-Allowance Policy.
- [x] **OPT-02**: Threading & enkiTS Task Scheduler Standardization.
- [x] **OPT-03**: Jolt Physics Boundary Decoupling & `PhysicsDebug` Extraction.
- [x] **OPT-04**: Modern RHI Device & Epoch Lifetime Management on D3D12 and Vulkan.
- [x] **OPT-05**: Unified Rendering Pipeline (`RenderWorld` CPU Snapshot, `RenderService`, `RenderGraph`).
- [x] **OPT-06**: Modular Plugin Architecture & Dynamic SDK.
- [x] **OPT-07**: Plugin Project Descriptor (`.axleproject`) & Dynamic Discovery.
- [x] **OPT-08**: Content Management, VFS & Sidecar-Free Native Asset Identity (`# AxleFileIdentity: 1`).
- [x] **OPT-09**: Monolithic Asset Packaging Cooker (`Data.axlepak`).
- [x] **OPT-11**: Decoupled Render Thread Concurrency & Transient VRAM Memory Aliasing (C11-01..C11-07).
- [x] **OPT-12**: DXC Material Graph Compiler, Pipeline Caching & Transactional Fallbacks (C12-01..C12-06).
- [x] **OPT-13 (Baseline)**: Cascaded Shadow Mapping (CSM C13-01), HDR Split-Sum IBL (C13-02), Point/Spot Local Shadows, and Viewport Fixture Grounding.

### Part B: What Remains To Be Done (Pending `[ ]`)
- [ ] **OPT-10 (Open Gates)**: Final Release full-product acceptance, detached export packaging stress test, and physical interactive UI acceptance.
- [ ] **Phase 01 (Deferred)**: Asynchronous parallel rendering default rollout.
- [ ] **Phase 02 (Fidelity Roadmap)**:
  - [ ] Multi-Bounce GGX (Kulla-Conty) Energy Compensation.
  - [ ] Dynamic Procedural Skybox IBL Bake.
  - [ ] Sun Angular Radius Expansion ($0.53^\circ$).
  - [ ] GTAO (Ground-Truth Ambient Occlusion) half-res compute pass.
  - [ ] Screen-Space Contact Shadows.
  - [ ] Hi-Z Depth Pyramid Generation & Hi-Z Screen-Space Reflections (SSR).
  - [ ] Parallax-Corrected Local Reflection Probes.
  - [ ] Linearly Transformed Cosines (LTC) Area Lights (Rect & Disk).
  - [ ] Froxel Volumetric Fog & God Rays (C13-04: $160 \times 90 \times 64$).
  - [ ] Dynamic Diffuse Global Illumination (C13-05: DDGI Probes with Chebyshev visibility).
  - [ ] Film-Grade Post-Processing (C13-06: Kawase Bloom, Histogram Auto-Exposure, ACES).
  - [ ] Temporal Anti-Aliasing (TAA: Halton jitter, history reprojection, YCoCg clamping).
  - [ ] Complex Shading Models (Screen-Space SSS, Charlie Sheen/Velvet, Beer-Lambert Glass).
  - [ ] Final Lighting Budget Verification (C13-07: 3.5ms - 5.0ms target).
- [ ] **Phase 03 (Geometry & Visibility)**:
  - [ ] OPT-16: Offline autoLOD decimation pipeline via `meshoptimizer` (C16-01..C16-03).
  - [ ] OPT-14: GPU Instance Buffer, GPU Frustum/Cone/Hi-Z culling, Meshlets, Cluster DAG hierarchy, and Hardware Task & Mesh Shaders (C14-01..C14-07).
- [ ] **Phase 04 (Simulation Scale)**:
  - [ ] OPT-15: Monotonic tick identity, Significance distance tiers, deterministic modulo scheduler, spatial hash grid, dormancy bubble hibernation, transform interpolation, Jolt physics fast-path, and Animation URO (C15-01..C15-09).
- [ ] **Phase 05 (Content & Authoring Tools)**:
  - [ ] OPT-17: Skeletal Animation Studio (timeline editor, animator controller, blend spaces, 128-bone GPU skinning, sockets, foot IK, additive layers) (C17-01..C17-07).
  - [ ] OPT-18: Visual FX Studio (multi-emitter document, advanced curves/modules, ribbons/trails, mesh particles, collision triggers, GPU compute simulation) (C18-01..C18-08).
  - [ ] OPT-19: Material Graph UI node canvas (`imnodes`), Clearcoat/Anisotropy/Refraction nodes, Reflection probe gizmos, Decals, and Prefab variants (C19-01..C19-05).
  - [ ] OPT-Tools: Standalone Asset Cooker, Monolithic Pak builder, and Headless Asset Linter (CTools-01..CTools-03).
- [ ] **Phase 06 (Performance & Rollout)**:
  - [ ] OPT-20: Standardized benchmark matrix, cross-backend image parity (PSNR/SSIM), GPU/CPU telemetry profiling, device-loss recovery stress, and final production closure (C20-01..C20-06).

---

## 3. Execution Rules & Authority

1. **Phase Authority:** Phase is the single active ordering system. OPT and C-package identifiers remain inside plans and review reports as historical references and evidence keys.
2. **Two-Document Rule:** Each Phase owns exactly one `PLAN.md` (work breakdown and checklist) and one `ARCHITECTURE.md` (technical design and boundaries).
3. **Evidence Archiving:** Review and test evidence lives in `docs/reviews/`. Historical material lives in `docs/archive/`.
