# AxleEngine Master Development Roadmap

This is the canonical master execution roadmap for AxleEngine.
The entire engine lifecycle is governed by a unified **Milestone** architecture spanning **Milestones 01 through 19**. Each Milestone manages its sequential **Phases**.

Detailed task checklists, work breakdown packages, and verification criteria live strictly within each Milestone's dedicated `PLAN.md` under [`docs/milestones/`](docs/milestones/README.md). Active technical milestones (Milestones 12–18) also maintain a companion `ARCHITECTURE.md` establishing subsystem designs, data contracts, and technology limits.

---

## 1. Master Milestones Directory

| Milestone | Milestone Name | Scope & Status | Technical Documents |
| :---: | :--- | :--- | :---: |
| **01** | **Core Foundations & Physics Integration** | **Completed:** Physics engine setup, UI telemetry overlay, scene transforms, vehicle mechanics (Phases 1–4). | [Plan](docs/milestones/MILESTONE_01_FOUNDATIONS/PLAN.md) · [Architecture](docs/milestones/MILESTONE_01_FOUNDATIONS/ARCHITECTURE.md) |
| **02** | **Simulation Lifecycle & Viewport Tooling** | **Completed:** Play/Pause/Stop simulation loop, 3D viewport navigation, transform gizmos, obstacle playground (Phases 5–7). | [Plan](docs/milestones/MILESTONE_02_SIMULATION_VIEWPORT/PLAN.md) · [Architecture](docs/milestones/MILESTONE_02_SIMULATION_VIEWPORT/ARCHITECTURE.md) |
| **03** | **Asset Pipeline, Serialization & Scripting** | **Completed:** Assimp asset ingest, JSON serialization, PBR materials, `.axle` assemblies, C++ scripting (Phases 8–13). | [Plan](docs/milestones/MILESTONE_03_ASSETS_SERIALIZATION_SCRIPTING/PLAN.md) · [Architecture](docs/milestones/MILESTONE_03_ASSETS_SERIALIZATION_SCRIPTING/ARCHITECTURE.md) |
| **04** | **Scene Graph, Hot-Reloading & Multithreading** | **Completed:** EnTT ECS hierarchy, hierarchy tree UI, reflection inspector, script hot-reload DLL, enkiTS jobs (Phases 14–19). | [Plan](docs/milestones/MILESTONE_04_SCENE_GRAPH_MULTITHREADING/PLAN.md) · [Architecture](docs/milestones/MILESTONE_04_SCENE_GRAPH_MULTITHREADING/ARCHITECTURE.md) |
| **05** | **Component Architecture & Standalone Runtime** | **Completed:** Component vehicle architecture, decoupled renderer, standalone `AxleRuntime.exe`, VFS cooker (Phases 20–23). | [Plan](docs/milestones/MILESTONE_05_COMPONENT_ARCHITECTURE_RUNTIME/PLAN.md) · [Architecture](docs/milestones/MILESTONE_05_COMPONENT_ARCHITECTURE_RUNTIME/ARCHITECTURE.md) |
| **06** | **High-Fidelity Rendering, PBR & Audio** | **Completed:** Material flyweight, soft shadows, HDR skybox IBL, miniaudio 3D spatial acoustics (Phases 24–28). | [Plan](docs/milestones/MILESTONE_06_RENDERING_PBR_AUDIO/PLAN.md) · [Architecture](docs/milestones/MILESTONE_06_RENDERING_PBR_AUDIO/ARCHITECTURE.md) |
| **07** | **Terrain, Spline Tracks & Skeletal Systems** | **Completed:** Spline track & terrain modular plugins, skeletal animation CPU evaluation (Phases 29–30). | [Plan](docs/milestones/MILESTONE_07_TERRAIN_SPLINES_ANIMATION/PLAN.md) · [Architecture](docs/milestones/MILESTONE_07_TERRAIN_SPLINES_ANIMATION/ARCHITECTURE.md) |
| **08** | **Visual Effects, Modern RHI & Cross-Platform** | **Completed:** CPU particle system, NVIDIA NRI modern RHI (D3D12/Vulkan), Forward+ viewport, Canvas2D UI (Phases 31–34). | [Plan](docs/milestones/MILESTONE_08_VFX_RHI_CROSSPLATFORM/PLAN.md) · [Architecture](docs/milestones/MILESTONE_08_VFX_RHI_CROSSPLATFORM/ARCHITECTURE.md) |
| **09** | **Production Hardening & Unified Asset Architecture** | **Completed:** Vulkan 1.3 backend parity, unified `.axleasset` format, raw media ingestion pipeline (Phases 35–37). | [Plan](docs/milestones/MILESTONE_09_PRODUCTION_HARDENING_VULKAN/PLAN.md) · [Architecture](docs/milestones/MILESTONE_09_PRODUCTION_HARDENING_VULKAN/ARCHITECTURE.md) |
| **10** | **Complete Visual Tooling Ecosystem Foundations** | **Completed:** Visual material studio, embedded 3D material preview, dynamic HLSL shader reflection (Phases 38–40). | [Plan](docs/milestones/MILESTONE_10_TOOLING_ECOSYSTEM/PLAN.md) · [Architecture](docs/milestones/MILESTONE_10_TOOLING_ECOSYSTEM/ARCHITECTURE.md) |
| **11** | **Modern RHI-Primary Rendering Foundation** | **Completed:** RHI-primary rendering architecture, GPU safety, device lifetime management (Phase 41). | [Plan](docs/milestones/MILESTONE_11_RHI_PRIMARY_FOUNDATION/PLAN.md) · [Architecture](docs/milestones/MILESTONE_11_RHI_PRIMARY_FOUNDATION/ARCHITECTURE.md) |
| **12** | **Architecture Foundation & Boundary Hardening** | **Completed:** Zero-allowance layer enforcement, enkiTS multithreading, Jolt physics decoupling, NRI device epochs, `RenderWorld` snapshot, Plugin SDK, VFS identity, pak cooker.<br>**Open Gates:** 3 final release acceptance gates (Phase 42.10). | [Plan](docs/milestones/MILESTONE_12_ARCHITECTURE_FOUNDATION/PLAN.md) · [Architecture](docs/milestones/MILESTONE_12_ARCHITECTURE_FOUNDATION/ARCHITECTURE.md) |
| **13** | **Render Concurrency & Shader Compilation Pipeline** | **Completed:** Decoupled Render Thread, transient VRAM memory aliasing, DXC Material Graph compiler & pipeline caching (Phases 43.1–43.2).<br>**Deferred:** Asynchronous parallel command recording rollout (Phase 43.3). | [Plan](docs/milestones/MILESTONE_13_RENDER_CONCURRENCY/PLAN.md) · [Architecture](docs/milestones/MILESTONE_13_RENDER_CONCURRENCY/ARCHITECTURE.md) |
| **14** | **Render Architecture Refactor** | **Accepted:** U0-U9 final source, ownership contracts, RenderGraph/resource lifetime, service seams, backend/fallback policy, migration tooling and M15 handoff. M15 feature fidelity remains deferred. | [Plan](docs/milestones/MILESTONE_14_RENDER_ARCHITECTURE/PLAN.md) · [Architecture](docs/milestones/MILESTONE_14_RENDER_ARCHITECTURE/ARCHITECTURE.md) · [U9 review](docs/reviews/M14_U9_FINAL_ACCEPTANCE_REVIEW.md) |
| **15** | **Lighting & Material Fidelity** | **HOLD:** Former M14 feature milestone, now dependent on M14 architecture contracts. Includes direct BRDF, GTAO/contact, SSR, reflection captures, fog, DDGI, post/temporal and advanced materials (Phase 45). | [Plan](docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md) · [Architecture](docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/ARCHITECTURE.md) |
| **16** | **Scalable Geometry & Virtual Geometry** | **Proposed:** Offline algorithmic autoLOD pipeline via `meshoptimizer` (Phase 46.1) and Nanite-style continuous virtual geometry with Task/Mesh Shaders (Phase 46.2). | [Plan](docs/milestones/MILESTONE_16_GEOMETRY_VISIBILITY/PLAN.md) · [Architecture](docs/milestones/MILESTONE_16_GEOMETRY_VISIBILITY/ARCHITECTURE.md) |
| **17** | **Simulation Scale & Logic LOD** | **Proposed:** Significance evaluation tiers, deterministic staggered modulo scheduler, spatial hash grid, 0% CPU spatial dormancy bubble, kinematic physics bypass (Phases 47.1–47.4). | [Plan](docs/milestones/MILESTONE_17_SIMULATION_SCALE/PLAN.md) · [Architecture](docs/milestones/MILESTONE_17_SIMULATION_SCALE/ARCHITECTURE.md) |
| **18** | **Content Authoring Studios & Tooling** | **Proposed:** Skeletal Animation Studio (Phase 48.1), Visual FX Studio & GPU Particles (Phase 48.2), Material Graph UI Canvas (Phase 48.3), Packaging Cooker & Asset Linter (Phase 48.4). | [Plan](docs/milestones/MILESTONE_18_CONTENT_TOOLING/PLAN.md) · [Architecture](docs/milestones/MILESTONE_18_CONTENT_TOOLING/ARCHITECTURE.md) |
| **19** | **Performance Profiling, Parity & Master Rollout** | **Proposed:** Standardized benchmark matrix, automated multi-backend image parity (PSNR/SSIM), GPU timestamp telemetry, simulated device-loss recovery, final production scorecard (Phases 49.1–49.6). | [Plan](docs/milestones/MILESTONE_19_PERFORMANCE_ROLLOUT/PLAN.md) · [Architecture](docs/milestones/MILESTONE_19_PERFORMANCE_ROLLOUT/ARCHITECTURE.md) |

