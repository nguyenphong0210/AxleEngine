# 🗺️ AxleEngine Development Roadmap & Milestone Tracker

Welcome to the **AxleEngine Development Roadmap**. This document tracks the complete architectural evolution of AxleEngine from its initial core foundation to a modern, modular, general-purpose 3D game engine.

---

## 📊 Roadmap Overview & Executive Summary

* **Engine Philosophy**: Modern C++20, Data-Oriented Design (EnTT ECS), Jolt Physics 3D, High-Performance Multi-threaded Job System (enkiTS), PBR Graphics (OpenGL 4.5 Core), Spatial Audio (miniaudio), Native C++ Scripting with Reflection & DLL Hot-Reloading, Prefab Assemblies, and WYSIWYG Editor tooling.
* **Completed Phases**: **Phase 1 through Phase 32** (100% implemented, verified, and stabilized).
* **Automated Test Verification**: **52 Comprehensive Headless Test Suites** (`AxleTests.exe`, 59 individual test steps) passing with **100% pass rate**.
* **Upcoming Focus**: **Phase 33 (Multi-Backend 3D Viewport Scene Renderer & PBR Forward+ Pipeline)** and beyond.

---

## 🏛️ Milestone Breakdown

```plaintext
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        AXLE ENGINE DEVELOPMENT MILESTONES                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ [x] MILESTONE 1: Foundations, Physics & Vehicle Prototype (Phases 1 - 4)               │
│ [x] MILESTONE 2: Simulation Lifecycle & Viewport Gizmos (Phases 5 - 7)                 │
│ [x] MILESTONE 3: Assets, Scene Serialization & Scripting Reflection (Phases 8 - 13)   │
│ [x] MILESTONE 4: Scene Graph, Hot-Reloading & Multithreaded Jobs (Phases 14 - 19)      │
│ [x] MILESTONE 5: Component-Driven Architecture & Standalone Runtime (Phases 20 - 23)   │
│ [x] MILESTONE 6: AAA PBR Lighting, Soft Shadows, IBL & Audio (Phases 24 - 28)          │
│ [x] MILESTONE 7: Large-Scale Terrain, Splines & Character Systems (Phases 29 - 30)     │
│ [ ] MILESTONE 8: Visual Effects, Modern RHI & Cross-Platform Engine (Phases 31+)      │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 📋 Detailed Phase Breakdown

### Milestone 1: Foundations & Physics Integration
- [x] **Phase 1: UI & Telemetry Overlay (`Engine/UI`)**
  - [x] Integrate Dear ImGui (docking branch) as a submodule.
  - [x] Encapsulate `ImGuiLayer` (lifecycle, modern dark theme, multi-viewport support).
  - [x] Implement `DebugPanel` to display real-time FPS, delta time, and camera transform.
- [x] **Phase 2: Physics Engine Foundation (`Engine/Physics`)**
  - [x] Integrate Jolt Physics submodule into `ThirdParty/JoltPhysics` (v5.2.0).
  - [x] Configure CMake build options for Jolt Physics and `Axle::Physics`.
  - [x] Implement `PhysicsSystem` & `PhysicsScene` (multithreaded job system, gravity, collision layers).
  - [x] Simulate static ground plane, dynamic falling box, and bouncy sphere.
  - [x] Integrate `JPH::DebugRenderer` (`PhysicsDebugRenderer`) to draw collider wireframes in OpenGL.
- [x] **Phase 3: Scene & Transform Synchronization (`Engine/Scene`)**
  - [x] Integrate header-only ECS library (EnTT v3.14.0) via submodule in `ThirdParty/entt`.
  - [x] Define `TagComponent`, `TransformComponent`, `MeshInstance3D` (Godot-style, alias `MeshComponent`), `RigidBody3D` (Godot-style body dynamics), and `CollisionShape3D` (Godot-style decoupled collider geometry with Box, Sphere, Cylinder, Cone, Plane, Mesh shapes).
  - [x] Implement `Scene::OnUpdate()` to automatically bind Jolt physics transforms to render meshes.
- [x] **Phase 4: Automotive Mechanics (`Engine/Vehicle`)**
  - [x] Implement `Vehicle` utilizing Jolt Physics' `VehicleConstraint` and `WheeledVehicleController`.
  - [x] Configure 4-wheel suspension springs (frequency & damping), anti-roll bars, and tire slip models.
  - [x] Build powertrain model (engine torque curve, automatic transmission, differentials, throttle & brake).
  - [x] Wire vehicle telemetry (speedometer, tachometer progress bar, gear indicator, 4-wheel contact status) and smooth chase camera into ImGui and Sandbox.

---

### Milestone 2: Simulation Lifecycle & Viewport Tooling
- [x] **Phase 5: Editor Ecosystem & Simulation Lifecycle (`Engine/UI` & `Engine/Renderer`)**
  - [x] Offscreen Viewport with Framebuffer (OpenGL FBO, auto-resize, aspect ratio handling).
  - [x] 3-State Simulation Lifecycle: Edit (Stop), Play, Pause, Step 1 Frame (1/60s), Live Speed Scaling (0.1x - 2.0x).
  - [x] Fixed Top Control Toolbar & Global Main Menu Bar.
  - [x] Scene Hierarchy Panel (Entity tree, create/delete entity).
  - [x] Properties / Inspector Panel (Tag rename, Transform X/Y/Z controls, Mesh & Material color/pattern, RigidBody3D with Motion Type & Damping, CollisionShape3D with Shape Type & dimensions live tweaking, Sleeping controls).
  - [x] Vehicle Live Tuner Panel (springs, dampers, steering, torque, 4-wheel telemetry table).
  - [x] Engine Console Panel (spdlog integration, log levels, search & auto-scroll).
  - [x] Procedural Primitive Meshes: Plane, Cube, Sphere, Cylinder, Cone with procedural shader patterns.
- [x] **Phase 6: Interactive 3D Viewport & Transform Gizmos (`Engine/UI` & `Sandbox`)**
  - [x] **3D Transform Gizmo (ImGuizmo)**: Manipulate Entity Translation (`W`), Rotation (`E`), and Scale (`R`) directly inside the 3D Viewport with interactive coordinate arrows and real-time sync with Jolt Physics.
  - [x] **Viewport Mouse Picking**: Raycast query directly on 3D physics meshes in the viewport to select them in the Hierarchy & Inspector, with background click-to-deselect.
  - [x] **Gizmo Controls Toolbar & Snapping**: Dedicated toolbar buttons for Select (`Q`), Translate (`W`), Rotate (`E`), Scale (`R`), Local/World coordinate system toggle, and adjustable distance/angle snapping (`0.5m` / `45°`).
  - [x] **Grid & World Coordinates**: Render dynamic infinite metric grid floor using `ImGuizmo::DrawGrid` in Edit mode.
- [x] **Phase 7: Automotive Test Playground & Obstacle Course (`Engine/Physics` & `Sandbox`)**
  - [x] **Jump Ramps & Inclines**: Multi-height ramps (Runway Launch Ramp, Landing Pad, Stunt Mega Ramp) testing airborne trajectories, flight pitch, and suspension landing cushion.
  - [x] **Washboard & Bumps Track**: 10 transverse rhythmic rumble bumps along test lane causing rapid suspension oscillation, spring frequency, and damper rebound.
  - [x] **Banked Curved Track**: Multi-segment high-speed camber turn (22° inward banking) testing centrifugal road holding and lateral tire slip.
  - [x] **Dynamic Destruction / Crash Barriers**: Destructible 10-crate wooden pyramid, 6 slalom agility cones, and 6 dynamic tire impact barriers with one-click full scene reset (`R`).

---

### Milestone 3: Asset Pipeline, Scene Serialization & Scripting
- [x] **Phase 8: C++ Native Scripting & Vehicle Component (`Engine/Scene`)**
  - [x] **VehicleComponent in ECS**: Encapsulated vehicle configuration and physics handle as an entity component.
  - [x] **`ScriptableEntity` Lifecycle**: Base C++ class exposing `OnCreate()`, `OnUpdate(Timestep)`, and `OnDestroy()`.
  - [x] **`NativeScriptComponent`**: Template-based script binding to entities with zero runtime overhead.
  - [x] **Modular Gameplay Scripts**: Decoupled vehicle input (`CarPlayerScript`) into clean, reusable C++ scripts.
  - [x] **Editor Component Management**: Interactive `+ Add Component` popup in Properties panel, component removal options (`Remove Component`), prominent `+ Create` button with Search filter in Scene Hierarchy, and File menu actions (`Ctrl+S`, `Ctrl+O`, `Ctrl+N`).
  - [x] **Window Work Area & Fullscreen**: Automatic maximized window startup matching OS work area (zero taskbar overlap) and `F11` Maximize/Restore toggle.
- [x] **Phase 9: Asset System & 3D Model Ingest Pipeline (`Engine/Asset` & `Sandbox/assets`)**
  - [x] **Project Asset Architecture**: Standardized `Sandbox/assets/` structure (`models/`, `textures/`, `scenes/`, `materials/`, `shaders/`) and `Axle::FileSystem` resolver - separating game project assets from engine core.
  - [x] **Content Browser Panel (`Engine/UI`)**: ImGui-based visual asset browser with directory navigation, file type icons, search filter, path breadcrumbs, Explorer integration, and Drag & Drop payload support.
  - [x] **Drag & Drop Workflow**: Drag 3D model files from Content Browser directly into Viewport or Properties panel to instantiate meshes with zero-gap ground snapping and centered pivots.
  - [x] **3D Asset Thumbnails & Interactive Asset Preview (`Engine/UI`)**: On-demand studio-lit 3D thumbnails rendered directly on Content Browser cards with frame-sliced caching, plus a dedicated interactive 3D Asset Preview window with turntable auto-orbit, wireframe inspection, geometry stats, and direct scene instantiation.
  - [x] **Unified `.axle` Asset Pipeline & Auto-Conversion**:
    - [x] **Step 1: Core Engine/Asset Module & AssetSerializer**: Structured text format `.axle` (AI-friendly & human-editable) + Automated flat binary caching `.axlebin` (<1ms GPU loading) with smart timestamp validation (`last_write_time`) for automatic re-baking when text is modified. Seamless drag & drop support for `.axle` assets into Viewport, Scene Hierarchy, and Mesh Inspector.
    - [x] **Step 2: Ingest & Auto-Conversion Pipeline**: Dragging external 3D files (`.obj`, `.gltf`, `.glb`, `.fbx`) directly from Windows Explorer into Content Browser or Viewport automatically ingests and converts them into engine-standard `.axle` format with instant background `.axlebin` cache generation and seamless scene instantiation.
    - [x] **Step 3: AI Mesh Mutation & Hot-Reload**: AI agents and developers can edit `.axle` files as text; the Engine features a dedicated Editor Compiler button (`F9`), auto-compile before simulation Play (`F5`), and instant GPU VertexArray and material hot-reloading in real time without restart.
    - [x] **Step 4: Reverse Mesh Export (`.obj`)**: Export `.axle` / `.axlebin` meshes back into standard Wavefront `.obj` format with full material libraries (`.mtl`), grouped subparts, and smooth vertex normals for external DCC (Blender, Maya) interop and mesh inspection via right-click context menu.
- [x] **Phase 10: Scene Serialization & Project Management (`Engine/Scene` & `Engine/Project`)**
  - [x] **Step 1: Core SceneSerializer (`.axlescene`)**: Built-in structured YAML-like scene serializer saving and restoring complete ECS scene state (UUIDs, TagComponents, TransformComponents, `MeshInstance3D` with builtin/custom models, Jolt `RigidBody3D` with decoupled `CollisionShape3D` shapes and backward-compatible auto-migration, HierarchyComponents parent-child links, and NativeScriptComponents; backward-compatible with legacy names).
  - [x] **Step 2: Interactive Save/Open Workflows**: Connect `Ctrl+S` (Save Scene), `Ctrl+Shift+S` (Save Scene As), `Ctrl+O` (Open Scene), `Ctrl+N` (New Scene) with native OS file dialogs (`Axle::FileDialogs`), dynamic window title indicator (`Axle Engine - Project - [SceneName.axlescene]`), and full Main Menu Bar integration (`File -> New / Open / Save / Save As / Exit`).
  - [x] **Step 3: Content Browser Scene Loading**: Double-click scene files (`.axlescene`) in Content Browser to instantly load them into the active Viewport, with right-click context menu ("Open Scene in Viewport"), dedicated Scene clapperboard icon (`SCN`), and drag-and-drop scene loading onto the Viewport.
  - [x] **Step 4: Project Configuration System (`.axleproj`)**: Project file storing project name, asset directory root, and start scene with `File -> New Project`, `File -> Open Project` (`Ctrl+Shift+O`), `File -> Save Project`, Content Browser `.axleproj` project blueprint icon (`PRJ`), and dynamic window title synchronization.
- [x] **Phase 11: Lighting, Dynamic Shadows & PBR Materials**
  - [x] **Directional Sun Light & Real-Time Shadow Mapping**:
    - [x] Depth Framebuffer (`GL_DEPTH_COMPONENT32F`, 2048x2048, border clamped), Light Space Matrix calculation, and `Scene::RenderShadowPass()`.
    - [x] Two-pass rendering pipeline with 3x3 Percentage-Closer Filtering (PCF) soft shadows and hardware polygon offset slope-scaled depth bias to eliminate shadow acne.
    - [x] ECS `DirectionalLightComponent`, Sun Light entity management, interactive Inspector controls (Sun Azimuth & Elevation sliders, Color picker, Intensity, Ambient fill, Cast Shadows toggle, Atmosphere presets), and full scene serialization.
  - [x] **Texture Mapping (`stb_image`)**:
    - [x] Standard `stb_image.h` integration, OpenGL 4.5 Core Profile `Axle::Texture2D` abstraction (PNG/JPG/TGA/BMP, RGBA8/RGB8/RG8/R8, auto mipmaps, configurable wrap/filter modes), with pre-baked 1x1 fallback textures.
    - [x] Upgraded Vertex Layout & UV coordinates (`a_TexCoord`) across Primitive Meshes and `.axle` loaders; Texture Sampler Slots (`u_AlbedoMap`, `u_ShadowMap`, `u_NormalMap`, `u_RoughnessMap`, `u_MetallicMap`) with screen-space cotangent-frame normal perturbation.
    - [x] `MaterialComponent` in ECS, Inspector UI with texture map slots, live thumbnail previews, and drag-and-drop assignment.
  - [x] **PBR Material System (Cook-Torrance Microfacet BRDF)**:
    - [x] Physically based Cook-Torrance microfacet specular BRDF (Trowbridge-Reitz GGX distribution, Smith-Schlick shadowing, Fresnel-Schlick reflectance), energy conservation, metallic workflow, and directional sun radiance integration.
    - [x] HDR Filmic Tone Mapping (ACES Narkowicz approximation, Reinhard, Uncharted 2 Hable, and Linear Bypass modes) & display gamma correction ($1/\gamma$), with real-time Exposure and Tone Mapping controls.
  - [x] **Atmospheric Skybox**:
    - [x] Procedural atmospheric Rayleigh / Mie scattering skybox pipeline (`Axle::Skybox`), camera-centric unit cube renderer at infinity, analytical sharp sun disk with anti-aliasing, sunset/sunrise golden horizon warming, night darkening with procedural twinkling stars.
    - [x] ECS `SkyboxComponent`, dynamic Sun Light orientation synchronization, interactive Inspector controls, and ambient hemisphere irradiance calculation.
- [x] **Phase 12: Assembly Pipeline & Unified `.axle` Prefab System (`Engine/Asset`, `Engine/Scene` & `Engine/UI`)**
  - [x] Standardize unified asset specification in `.axle` format with `AssetType: Assembly` header (YAML-like structured text, optimized for both human editing and AI prompt mutation).
  - [x] `AssemblySerializer::Serialize()` to recursively save complete entity hierarchies preserving all ECS components.
  - [x] `AssemblySerializer::Deserialize()` with automatic UUID Remapping to guarantee collision-free instantiation when spawning multiple assembly instances in the same scene.
  - [x] Drag & drop prefab workflows, dedicated `AssemblyEditorPanel` isolated 3D viewport, and scene instantiation.
- [x] **Phase 13: C++ Native Scripting Ecosystem & Mechanical Assembly Architecture (`Engine/Scene` & `Engine/UI`)**
  - [x] Core Static Reflection & Script Registry (`ScriptField` & `ScriptRegistry`) supporting `Float`, `Int`, `Bool`, `Vec3`, and `String` with min/max slider ranges and tooltips.
  - [x] Automated `AXLE_REGISTER_SCRIPT(ClassName)` macro registration.
  - [x] Equipping `Entity` and `ScriptableEntity` with hierarchy traversal helpers (`FindChildByTag`, `GetParent`, `GetChildren`, `ForEachChild`) utilizing `HierarchyComponent`.
  - [x] Editor Workflow: Right-click **`Create -> C++ Script (.hpp)`** in Content Browser with auto-generated boilerplate.

---

### Milestone 4: Scene Graph, Hot-Reloading & Multithreading
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

### Milestone 5: Component-Driven Architecture & Standalone Runtime
- [x] **Phase 20: Component-Driven Vehicle Creation from Scratch (`Engine/Vehicle`, `Engine/Scene` & `Engine/UI`)**
  - [x] **Phase 20.1: Decoupled Tags & Component-per-Wheel Architecture (N-Wheeled)**: Support arbitrary wheel counts (2-wheel bike, 4-wheel car, 6x6 truck, 8x8 APC) via `WheelComponent` (`VehicleWheel3D`) with differential torque splitting.
  - [x] **Phase 20.2: Vehicle Inspector Tuning & Modular Workflow**: Live telemetry cards, chassis mass, peak engine torque, and RPM limits.
  - [x] **Phase 20.3: Modern Unity ECS (`com.unity.vehicles`) Architecture & Full Scripting Decoupling**: Pure data container components, dynamic N-wheel discovery, and 100% neutral `EditorLayer`.
  - [x] **Phase 20.4: Editor Decoupling & Camera / Input Standardization**: Decoupled Game View & Eject Mode (`F2` key), `CarController.hpp` template, and `VehicleChaseCamera.hpp` dynamic script.
- [x] **Phase 21: Decoupled Scene Renderer Architecture (`Engine/Scene`)**
  - [x] Encapsulate full OpenGL rendering pipeline into `Axle::SceneRenderer`.
  - [x] Multi-pass pipeline: Shadow depth pass, PBR lighting pass, atmospheric skybox pass, and tonemapping/gamma post-processing.
  - [x] Clean separation of concerns between `EditorLayer` and rendering backend.
- [x] **Phase 22: Standalone Game Runtime Player (`AxleRuntime.exe`)**
  - [x] Standalone Executable Target (`Runtime/`): completely detached from `Axle::UI` (zero ImGui overhead for maximum FPS).
  - [x] Clean `RuntimeLayer` bootstrapping directly into `.axleproj` startup scenes.
  - [x] Game Controls & Windowing Ergonomics: Fullscreen toggle (`Alt+Enter` / `F11`), pause menu (`Escape`), and smooth exit handling.
- [x] **Phase 23: Asset Packaging & Cooking Pipeline (`.axlepak` & VFS)**
  - [x] Binary Package Specification (`.axlepak`) with `AXPK` magic header, TOC, and CRC32 checksums.
  - [x] In-Editor Asset Cooker & Project Exporter (`File -> Export Standalone Game...` and headless CLI `--export`).
  - [x] Virtual File System (VFS) in `Axle::FileSystem`: Mount and stream directly from `Data.axlepak` in runtime distribution mode.

---

### Milestone 6: High-Fidelity Rendering, PBR & Audio Synthesis
- [x] **Phase 24: Decoupled Material Asset System & Flyweight Architecture (`Engine/Renderer`, `Engine/Asset` & `Engine/Scene`)**
  - [x] Core `Axle::Material` Asset Class encapsulating PBR properties and texture maps.
  - [x] Centralized Asset Caching in `AssetManager` (`TextureCache` & `MaterialCache`) eliminating duplicate GPU texture allocations.
  - [x] Flyweight `MaterialComponent` in ECS storing 8-byte `UUID MaterialHandle` with `MakeUnique()` instance overrides.
- [x] **Phase 25: AAA Soft Shadows & Precision Lighting Pipeline (`Engine/Renderer`, `Engine/Scene` & `Engine/UI`)**
  - [x] Hardware Bilinear PCF (`GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE`).
  - [x] 16-Tap Vogel Spiral / Poisson Disk soft shadows with interleaved gradient noise.
  - [x] Dynamic Ortho Size, Shadow Distance, and Bias in `DirectionalLightComponent`.
- [x] **Phase 26: HDR Skybox Cubemap & Image-Based Lighting (IBL) Pipeline (`Engine/Renderer`, `Engine/Scene` & `Engine/UI`)**
  - [x] HDR Image Loader & Equirectangular-to-Cubemap Converter (`.hdr` / `.exr`).
  - [x] Image-Based Lighting (IBL) Convolution: Diffuse Irradiance Map, Specular Pre-filtered Environment Map, and 2D BRDF Integration LUT.
  - [x] Hyper-realistic metallic reflections on PBR materials matching surrounding environment.
- [x] **Phase 27: High-Fidelity 3D Audio System & Vehicle Acoustics (`Engine/Audio`)**
  - [x] Core Audio Subsystem (`miniaudio.h` integration): `AudioEngine`, `AudioClip`, `AudioSource`, `AudioListener`.
  - [x] ECS Audio Components: `AudioListenerComponent` and `AudioSourceComponent`.
  - [x] Vehicle Audio Synthesizer: Granular engine RPM synthesizer, tire squeal & slip acoustics, suspension travel thuds, and collision impact acoustics.
- [x] **Phase 28: Gameplay Polish, Multi-Part Vehicle Rigging & Dynamic HUD (`Engine/Vehicle`, `Engine/UI` & Content)**
  - [x] **Step 1 - Multi-Part 3D Car Rigging & Assembly Pipeline**: Modular chassis body mesh, 4 separate wheel meshes, visual kinematics (`AutoSyncMesh`), and dynamic lighting (headlights, brake lights, reverse lights).
  - [x] **Step 2 - Multi-Angle Dynamic Vehicle Camera Controller**: Spring-Arm Chase Camera, Cockpit First-Person Driver POV, Hood Camera, Free Orbit Camera, and smooth transition interpolation.
  - [x] **Step 3 - Real-Time Racing HUD & Dashboard Instrumentation**: Analog/circular Tachometer, Digital Speedometer (km/h & mph), Gear Indicator, G-Force meter, and input telemetry meters.
  - [x] **Step 4 - Scene Integration & Visual Polish**: Completed `CarTest.axlescene` and `VehicleSandbox.axlescene` automotive testing playground (Launch Ramp, Landing Pad, Washboard Bumps, Slalom Cones, Destructible Crates Pyramid), with Cabin Acoustic Insulation (Cockpit volume attenuation).
  - [x] **Step 5 - Automated Test Suite Verification**: Step 32 test in `Editor/src/Test.cpp` validating multi-part rigging, kinematics, lighting, camera modes, HUD rendering, cabin acoustics, and sandbox deserialization.
  - [x] **Step 6 - Runtime HUD Canvas & Public API Hardening (Fast-Follow)**:
    - [x] Deploy Direction A: In-game transparent HUD canvas overlay (`##GameHUDCanvas`) in `AxleRuntime.exe` via `Axle::ImGuiLayer`.
    - [x] Standardize `RacingHUD.hpp` on 100% public ImGui APIs (eliminate `#include <imgui_internal.h>` and private struct access).
    - [x] Fix input edge cases: Shift+F1 physics wireframe toggle guard, Esc cursor unlock vs scene stop frame consumption, and mouse scroll Y accumulation.

