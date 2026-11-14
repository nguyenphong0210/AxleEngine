# Milestone 12 — Architecture Foundation Plan

**Status:** Historical foundations (Milestones 01–11) completed; Phase 42 baseline (formerly OPT-00..09) accepted; Phase 42.10 (formerly OPT-10) has open release gates.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## 1. Executive Summary

Milestone 12 encapsulates the foundational architecture and boundary hardening of AxleEngine. It bridges the historical evolution of the engine across its first 11 milestones (Phases 1–41) with the rigorous architectural stabilization program (Phase 42, formerly OPT-00 through OPT-10) that eliminated technical debt, isolated third-party vendors, established strict architectural boundaries, and unified rendering across all products.

---

## 2. Historical Milestones & Predecessor Phases (Completed Baseline)

Before Milestone 12, AxleEngine was constructed through 11 major milestones encompassing 41 development phases:

### Milestone 01: Core Foundations & Physics Integration
- [x] **Phase 1: UI & Telemetry Overlay** (`Engine/UI`) — ImGui debug overlays, performance counters, and basic platform windowing.
- [x] **Phase 2: Physics Engine Foundation** (`Engine/Physics`) — Initial physics world, rigid body integration, and fixed simulation time-steps.
- [x] **Phase 3: Scene & Transform Synchronization** (`Engine/Scene`) — Entity hierarchies, local/world transform matrices, and tick synchronization.
- [x] **Phase 4: Automotive Mechanics** (`Engine/Vehicle`) — Prototype vehicle mechanics and wheel raycasting (retired in Phase 42.0 into an optional plugin).

### Milestone 02: Simulation Lifecycle & Viewport Tooling
- [x] **Phase 5: Editor Ecosystem & Simulation Lifecycle** (`Engine/UI`, `Engine/Renderer`) — Play/Pause/Stop simulation states, docking workspace.
- [x] **Phase 6: Interactive 3D Viewport & Transform Gizmos** (`Engine/UI`, `Sandbox`) — ImGuizmo translation, rotation, scale gizmos, camera navigation.
- [x] **Phase 7: Automotive Test Playground & Obstacle Course** (`Engine/Physics`, `Sandbox`) — Physical obstacle courses, suspension ramps, vehicle test track.

### Milestone 03: Asset Pipeline, Scene Serialization & Scripting
- [x] **Phase 8: C++ Native Scripting & Vehicle Component** (`Engine/Scene`) — Native C++ game scripts and component binding.
- [x] **Phase 9: Asset System & 3D Model Ingest Pipeline** (`Engine/Asset`, `Sandbox/assets`) — Assimp mesh loading, texture ingestion, material definitions.
- [x] **Phase 10: Scene Serialization & Project Management** (`Engine/Scene`, `Engine/Project`) — JSON scene serializer, project directory structures.
- [x] **Phase 11: Lighting, Dynamic Shadows & PBR Materials** — Directional shadow mapping, Cook-Torrance microfacet BRDF.
- [x] **Phase 12: Assembly Pipeline & Unified `.axle` Prefab System** (`Engine/Asset`, `Engine/Scene`, `Engine/UI`) — Nested entity assemblies and prefab instantiation.
- [x] **Phase 13: C++ Native Scripting Ecosystem & Mechanical Assembly Architecture** — Advanced mechanical scripting and assembly bindings.

### Milestone 04: Scene Graph, Hot-Reloading & Multithreading
- [x] **Phase 14: Core ECS & Relationship Hierarchy** — Parent-child entity relationships, scene graph traversal.
- [x] **Phase 15: Scene Hierarchy Panel Upgrade** — Entity tree UI, drag-and-drop reparenting, entity search/filter.
- [x] **Phase 16: Properties Panel & C++ Scripting Reflection** — Component property inspector, reflection macros, serializable fields.
- [x] **Phase 17: Content Browser & Material/Asset System** — Visual thumbnail browser, asset drag-and-drop, physical asset creation.
- [x] **Phase 18: Integrated Drag & Drop, Safe Simulation & In-Editor Live Coding** — Dynamic C++ script DLL compilation and hot-reloading.
- [x] **Phase 19: Multithreaded Job System & enkiTS Integration** (`Engine/Core`, `Engine/Physics`) — Task graph scheduler and parallel job dispatching.

### Milestone 05: Component-Driven Architecture & Standalone Runtime
- [x] **Phase 20: Component-Driven Vehicle Creation from Scratch** (`Engine/Vehicle`, `Engine/Scene`, `Engine/UI`) — Modular vehicle components.
- [x] **Phase 21: Decoupled Scene Renderer Architecture** (`Engine/Scene`) — Separation of scene graph state from rendering submission.
- [x] **Phase 22: Standalone Game Runtime Player** (`AxleRuntime.exe`) — Dedicated lightweight executable without Editor dependencies.
- [x] **Phase 23: Asset Packaging & Cooking Pipeline** (`.axlepak` & VFS) — Monolithic asset packaging and virtual file system mounts.