---

## 2. High-Level Lifecycle & Strategic Overview

```mermaid
flowchart LR
    subgraph Era1 ["Foundations: Milestones 01 - 11"]
        M01_11["Historical Milestones 01..11<br/>Phases 1 - 41<br/>Core, Physics, ECS, RHI, Tools"]
    end

    subgraph Era2 ["Stabilization: Milestones 12 - 15"]
        M12["M12: Architecture<br/>Zero Allowance"]
        M13["M13: Concurrency<br/>Render Thread & DXC"]
        M14["M14: Render Architecture<br/>ViewContext & Passes"]
        M15["M15: Lighting<br/>Material Fidelity"]
    end

    subgraph Era3 ["Next-Gen Scale: Milestones 16 - 19"]
        M16["M16: Geometry<br/>autoLOD & Meshlets"]
        M17["M17: Simulation<br/>Significance & Dormancy"]
        M18["M18: Content<br/>Anim, VFX & Mat Studios"]
        M19["M19: Rollout<br/>Benchmarks & Parity"]
    end

    M01_11 --> M12 --> M13 --> M14 --> M15 --> M16 --> M17 --> M18 --> M19

```

### 2.1. Historical Foundations (Milestones 01 through 11)
- **Status:** 100% Completed.
- Encapsulates the initial engine build-up from a basic OpenGL/ImGui window to a modern multi-backend architecture utilizing EnTT ECS, Jolt Physics 3D, NVIDIA NRI (Direct3D 12 and Vulkan), and preliminary authoring viewports.