---

### Milestone 7: Large-Scale Terrain, Spline Tracks & Skeletal Character Systems (Phases 29 - 30)
- [x] **Phase 29: Modular Plugin Architecture — Spline Track & Large-Scale Terrain System (`Plugins/Spline` & `Plugins/Terrain`)**
  - [x] **Phase 29A: Spline Curve & Procedural Track Generator Plugin (`Plugins/Spline`)**
    - [x] **Step 1 - Plugin Infrastructure & CMake Integration**: Establish the modular static plugin standard under `Plugins/Spline/` with clean linkage into `AxleEditor`, `AxleRuntime`, and `AxleTests`.
    - [x] **Step 2 - 3D Curve Mathematics Core (`Curve3D`)**: Parametric curves (Catmull-Rom & Cubic Bézier), arc-length reparameterization (uniform speed sampling), and Rotation Minimizing Frame (RMF / Parallel Transport) to prevent 180° flipping on steep loops and banked turns.
    - [x] **Step 3 - `SplineComponent` & Procedural Track Extrusion**: Extrude custom 2D cross-sections (Asphalt road, curbs, rumble strips, guardrails) along 3D splines with variable width, roll/banking angles, and continuous texture UV mapping.
    - [x] **Step 4 - Physics Scene Integration**: Dynamically generate high-performance static collision meshes (`JPH::MeshShape`) via `Axle::Physics` so vehicles can drive, bank, and collide on the generated tracks.
    - [x] **Step 5 - Viewport Spline Gizmo & Editor Tooling**: Interactive Editor gizmos for selecting, adding, moving, and curving control points directly within the 3D Viewport.
    - [x] **Step 6 - Automated Test Suite 33 (`AxleTests.exe`)**: Unit tests for curve arc-length interpolation, tangent/normal frame continuity, track mesh triangle generation, and serialization.
  - [x] **Phase 29B: Off-Road Terrain & Sculpting System Plugin (`Plugins/Terrain`)**
    - [x] **Step 1 - Plugin Setup & Heightfield Physics Engine**: Establish `Plugins/Terrain/` and integrate high-performance `JPH::HeightFieldShape` for realistic off-road vehicle suspension travel and tire damping on irregular soil.
    - [x] **Step 2 - Multithreaded Chunked Grid & LOD (`enkiTS`)**: Divide large terrains into hierarchical chunks with distance-based Level of Detail (LOD), dynamically generated without hitching via `enkiTS` worker threads.
    - [x] **Step 3 - Interactive Sculpting Tools (Editor)**: Viewport circular brushes (Raise, Lower, Smooth, Flatten) with real-time vertex buffer deforming and asynchronous Jolt physics shape re-baking.
    - [x] **Step 4 - Multi-Layer Splatmap & Triplanar Shaders**: Dynamic multi-texture blending (Grass, Dirt, Gravel, Mud, Rock) with slope-based triplanar projection shaders and texture painting brush tools.
    - [x] **Step 5 - Spline Road Carving & Terrain Snapping**: One-way integration allowing the Terrain plugin to query `SplineComponent` paths and automatically carve/flatten terrain elevations directly beneath road ribbons.
    - [x] **Step 6 - Automated Test Suite 34 (`AxleTests.exe`)**: Unit tests for heightfield matrix generation, brush deformation math, LOD stitching, and terrain save/load serialization.
