# AxleEngine

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.5%20Core-green.svg)](https://www.opengl.org/)
[![Jolt Physics](https://img.shields.io/badge/Physics-Jolt%205.2-orange.svg)](https://github.com/jrouwe/JoltPhysics)
[![EnTT ECS](https://img.shields.io/badge/ECS-EnTT%20v3.14-purple.svg)](https://github.com/skypjack/entt)
[![miniaudio](https://img.shields.io/badge/Audio-miniaudio-yellow.svg)](https://miniaud.io/)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-red.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)]()
[![Tests](https://img.shields.io/badge/CTest-50%2F50%20Passing%20(100%25)-brightgreen.svg)]()

**AxleEngine** is a modern, modular, high-performance **General-Purpose 3D Game Engine** written in **C++20**. 

Engineered with a **Data-Oriented Design (DOD)** philosophy, AxleEngine blends the ergonomics, component-driven workflows, and WYSIWYG tooling of commercial game engines (such as Godot and Unity) with native C++ execution speed, lock-free multithreaded task scheduling, and zero bloat.

Whether building **3D Action/Adventure titles, First/Third-Person Shooters, Physics Sandboxes, Mechanical Simulators, or Procedural Worlds**, AxleEngine provides a clean, robust, and extensible architectural foundation.

Agent collaboration: [shared Codex/Gemini workflow](docs/agents/README.md).

---

## 🌟 Key Architectural Highlights

* **Data-Oriented Entity Component System (ECS)**:
  * Powered by **[EnTT](https://github.com/skypjack/entt)** for contiguous, cache-friendly component storage with zero virtual function dispatch in bulk updates.
  * Hierarchical **Parent-Child Scene Graph** (`HierarchyComponent`) with world-space matrix propagation and Floyd cycle prevention.
  * Read-only transform evaluation with monotonic revisions (`GetTransformRevision()`) avoiding redundant recalculations.
  * Multithreaded batch processing via `Axle::ParallelECS` with tombstone safety and deferred `SceneCommandBuffer` mutation boundaries.
* **Modern Physically Based Rendering (PBR) Pipeline**:
  * **OpenGL 4.5 Core Profile** rendering plus an NRI-based Modern RHI path for Direct3D 12/Vulkan.
  * **Cook-Torrance Microfacet Specular BRDF** (GGX normal distribution, Smith-Schlick shadowing, Fresnel-Schlick reflectance).
  * **16-Tap Vogel Spiral / Poisson Disk Soft Shadows** with hardware bilinear PCF and slope-scaled normal bias.
  * **Atmospheric Skybox** (analytical Rayleigh/Mie scattering) and **HDR Cubemaps with Image-Based Lighting (IBL)** (Irradiance, Pre-filtered Specular, 2D BRDF LUT).
  * **ACES Filmic Tone Mapping** and linear-to-sRGB gamma correction.
  * Automated **`.axlebin` Flat Binary Mesh Cache** with microsecond GPU upload times.
* **Multithreaded 3D Physics Simulation**:
  * Powered by **[Jolt Physics](https://github.com/jrouwe/JoltPhysics)** (v5.2.0).
  * Lock-free task scheduling via **[enkiTS](https://github.com/dougbinks/enkiTS)** (`JobSystemEnkiTS`), eliminating thread oversubscription.
  * Decoupled body dynamics (`RigidBody3D`: Static, Kinematic, Dynamic) and collision geometries (`CollisionShape3D`: Box, Sphere, Capsule, Cylinder, ConvexHull, MeshShape).
  * Multi-layer raycasting and camera spring-arm anti-clipping collision detection.
* **Spatial 3D Audio Subsystem**:
  * Built on **[miniaudio](https://miniaud.io/)** with zero dynamic library dependencies.
  * Full 3D spatial attenuation (Inverse, Linear, Exponential), Doppler shift, directional sound emission cones, and ear listener tracking.
* **Native C++ Scripting with In-Editor Hot-Reloading**:
  * Write clean, type-safe gameplay code in C++20 inheriting `ScriptableEntity` (`OnCreate`, `OnFixedUpdate`, `OnUpdate`, `OnLateUpdate`, `OnImGuiRender`, `OnDestroy`).
  * Lightweight static reflection via `AXLE_SCRIPT_FIELDS` macro exposing live sliders, checkboxes, colors, and entity references in the Inspector.
  * Dynamic DLL hot-reloading (`AxleScripts.dll`) with shadow-copy anti-locking (`F9` / `[🔨 Compile]`) and ABI validation (`AXLE_SCRIPT_ABI_VERSION = 2`).
* **Unified Simulation Architecture & Dual Runtime Targets**:
  * Shared `SimulationDriver`, `SimulationClock` (fixed 60Hz tick accumulation with overload protection), and `SimulationInputAdapter` resolving Editor vs Runtime divergence.
  * **`AxleEditor.exe`**: Multi-window docking IDE with ImGuizmo 3D gizmos, viewport mouse picking, asset browser, prefab authoring, and safe simulation controls (Play, Pause, Step, Stop).
  * **`AxleRuntime.exe`**: Standalone player sharing the exact simulation pipeline, scene loading, native scripts, and shipping package support (`Data.axlepak`).
* **Skeletal Animation & GPU Vertex Skinning (`Engine/Animation`)**:
  * Embedded zero-dependency asset parsing using **[ufbx](https://github.com/bqqbarbhg/ufbx)** (`.fbx`) and **[cgltf](https://github.com/jkuhlmann/cgltf)** (`.gltf`/`.glb`) with procedural biped fallback skeleton.
  * **Linear Blend Skinning (LBS)** pipeline (`SkinningPipeline`) with 64-byte vertex stride (`ivec4 BoneIDs`, `vec4 BoneWeights`) supporting up to 128 bones per palette.
  * **1D Blend Spaces** (`BlendSpace1D`) with phase-matched time synchronization and **Hierarchical Animation State Machines** (`AnimationStateMachine`) with smooth crossfades and parameter-driven transitions.
* **Kinematic Character Controller (KCC) & Third-Person Gameplay**:
  * Powered by **Jolt Physics** `JPH::CharacterVirtual` with capsule collision, slope traversal limits, automatic curb/stair step-up (`StepOffset`), ground snapping (`StickToFloor`), and dynamic push impulse transfer.
  * Decoupled `CharacterControllerComponent`, smooth camera-relative `ThirdPersonController.hpp`, and spring-arm chase camera (`ThirdPersonCamera.hpp`).
* **Modular Plugin Architecture (`Plugins/Spline` & `Plugins/Terrain`)**:
  * **Spline Curves & Procedural Tracks (`Plugins/Spline`)**: Parametric Catmull-Rom & Cubic Bézier 3D curves with Rotation Minimizing Frames (RMF) to prevent flipping, continuous cross-section extrusion, and auto-generated static Jolt `MeshShape` collision.
  * **Off-Road Terrain & Sculpting (`Plugins/Terrain`)**: Multithreaded chunked heightfields (`enkiTS`), dynamic Level of Detail (LOD), viewport sculpting brushes, slope-based triplanar splatmap multi-texturing, and native Jolt `HeightFieldShape`.
* **Advanced Particle & Visual Effects Subsystem (`Engine/VFX`)**:
  * High-performance Data-Oriented **Struct-of-Arrays (SoA)** particle pool (`ParticlePool`) eliminating dynamic heap allocations.
  * **GPU Instanced Particle Rendering** (`ParticleRenderer`) streaming 84-byte compact instance vertex buffers (`glDrawElementsInstanced`).
  * Dynamic billboarding orientations with multi-mode blending (Additive, Alpha, Premultiplied) and depth-tested soft particles.
  * Texture sheet flipbook animations with sub-frame blending, curl noise turbulence, and dedicated **Visual FX Editor Panel**.

---

## 🏛️ Architecture Stack

The engine follows a clean layered dependency architecture:

```plaintext
Level 4:  [ AxleEditor (IDE) ]           [ AxleRuntime (Game Player) ]  [ AxleTests (CI/CD) ]
Level 3:  [ Engine/Scene (SceneData, SimulationDriver) ]                 [ AxleScripts (DLL) ]
          [ Plugins/Spline ]             [ Plugins/Terrain ]            [ Engine/Animation ]
          [ Engine/VFX ]
Level 2:  [ Engine/Physics (Jolt) ]      [ Engine/Renderer (PBR) ]      [ Engine/Audio (miniaudio) ]
          [ Engine/Asset (Pure CPU VFS & Registry) ] [ Engine/UI (ImGui) ] [ Engine/Project (.axleproj) ]
Level 1:  [ Engine/Platform (GLFW, Window, Input, Application, LayerStack) ]
Level 0:  [ Engine/Core (JobSystem enkiTS, spdlog Log, Timestep, UUID, FileSystem) ]
```

---

## 📚 Comprehensive Documentation Hub

For in-depth guides, API references, and architectural walk-throughs, start with the [documentation hub](docs/README.md):

| Document | Description |
| :--- | :--- |
| **[Roadmap](ROADMAP.md)** | Single Phase order, progress, plan links and architecture links. |
| **[Architecture Overview](docs/architecture/OVERVIEW.md)** | Subsystem structure, lifecycle, and modular boundaries post OPT-03. |
| **[Architecture & Design Set](docs/architecture/README.md)** | SAD, SDD, UML class relationships, execution flows, and target layered architecture. |
| **[ECS & Scene Graph Guide](docs/guides/SCENE_AND_ECS.md)** | EnTT component schemas, parent-child transform propagation, and `SceneCommandBuffer`. |
| **[C++ Native Scripting Guide](docs/guides/SCRIPTING.md)** | `ScriptableEntity` lifecycle, reflection macros (`AXLE_SCRIPT_FIELDS`), and dynamic DLL hot-reloading (`F9`). |
| **[Physics & Simulation Guide](docs/guides/PHYSICS.md)** | Unified `SimulationDriver`, 60Hz fixed clock, Jolt Physics, and CharacterVirtual integration. |
| **[Rendering Pipeline & Shading](docs/guides/RENDERING.md)** | Multi-pass rendering, Cook-Torrance BRDF, Vogel spiral soft shadows, and `.axlebin` caching. |
| **[AI Agent & Developer Handbook](docs/guides/DEVELOPMENT.md)** | Engineering handbook: non-negotiable rules, extension recipes, and common pitfalls. |

---

## 🗺️ Project Milestones & Roadmap

Current work follows the single **[Phase roadmap](ROADMAP.md)**. Each Phase owns
one `PLAN.md` and one `ARCHITECTURE.md`; package identifiers remain inside those
documents and their review evidence.

---

## ⚙️ Closed-Loop Scripting Workflow

AxleEngine brings the seamless ergonomics of commercial game engines to high-performance native C++:

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                          CLOSED-LOOP SCRIPTING WORKFLOW                               │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ STEP 1: CREATE SCRIPT TEMPLATE                                                        │
│ • Content Browser: Right-Click -> Create -> C++ Script (.hpp)                         │
│ • Auto-generates clean boilerplate inheriting ScriptableEntity with reflection        │
│                                           ▼                                           │
│ STEP 2: DRAG & DROP ATTACHMENT                                                        │
│ • Drag TurretController.hpp from Content Browser directly onto Entity in Viewport     │
│ • Automatically attaches NativeScriptComponent and binds script metadata              │
│                                           ▼                                           │
│ STEP 3: LIVE INSPECTOR TUNING                                                         │
│ • Properties panel dynamically generates sliders, checkboxes, and Vec3 widgets       │
│ • Live parameter edits (Speed, Torque, Angle) are saved in component edit buffer      │
│                                           ▼                                           │
│ STEP 4: PACKAGE INTO REUSABLE PREFAB                                                  │
│ • Click "Save Assembly" -> packages meshes, physics colliders, joints, and scripts    │
│ • Drag Assembly into ANY scene -> fully functional entity simulates immediately!      │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 🚀 Getting Started

### Prerequisites
* **Compiler**: Modern C++20 compiler (MSVC 2022 v19.30+, Clang 16+, GCC 12+)
* **Build System**: CMake 3.22 or higher
* **Git** (with submodule support)

### Clone Repository (with Submodules)
```bash
git clone --recursive https://github.com/nguyenphong0210/AxleEngine.git
cd AxleEngine
```
*If cloned without `--recursive`, initialize submodules manually:*
```bash
git submodule update --init --recursive
```

Development startup opens `Samples/Sandbox/Sandbox.axleproj`. Use
`--empty-project` for a fresh untitled project, or open another project with
File -> Open Project / `--project path/to/Game.axleproj`. Test fixtures remain
isolated under [Tests/Fixtures](Tests/README.md).

Build/export details: [separate Editor, tests and on-demand Runtime workflows](docs/guides/BUILD_WORKFLOWS.md).

### Build via CMake

```bash
# 1. Configure CMake in Release mode
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build all engine targets in parallel
cmake --build build --config Release --parallel

# 3. Launch the Axle Editor into the configured Samples project
./build/Editor/Release/AxleEditor.exe

# 4. Build tests explicitly (Editor builds exclude Runtime and tests)
cmake --build build --config Release --target AxleAllTests --parallel

# 5. Run registered tests via CTest
ctest --test-dir build -C Release --output-on-failure
```

---

## 🎮 Controls & Shortcuts

### Editor Hotkeys
| Hotkey | Action |
| :--- | :--- |
| **Ctrl + N** | New Empty Scene |
| **Ctrl + O** | Open Scene (`.axlescene`) via native OS file dialog |
| **Ctrl + S** | Quick Save active scene |
| **Ctrl + Shift + S** | Save Scene As... |
| **F5** | **Play Simulation** (Starts physics, scripts, and simulation driver) |
| **F6** | **Pause Simulation** (Freezes simulation clock for inspection) |
| **F7 / Escape** | **Stop Simulation** (Restores pristine editor scene with zero physics drift) |
| **O** | Single Frame Step (Fixed 60Hz tick + presentation when paused) |
| **F9 / Ctrl + B** | **Compile & Hot-Reload C++ Scripts** (`AxleScripts.dll`) |
| **F11** | Toggle Window Fullscreen / Maximize |
| **F1** | Toggle Jolt Physics Collision Wireframes |
| **F2** | **Toggle Camera Mode** (Game View / Primary Camera vs Eject Free-Fly Camera in Play Mode) |

### 3D Viewport Navigation (Edit Mode)
| Action | Description |
| :--- | :--- |
| **Right Mouse + Drag** | Look / Rotate Camera (Pitch & Yaw) |
| **W / A / S / D** | Fly Camera Forward / Left / Backward / Right |
| **E / Q** | Fly Camera Up / Down |
| **Left Shift (Hold)** | Sprint Boost (2.5x speed) |
| **Left Click** | Raycast select entity in 3D Viewport |
| **W / E / R** | Switch Gizmo to Translate (`W`), Rotate (`E`), Scale (`R`) |

---

## 📜 License

AxleEngine is open-source software licensed under the MIT License.
