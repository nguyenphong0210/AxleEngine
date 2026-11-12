# AxleEngine Master Development Roadmap

This is the canonical master execution roadmap for AxleEngine.
The entire engine lifecycle is governed by a unified **Milestone** architecture spanning **Milestones 01 through 18**. The former `OPT` and `Phase` constructs have been unified: each Milestone manages its sequential **Phases**. Each active technical Milestone (Milestones 12–18) owns exactly one `PLAN.md` (work breakdown & checklist) and one `ARCHITECTURE.md` (technical architecture & specifications) located under [`docs/milestones/`](docs/milestones/README.md).

---

## 1. Master Milestones Directory

| Milestone | Milestone Name | Scope & Status | Technical Documents |
| :---: | :--- | :--- | :---: |
| **01** | **Core Foundations & Physics Integration** | **Completed:** Physics engine setup, UI telemetry overlay, scene transforms, vehicle mechanics (Phases 1–4). | Historical Milestone |
| **02** | **Simulation Lifecycle & Viewport Tooling** | **Completed:** Play/Pause/Stop simulation loop, 3D viewport navigation, transform gizmos, obstacle playground (Phases 5–7). | Historical Milestone |
| **03** | **Asset Pipeline, Serialization & Scripting** | **Completed:** Assimp asset ingest, JSON serialization, PBR materials, `.axle` assemblies, C++ scripting (Phases 8–13). | Historical Milestone |
| **04** | **Scene Graph, Hot-Reloading & Multithreading** | **Completed:** EnTT ECS hierarchy, hierarchy tree UI, reflection inspector, script hot-reload DLL, enkiTS jobs (Phases 14–19). | Historical Milestone |
| **05** | **Component Architecture & Standalone Runtime** | **Completed:** Component vehicle architecture, decoupled renderer, standalone `AxleRuntime.exe`, VFS cooker (Phases 20–23). | Historical Milestone |
| **06** | **High-Fidelity Rendering, PBR & Audio** | **Completed:** Material flyweight, soft shadows, HDR skybox IBL, miniaudio 3D spatial acoustics (Phases 24–28). | Historical Milestone |
| **07** | **Terrain, Spline Tracks & Skeletal Systems** | **Completed:** Spline track & terrain modular plugins, skeletal animation CPU evaluation (Phases 29–30). | Historical Milestone |
| **08** | **Visual Effects, Modern RHI & Cross-Platform** | **Completed:** CPU particle system, NVIDIA NRI modern RHI (D3D12/Vulkan), Forward+ viewport, Canvas2D UI (Phases 31–34). | Historical Milestone |
| **09** | **Production Hardening & Unified Asset Architecture** | **Completed:** Vulkan 1.3 backend parity, unified `.axleasset` format, raw media ingestion pipeline (Phases 35–37). | Historical Milestone |
| **10** | **Complete Visual Tooling Ecosystem Foundations** | **Completed:** Visual material studio, embedded 3D material preview, dynamic HLSL shader reflection (Phases 38–40). | Historical Milestone |
| **11** | **Modern RHI-Primary Rendering Foundation** | **Completed:** RHI-primary rendering architecture, GPU safety, device lifetime management (Phase 41). | Historical Milestone |
| **12** | **Architecture Foundation & Boundary Hardening** | **Completed:** 0-allowance layer enforcement, enkiTS multithreading, Jolt physics decoupling, NRI device epochs, `RenderWorld` snapshot, Plugin SDK, VFS identity, pak cooker.<br>**Open Gates:** 3 final release acceptance gates (Phase 42.10). | [Plan](docs/milestones/MILESTONE_12_ARCHITECTURE_FOUNDATION/PLAN.md) · [Architecture](docs/milestones/MILESTONE_12_ARCHITECTURE_FOUNDATION/ARCHITECTURE.md) |
| **13** | **Render Concurrency & Shader Compilation Pipeline** | **Completed:** Decoupled Render Thread, transient VRAM memory aliasing, DXC Material Graph compiler & pipeline caching (Phases 43.1–43.2).<br>**Deferred:** Asynchronous parallel command recording rollout (Phase 43.3). | [Plan](docs/milestones/MILESTONE_13_RENDER_CONCURRENCY/PLAN.md) · [Architecture](docs/milestones/MILESTONE_13_RENDER_CONCURRENCY/ARCHITECTURE.md) |
| **14** | **Lighting & Material Fidelity** | **Completed:** CSM 4-cascade, HDR/IBL, Point/Spot local shadow atlas, grounded fixtures (Phases 44.1–44.3).<br>**Queued:** Kulla-Conty GGX, GTAO, Froxel Volumetric Fog, DDGI Probes, LTC Area Lights, TAA, SSS (Phases 44.4–44.17). | [Plan](docs/milestones/MILESTONE_14_LIGHTING_MATERIALS/PLAN.md) · [Architecture](docs/milestones/MILESTONE_14_LIGHTING_MATERIALS/ARCHITECTURE.md) |
| **15** | **Scalable Geometry & Virtual Geometry** | **Proposed:** Offline algorithmic autoLOD pipeline via `meshoptimizer` (Phase 45.1) and Nanite-style continuous virtual geometry with Task/Mesh Shaders (Phase 45.2). | [Plan](docs/milestones/MILESTONE_15_GEOMETRY_VISIBILITY/PLAN.md) · [Architecture](docs/milestones/MILESTONE_15_GEOMETRY_VISIBILITY/ARCHITECTURE.md) |
| **16** | **Simulation Scale & Logic LOD** | **Proposed:** Significance evaluation tiers, deterministic staggered modulo scheduler, spatial hash grid, 0% CPU spatial dormancy bubble, kinematic physics bypass (Phases 46.1–46.4). | [Plan](docs/milestones/MILESTONE_16_SIMULATION_SCALE/PLAN.md) · [Architecture](docs/milestones/MILESTONE_16_SIMULATION_SCALE/ARCHITECTURE.md) |
| **17** | **Content Authoring Studios & Tooling** | **Proposed:** Skeletal Animation Studio (Phase 47.1), Visual FX Studio & GPU Particles (Phase 47.2), Material Graph UI Canvas (Phase 47.3), Packaging Cooker & Asset Linter (Phase 47.4). | [Plan](docs/milestones/MILESTONE_17_CONTENT_TOOLING/PLAN.md) · [Architecture](docs/milestones/MILESTONE_17_CONTENT_TOOLING/ARCHITECTURE.md) |
| **18** | **Performance Profiling, Parity & Master Rollout** | **Proposed:** Standardized benchmark matrix, automated multi-backend image parity (PSNR/SSIM), GPU timestamp telemetry, simulated device-loss recovery, final production scorecard (Phases 48.1–48.6). | [Plan](docs/milestones/MILESTONE_18_PERFORMANCE_ROLLOUT/PLAN.md) · [Architecture](docs/milestones/MILESTONE_18_PERFORMANCE_ROLLOUT/ARCHITECTURE.md) |