- [x] **Phase 30: Skeletal Animation & Character Controller System (`Engine/Animation`)**
  - [x] **Step 1 - Core Skeletal Data Structures & Asset Importer (`ufbx` & `cgltf`)**:
    - [x] Native bone hierarchy, joint indices, inverse bind matrices (`InverseBindPose`), and skeleton metadata.
    - [x] Animation track sampling data structures (`AnimationClip`, `AnimationChannel`, keyframe timestamps, translation/rotation/scale curves).
    - [x] Lightweight, zero-dependency asset parsing using embedded `ufbx` (`.fbx`) and `cgltf` (`.gltf`/`.glb`).
  - [x] **Step 2 - GPU Vertex Skinning & Shader Pipeline (`Engine/Renderer`)**:
    - [x] Extended vertex layout: `a_BoneIDs` (`ivec4`) and `a_BoneWeights` (`vec4`) supporting up to 4 influencing bones per vertex (64-byte aligned stride).
    - [x] Dynamic bone matrix palette (`u_BoneMatrices[128]`) updated per animated draw call via `SetMat4Array` with identity pose fallback.
    - [x] Optimized Linear Blend Skinning (LBS) vertex shaders (`SkeletalMesh.vert` and `SkeletalShadowDepth.vert`) for PBR lighting pass and directional shadow depth pass.
    - [x] Dedicated `SkinningPipeline` decoupled renderer class integrated directly into `SceneRenderer`.
    - [x] Automated test suite Step 37 in `Editor/src/Test.cpp` verifying shader compilation, dynamic bone palette upload, 64-byte layout verification, and animated skinning dispatch.
  - [x] **Step 3 - Animation Pose Evaluator & Blending Tree (`Engine/Animation`)**:
    - [x] Runtime track sampling with shortest-path spherical linear interpolation (`glm::slerp`) for quaternions and linear interpolation (`glm::mix`) for translation/scale via `BoneTransform` & `AnimationPose`.
    - [x] Hierarchical forward kinematics solving global bone transform matrices and GPU skinning palettes from local bone transforms.
    - [x] 1D Blend Spaces (`BlendSpace1D`): Smooth locomotion interpolation (Idle $\leftrightarrow$ Walk $\leftrightarrow$ Run) with phase-matched footstep time synchronization.
    - [x] Hierarchical Animation State Machine (`AnimationStateMachine`): Clip & BlendSpace states, parameter control (`Speed`, `IsGrounded`, `VerticalVelocity`, triggers), smooth cross-fading, exit times, and state transitions (Locomotion $\leftrightarrow$ Jump $\leftrightarrow$ Fall $\leftrightarrow$ Land).
    - [x] High-level runtime `Animator` controller with direct GPU skinning palette upload to `SkinningPipeline`.
    - [x] Automated test suite Step 38 in `Editor/src/Test.cpp` verifying SLERP/LERP arithmetic, 1D blend spaces, crossfades, complete jump lifecycle, and GPU skinning dispatch.
  - [x] **Step 4 - Kinematic Character Controller (KCC) via Jolt Physics (`Engine/Physics`)**:
    - [x] Robust character physics integration using `JPH::CharacterVirtual` with capsule collision shape (`CharacterController`).
    - [x] Slope traversal handling (`MaxSlopeAngle`), automatic stair/curb stepping (`StepOffset`), and ground snapping (`StickToFloor`).
    - [x] Dynamic collision response with dynamic physics objects (pushback resistance and forward impulse transfer to rigid bodies).
    - [x] Automated test suite Step 39 in `Editor/src/Test.cpp` verifying freefall landing, kinematic movement & jump trajectory, stair stepping, steep wall blocking, and dynamic box pushing.
  - [x] **Step 5 - ECS Integration & Third-Person Gameplay Controller**:
    - [x] Decoupled ECS components: `SkeletalMeshComponent`, `AnimatorComponent`, and `CharacterControllerComponent` in `Engine/Scene`.
    - [x] Third-person player controller script (`ThirdPersonController.hpp`) supporting camera-relative directional movement, smooth yaw turning toward velocity, jump action, and automatic animator parameter synchronization (`Speed`, `IsGrounded`, `VerticalVelocity`).
    - [x] Third-person chase camera script (`ThirdPersonCamera.hpp`) with spring-arm orbit, mouse look, pitch clamping, target tracking, and height offset.
    - [x] Scene renderer integration: `Scene::RenderSkeletalMeshes` dispatches dynamic GPU bone palettes to both forward PBR lighting and shadow depth passes.
    - [x] Scene serialization & deserialization support for all 3 components in `SceneSerializer`.
    - [x] Automated test suite Step 40 in `Editor/src/Test.cpp` validating component attachment, kinematics, animator sync, camera-relative movement vectors, spring-arm camera, and full serialization fidelity.
  - [x] **Step 6 - Character Sandbox Playground & Scene Integration**:
    - [x] Interactive 3D scene (`CharacterSandbox.axlescene`) integrating animated skeletal mesh, third-person camera, stairs, slopes, and dynamic obstacles.
    - [x] Automated test suite Step 41 in `Editor/src/Test.cpp` validating full character sandbox scene, staircase ascent via `StepOffset`, 90-degree wall blocking, dynamic crate push, spring-arm follow, and multi-pass skeletal mesh rendering.
