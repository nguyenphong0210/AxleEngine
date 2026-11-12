# Milestone 04 — Scene Graph, Hot-Reloading & Multithreading Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [x] **Phase 14: Core ECS & Relationship Hierarchy (Parent - Child & Scene Graph)**
  - [x] Automatic attachment of `IDComponent` (64-bit UUID), `TagComponent`, `TransformComponent`, and `RelationshipComponent` across all entities.
  - [x] Scene Graph Matrix Propagation: $\text{WorldMatrix}_{\text{Child}} = \text{WorldMatrix}_{\text{Parent}} \times \text{LocalMatrix}_{\text{Child}}$.
  - [x] World Position Stays calculation with ancestor cycle detection.
- [x] **Phase 15: Scene Hierarchy Panel Upgrade (Presets, Tree UI & Drag-and-Drop Reparenting)**
  - [x] Comprehensive Archetype Presets (`Cube`, `Sphere`, `Cylinder`, `Lights`, `Camera`, `Skybox`).
  - [x] Hierarchical Tree View with collapsible arrow nodes.
  - [x] Visual Drag & Drop Reparenting (`"SCENE_HIERARCHY_ENTITY"`).
  - [x] Deferred & Recursive Lifecycle (`m_EntitiesToDestroy`) and deep recursive duplication (`Ctrl+D`).
- [x] **Phase 16: Properties Panel & C++ Scripting Reflection (Category Component Adder & Macro Fields)**
  - [x] Structured Component Widgets with collapsible headers and `DrawVec3Control`.
  - [x] Categorized `+ Add Component` Popup (Rendering, Physics, Scripts, Audio/VFX).
  - [x] C++ Script Reflection Macro (`AXLE_SCRIPT_FIELDS`) supporting `Float`, `Int`, `Bool`, `Vec3`, `String`, and `Entity Reference` (UUID).
  - [x] Dual-Context Properties Inspector (Entity inspection vs Asset inspection).
- [x] **Phase 17: Content Browser & Material/Asset System (.meta & Physical Asset Creation)**
  - [x] Asset UUID & `.meta` File Pipeline with persistent 64-bit UUIDs.
  - [x] Right-Click Asset Creation (`New Folder`, `Scene`, `Material Asset`, `C++ Script`, `Physics Material`).
  - [x] Dedicated Material Inspector for `.amat` / `.axlemat`.
- [x] **Phase 18: Integrated Drag & Drop, Safe Simulation & In-Editor Live Coding (Hot-Reload DLL)**
  - [x] Full 3-Flow ImGui Drag & Drop (`"CONTENT_BROWSER_ITEM"` and `"SCENE_HIERARCHY_ENTITY"`).
  - [x] Safe Scene State Simulation (Play / Stop Mode) with deep ephemeral scene cloning.
  - [x] In-Editor C++ Script Hot-Reload & Live Coding: Decoupled `AxleScripts.dll` shared library, anti-lock timestamped shadow-copying (`AxleScripts_Runtime_<timestamp>.dll`), async background compiler (`ScriptCompiler`), and `[🔨 Compile] (F9 / Ctrl+B)` toolbar button.
- [x] **Phase 19: Multithreaded Job System & enkiTS Integration (`Engine/Core` & `Engine/Physics`)**
  - [x] Embedded `enkiTS` (Enki Task Scheduler) in `ThirdParty/enkiTS`.
  - [x] Core JobSystem Engine (`Axle::JobSystem`) managing lock-free worker threads matching CPU hardware concurrency.
  - [x] Custom `JPH::JobSystem` backend (`JobSystemEnkiTS`) routing Jolt Physics constraint solving and collision tasks into enkiTS.
  - [x] Parallel ECS Processing (`ParallelECS::ForEach`) distributing bulk entity iteration across worker threads.

---