### 2.2. Architectural Hardening & Rendering (Milestones 12 through 15)
- **Status:** M12/M13 baseline completed; M14 architecture accepted; M15 lighting/material fidelity remains held for its own released scope and evidence.
- M14 makes the renderer modular and establishes the `ViewContext`, pass, material, resource-generation and fallback contracts required for reliable lighting fidelity work in M15.

### 2.3. Next-Gen Scale, Content Studios & Production Rollout (Milestones 16 through 19)
- **Status:** Proposed & Fully Planned.
- Targets AAA geometric throughput (autoLOD and Task/Mesh Shaders), vast entity simulation scaling (significance tiers and spatial hibernation), end-to-end visual authoring tools (Animation Studio, Visual FX Studio, and Material Graph Node Canvas), concluding with exhaustive benchmark verification across Direct3D 12, Vulkan, and OpenGL.

---

## 3. Governance & Contribution Principles

1. **Single Source of Authority**: `ROADMAP.md` is the only execution authority. All work is ordered by Milestone and executed through its sequential child Phases.
2. **Dedicated Plan Files**: Granular checklists, work breakdown packages, and commit histories live within each Milestone's [`PLAN.md`](docs/milestones/).
3. **Dedicated Architecture Files**: Every milestone maintains a companion [`ARCHITECTURE.md`](docs/milestones/) defining technical contracts, subsystem models, and fallback policies.
4. **Evidence Archiving**: Verification and test reports are recorded under [`docs/reviews/`](docs/reviews/). Historical design archives live under [`docs/archive/`](docs/archive/).