---

### Milestone 8: Visual Effects, Modern RHI & Cross-Platform Engine (Phases 31+)
- [x] **Phase 31: Advanced Particle & Visual Effects System (`Engine/VFX`)**
  - [x] **Step 1 - Core VFX Architecture, Particle Emitter Data & Memory Model (`Engine/VFX`)**:
    - [x] High-performance Data-Oriented Struct-of-Arrays (SoA) particle pool (`ParticlePool`) with pre-allocated contiguous memory to eliminate dynamic heap allocations.
    - [x] Particle lifecycle management (`ParticleSystem`): spawn rate, lifetime duration, position, velocity, acceleration, rotational speed, size-over-lifetime curve, and RGBA color/alpha curves.
    - [x] Emitter geometric shapes: Point, Sphere/Hemisphere, Box volume, Cone (for exhaust/burnout), and directional spray nozzles.
  - [x] **Step 2 - High-Performance GPU Instancing & Billboarding Pipeline (`Engine/Renderer`)**:
    - [x] Hardware instanced quad rendering (`glDrawElementsInstanced` / instance VBO layout) streaming compact instance data (`vec3 Position`, `float Size`, `vec4 Color`, `float Rotation`, `vec4 UVOffsetScale`).
    - [x] Dynamic billboarding orientations: Spherical Camera-Facing (standard smoke/dust), Cylindrical/Axial (Y-axis aligned for rising plumes), and Velocity-Aligned stretched quads (high-speed sparks, rain drops, debris).
    - [x] Multi-mode blending: Additive (fire, embers, energy sparks), Alpha Blending (dense smoke, fog, clouds), and Premultiplied Alpha.
    - [x] Soft Particles: Sample linear depth from `u_SceneDepthBuffer` to compute depth-difference fade factor, eliminating hard intersection edges where particles clip into terrain and geometry.
  - [x] **Step 3 - Advanced Simulation Dynamics: Curl Noise, Physics Collisions & Fields (`Engine/VFX`)**:
    - [x] 3D Simplex / Curl Noise vector fields providing divergence-free fluid turbulence for organic smoke billows, flame wisps, and floating dust motes.
    - [x] Particle physics collisions: static ground plane and raycast collision detection with restitution (bounciness damping) and tangential friction.
    - [x] Environmental forces: directional wind vector, buoyancy/updraft, drag/air resistance, and point gravity attractors/vortices.
  - [x] **Step 4 - Texture Sheet Flipbooks & Shading Polish (`Engine/Renderer` & `Engine/VFX`)**:
    - [x] Texture sheet / flipbook animations ($N \times M$ grid) with automatic frame progression and smooth sub-frame interpolation over particle lifetime.
    - [x] Normal-mapped particle lighting: approximate volumetric lighting from directional sun and point lights with self-shadowing factor.
    - [x] Dedicated VFX shaders (`Particle.vert` and `Particle.frag`) with smooth softness curve, distance fading, and camera near-plane fade.
  - [x] **Step 5 - ECS Integration & Gameplay Event VFX Emitters (`Engine/Scene`)**:
    - [x] `ParticleEmitterComponent` in EnTT ECS: linking emitter assets to scene entities with local/world simulation space toggles.
    - [x] Vehicle gameplay FX hooks:
      - [x] Tire burnout smoke and skid debris generated dynamically based on lateral/longitudinal tire slip.
      - [x] Off-road dirt/gravel rooster tails kicked up dynamically based on speed and terrain surface splatmap material.
      - [x] Exhaust backfire flames and popping sparks triggered by high engine RPM and throttle lift-off.
    - [x] Character gameplay FX hooks:
      - [x] Footstep ground impact dust puffs on walking/running.
      - [x] Landing shockwave dust ripples upon landing from jump/freefall.
    - [x] Scene serialization & deserialization support for `ParticleEmitterComponent` in `SceneSerializer`.
  - [x] **Step 6 - Visual FX Editor Panel & Final VFX Integration**:
    - [x] Dedicated ImGui VFX Editor panel: live bezier curve tweaking (size, alpha, color gradient over time), texture slot assignment, burst controls, and instant viewport preview.
    - [x] Automated test suites (Steps 42-47) in `Editor/src/Test.cpp`: validating particle pool memory allocation, GPU instancing, curl noise determinism, physics collisions, emitter serialization fidelity, and gameplay slip event triggering.