---

## 2. Complete Exhaustive Inventory: Completed vs. Pending Work

### Part A: Completed Work (`[x]`)

#### Historical Foundations (Milestones 01 through 11)
- [x] **Milestone 01: Core Foundations & Physics Integration** (Phases 1–4): ImGui debug overlays, Jolt physics initial world setup, transform hierarchies, and vehicle mechanics.
- [x] **Milestone 02: Simulation Lifecycle & Viewport Tooling** (Phases 5–7): Play/Pause/Stop simulation states, docking workspace, ImGuizmo gizmos, and vehicle test playground.
- [x] **Milestone 03: Asset Pipeline, Serialization & Scripting** (Phases 8–13): Native C++ game scripts, Assimp mesh loading, JSON serializer, Cook-Torrance PBR, `.axle` prefabs, and mechanical assembly bindings.
- [x] **Milestone 04: Scene Graph, Hot-Reloading & Multithreading** (Phases 14–19): EnTT ECS parent-child hierarchy, scene tree UI, property reflection, content browser, live-coding C++ hot-reload DLLs, and enkiTS job scheduler.
- [x] **Milestone 05: Component Architecture & Standalone Runtime** (Phases 20–23): Modular vehicle components, decoupled scene renderer, standalone lightweight `AxleRuntime.exe`, and `.axlepak` VFS cooker.
- [x] **Milestone 06: High-Fidelity Rendering, PBR & Audio** (Phases 24–28): Flyweight material descriptors, soft shadows (PCSS), HDR skybox cubemap IBL, miniaudio 3D spatial acoustics, and vehicle cockpit HUD.
- [x] **Milestone 07: Terrain, Spline Tracks & Skeletal Systems** (Phases 29–30): Spline track & terrain modular plugins, basic skeletal bone hierarchy evaluation.
- [x] **Milestone 08: Visual Effects, Modern RHI & Cross-Platform** (Phases 31–34): CPU particle system, NVIDIA NRI modern RHI (D3D12/Vulkan), Forward+ viewport, and declarative Canvas2D UI.
- [x] **Milestone 09: Production Hardening & Unified Asset Architecture** (Phases 35–37): High-performance Vulkan 1.3 backend parity, unified `.axleasset` format, and raw media asset ingestion.
- [x] **Milestone 10: Complete Visual Tooling Ecosystem Foundations** (Phases 38–40): Visual material studio, embedded 3D material preview, and dynamic HLSL shader reflection.
- [x] **Milestone 11: Modern RHI-Primary Rendering Foundation** (Phase 41): RHI-primary rendering architecture, GPU safety, device epochs, and lifetime management.