### Milestone 06: High-Fidelity Rendering, PBR & Audio Synthesis
- [x] **Phase 24: Decoupled Material Asset System & Flyweight Architecture** — Material asset descriptors, GPU parameter sharing.
- [x] **Phase 25: AAA Soft Shadows & Precision Lighting Pipeline** — Percentage-Closer Soft Shadows (PCSS), contact hardening.
- [x] **Phase 26: HDR Skybox Cubemap & Image-Based Lighting (IBL) Pipeline** — HDR environment maps, split-sum specular reflection.
- [x] **Phase 27: High-Fidelity 3D Audio System & Vehicle Acoustics** (`Engine/Audio`) — miniaudio integration, 3D spatial attenuation, engine Doppler.
- [x] **Phase 28: Gameplay Polish, Multi-Part Vehicle Rigging & Dynamic HUD** — Cockpit instrumentation, spring arm cameras.

### Milestone 07: Large-Scale Terrain, Spline Tracks & Skeletal Characters
- [x] **Phase 29: Modular Plugin Architecture — Spline Track & Large-Scale Terrain System** (`Plugins/Spline`, `Plugins/Terrain`).
- [x] **Phase 30: Skeletal Animation & Character Controller System** (`Engine/Animation`) — Basic skeleton evaluation and bone transforms.

### Milestone 08: Visual Effects, Modern RHI & Cross-Platform Engine
- [x] **Phase 31: Advanced Particle & Visual Effects System** (`Engine/VFX`) — CPU particle emitters, velocity curves, color gradients.
- [x] **Phase 32: Modern RHI Abstraction & Cross-Platform Engine** (NVIDIA NRI — Vulkan 1.3 & Direct3D 12) — Modern GPU abstraction layer.
- [x] **Phase 33: Multi-Backend 3D Viewport Scene Renderer & PBR Forward+ Pipeline** (`Engine/Renderer`, `Engine/RHI`).
- [x] **Phase 34: Declarative AxleUI In-Game UI & Hardware-Accelerated Canvas2D Subsystem** (`Engine/UI`, `Engine/Renderer`).

### Milestone 09: Production Hardening, Vulkan 1.3 Backend Parity & Next-Gen Architecture
- [x] **Phase 35: High-Performance Vulkan 1.3 Graphics Backend & Editor Parity** (`Engine/RHI`, `Engine/UI`).
- [x] **Phase 36: Unified `.axleasset` Architecture & Multi-Backend Component Resource Standardization** (`Engine/Asset`, `Engine/Scene`, `Engine/UI`).
- [x] **Phase 37: Raw Media Asset Ingestion Pipeline & Opt-in Descriptor Architecture** (`Engine/Asset`, `Engine/UI`, `Editor`).

### Milestone 10: Complete Visual Tooling Ecosystem Foundations
- [x] **Phase 38: Visual Material Studio & Multi-Pass Surface Pipeline** (`Engine/Asset`, `Engine/Renderer`, `Engine/UI`, `Editor`).
- [x] **Phase 38.1: Embedded 3D Material Preview & HLSL Studio** (`Engine/UI`, `Engine/RHI`, `Editor`).
- [x] **Phase 38.2: Dynamic Shader Reflection & Advanced Shading** (`Engine/RHI`, `Engine/Asset`, `Engine/UI`).
- [ ] **Phase 38.3: Material Pipeline Completion & Automotive Materials** — *(Deferred to Milestone 18)*.
- [ ] **Phase 39: Visual Particle & VFX Studio Completion** — *(Deferred to Milestone 18)*.
- [ ] **Phase 40: Skeletal Animation Studio Completion** — *(Deferred to Milestone 18)*.

### Milestone 11: RHI-Primary Rendering Foundation
- [x] **Phase 41: Shared Rendering Architecture, GPU Safety & Product Integration** — Absorbed into Milestones 12 and 13.

---

## 3. Architecture Foundation Program (Phase 42, formerly OPT-00..OPT-10)