- [x] **Phase 32: Modern RHI Abstraction & Cross-Platform Engine (NVIDIA NRI — Vulkan 1.3 & Direct3D 12)**
  - [x] **Step 1 - NRI Core Submodule & RHI Device Lifecycle (`Engine/RHI`)**:
    - [x] Integrate lightweight NVIDIA Rendering Interface (MIT) into `ThirdParty/NRI` (Core, Helper, and Streamer modules).
    - [x] Unified Device Abstraction: Physical GPU enumeration, hardware capability query (Ray Tracing, Mesh Shaders, Bindless Resources), and dynamic backend selection (Direct3D 12, Vulkan 1.3, with OpenGL 4.5 legacy fallback).
    - [x] Native Windowing & Surface Integration: Bind GLFW window handles to native surfaces (`HWND` for D3D12 on Windows / `VkSurfaceKHR` for Vulkan).
  - [x] **Step 2 - Swapchain, Command Allocators & Frames-in-Flight Orchestration**:
    - [x] High-performance double/triple buffered swapchain (`nri::SwapChainDesc`) with configurable presentation modes (VSync vs Low-Latency Mailbox).
    - [x] Frame Synchronization Engine: 2-3 Frames-in-Flight architecture with per-frame Command Allocators, Fences, and Semaphores to eliminate CPU stalls and prevent GPU read/write hazards.
    - [x] Dynamic window resize handling, minimized window guard, and seamless borderless/exclusive fullscreen toggle without resource leaks.
  - [x] **Step 3 - Unified Cross-API Shader Pipeline & Bytecode Toolchain**:
    - [x] Cross-API shader compilation pipeline (Slang or DXC + `glslang`/SPIRV-Cross) compiling single-source shaders into SPIR-V (Vulkan 1.3) and DXIL/DXBC (Direct3D 12).
    - [x] Automated bytecode caching (`.spv` and `.dxil`) with `last_write_time` dependency validation in asset directory.
    - [x] Shader Reflection Subsystem: Automatically extract Root Signatures (D3D12) and Descriptor Set Layouts (Vulkan) directly from compiled shader reflection metadata.
  - [x] **Step 4 - Pipeline State Objects (PSO) & Explicit Barrier Management**:
    - [x] 64-bit PSO hashing and LRU memory cache for Graphics and Compute pipelines (Rasterizer, Depth/Stencil, Blend States, Shaders).
    - [x] Explicit resource layout transitions (D3D12 `ResourceBarrier` / Vulkan `VkImageMemoryBarrier`) via unified NRI command lists (Color Attachment $\leftrightarrow$ Shader Resource $\leftrightarrow$ Depth/Stencil Write).
    - [x] Unified Pipeline Layout abstraction supporting Push Constants, Root Descriptors, and Descriptor Tables.
  - [x] **Step 5 - GPU Memory Management & Dynamic Staging Streamer**:
    - [x] Device-local VRAM allocation vs Host-visible Staging Ring Buffer management utilizing `NRIHelper` and `StreamerInterface`.
    - [x] High-throughput asynchronous texture streaming and non-blocking vertex/index buffer uploads.
    - [x] Direct EnTT ECS memory streaming: packed component data streamed into GPU dynamic buffers and structured buffers (SSBOs) with GPU indirect draw dispatch (`CmdDrawIndexedIndirect`).
  - [x] **Step 6 - Editor ImGui Viewport Integration & Multi-Backend Coexistence**:
    - [x] Multi-backend ImGui rendering: equip `ImGuiLayer` with `imgui_impl_dx12` and `imgui_impl_vulkan` backends alongside legacy `imgui_impl_opengl3`.
    - [x] Viewport Descriptor Binding: Register offscreen scene render targets (`nri::Texture`) into ImGui GPU Descriptor Heap as valid `ImTextureID`.
    - [x] Engine RHI Switcher: Configurable startup flag (`--rhi=dx12`, `--rhi=vulkan`, `--rhi=opengl`) with graceful fallback.
    - [x] Native D3D12 Swapchain Presentation: Direct backbuffer presentation (`IDXGISwapChain3`, 3 backbuffers flip-discard) resolving unpainted OS window canvas.
  - [x] **Step 7 - Automated Headless Test Suite & Multi-Backend Validation**:
    - [x] Headless RHI test suite in `AxleTests.exe`: Device initialization (WARP for D3D12, SwiftShader/Mock ICD for Vulkan).
    - [x] Multithreaded command recording verification: worker threads via `enkiTS` recording into secondary command buffers concurrently.
    - [x] Validation of pipeline barrier state machines, PSO cache hit rate, and swapchain recreation stability.