#### Modern Architectural Stabilization (Milestones 12 through 14)
- [x] **Milestone 12: Architecture Foundation & Boundary Hardening** (Phase 42, formerly OPT-00..OPT-10):
  - [x] **Phase 42.0**: Decoupled legacy vehicle code into an optional plugin; verified legacy data preservation.
  - [x] **Phase 42.1**: Automated architectural boundary checker (`scripts/check_architecture.py`) enforcing zero baseline allowances across 576+ source files and 25 CMake targets.
  - [x] **Phase 42.2**: Threading & enkiTS task scheduler standardization (`JobSystem`) with verified fiber/worker safety.
  - [x] **Phase 42.3**: Jolt Physics boundary decoupling and `Axle::PhysicsDebug` interface extraction.
  - [x] **Phase 42.4**: Modern RHI device and GPU epoch lifetime management on Direct3D 12 and Vulkan.
  - [x] **Phase 42.5**: Unified rendering pipeline: immutable `RenderWorld` CPU snapshot, `RenderService`, and sequential `RenderGraph`.
  - [x] **Phase 42.6**: Modular Plugin SDK with clean C-ABI boundary and dynamic DLL loading.
  - [x] **Phase 42.7**: Plugin project descriptor (`.axleproject`) and dynamic runtime discovery.
  - [x] **Phase 42.8**: Asset management VFS with embedded sidecar-free identity (`# AxleFileIdentity: 1 <UUID>`).
  - [x] **Phase 42.9**: Standalone asset packaging cooker generating monolithic `Data.axlepak` containers.
- [x] **Milestone 13: Render Concurrency & Shader Pipeline** (Phase 43, formerly OPT-11 and OPT-12):
  - [x] **Phase 43.1**: Decoupled Render Thread concurrency (1-frame latency pipeline) and transient VRAM memory aliasing (`ID3D12Heap` placed resources and Vulkan suballocation, reducing transient VRAM by 40%–60%).
  - [x] **Phase 43.2**: Versioned Material Graph IR (`.axleasset`), DXC compiler backend generating native DXIL and SPIR-V, pipeline layout caching, and transactional last-known-good reload fallback.
- [x] **Milestone 14: Lighting & Material Fidelity Baseline** (Phase 44 Baseline, formerly OPT-13 Baseline):
  - [x] **Phase 44.1**: Cascaded Shadow Mapping (CSM 4-cascade, texel stabilization, PCF 5x5 across D3D12 and Vulkan).
  - [x] **Phase 44.2**: HDR Environment loading, irradiance convolution, prefilter roughness mip chain, and 2D split-sum BRDF LUT integration.
  - [x] **Phase 44.3**: Local Light Shadow Atlas (Point/Spot lights) and Viewport Grounded Fixtures (corrected Sphere CCW winding order, grounded floor $Y=0.0$, and connected `Studio_Softbox.hdr`).