- [x] **Phase 42.0 (OPT-00): Diagnostics, Baseline Inventory & Vehicle Retirement** — Retired built-in `Engine/Vehicle` legacy code into decoupled optional plugin architecture; verified legacy data preservation. Evidence: [`docs/reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md`](../../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md).
- [x] **Phase 42.1 (OPT-01): Reproducible Baseline & Architecture Checking Tooling** — Implemented `scripts/check_architecture.py` enforcing strict layered DAG rules across 576+ files and 25 CMake targets; added intentional violation fixtures. Evidence: [`docs/reviews/OPT_01_BASELINE_DELIVERY.md`](../../reviews/OPT_01_BASELINE_DELIVERY.md), [`OPT_01_AF01_DELIVERY.md`](../../reviews/OPT_01_AF01_DELIVERY.md).
- [x] **Phase 42.2 (OPT-02): Threading, Job Scheduler & Async Safety** — Standardized `enkiTS` (`JobSystem`), fiber/worker isolation, and safe async job queues across thread counts 1, 2, and 16. Evidence: [`docs/reviews/OPT_02_GEMINI_EXECUTION.md`](../../reviews/OPT_02_GEMINI_EXECUTION.md), [`OPT_02_03_CODEX_REVIEW.md`](../../reviews/OPT_02_03_CODEX_REVIEW.md).
- [x] **Phase 42.3 (OPT-03): Physics Engine Boundary Hardening** — Fully encapsulated Jolt Physics inside `Engine/Physics`, decoupling fixed-step physics from frame rendering; established clean `Axle::PhysicsDebug` interface. Evidence: [`docs/reviews/OPT_03_GEMINI_EXECUTION.md`](../../reviews/OPT_03_GEMINI_EXECUTION.md).
- [x] **Phase 42.4 (OPT-04): Modern RHI Device & GPU Lifetime Foundation** — Native D3D12 and Vulkan backend management via NVIDIA NRI; implemented transactional device epochs, submission fences, and swapchain management. Evidence: [`docs/reviews/OPT_04_CODEX_EXECUTION.md`](../../reviews/OPT_04_CODEX_EXECUTION.md).
- [x] **Phase 42.5 (OPT-05): Unified Rendering Across Editor, Previews, and Standalone Runtime** — Delivered immutable `RenderWorld` CPU snapshot extraction, unified `RenderService`, and `RenderGraph` pass coordination. Evidence: [`docs/reviews/OPT_05_GEMINI_EXECUTION.md`](../../reviews/OPT_05_GEMINI_EXECUTION.md), [`OPT_05_CODEX_REVIEW.md`](../../reviews/OPT_05_CODEX_REVIEW.md).
- [x] **Phase 42.6 (OPT-06): Plugin System Architecture & SDK** — Created `Engine/Plugin` SDK with clean C-ABI isolation, DLL dynamic loading, lifecycle hooks (`OnLoad`/`OnUnload`), and schema gates. Evidence: [`docs/reviews/OPT_06_CODEX_REVIEW.md`](../../reviews/OPT_06_CODEX_REVIEW.md), [`OPT_06_PLG_03_SCHEMA_GATE.md`](../../reviews/OPT_06_PLG_03_SCHEMA_GATE.md).
- [x] **Phase 42.7 (OPT-07): Plugin Project & Runtime Selection** — Configured project descriptor (`.axleproject`) and dynamic plugin discovery. Evidence: [`docs/reviews/OPT_07_08_FOUNDATION_REVIEW.md`](../../reviews/OPT_07_08_FOUNDATION_REVIEW.md).
- [x] **Phase 42.8 (OPT-08): Content & Asset Management** — Standardized `AssetRegistry`, Virtual File System (`VFS`), texture/mesh streaming, and embedded `# AxleFileIdentity: 1 <UUID>` (eliminating `.meta` files). Evidence: [`docs/reviews/OPT_07_08_FOUNDATION_REVIEW.md`](../../reviews/OPT_07_08_FOUNDATION_REVIEW.md).
- [x] **Phase 42.9 (OPT-09): Build & Standalone Packaging Cooker** — Created standalone asset cooker compiling scene/asset trees into monolithic `Data.axlepak` archive. Evidence: [`docs/reviews/OPT_09_IMPLEMENTATION.md`](../../reviews/OPT_09_IMPLEMENTATION.md).
- [ ] **Phase 42.10 (OPT-10): Cross-Product Architecture Acceptance & Multi-Platform Enforcement**:
  - [x] Zero-baseline architecture enforcement (all 8 historical allowances retired).
  - [x] Stress target: scheduler sizes 1/2/16, 2,048 Jolt bodies, parallel ECS mutation batches.
  - [x] 73/73 CTest suites passing on Debug configuration.
  - [ ] **Open Gate 1**: Full Release Standalone Product Build Verification (`AxleEditor.exe` and `AxleRuntime.exe`).
  - [ ] **Open Gate 2**: Detached export packaging soak stress test.
  - [ ] **Open Gate 3**: Physical interactive UI native test verification.
  - Evidence: [`docs/reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md`](../../reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md).