- [ ] **Phase 33: Multi-Backend 3D Viewport Scene Renderer & PBR Forward+ Pipeline (`Engine/Renderer` & `Engine/RHI`)**
  - [ ] **Step 1 - Core Geometry, Mesh VRAM Sub-Allocation & Dynamic ECS Transforms**:
    - [x] Stream 3D scene geometry (SportsCar chassis OBJ/axlemesh, racing wheels, primitive shapes, and procedural terrain heightfield) into GPU-resident `RHIBuffer` vertex/index buffers via `RHIStreamer`.
    - [x] Comprehensive vertex layouts: Position (`vec3`), Normal (`vec3`), Tangent/Bitangent (`vec4`), UV Coordinates (`vec2`), Color (`vec4`), and Skeletal Joint Weights/IDs (`vec4`/`uvec4`).
    - [ ] High-performance transform streaming: Bind Camera View-Projection matrices, Camera World Position, and Per-Entity Model Transforms dynamically via Push Constants / Root Descriptors without CPU-GPU pipeline stalls.
  - [ ] **Step 2 - PBR Materials, Bindless Textures & Multi-Light Subsystem**:
    - [ ] Industry-standard Metallic-Roughness PBR pipeline: Albedo/BaseColor, Normal (MikkTSpace tangent space), Roughness, Metallic, Ambient Occlusion (AO), and Emissive maps.
    - [ ] Bindless Texture Descriptors & Anisotropic Filtering: Tier 2 descriptor indexing (`nri::DescriptorPool`), Anisotropic 16x filtering, and trilinear mipmap sampling across all material slots.
    - [ ] Forward+ Multi-Light GPU Buffer: 1 Primary Directional Sun Light with physical illuminance (Lux), up to 16 Point Lights (vehicle brake/reverse lights, environment lamps) with inverse-square falloff, and up to 16 Spot Lights (vehicle dual headlights, high-beams) with inner/outer cone cutoff angles.
  - [ ] **Step 3 - Cascaded Shadow Mapping (CSM) & Atmospheric Skybox / IBL Passes**:
    - [ ] Real-Time Directional Shadow Mapping (1024x1024 to 4096x4096) with Percentage-Closer Filtering (PCF 5x5) and slope-scaled depth bias to eliminate shadow acne and light bleeding.
    - [ ] Image-Based Lighting (IBL) & Environment Maps: Pre-filtered environment radiance cubemap, diffuse irradiance cubemap, and split-sum BRDF LUT integration for realistic automotive metallic sheen.
    - [ ] Procedural Atmospheric Skybox Pass: Rayleigh & Mie atmospheric scattering simulation with dynamic sun elevation/azimuth synchronized with directional sun light vector.
  - [ ] **Step 4 - Post-Processing, HDR Tone Mapping & Anti-Aliasing (FXAA/MSAA)**:
    - [ ] Floating-point offscreen HDR render target (`RGBA16_FLOAT` / `RGBA8_UNORM`) with depth-stencil (`D32_FLOAT`).
    - [ ] Exposure Control & Film-Grade Tone Mapping: ACES Filmic (Narkowicz fit), Reinhard, and Uncharted 2 curves with adjustable exposure compensation and gamma correction (sRGB EOTF).
    - [ ] Anti-Aliasing Integration: Fast Approximate Anti-Aliasing (FXAA) and hardware MSAA (2x/4x/8x) to eliminate jagged specular edges and aliased wireframes.
  - [ ] **Step 5 - Secondary Visual Passes (GPU Particle VFX, Physics Wireframes & GPU Object Picking)**:
    - [ ] GPU Instanced Particle VFX Pass: Port `ParticleRenderer` (Phase 31) to RHI instanced draw commands (`CmdDraw`), rendering tire smoke, drift dust, exhaust backfire flames, and ground impacts directly in the D3D12/Vulkan viewport.
    - [ ] Jolt Physics Debug Wireframe Pass: Line-topology graphics pipeline rendering collision shapes, wheel suspension raycasts, and chassis bounding boxes when toggling F1 / `ShowPhysicsDebug`.
    - [ ] Interactive Viewport GPU Object ID Picking: Secondary integer render target (`R32_UINT`) encoding Entity IDs per pixel, enabling instantaneous single-click entity selection in Viewport on DX12, Vulkan, and OpenGL without CPU raycast overhead.
  - [ ] **Step 6 - Triple-Backend Seamless Switching & Automated Verification (100% Visual Parity)**:
    - [ ] Seamless CLI switcher verification: `--rhi=dx12`, `--rhi=vulkan`, and `--rhi=opengl` producing pixel-accurate scene renders, lighting, shadows, and physics interactions.
    - [ ] Automated test suites (Step 64) in `AxleTests.exe` validating mesh upload bandwidth, draw call dispatch rates, descriptor recycling, and zero memory leaks upon scene reload.
- [ ] **Phase 34: Dedicated 2D Canvas & In-Game UI Subsystem (`Engine/UI` & `Engine/Renderer`) [Direction B]**
  - [ ] **Hardware-Accelerated 2D Batch Renderer (`Canvas2D`)**: High-performance sprite, quad, and line batch rendering with z-index sorting and clipping rectangles.
  - [ ] **Zero-ImGui Shipping Runtime**: Pure decoupled UI pipeline for commercial game releases without ImGui library linkage.
  - [ ] **Font Glyphs & Distance Fields (MSDF)**: Dynamic FreeType and Multi-channel Signed Distance Field font rendering with sharp scaling across resolutions.
  - [ ] **Anchor & Flex Layout Engine**: Responsive anchor anchors (TopLeft, Center, Stretch, Margins), UI Containers, and automatic DPI scaling.
  - [ ] **UI Event Dispatching**: First-class support for `OnClick`, `OnHover`, `OnFocus`, and gamepad UI navigation.
  - [ ] **In-Game HUD Migration**: Seamlessly port `RacingHUD` and in-game dashboards to the native `Canvas2D` subsystem.

---