---

### Part B: Pending Work (`[ ]`)

#### Milestone 12: Architecture Foundation (Final Release Gates)
- [ ] **Phase 42.10 (Open Release Gates)**:
  - [ ] Full Release standalone product build verification (`AxleEditor.exe` and `AxleRuntime.exe`).
  - [ ] Detached export packaging soak stress test.
  - [ ] Physical interactive UI native verification on OS windows.

#### Milestone 13: Render Concurrency (Extended Rollout)
- [ ] **Phase 43.3**: Engine-wide asynchronous parallel command recording rollout as the default execution pipeline.
- [ ] **Phase 43.4**: Dedicated GLSL 4.50 lowering backend for Material Graph on OpenGL compatibility path.

#### Milestone 14: Lighting & Material Fidelity (Cinematic Photorealism Roadmap)
- [ ] **Phase 44.4**: Multi-Bounce GGX Energy Compensation (Kulla-Conty 2D LUT / analytical term) restoring luster to rough metals.
- [ ] **Phase 44.5**: Real-time Dynamic Procedural Skybox IBL Bake generating real-time irradiance and prefilter mips from atmospheric skies.
- [ ] **Phase 44.6**: Sun Angular Radius Specular Expansion ($0.53^\circ$) softening directional sun specular reflection lobes.
- [ ] **Phase 44.7**: GTAO (Ground-Truth Ambient Occlusion) half-resolution compute pass ($960 \times 540$) with bilateral spatial reconstruction denoiser.
- [ ] **Phase 44.8**: Screen-Space Contact Shadows raymarching micro-shadows along sun light vectors to eliminate Peter Panning.
- [ ] **Phase 44.9**: Hi-Z Depth Pyramid Generation and Hi-Z Screen-Space Reflections (SSR) with roughness cone-tracing.
- [ ] **Phase 44.10**: Parallax-Corrected Local Reflection Probes with smooth falloff and seamless SSR fallback.
- [ ] **Phase 44.11**: Linearly Transformed Cosines (LTC) Area Lights for rectangular and disk softbox lights.
- [ ] **Phase 44.12**: Froxel Volumetric Fog & God Rays ($160 \times 90 \times 64$) with in-scattering, Mie phase function, and temporal reprojection.
- [ ] **Phase 44.13**: Dynamic Diffuse Global Illumination Probes (DDGI) with $32 \times 16 \times 32$ probe grid, dual octahedral atlases, and Chebyshev visibility.
- [ ] **Phase 44.14**: Film-Grade Post-Processing (Kawase 6-tier Pyramid Bloom, compute log-luminance histogram auto-exposure, ACES tonemapping).
- [ ] **Phase 44.15**: Temporal Anti-Aliasing (TAA) with Halton sub-pixel jittering, history reprojection, and YCoCg neighborhood clamping.
- [ ] **Phase 44.16**: Complex Shading Models (Screen-Space Subsurface Scattering / SSS, Charlie Sheen/Velvet for cloth, Beer-Lambert volumetric glass absorption, and Cauchy chromatic dispersion).
- [ ] **Phase 44.17**: Final Lighting Acceptance maintaining strict $3.5\text{ms} - 5.0\text{ms}$ GPU budget on mainstream PCs (1080p/1440p @ 120–144+ FPS).

#### Milestone 15: Scalable Geometry & Virtual Geometry
*(Formerly OPT-16 and OPT-14)*
- **Phase 45.1: Offline autoLOD Pipeline via `meshoptimizer`** (formerly OPT-16):
  - [ ] `C16-01`: Automated mesh decimation via `meshopt_simplify` producing discrete fallback LOD0..LOD3 chains on asset import.
  - [ ] `C16-02`: Screen-size metric coverage thresholds and temporal dither cross-fading to eliminate popping.
  - [ ] `C16-03`: Simplified shadow-caster LOD selection and cooker cache invalidation.
