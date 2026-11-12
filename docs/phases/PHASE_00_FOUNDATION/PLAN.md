# Milestone 00 — Architecture Foundation Plan

**Status:** Historical foundations (Milestones 1 - 10) completed; OPT-00 through OPT-09 accepted within recorded boundaries; OPT-10 implementation delivered with open acceptance gates.
**Supporting Architectural Document:** [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. Executive Summary

Phase 00 encapsulates the entire foundational construction and architectural hardening of AxleEngine. It bridges the historical evolution of the engine across its first 41 phases with the rigorous 80-package **Foundation Optimization Program (OPT-00 through OPT-10)** that eliminated technical debt, isolated third-party vendors, established strict architectural boundaries, and unified rendering across all products.

---

## 2. Historical Milestones & 41 Original Phases (Completed Baseline)

Before the OPT stabilization program, AxleEngine was constructed through 11 major milestones encompassing 41 development phases:

### Milestone 1: Foundations & Physics Integration
- [x] **Phase 1: UI & Telemetry Overlay** (Engine/UI) — ImGui debug overlays, performance counters, and basic platform windowing.
- [x] **Phase 2: Physics Engine Foundation** (Engine/Physics) — Initial physics world, rigid body integration, and fixed simulation time-steps.
- [x] **Phase 3: Scene & Transform Synchronization** (Engine/Scene) — Entity hierarchies, local/world transform matrices, and tick synchronization.
- [x] **Phase 4: Automotive Mechanics** (Engine/Vehicle) — Prototype vehicle mechanics and wheel raycasting (retired in OPT-00 into an optional plugin).

### Milestone 2: Simulation Lifecycle & Viewport Tooling
- [x] **Phase 5: Editor Ecosystem & Simulation Lifecycle** (Engine/UI, Engine/Renderer) — Play/Pause/Stop simulation states, docking workspace.
- [x] **Phase 6: Interactive 3D Viewport & Transform Gizmos** (Engine/UI, Sandbox) — ImGuizmo translation, rotation, scale gizmos, camera navigation.
- [x] **Phase 7: Automotive Test Playground & Obstacle Course** (Engine/Physics, Sandbox) — Physical obstacle courses, suspension ramps, vehicle test track.

### Milestone 3: Asset Pipeline, Scene Serialization & Scripting
- [x] **Phase 8: C++ Native Scripting & Vehicle Component** (Engine/Scene) — Native C++ game scripts and component binding.
- [x] **Phase 9: Asset System & 3D Model Ingest Pipeline** (Engine/Asset, Sandbox/assets) — Assimp mesh loading, texture ingestion, material definitions.
- [x] **Phase 10: Scene Serialization & Project Management** (Engine/Scene, Engine/Project) — JSON scene serializer, project directory structures.
- [x] **Phase 11: Lighting, Dynamic Shadows & PBR Materials** — Directional shadow mapping, Cook-Torrance microfacet BRDF.
- [x] **Phase 12: Assembly Pipeline & Unified .axle Prefab System** (Engine/Asset, Engine/Scene, Engine/UI) — Nested entity assemblies and prefab instantiation.
- [x] **Phase 13: C++ Native Scripting Ecosystem & Mechanical Assembly Architecture** — Advanced mechanical scripting and assembly bindings.

### Milestone 4: Scene Graph, Hot-Reloading & Multithreading
- [x] **Phase 14: Core ECS & Relationship Hierarchy** — Parent-child entity relationships, scene graph traversal.
- [x] **Phase 15: Scene Hierarchy Panel Upgrade** — Entity tree UI, drag-and-drop reparenting, entity search/filter.
- [x] **Phase 16: Properties Panel & C++ Scripting Reflection** — Component property inspector, reflection macros, serializable fields.
- [x] **Phase 17: Content Browser & Material/Asset System** — Visual thumbnail browser, asset drag-and-drop, physical asset creation.
- [x] **Phase 18: Integrated Drag & Drop, Safe Simulation & In-Editor Live Coding** — Dynamic C++ script DLL compilation and hot-reloading.
- [x] **Phase 19: Multithreaded Job System & enkiTS Integration** (Engine/Core, Engine/Physics) — Task graph scheduler and parallel job dispatching.

### Milestone 5: Component-Driven Architecture & Standalone Runtime
- [x] **Phase 20: Component-Driven Vehicle Creation from Scratch** (Engine/Vehicle, Engine/Scene, Engine/UI) — Modular vehicle components.
- [x] **Phase 21: Decoupled Scene Renderer Architecture** (Engine/Scene) — Separation of scene graph state from rendering submission.
- [x] **Phase 22: Standalone Game Runtime Player** (AxleRuntime.exe) — Dedicated lightweight executable without Editor dependencies.
- [x] **Phase 23: Asset Packaging & Cooking Pipeline** (.axlepak & VFS) — Monolithic asset packaging and virtual file system mounts.

### Milestone 6: High-Fidelity Rendering, PBR & Audio Synthesis
- [x] **Phase 24: Decoupled Material Asset System & Flyweight Architecture** — Material asset descriptors, GPU parameter sharing.
- [x] **Phase 25: AAA Soft Shadows & Precision Lighting Pipeline** — Percentage-Closer Soft Shadows (PCSS), contact hardening.
- [x] **Phase 26: HDR Skybox Cubemap & Image-Based Lighting (IBL) Pipeline** — HDR environment maps, split-sum specular reflection.
- [x] **Phase 27: High-Fidelity 3D Audio System & Vehicle Acoustics** (Engine/Audio) — miniaudio integration, 3D spatial attenuation, engine Doppler.
- [x] **Phase 28: Gameplay Polish, Multi-Part Vehicle Rigging & Dynamic HUD** — Cockpit instrumentation, spring arm cameras.

### Milestone 7: Large-Scale Terrain, Spline Tracks & Skeletal Characters
- [x] **Phase 29: Modular Plugin Architecture — Spline Track & Large-Scale Terrain System** (Plugins/Spline, Plugins/Terrain).
- [x] **Phase 30: Skeletal Animation & Character Controller System** (Engine/Animation) — Basic skeleton evaluation and bone transforms.

### Milestone 8: Visual Effects, Modern RHI & Cross-Platform Engine
- [x] **Phase 31: Advanced Particle & Visual Effects System** (Engine/VFX) — CPU particle emitters, velocity curves, color gradients.
- [x] **Phase 32: Modern RHI Abstraction & Cross-Platform Engine** (NVIDIA NRI — Vulkan 1.3 & Direct3D 12) — Modern GPU abstraction layer.
- [x] **Phase 33: Multi-Backend 3D Viewport Scene Renderer & PBR Forward+ Pipeline** (Engine/Renderer, Engine/RHI).
- [x] **Phase 34: Declarative AxleUI In-Game UI & Hardware-Accelerated Canvas2D Subsystem** (Engine/UI, Engine/Renderer).

### Milestone 9: Production Hardening, Vulkan 1.3 Backend Parity & Next-Gen Architecture
- [x] **Phase 35: High-Performance Vulkan 1.3 Graphics Backend & Editor Parity** (Engine/RHI, Engine/UI).
- [x] **Phase 36: Unified .axleasset Architecture & Multi-Backend Component Resource Standardization** (Engine/Asset, Engine/Scene, Engine/UI).
- [x] **Phase 37: Raw Media Asset Ingestion Pipeline & Opt-in Descriptor Architecture** (Engine/Asset, Engine/UI, Editor).

### Milestone 10: Complete Visual Tooling Ecosystem
- [x] **Phase 38: Visual Material Studio & Multi-Pass Surface Pipeline** (Engine/Asset, Engine/Renderer, Engine/UI, Editor).
- [x] **Phase 38.1: Embedded 3D Material Preview & HLSL Studio** (Engine/UI, Engine/RHI, Editor).
- [x] **Phase 38.2: Dynamic Shader Reflection & Advanced Shading** (Engine/RHI, Engine/Asset, Engine/UI).
- [ ] **Phase 38.3: Material Pipeline Completion, Godot-style Inspector & Automotive Materials** — *(Deferred to Phase 05)*.
- [ ] **Phase 39: Visual Particle & VFX Studio - End-to-End Editor Completion** — *(Delivered baseline; deferred expansion to Phase 05)*.
- [ ] **Phase 39.1: Particle Module & Rendering Expansion toward Unity-style Authoring** — *(Deferred to Phase 05)*.
- [ ] **Phase 39.2: GPU Particle Simulation & Visual Effect Graph** — *(Deferred to Phase 05)*.
- [ ] **Phase 40: Skeletal Animation Studio & Visual Animator State Machine** — *(Delivered CPU foundations; deferred authoring to Phase 05)*.

### Milestone 11: RHI-Primary Rendering Foundation
- [x] **Phase 41: Shared Rendering Architecture, GPU Safety & Product Integration** — Absorbed and delivered into OPT-04, OPT-05, and OPT-11.

---

## 3. The Foundation Optimization Program (OPT-00 through OPT-10)

Following Milestone 11, the team undertook the comprehensive **OPT Foundation Program** to eliminate architectural coupling and enforce strict layering:

### OPT-00: Diagnostics, Baseline Inventory & Vehicle Retirement
- [x] Retired built-in Engine/Vehicle legacy code into decoupled optional plugin architecture.
- [x] Implemented legacy data preservation tests ensuring old project scenes load safely without data loss.
- [x] Captured baseline test matrix across Debug and Release configurations.
- *Evidence:* [docs/reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md](../../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md).

### OPT-01: Reproducible Baseline & Architecture Checking Tooling
- [x] Implemented scripts/check_architecture.py enforcing strict layered DAG rules across 576+ files and 25 CMake targets.
- [x] Added intentionally failing violation fixtures to prevent architecture erosion.
- [x] Structured package delivery for AF-00, AF-01, P41-00, PLG-00, and EJ-00.
- *Evidence:* [docs/reviews/OPT_01_BASELINE_DELIVERY.md](../../reviews/OPT_01_BASELINE_DELIVERY.md), [docs/reviews/OPT_01_AF01_DELIVERY.md](../../reviews/OPT_01_AF01_DELIVERY.md).

### OPT-02: Threading, Job Scheduler & Async Safety
- [x] Standardized enkiTS (JobSystem), fiber/worker isolation, and safe async job queues.
- [x] Verified deterministic scheduler sizes (1, 2, 16 threads), thread counts, and memory leak prevention.
- [x] Packages EJ-01 through EJ-05 delivered and verified.
- *Evidence:* [docs/reviews/OPT_02_GEMINI_EXECUTION.md](../../reviews/OPT_02_GEMINI_EXECUTION.md), [docs/reviews/OPT_02_03_CODEX_REVIEW.md](../../reviews/OPT_02_03_CODEX_REVIEW.md).

### OPT-03: Physics Engine Boundary Hardening
- [x] Fully encapsulated Jolt Physics inside Engine/Physics, decoupling fixed-step physics from frame rendering.
- [x] Established clean physics debug extraction consumer interface (Axle::PhysicsDebug).
- [x] Packages AF-02..04 and EJ-06..14 delivered and verified.
- *Evidence:* [docs/reviews/OPT_03_GEMINI_EXECUTION.md](../../reviews/OPT_03_GEMINI_EXECUTION.md).

### OPT-04: Modern RHI Device & GPU Lifetime Foundation
- [x] Native D3D12 and Vulkan backend management via NVIDIA NRI.
- [x] Implemented transactional GPU device epoch tracking, command queue submission fences, and swapchain management.
- [x] Packages P41-01 through P41-11 delivered and verified.
- *Evidence:* [docs/reviews/OPT_04_CODEX_EXECUTION.md](../../reviews/OPT_04_CODEX_EXECUTION.md).

### OPT-05: Unified Rendering Across Editor, Previews, and Standalone Runtime
- [x] Delivered immutable RenderWorld CPU snapshot extraction, unified RenderService, and RenderGraph pass coordination.
- [x] Guaranteed unified visual behavior across Editor viewports, asset previews, and AxleRuntime.exe.
- [x] Packages P41-12 through P41-20 delivered and verified.
- *Evidence:* [docs/reviews/OPT_05_GEMINI_EXECUTION.md](../../reviews/OPT_05_GEMINI_EXECUTION.md), [docs/reviews/OPT_05_CODEX_REVIEW.md](../../reviews/OPT_05_CODEX_REVIEW.md).

### OPT-06: Plugin System Architecture & SDK
- [x] Created Engine/Plugin SDK with explicit ABI isolation, DLL dynamic loading, and lifecycle hooks (OnLoad, OnUnload).
- [x] Added schema gate validating plugin metadata and project requirements.
- [x] Packages PLG-01 through PLG-14 delivered and verified.
- *Evidence:* [docs/reviews/OPT_06_CODEX_REVIEW.md](../../reviews/OPT_06_CODEX_REVIEW.md), [docs/reviews/OPT_06_PLG_03_SCHEMA_GATE.md](../../reviews/OPT_06_PLG_03_SCHEMA_GATE.md).

### OPT-07: Plugin Project & Runtime Selection
- [x] Configured project descriptor (.axleproject) and dynamic plugin discovery.
- [x] Implemented clean separation between Editor-only tooling plugins and standalone Runtime plugins.
- [x] Packages PLG-15 through PLG-18 delivered and verified.
- *Evidence:* [docs/reviews/OPT_07_08_FOUNDATION_REVIEW.md](../../reviews/OPT_07_08_FOUNDATION_REVIEW.md).

### OPT-08: Content & Asset Management
- [x] Standardized AssetRegistry, Virtual File System (VFS), texture/mesh streaming foundations.
- [x] Embedded # AxleFileIdentity: 1 <UUID> in native assets, completely eliminating fragile .meta sidecar files.
- [x] Packages PLG-19 through PLG-20 delivered and verified.
- *Evidence:* [docs/reviews/OPT_07_08_FOUNDATION_REVIEW.md](../../reviews/OPT_07_08_FOUNDATION_REVIEW.md).

### OPT-09: Build & Standalone Packaging Cooker
- [x] Created standalone asset cooker compiling scene/asset trees into monolithic Data.axlepak archive.
- [x] Validated cooked runtime startup in AxleRuntime.exe.
- [x] Packages PLG-21 through PLG-23 delivered and verified.
- *Evidence:* [docs/reviews/OPT_09_IMPLEMENTATION.md](../../reviews/OPT_09_IMPLEMENTATION.md).

### OPT-10: Cross-Product Architecture Acceptance & Multi-Platform Enforcement
- **Delivered Deliverables:**
  - [x] Architecture enforcement script zero-baseline policy: all 8 historical allowances retired.
  - [x] Graphics-independent stress target: scheduler sizes 1/2/16, 2,048 Jolt bodies, parallel ECS mutation batches.
  - [x] Maintained Debug/Release acceptance runner with exact commands/logs.
  - [x] 73/73 CTest test cases passing on Debug configuration.
- **Open Acceptance Gates (Chưa hoàn thành):**
  - [ ] **Full Release Product Acceptance:** Complete Release build and test run of full Editor, Runtime, and test matrix.
  - [ ] **Detached Export Packaging Soak:** Long-running stress verification of detached asset export without timeout.
  - [ ] **Physical Interactive UI Acceptance:** Automated headless and interactive verification of UI docking and viewport interactions.
- *Evidence:* [docs/reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md](../../reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md).
