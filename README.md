# AxleEngine

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.5%20Core-green.svg)](https://www.opengl.org/)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-orange.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)]()

**AxleEngine** is a modern, modular 3D game engine written in **C++20**, focused on realistic **Vehicle Physics Simulation** powered by **Jolt Physics**.

The engine adheres to strict modular design principles, zero raw OpenGL exposure outside the renderer, and modern RAII resource management.

---

## 🏛️ Architecture Stack

AxleEngine is organized into a strictly layered architecture to decouple graphics, physics, UI, and vehicle gameplay logic:

```plaintext
Level 4:  [ Sandbox / Game Client ]
Level 3:  [ Engine/Scene ]            [ Engine/Vehicle ]
Level 2:  [ Engine/Physics ]          [ Engine/Renderer ] [ Engine/Asset ]    [ Engine/UI ]
Level 1:  [ Engine/Platform ]
Level 0:  [ Engine/Core ]
```

### Module Breakdown:
- **Level 0 - `Engine/Core`**: Application lifecycle orchestrator (`Axle::Application`, `Axle::Layer`, `Axle::LayerStack`), primitive typedefs (`Axle::f32`, `Axle::u32`), lock-free multithreaded Job System via **enkiTS** (`Axle::JobSystem`), high-performance multithreaded logging via **spdlog** (`Axle::Log`, `AXLE_CORE_*`), frame timing (`Axle::Timestep`), and debugging assertion macros.
- **Level 1 - `Engine/Platform`**: Window lifecycle, OpenGL context creation, event dispatching, and static input polling (`Axle::Input`) via **GLFW**.
- **Level 2 - Core Subsystems**:
  - **`Engine/Renderer`**: Complete OpenGL 4.5 Core Profile abstraction (Buffers, Vertex Arrays, Shaders with uniform caches, Perspective Camera & Free-Fly Controller).
  - **`Engine/UI`**: Dedicated **Dear ImGui** (docking-enabled) integration with telemetry overlay (`DebugPanel`) for FPS, frametime, and camera statistics.
  - **`Engine/Physics`**: Multithreaded physics simulation bridge using **Jolt Physics** (JobSystem, BroadPhase, RigidBodies, collision math bridge).
  - **`Engine/Asset`**: 3D mesh loaders (`.gltf`/`.obj`) and image texture loading into GPU memory.
- **Level 3 - High-Level Systems**:
  - **`Engine/Scene`**: Entity Component System (ECS) synchronizing Jolt physics bodies with renderer transforms.
  - **`Engine/Vehicle`**: Specialized vehicle dynamics (Ackermann steering, suspension spring dampening, tire slip, engine torque curve, and transmission).
- **Level 4 - `Sandbox`**: Client applications demonstrating simulation scenarios.

---

## ⚙️ C++ Native Scripting: ECS Component Attachment & Hierarchy Traversal

AxleEngine adopts a high-performance **Hybrid ECS / DOD Scripting Architecture** designed specifically for physics-driven mechanical sandbox games and vehicle simulations:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              AXLE ENGINE SCRIPTING STACK                               │
├───────────────────────────────────┬────────────────────────────────────────────────────┤
│    LAYER A: MACHINE CONTROLLERS   │            LAYER B: MECHANICAL SYSTEMS             │
│      (OOP - ScriptableEntity)     │            (DOD - Pure EnTT Component)             │
├───────────────────────────────────┼────────────────────────────────────────────────────┤
│ • Low entity count (1 - 10)       │ • Massive bulk entity count (100 - 50,000 parts)   │
│ • Vehicles, Player Camera, Turrets│ • Mechanical Hinges, Pistons, Rotors, Welds        │
│ • Intuitive OnCreate / OnUpdate   │ • Batch memory iteration via registry.view().each()│
│ • Unity-like Component Attachment │ • 100% Cache-friendly, Zero vtable, Maximum SIMD   │
└───────────────────────────────────┴────────────────────────────────────────────────────┘
```

### 1. The 4-Step Closed-Loop Editor Workflow
AxleEngine brings the seamless ergonomics of commercial engines to high-performance native C++:

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                          CLOSED-LOOP SCRIPTING WORKFLOW                               │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ STEP 1: CREATE SCRIPT TEMPLATE                                                        │
│ • Content Browser: Right-Click -> Create -> C++ Script (.hpp)                         │
│ • Auto-generates clean boilerplate inheriting ScriptableEntity with reflection        │
│                                           ▼                                           │
│ STEP 2: DRAG & DROP ATTACHMENT                                                        │
│ • Drag TankController.hpp from Content Browser directly onto Entity in Viewport/Tree   │
│ • Automatically attaches NativeScriptComponent and binds script metadata              │
│                                           ▼                                           │
│ STEP 3: LIVE INSPECTOR TUNING                                                         │
│ • Properties panel dynamically generates sliders, checkboxes, and Vec3 widgets       │
│ • Live parameter edits (Speed, Torque, Angle) are saved in component edit buffer      │
│                                           ▼                                           │
│ STEP 4: PACKAGE INTO UNIFIED `.axle` PREFAB                                           │
│ • Click "Save Assembly" -> packages meshes, physics colliders, joints, and scripts    │
│ • Drag Tank.axle into ANY scene -> fully functional machine drives immediately!       │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

### 2. Reference Script Implementation (`TankController.hpp`)
```cpp
#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/Input/Input.hpp>

class TankController : public Axle::ScriptableEntity {
public:
    // Exposed live-tunable fields
    float MoveSpeed = 12.0f;
    float TurretRotationSpeed = 45.0f;
    bool EnableNitro = false;

    // Cached child part entity handles (O(1) access)
    Axle::Entity m_Turret;
    Axle::Entity m_Barrel;
    Axle::Entity m_WheelFL;

    void OnCreate() override {
        // Hierarchy Traversal: resolve once at spawn time
        m_Turret  = FindChildByTag("Turret");
        m_Barrel  = FindChildByTag("Barrel");
        m_WheelFL = FindChildByTag("Wheel_FL");
    }

    void OnUpdate(Axle::Timestep ts) override {
        // O(1) Instant EnTT component query without string lookups
        if (m_Turret && Axle::Input::IsKeyPressed(Axle::KeyCode::Left)) {
            auto& tc = m_Turret.GetComponent<Axle::TransformComponent>();
            tc.Rotation = glm::rotate(tc.Rotation, glm::radians(TurretRotationSpeed * ts), glm::vec3(0, 1, 0));
        }
    }

    // Static Field Reflection (auto-generates Editor UI controls)
    static void RegisterFields(Axle::ScriptFieldRegistry& reg) {
        reg.AddField("MoveSpeed", &TankController::MoveSpeed, 1.0f, 50.0f, "Maximum forward drive speed (m/s)");
        reg.AddField("TurretRotationSpeed", &TankController::TurretRotationSpeed, 10.0f, 180.0f, "Turret yaw rotation speed (deg/s)");
        reg.AddField("EnableNitro", &TankController::EnableNitro, "Enable nitro speed boost");
    }
};

AXLE_REGISTER_SCRIPT(TankController);
```

### 3. Hierarchy Traversal & Zero-Cost $O(1)$ Caching
In complex mechanical vehicles (assemblies with dozens of connected wheels, suspension arms, and weapon turrets), manually binding individual object slots in the Inspector is tedious and prone to broken references. AxleEngine solves this with **Hierarchy Traversal**:
- **Traversal API**: `FindChildByTag(tag, recursive)`, `GetParent()`, `GetChildren()`, and `ForEachChild(callback)` safely navigate the entity tree via [HierarchyComponent](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/Components.hpp).
- **$O(1)$ Caching Pattern**: Child entities are resolved once in `OnCreate()` and stored in 8-byte `Axle::Entity` handles. In `OnUpdate()`, component queries execute directly on EnTT Sparse Sets with zero string comparisons.

### 4. Performance Benchmark: AxleEngine vs Unreal Engine vs Unity

| Feature / Metric | 🔷 Unreal Engine (`AActor`) | 🟩 Unity (`MonoBehaviour`) | ⚙️ AxleEngine (Hybrid EnTT ECS) |
| :--- | :--- | :--- | :--- |
| **Object Representation**| Heavy `UObject` base class | Managed C# Object on Heap | **32-bit Integer (`uint32_t`) ID** |
| **Metadata Overhead** | **~200 - 1,000+ bytes** per Actor | ~64 - 128 bytes + GC Header | **0 bytes** (Components are pure Plain Old Data) |
| **Component Lookup** | `GetComponentByClass<T>()` RTTI lookup | `GetComponent<T>()` C# search | **$O(1)$ Direct Sparse Set Index** (Inlined assembly) |
| **Memory Locality** | Heap fragmented, high cache misses | Managed heap, cache misses | **Contiguous Packed Arrays** (High L1/L2 Cache hit) |
| **Garbage Collector** | Periodic GC sweeps (micro-stutters) | Mono/Boehm GC (frametime spikes)| **Zero GC** (Standard C++ RAII deterministic freeing) |
| **Child Part Wiring** | Manual pointer assignment | Manual Drag-and-Drop Inspector slots | **Hierarchy Traversal** + $O(1)$ cached handle resolution |

---

## 🖥️ Modern Editor Architecture & Tri-Panel Ecosystem

AxleEngine implements a strict, commercial-grade architectural separation between **Runtime Execution Memory (ECS Registry)** and **Persistent Disk Storage (Asset Pipeline)** across three core editor panels:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              AXLE ENGINE EDITOR ARCHITECTURE                           │
├──────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│    1. CONTENT BROWSER    │     2. SCENE HIERARCHY      │      3. PROPERTIES PANEL      │
│  (Asset Pipeline / Disk) │  (Active Entities / RAM)    │  (Component Data Inspector)   │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ • Disk Storage (Assets/) │ • RAM (entt::registry)      │ • RAM (Component Sparse Sets) │
│ • Flyweight UUID Handles │ • Parent-Child Transforms   │ • ImGui Sliders & ColorEdit   │
│ • Binary Cache (.axlebin)│ • Archetype & Presets       │ • C++ Script Field Reflection │
│ • Right-Click: Create    │ • + Create: Entity Presets  │ • + Add Component (Categories)│
│   Folder, Scene, Script, │   (Empty, Primitives, Sun,  │ • Drag & Drop Asset Assignment│
│   Material, Prefab       │    Atmosphere, Camera, Rig) │   (Model, Texture, Material)  │
└──────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

### 1. Architectural Boundaries & Responsibility Matrix

| Editor Panel | ECS / Engine Underlying Nature | Storage Location | Creation Target (`Create / Add`) |
| :--- | :--- | :--- | :--- |
| **Scene Hierarchy** | Live `Entity` list (`uint32_t` / `entt::entity`) in active Scene | **RAM** (`ECS Registry`) & serialized into `.axlescene` | Spawns a new Entity ID with an Archetype/Preset of baseline components. |
| **Properties (Inspector)** | Data reader/writer (Reflection / ImGui) for components on selected Entity | **RAM** (Component structs in contiguous sparse sets) | Emplaces a new component struct or binds a C++ Script to the active Entity (`registry.emplace<T>()`). |
| **Content Browser** | Physical Asset Manager and project directory explorer (`Assets/`) | **Disk Storage** & managed via `AssetManager` (`UUID` handles) | Generates new physical files on disk (Material, Prefab, Scene, C++ Script) or re-bakes binary assets (`F9`). |

### 2. Core Architectural Principles
* **The Flyweight Asset Pattern**: Heavy binary payloads (vertex arrays, raw textures, sound buffers) are **NEVER** stored directly inside ECS components. Components (e.g., `MeshInstance3D`, `MaterialComponent`) only store an 8-byte `AssetHandle` (`Axle::UUID`). Multiple scene entities sharing the same 3D mesh share a single cached GPU buffer.
* **World Position Stays**: When reparenting or unparenting an entity in the hierarchy, local transforms are automatically recomputed via inverted parent matrices (`Local = Inverse(ParentWorld) * ChildWorld`) and decomposed to ensure zero visual displacement in 3D space, fortified by graph cycle detection.
* **Deferred & Recursive Lifecycle**: Entity destruction is queued safely to the end of the frame to prevent ImGui iterator invalidation, recursively freeing all child entities and associated physics bodies. Duplication (`Ctrl+D`) deep-copies child hierarchies with freshly minted UUIDs.
* **Dual-Context Inspector**: The Properties panel automatically toggles between `InspectorMode::Entity` (when an entity is selected in the Scene Hierarchy) and `InspectorMode::Asset` (when an asset file such as `.axlemat` or `.axle` is selected in the Content Browser).
* **Safe Scene State Simulation (Edit vs Play)**: Clicking `Play (F5)` deep-clones `m_EditorScene` into an ephemeral `m_RuntimeScene` using UUID remapping (`std::unordered_map<UUID, EntityID>`) for hierarchy and script references. Clicking `Stop (F7 / Esc)` discards the runtime state and restores the editor scene with zero lingering physics drift.
* **In-Editor Live Coding & Script Hot-Reload (Shadow-Copy DLL)**: User C++ scripts are compiled into an isolated `AxleScripts.dll`. When clicking `Compile (F9)`, the Editor triggers an async CMake build and loads a timestamped shadow copy (`AxleScripts_Runtime_<timestamp>.dll`) using `LoadLibrary()`. This bypasses Windows file locking (`LNK1104`), reloads new script code, and preserves existing field property values across reload without restarting the editor.

### 3. Drag & Drop Workflow (Unified ImGui Payloads)

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        STANDARDIZED DRAG & DROP PIPELINE                               │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ FLOW 1: Content Browser ──["CONTENT_BROWSER_ITEM"]──> Viewport / Scene Hierarchy       │
│ • Drag .axle / .obj model file into 3D Viewport -> Raycast spawn at ground coordinates │
│ • Automatically calls registry.create(), binds TransformComponent + MeshInstance3D    │
│                                                                                        │
│ FLOW 2: Content Browser ──["CONTENT_BROWSER_ITEM"]──> Properties Panel                 │
│ • Drag .obj/.axle into Mesh slot -> Instant model swap                                 │
│ • Drag .png into Texture slot -> Instant PBR albedo map binding                        │
│ • Drag .hpp script into Inspector -> Binds NativeScriptComponent                       │
│                                                                                        │
│ FLOW 3: Scene Hierarchy ──["SCENE_HIERARCHY_ENTITY"]──> Properties Panel / Hierarchy   │
│ • Drag Entity A onto Entity B -> Re-parents Entity A under Entity B (Scene Graph)      │
│ • Drag Entity into Script Target slot -> Injects target Entity UUID into C++ Script    │
│ • Drag Entity to Content Browser -> Packages entity & children into reusable .prefab   │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 🗺️ Project Roadmap

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
    - [x] **Sub-step 1.1**: Core Depth Framebuffer (`GL_DEPTH_COMPONENT32F`, 2048x2048, border clamped), Light Space Matrix calculation, and `Scene::RenderShadowPass()`.
    - [x] **Sub-step 1.2**: Two-pass rendering pipeline with 3x3 Percentage-Closer Filtering (PCF) soft shadows and hardware polygon offset slope-scaled depth bias to eliminate shadow acne.
    - [x] **Sub-step 1.3**: ECS `DirectionalLightComponent`, Sun Light entity management, interactive Inspector controls (Sun Azimuth & Elevation sliders, Color picker, Intensity, Ambient fill, Cast Shadows toggle, Atmosphere presets), and full scene serialization.
  - [x] **Texture Mapping (`stb_image`)**:
    - [x] **Sub-step 2.1**: Integrate standard `stb_image.h` (v2.30) library, implement OpenGL 4.5 Core Profile `Axle::Texture2D` abstraction (support PNG/JPG/TGA/BMP, RGBA8/RGB8/RG8/R8 formats, auto mipmap generation, configurable wrap & filter modes, slot binding management), with pre-baked 1x1 fallback textures (White, Flat Normal, Black, Roughness).
    - [x] **Sub-step 2.2**: Upgrade Vertex Layout & UV coordinates (`a_TexCoord`) across Primitive Meshes and `.axle` loaders; configure Texture Sampler Slots in Shader (`u_AlbedoMap`, `u_ShadowMap`, `u_NormalMap`, `u_RoughnessMap`, `u_MetallicMap`) with screen-space cotangent-frame normal perturbation, roughness/metallic sampling, and 1x1 fallback texture slot binding.
    - [x] **Sub-step 2.3**: Add `MaterialComponent` in ECS, integrate Inspector UI with texture map slots (Albedo, Normal, Roughness, Metallic), live thumbnail previews, drag-and-drop assignment from Content Browser, quick presets, and complete scene serialization (`.axlescene`).
  - [x] **PBR Material System (Cook-Torrance Microfacet BRDF)**:
    - [x] **Sub-step 3.1**: Physically based Cook-Torrance microfacet specular BRDF (Trowbridge-Reitz GGX normal distribution $D$, Smith-Schlick shadowing/masking $G$, Fresnel-Schlick reflectance $F$), energy conservation ($k_D + k_S = 1$), metallic workflow, and directional sun radiance integration with PCF soft shadows.
    - [x] **Sub-step 3.2**: HDR Filmic Tone Mapping (ACES Narkowicz approximation, Reinhard, Uncharted 2 Hable, and Linear Bypass modes) & Linear-to-sRGB display gamma correction ($1/\gamma$), with real-time Exposure and Tone Mapping controls in Sun Light Inspector and complete scene serialization.
  - [x] **Atmospheric Skybox**:
    - [x] **Sub-step 4.1**: Procedural atmospheric Rayleigh / Mie scattering skybox pipeline (`Axle::Skybox`), camera-centric unit cube renderer at infinity (`glDepthFunc(GL_LEQUAL)`, $z/w = 1.0$), analytical sharp sun disk with anti-aliasing, sunset/sunrise golden horizon warming, night darkening with procedural twinkling stars, and full HDR ACES filmic tone mapping.
    - [x] **Sub-step 4.2**: ECS `SkyboxComponent`, dynamic Sun Light orientation & photometric post-process synchronization, interactive Inspector controls (Zenith, Horizon, Ground colors, Turbidity, Sun Disk Size & Corona Glow, Sync with Sun toggle, Atmosphere presets), and complete scene serialization (`.axlescene`).
    - [x] **Sub-step 4.3**: Ambient hemisphere irradiance calculation from atmospheric sky.
- [x] **Phase 12: Assembly Pipeline & Unified `.axle` Prefab System (`Engine/Asset`, `Engine/Scene` & `Engine/UI`)**
  - [x] **Step 1: Unified `.axle` Assembly Specification (`AssetType: Assembly`) & Serializer**:
    - Standardize unified asset specification in `.axle` format with `AssetType: Assembly` header (YAML-like structured text, optimized for both human editing and AI prompt mutation, eliminating file format fragmentation).
    - Implement `AssemblySerializer::Serialize()` to recursively save complete entity hierarchies (Root + Children) preserving all ECS components (`TransformComponent`, `MeshInstance3D`, `CollisionShape3D`, `RigidBody3D`, `HierarchyComponent`, `MaterialComponent`, `VehicleComponent`, `NativeScriptComponent`).
    - Implement `AssemblySerializer::Deserialize()` with automatic **UUID Remapping** to guarantee collision-free instantiation when spawning multiple assembly instances in the same scene.
  - [x] **Step 2: Scene Assembly Instantiation & Physics Binding**:
    - Implement `Scene::InstantiateAssembly()` spawning assemblies at arbitrary world/raycast positions with ground snapping and centered pivot alignment.
    - Automatically establish parent-child transform hierarchies (`HierarchyComponent`) and instantiate corresponding Jolt runtime bodies and shapes (`RigidBody3D` + `CollisionShape3D`).
    - Add `AssemblyComponent` tracking metadata (`SourceAssetPath`, `AssetUUID`) for asset linkage management.
  - [x] **Step 3: Editor Drag-and-Drop & Hierarchy Integration**:
    - Right-click context menu in Content Browser blank space: **`Create -> Assembly (.axle)`** to quickly generate a new assembly template in the active folder.
    - Drag & drop entities from `Scene Hierarchy Panel` into `Content Browser` to package and export as `.axle` (Type: Assembly).
    - Drag & drop `.axle` Assembly assets from `Content Browser` into `3D Viewport` or `Scene Hierarchy` for instant scene instantiation.
    - Add entity context menu actions: *"Save as Assembly..."* and *"Unpack Assembly"*.
  - [x] **Step 4: Dedicated Assembly Editor Window (`AssemblyEditorPanel`)**:
    - Double-clicking an `.axle` Assembly in Content Browser opens a dedicated **Assembly Editor** window for isolated inspection and modification (similar to dedicated Material and Physics inspectors).
    - Features an isolated 3D viewport with orbit camera, interactive hierarchy tree, and inspectors for `TransformComponent`, `MeshInstance3D`, `CollisionShape3D`, and `RigidBody3D`.
    - Includes "Save Assembly" back to disk and "Add to Scene" action, allowing users to configure assemblies thoroughly before dragging them into the active scene.
    - Content Browser visual identification with a dedicated `ASY` badge and distinct styling.
- [x] **Phase 13: C++ Native Scripting Ecosystem & Mechanical Assembly Architecture (`Engine/Scene` & `Engine/UI`)**
  - [x] **Step 1: Core Static Reflection & Script Registry (`ScriptField` & `ScriptRegistry`)**:
    - Implement lightweight memory-offset reflection system (`ScriptField`) supporting `Float`, `Int`, `Bool`, `Vec3`, and `String` with min/max slider ranges and tooltips.
    - Build thread-safe `ScriptRegistry` singleton with automated `AXLE_REGISTER_SCRIPT(ClassName)` macro registration.
    - Upgrade `NativeScriptComponent` with Edit-mode field maps (`FloatFields`, `IntFields`, `BoolFields`, `Vec3Fields`) and bidirectional instance synchronization (`SyncFieldsToInstance()` / `SyncFieldsFromInstance()`).
  - [x] **Step 2: ECS Component Attachment & Hierarchy Traversal**:
    - Equip `Entity` and `ScriptableEntity` with robust hierarchy traversal helpers (`FindChildByTag`, `GetParent`, `GetChildren`, `ForEachChild`) utilizing `HierarchyComponent`.
    - Enforce $O(1)$ zero-cost caching pattern inside `OnCreate()` — resolving child entities once at spawn time to eliminate expensive runtime string lookups.
    - Decouple assembly mechanics: allow individual child parts (turrets, robotic arms, steering linkages) to be queried and actuated directly from root scripts.
  - [x] **Step 3: Editor Workflow & Intuitive Drag-and-Drop (Unity-like Component Attachment)**:
    - Add right-click context menu in Content Browser: **`Create -> C++ Script (.hpp)`** to automatically generate boilerplate C++ scripts inheriting `ScriptableEntity` with registration macros.
    - Support drag-and-drop workflow: drag `.hpp`/`.cpp` script files from Content Browser directly onto Scene Hierarchy entities or Viewport meshes to instantly attach `NativeScriptComponent`.
    - Auto-generate dynamic Inspector UI in `SceneHierarchyPanel`: ComboBox script selector, interactive drag sliders with custom ranges, checkboxes, color-coded Vec3 axis inputs, tooltips, and "Reset to Default" (`↺`) actions.
  - [x] **Step 4: Unified Assembly Prefab Packaging & Gameplay Vehicle Implementation**:
    - Upgrade `AssemblySerializer` (`.axle`) and `SceneSerializer` (`.axlescene`) to serialize and restore `NativeScriptComponent` and live-tuned field values.
    - Refactor `CarPlayerScript` with exposed drive torque, max steer angle, and child part control (headlights and wheels); implement smooth chase `CameraFollowScript`.
    - Verify complete roundtrip: create script $\rightarrow$ attach to vehicle $\rightarrow$ tune parameters $\rightarrow$ save as `.axle` prefab $\rightarrow$ instantiate in any scene and drive instantly upon pressing Play (`F5`).
- [x] **Phase 14: Core ECS & Relationship Hierarchy (Parent - Child & Scene Graph)**
  - [x] **UUID & Standard Component Integrity**: Enforce automatic attachment of `IDComponent` (64-bit UUID), `TagComponent`, `TransformComponent`, and `RelationshipComponent` across all entities created via `Scene::CreateEntity()`.
  - [x] **Scene Graph Matrix Propagation**: Traverse root entities (`Parent == NullEntity`), computing `WorldMatrix = LocalMatrix`, and recursively propagate down child trees: $\text{WorldMatrix}_{\text{Child}} = \text{WorldMatrix}_{\text{Parent}} \times \text{LocalMatrix}_{\text{Child}}$.
  - [x] **World Position Stays & Cycle Check**: Implement `Scene::SetParent(child, parent)` computing $\text{Local} = \text{Inverse}(\text{ParentWorld}) \times \text{ChildWorld}$ with local translation/rotation/scale decomposition to prevent 3D displacement, reinforced with ancestor graph cycle detection.
- [x] **Phase 15: Scene Hierarchy Panel Upgrade (Presets, Tree UI & Drag-and-Drop Reparenting)**
  - [x] **Comprehensive Archetype Presets**: Complete `+ Create` menu (Unity/Godot style) with `Create Empty`, `Create Empty Child`, 3D Objects (`Cube`, `Sphere`, `Cylinder`, `Cone`, `Plane`), Light (`Directional Light`, `Point Light`), `Camera`, and Environment (`Atmospheric Skybox`).
  - [x] **Hierarchical Tree View**: Render only root entities at the top level, displaying children via collapsible arrow nodes (`ImGuiTreeNodeFlags_OpenOnArrow`).
  - [x] **Visual Drag & Drop Reparenting**: Register `"SCENE_HIERARCHY_ENTITY"` payload on entity nodes to allow intuitive mouse drag-and-drop reparenting between entities.
  - [x] **Deferred & Recursive Lifecycle**: Implement end-of-frame deferred deletion queue (`m_EntitiesToDestroy`) with recursive child cleanup; implement deep recursive duplication (`Ctrl+D`) with fresh UUID generation.
- [x] **Phase 16: Properties Panel & C++ Scripting Reflection (Category Component Adder & Macro Fields)**
  - [x] **Structured Component Widgets**: Encapsulate components in collapsible headers with custom context menus (`Remove Component`), and implement color-coded, draggable 3-axis `DrawVec3Control` for transforms.
  - [x] **Categorized `+ Add Component` Popup**: Group available components into *Rendering*, *Physics*, *Scripts*, and *Audio/VFX* with real-time search filtering.
  - [x] **C++ Script Reflection Macro**: Support `RegisterFields()` / `AXLE_SCRIPT_FIELDS(...)` macros in `ScriptableEntity` for automatic Editor reflection of `Float`, `Int`, `Bool`, `Vec3`, `String`, and `Entity Reference` (UUID).
  - [x] **Dual-Context Properties Inspector**: Support seamless switching between `InspectorMode::Entity` (inspecting active scene entity) and `InspectorMode::Asset` (inspecting `.axlemat` materials or `.axle` assemblies from Content Browser).
- [x] **Phase 17: Content Browser & Material/Asset System (.meta & Physical Asset Creation)**
  - [x] **Asset UUID & `.meta` File Pipeline**: Auto-generate `.meta` companion files storing persistent 64-bit UUIDs for assets in `Assets/` for path-independent referencing.
  - [x] **Right-Click Asset Creation**: Right-click blank space in Content Browser to create `New Folder`, `Scene (.axlescene)`, `Material Asset (.amat / .axlemat)`, `C++ Script (.hpp)`, and `Physics Material (.physmat)`.
  - [x] **Dedicated Material Inspector**: Selecting `.amat` in Content Browser switches Properties panel into interactive PBR Material editor (Albedo, Roughness, Metallic, Texture slots).
- [x] **Phase 18: Integrated Drag & Drop, Safe Simulation & In-Editor Live Coding (Hot-Reload DLL)**
  - [x] **Full 3-Flow ImGui Drag & Drop**: Connect `"CONTENT_BROWSER_ITEM"` to Viewport/Hierarchy (Flow 1) and Properties (Flow 2); connect `"SCENE_HIERARCHY_ENTITY"` to Hierarchy reparenting, Script Target slots, and Content Browser prefab packaging (Flow 3), with dual C++ Script (`.hpp`) and Class (`.hpp + .cpp`) creation.
  - [x] **Safe Scene State Simulation (Play / Stop Mode)**: Clicking `Play (F5)` deep-clones `m_EditorScene` into an ephemeral `m_RuntimeScene` using UUID remapping (`std::unordered_map<UUID, EntityID>`); clicking `Stop (F7 / Esc)` restores the pristine editor scene with zero physics drift.
  - [x] **In-Editor C++ Script Hot-Reload & Live Coding (Unreal / Hazel Style)**:
    - [x] **Decoupled `AxleScripts.dll` Shared Module**: Separate user scripts into an auto-globbed dynamic library with export macros (`AXLE_SCRIPT_API`) and standardized lifecycle entry points (`Axle_InitializeScripts`, `Axle_ShutdownScripts`).
    - [x] **Anti-Lock Shadow-Copy & State Preservation**: Implement `ScriptHotReloadManager` copying `AxleScripts.dll` to runtime timestamped DLLs (`AxleScripts_Runtime_<timestamp>.dll`) to prevent Windows file locking (`LNK1104`), serializing and re-binding field properties on active entities seamlessly.
    - [x] **Async Background Compiler (`ScriptCompiler`)**: Spawn background worker thread executing `cmake --build build --config Release --target AxleScripts` with real-time stdout/stderr capture and error parsing without UI freezing.
    - [x] **Editor UI Integration (Compile Button & Menu)**: Unreal-style `[🔨 Compile] (F9 / Ctrl+B)` toolbar button with dynamic status animations (`Compiling...`, `[OK] Reloaded in X.Xs`, `[X] Failed`), along with `Build` menu bar items (`Open in Visual Studio`, `Open Scripts Directory`).
- [x] **Phase 19: Multithreaded Job System & enkiTS Integration (`Engine/Core` & `Engine/Physics`)**
  - [x] **ThirdParty Integration**: Embed `enkiTS` (Enki Task Scheduler) submodule/library into `ThirdParty/enkiTS` with modern CMake configuration.
  - [x] **Core JobSystem Engine**: Implement unified `Axle::JobSystem` in `Engine/Core` managing persistent lock-free worker threads (`enki::TaskScheduler`) matching CPU hardware concurrency.
  - [x] **Jolt Physics Bridge (`JobSystemEnkiTS`)**: Implement custom `JPH::JobSystem` backend routing Jolt Physics constraint solving and collision tasks into enkiTS, eliminating separate thread pools and thread oversubscription.
  - [x] **Parallel ECS Processing**: Implement `ParallelFor` utilities for `entt::registry` batch iteration, distributing bulk mechanical parts and transform evaluations across all worker cores without lock contention.
- [x] **Phase 20: Component-Driven Vehicle Creation from Scratch (`Engine/Vehicle`, `Engine/Scene` & `Engine/UI`)**
  - [x] **Phase 20.1: Decouple Hardcoded Tags & Component-per-Wheel Architecture (Godot/Unity-style N-Wheeled)**:
    - [x] **Decouple Hardcoded Tags**: Completely eliminate fixed name queries (`"Vehicle Chassis"`, `"Wheel FL/FR/RL/RR"`). All vehicle lifecycle logic is 100% component-driven via `VehicleComponent` in `SceneSerializer.cpp` and `EditorLayer.cpp`.
    - [x] **Component-per-Wheel (`WheelComponent` / `VehicleWheel3D`)**: Define `WheelComponent` for child wheel entities, exposing tire radius, width, suspension spring/damper, steerable flag, driven/traction flag, and handbrake settings.
    - [x] **Arbitrary N-Wheeled Jolt Constraint**: Support arbitrary wheel counts (2-wheel bike, 4-wheel car, 6x6 heavy truck, 8x8 APC) via `VehicleConfig.CustomWheels` with automatic differential torque splitting and anti-roll bar distribution.
    - [x] **Scene Serialization**: Full YAML serialization and deserialization for `WheelComponent` in `SceneSerializer.cpp`.
    - [x] **Runtime Simulation & Wheel Sync**: Dynamic discovery via `HierarchyComponent` and automatic synchronization of arbitrary N wheels with Jolt Physics in `EditorLayer.cpp` (preserving local transforms relative to chassis with zero drift).
    - [x] **Verification**: Step 25 in `AxleTests.exe` validating `WheelComponent` serialization and 6x6 all-wheel-drive truck physics simulation.
  - [x] **Phase 20.2: Vehicle Inspector Tuning & Modular Component-Driven Workflow (`Engine/UI`)**:
    - [x] **Interactive Sliders & Live Telemetry**: Sliders in `SceneHierarchyPanel` for chassis mass, peak engine torque, RPM limits, and brake/handbrake torque with live Speedometer & Tachometer progress bars and per-wheel suspension contact indicators.
    - [x] **Modular Manual Workflow**: Purely modular, decoupled vehicle authoring (Godot/Unity-style) where wheels are arbitrary child entities configured via `VehicleWheel3D` with individual suspension, steering, and traction tuning.
    - [x] **Direct Driving Controls**: Seamless W/A/S/D and Arrow key driving controls with Space handbrake in Play mode (`F5`), immediately operational on test scenes created from scratch without writing custom scripts.
    - [x] **Scene Serialization**: Full YAML serialization and deserialization for `VehicleComponent` tuning properties in `SceneSerializer.cpp`.
  - [ ] **Phase 20.3: Modern Unity ECS (`com.unity.vehicles`) Architecture & Full Scripting Decoupling**:
    - [ ] **Stage 1 - Pure ECS Component Standardization (`WheelComponent` & `VehicleComponent`)**:
      - Upgrade `WheelComponent` (mirroring Unity ECS `ControllableWheelAuthoring`): tire radius, width, suspension travel min/max, spring frequency/damping, steerable, driven, handbrake, `AutoSyncMesh` toggle, and live telemetry fields (`IsGrounded`, `SuspensionLength`, `AngularVelocity`, `SlipAngle`, `LateralSlip`).
      - Refine `VehicleComponent` (mirroring Unity ECS `VehicleControlAuthoring`): pure drivetrain & control data container with driver inputs (`Throttle`, `Steering`, `Brake`, `Handbrake`), anti-roll bar stiffness, center-of-mass offset, and clean C++ API (`SetDriverInput`, `GetSpeedKmh`, `GetRPM`, `GetWheelCount`).
      - Full YAML scene serialization/deserialization in `SceneSerializer.cpp`.
    - [ ] **Stage 2 - Core Scene Lifecycle & Dynamic Assembly (`VehicleUpdateColliders`)**:
      - Relocate Jolt vehicle constraint instantiation and physics setup from `EditorLayer` directly into `Scene::OnRuntimeStart()`.
      - Dynamic N-wheel discovery (`VehicleUpdateColliders` equivalent): automatically discover all child entities holding `WheelComponent` attached to root chassis entity with `RigidBody3D` + `VehicleComponent`.
      - Real-time cache-friendly wheel pose synchronization in `Scene::OnUpdateRuntime()`: update child wheel entity `TransformComponent` (suspension travel and wheel rotation) directly within contiguous EnTT memory pool.
      - Safe teardown and state restoration on simulation stop in `Scene::OnRuntimeStop()`.
    - [ ] **Stage 3 - Complete `EditorLayer` Decoupling (100% Neutral Editor)**:
      - Eliminate all vehicle physics hardcodes from `EditorLayer.cpp` (remove `SetupVehiclePhysics`, remove `m_Vehicle`, remove hardcoded WASD input polling, remove manual wheel transform copy loops).
      - Decouple `ChaseCamera` from hardcoded vehicle instances, dynamically binding to active player vehicle entities.
    - [ ] **Stage 4 - C++ Native Scripting Template (`CarController.hpp`) & Live Editor Reflection**:
      - Implement `CarController.hpp` template inheriting `Axle::ScriptableEntity`, reading keyboard/gamepad inputs and driving `VehicleComponent` cleanly.
      - Expose live-tunable fields (`Horsepower`, `MaxSteerAngle`, `BrakePower`, `AllWheelDrive`) using `AXLE_SCRIPT_FIELDS` for real-time Inspector tuning.
      - Update `SceneHierarchyPanel` with dedicated vehicle telemetry cards (speedometer, tachometer, per-wheel contact & compression readouts).
- [ ] **Phase 21: Audio System (`Engine/Audio`)**
  - [ ] Engine sound synthesis (RPM-dependent pitch/frequency).
  - [ ] Tire squeal on slip/drift, collision impact sounds, suspension compression thuds.
- [ ] **Phase 22: Off-Road Terrain & Spline Track System (`Engine/Terrain` & `Engine/UI`)**
  - [ ] **Heightfield Physics Engine**: High-performance off-road surface using `JPH::HeightFieldShape` for realistic wheel travel and suspension damping on irregular soil.
  - [ ] **Interactive Sculpting Tool (Editor)**: Viewport circular brushes (Raise / Lower / Smooth / Flatten) with real-time vertex buffer and Jolt collision updates.
  - [ ] **Splatmap Texture Painting**: Layer blending for Grass, Dirt, Gravel, Mud, and Asphalt with slope-based triplanar material shaders.
  - [ ] **Chunked Grid & Multithreaded LOD**: Chunk-based terrain mesh division with distance-dependent level of detail, generated asynchronously via enkiTS worker jobs.
  - [ ] **Spline Road & Track Generator**: Extrude 3D race track and barrier meshes dynamically along Bézier / Catmull-Rom spline paths (modular Godot-style track builder).
- [ ] **Phase 23: Gameplay Polish & Realistic Vehicle Rigging (`Engine/Vehicle` & Content)**
  - [ ] **Realistic Vehicle Chassis & Multi-Part Car Rigging**: Bind imported 3D car models (e.g. Volkswagen Jetta GLI multi-part package) directly to Jolt Physics vehicle simulation — synchronizing chassis body, animated spinning/steering wheels, and functional toggleable headlights/taillights via Assemblies.
  - [ ] **Dynamic In-Car Dashboard & HUD**: Real-time tachometer (RPM), speedometer (km/h), current gear, and turbo boost indicator.
  - [ ] **Multi-Angle Camera Controller**: Chase camera, first-person cockpit camera, hood camera, and free-orbit replay camera.

---

## 🚀 Getting Started

### Prerequisites
- **Compiler**: C++20 capable compiler (MSVC 2022 v19.30+, Clang 13+, GCC 11+)
- **Build System**: CMake 3.22 or higher
- **Git**

### Clone Repository (with Submodules)
```bash
git clone --recursive https://github.com/nguyenphong0210/AxleEngine.git
cd AxleEngine
```
*If cloned without `--recursive`, initialize submodules:*
```bash
git submodule update --init --recursive
```

### Build & Run

#### Using the build script:
```bash
# Build and run in Debug mode
./build.sh --run

# Build and run in Release mode
./build.sh --release --run
```

#### Using CMake directly:
```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build --config Release --parallel

# Execute Axle Editor
./build/Editor/Release/AxleEditor.exe
```

---

## 🎮 Controls

### Scene & Project Hotkeys
| Input | Action |
| :--- | :--- |
| **Ctrl + N** | New Empty Scene |
| **Ctrl + O** | Open Scene (`.axlescene`) via native OS file dialog |
| **Ctrl + S** | Quick Save active scene (or Save As if untitled) |
| **Ctrl + Shift + S** | Save Scene As... via native OS file dialog |
| **F9** | Compile & Hot-Reload modified `.axle` asset meshes |
| **F11** | Toggle Window Maximize / Restore (work-area aware) |

### Simulation State & Toolbar Hotkeys
| Input | Action |
| :--- | :--- |
| **F5** | Play Simulation (starts physics & car driving) |
| **F6** | Pause Simulation (freezes physics for free camera inspection) |
| **F7 / Escape** | Stop Simulation (resets bodies & car to initial edit state) |
| **O** | Step Single Frame (1/60s tick when paused) |
| **R** | Reset Scene (reloads entity transforms & zero velocities) |
| **C** | Toggle Camera Mode (Chase Camera vs Free-Fly Camera) |
| **F1** | Toggle Jolt Physics Wireframe Colliders |

### Edit Mode / Free-Fly Camera (Viewport)
| Input | Action |
| :--- | :--- |
| **Right Mouse Button + Drag** | Look / Rotate Camera (Pitch & Yaw) |
| **W / A / S / D** | Fly Camera Forward / Left / Backward / Right |
| **E / Q** | Fly Camera Up / Down |
| **Left Shift (Hold)** | Sprint Boost (2.5x speed) |
| **Left Click** | Select entity in Hierarchy or adjust UI widgets |

### Play Mode / Vehicle Driving
| Input | Action |
| :--- | :--- |
| **W / Up Arrow** | Throttle (Accelerate forward) |
| **S / Down Arrow** | Brake / Reverse |
| **A / Left Arrow** | Steer Left |
| **D / Right Arrow** | Steer Right |
| **Space** | Handbrake (Rear-wheel lock drift) |
| **Mouse Drag** | Orbit around car (3rd-Person Chase Cam) |

---

## 📜 License
This project is open-source under the MIT License.