- **Phase 45.2: Virtual Geometry & Hardware Task/Mesh Shaders** (formerly OPT-14):
  - [ ] `C14-01`: Unified GPU Instance Buffer (Transform, Bounds, Material ID, Visibility Flags).
  - [ ] `C14-02`: GPU-Driven Culling (frustum, normal cone, two-phase Hi-Z occlusion) and indirect draw dispatch (`ExecuteIndirect` / `vkCmdDrawIndexedIndirect`).
  - [ ] `C14-03`: Meshlet asset generation ($\\le 64$ verts, $\\le 126$ tris) via `meshopt_buildMeshlets`, bounding cones, and `.axlebin` format.
  - [ ] `C14-04`: Cluster DAG hierarchy with locked shared boundary loops guaranteeing 100% watertight, crack-free transitions.
  - [ ] `C14-05`: Hardware Task & Mesh Shaders evaluating projected screen error and direct rasterization on D3D12/Vulkan.
  - [ ] `C14-06`: Runtime capability detection querying NRI device features with seamless fallback to GPU-culled discrete LOD indexed draws.
  - [ ] `C14-07`: Dense-scene scale verification ($100,000+$ instances, $50\text{M}+$ polygons) measuring CPU draw submission elimination.

#### Milestone 16: Simulation Scale & Logic LOD
*(Formerly OPT-15)*
- **Phase 46.1: Monotonic Tick Identity & Significance Policy**:
  - [ ] `C15-01`: Public monotonic tick counter in `SimulationClock`.
  - [ ] `C15-02`: Significance distance tiers (Tier 1 `< 20m` @ 60/144 Hz, Tier 2 `20-100m` @ 15-30 Hz, Tier 3 `100-250m` @ 1-2 Hz, Tier 4 `> 250m` Dormant) with hysteresis.
- **Phase 46.2: Staggered Scheduling & Spatial Partitioning**:
  - [ ] `C15-03`: Deterministic staggered scheduler `(SimulationTick + EntityID) % Interval == 0` distributing non-critical entities across ticks.
  - [ ] `C15-04`: Dynamic 3D spatial hash grid ($64\text{m} \times 64\text{m}$ cells) with fast entity migration and radius queries.
- **Phase 46.3: Spatial Dormancy Bubble & Interpolation**:
  - [ ] `C15-05`: Spatial dormancy bubble (`DormantTag`, 0.000% CPU hibernation) with event-driven wake-up triggers for physics impulses and player proximity.
  - [ ] `C15-06`: Visual transform interpolation for skipped simulation ticks during `RenderWorld` snapshot extraction.
- **Phase 46.4: Physics Fast-Path & Animation URO**:
  - [ ] `C15-07`: Jolt Physics sleeping hints, deactivating distant rigid bodies and switching movement to kinematic translation.
  - [ ] `C15-08`: Read-only significance and tier query API for external gameplay plugins.
  - [ ] `C15-09`: Animation Update Rate Optimization (URO) & Bone LOD (LOD0 full skeleton, LOD1 primary bones, LOD2 root/limb pivots) with cached matrix reuse.

#### Milestone 17: Content Authoring Studios & Tooling
*(Formerly OPT-17, 18, 19, Tools & Historical Phases 38.3, 39, 40)*
- **Phase 47.1: Skeletal Animation Studio** (formerly OPT-17 / Phase 40):
  - [ ] `C17-01`: Rig and clip importer (FBX/glTF), procedural reference rig, bind-pose preservation for unkeyed channels, topological joint ordering.
  - [ ] `C17-02`: Keyframe timeline and Dope Sheet panel in AxleEditor; local TRS key manipulation, box-selection, copy/paste, undo/redo.
  - [ ] `C17-03`: Animator Controller state machine designer; parameters (Float, Int, Bool, Trigger), serializable transition rules, 1D/2D blend spaces with phase continuity.
  - [ ] `C17-04`: Skeletal runtime and RHI Structured Buffer streaming (128 bones) on Direct3D 12 and Vulkan.
  - [ ] `C17-05`: Sockets and bone attachments for weapons, helmets, and equipable props.
  - [ ] `C17-06`: Procedural Foot IK and ground collision snapping to eliminate foot sliding.
  - [ ] `C17-07`: Additive animation layers and upper-body masks for locomotion + actions.
- **Phase 47.2: Visual FX Studio** (formerly OPT-18 / Phase 39):
  - [ ] `C18-01`: Standalone `.axlevfx` document model with multi-emitter persistence; isolated preview viewport simulation.
  - [ ] `C18-02`: Advanced parameter modes: Constant, Two Constants, Curve, Two Curves, Color/Alpha gradients.
  - [ ] `C18-03`: Rate over distance (vehicle trails), start delay, prewarm, per-axis velocity/force over lifetime, orbital motion, emitter-motion inheritance.
  - [ ] `C18-04`: Ribbon & trail emitters with bounded history tracking and dynamic quad strip generation.
  - [ ] `C18-05`: Instanced 3D mesh particles with material overrides.
  - [ ] `C18-06`: World collision against ground planes and mesh colliders; sub-emitter event triggers on birth, collision, death.
  - [ ] `C18-07`: Flipbook sub-frame motion blending for smoke and fire sheets.
  - [ ] `C18-08`: GPU compute particle simulation and Visual Effect Graph with indirect draws.
- **Phase 47.3: Visual Material Graph UI & Scene Tooling** (formerly OPT-19 / Phase 38.3):
  - [ ] `C19-01`: Interactive node canvas in AxleEditor (`imnodes`), real-time AST lowering to HLSL via DXC with live shader compiler error reporting.
  - [ ] `C19-02`: Advanced surface nodes (Clearcoat, Anisotropy, Subsurface Scattering, Refraction with Cauchy Dispersion).
  - [ ] `C19-03`: Scene-placed box and sphere Reflection Probe volumes with live cubemap baking.
  - [ ] `C19-04`: Decal volumes and projected textures for bullet marks, tire tracks, and grime.
  - [ ] `C19-05`: Prefab variant authoring and visual property diffing.
- **Phase 47.4: Packaging Cooker & Diagnostic Tooling** (formerly OPT-Tools):
  - [ ] `CTools-01`: Standalone asset cooker traversing dependency trees, compressing textures to BCn, pre-warming shader caches.
  - [ ] `CTools-02`: Monolithic and chunked `Data.axlepak` archive generation.
  - [ ] `CTools-03`: Headless CI asset linter detecting dangling UUIDs, missing textures, and uncompressed assets.

#### Milestone 18: Performance Profiling, Parity & Master Rollout
*(Formerly OPT-20)*
- [ ] **Phase 48.1**: Standardized benchmark matrix (`MaterialReference`, `CarTest`, `OffRoadTrack`, dense geometry fixture) at 1080p, 1440p, 4K across quality tiers.
- [ ] **Phase 48.2**: Cross-backend image parity verification (automated PSNR, SSIM, maximum channel error across D3D12, Vulkan, OpenGL).
- [ ] **Phase 48.3**: GPU timestamp query telemetry (G-Buffer, Shadow Depth, Lighting, Post-Processing) and CPU frame-time percentiles (p50, p95, p99).
- [ ] **Phase 48.4**: Reliability, window resize, fullscreen toggle, DPI scaling, and simulated GPU device-loss recovery stress testing.
- [ ] **Phase 48.5**: Cutover, cleanup of temporary fallback shims, and disabling debug profiling overhead in Release builds.
- [ ] **Phase 48.6**: Master multi-configuration acceptance (Debug/Release, D3D12/Vulkan/OpenGL) and final production scorecard closure.

---

## 3. Governance & Contribution Principles

1. **Single Source of Truth**: `ROADMAP.md` is the only authoritative execution sequence. The former `OPT` construct has been unified into **Phases** organized under **Milestones 01 through 18**.
2. **Two-Document Rule**: Each active technical Milestone (Milestones 12–18) owns exactly one `PLAN.md` and one `ARCHITECTURE.md` under [`docs/milestones/`](docs/milestones/README.md).
3. **Verification Evidence**: All review reports and test evidence are archived under [`docs/reviews/`](docs/reviews/). Historical planning references are archived under [`docs/archive/`](docs/archive/).
