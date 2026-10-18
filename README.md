# AxleEngine

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![OpenGL](https://img.shields.io/badge/OpenGL-4.5%20Core-green.svg)](https://www.opengl.org/)
[![Jolt Physics](https://img.shields.io/badge/Physics-Jolt%205.2-orange.svg)](https://github.com/jrouwe/JoltPhysics)
[![EnTT ECS](https://img.shields.io/badge/ECS-EnTT%20v3.14-purple.svg)](https://github.com/skypjack/entt)
[![miniaudio](https://img.shields.io/badge/Audio-miniaudio-yellow.svg)](https://miniaud.io/)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-red.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)]()
[![Tests](https://img.shields.io/badge/Tests-32%2F32%20Passing%20(100%25)-brightgreen.svg)]()

**AxleEngine** is a modern, modular, high-performance **General-Purpose 3D Game Engine** written in **C++20**. 

Engineered with a **Data-Oriented Design (DOD)** philosophy, AxleEngine blends the ergonomics, component-driven workflows, and WYSIWYG tooling of commercial game engines (such as Godot and Unity) with native C++ execution speed, lock-free multithreaded task scheduling, and zero bloat.

Whether building **3D Action/Adventure titles, First/Third-Person Shooters, Physics Sandboxes, Mechanical Simulators, or Racing Games**, AxleEngine provides a clean, robust, and extensible architectural foundation.

---

## 🌟 Key Architectural Highlights

* **Data-Oriented Entity Component System (ECS)**:
  * Powered by **[EnTT](https://github.com/skypjack/entt)** for contiguous, cache-friendly component storage with zero virtual function dispatch in bulk updates.
  * Hierarchical **Parent-Child Scene Graph** (`HierarchyComponent`) with world-space matrix propagation and transform cycle prevention.
  * Multithreaded batch processing via `Axle::ParallelECS` utilizing worker threads without lock contention.
* **Modern Physically Based Rendering (PBR) Pipeline**:
  * Pure **OpenGL 4.5 Core Profile** abstraction with zero graphics API leaks outside the renderer.
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
  * Write clean, type-safe gameplay code in C++20 inheriting `ScriptableEntity` (`OnCreate`, `OnUpdate`, `OnImGuiRender`, `OnDestroy`).
  * Lightweight static reflection via `AXLE_SCRIPT_FIELDS` macro exposing live sliders, checkboxes, colors, and entity references in the Inspector.
  * Dynamic DLL hot-reloading (`AxleScripts.dll`) with shadow-copy anti-locking (`F9` / `[🔨 Compile]`)—edit code and reload without restarting the editor.
* **Dual Runtime Targets**:
  * **`AxleEditor.exe`**: Multi-window docking IDE with ImGuizmo 3D gizmos, viewport mouse picking, asset browser, prefab authoring, and safe 3-state simulation (Play, Pause, Step, Stop).
  * **`AxleRuntime.exe`**: Standalone, lightweight game player executing direct backbuffer rendering, transparent in-game HUD overlay canvas (`ScriptableEntity::OnImGuiRender()`), and zero-overhead distribution booting directly from monolithic cooked packages (`Data.axlepak`).
* **First-Class Domain Toolkits**:
  * **Modular Vehicle Physics Toolkit**: N-wheeled vehicle constraints, Ackermann steering, 4WD powertrain, suspension springs/dampers, and granular procedural engine RPM audio synthesis.
  * **Multi-Angle Camera Toolkit**: Spring-Arm Chase Camera, First-Person Cockpit (Driver POV), Hood Camera, and 360° Free Orbit Inspection.

---

## 🏛️ Architecture Stack

```plaintext
Level 4:  [ AxleEditor (IDE) ]           [ AxleRuntime (Game Player) ]  [ AxleTests (CI/CD) ]
Level 3:  [ Engine/Scene (ECS) ]         [ Engine/Vehicle (Toolkit) ]   [ AxleScripts (DLL) ]
Level 2:  [ Engine/Physics (Jolt) ]      [ Engine/Renderer (PBR) ]      [ Engine/Audio (miniaudio) ]
          [ Engine/Asset (VFS & Cache) ] [ Engine/UI (ImGui) ]          [ Engine/Project (.axleproj) ]
Level 1:  [ Engine/Platform (GLFW, Input, DynamicLibrary) ]
Level 0:  [ Engine/Core (JobSystem enkiTS, spdlog Log, Timestep, UUID, FileSystem) ]
```

---

## 📚 Comprehensive Documentation Hub

For in-depth guides, API references, and architectural walk-throughs, refer to the documentation suite in `docs/`:

| Document | Description |
| :--- | :--- |
| **[Architecture Overview](docs/ARCHITECTURE.md)** | Core design principles, layered stack, application lifecycle, memory strategy, and thread scheduling. |
| **[Subsystems & Module Reference](docs/SUBSYSTEMS.md)** | Detailed breakdown of every engine module (`Core`, `Platform`, `Renderer`, `Physics`, `Audio`, `Scene`, `UI`, `Asset`). |
| **[ECS & Scene Graph Guide](docs/ECS_AND_SCENE_GRAPH.md)** | EnTT component schemas, parent-child transform propagation, prefab assemblies (`.axle`), and `ParallelECS`. |
| **[C++ Native Scripting Guide](docs/SCRIPTING_GUIDE.md)** | `ScriptableEntity` lifecycle, reflection macros (`AXLE_SCRIPT_FIELDS`), and dynamic DLL hot-reloading (`F9`). |
| **[Rendering Pipeline & Shading](docs/RENDERER_AND_PIPELINE.md)** | Multi-pass rendering, Cook-Torrance BRDF, Vogel spiral soft shadows, atmospheric skybox, and `.axlebin` caching. |
| **[Physics & Simulation Guide](docs/PHYSICS_AND_SIMULATION.md)** | Jolt Physics integration, rigid body dynamics, collision shapes, raycasting, and vehicle constraint mechanics. |
| **[AI Agent & Developer Handbook](docs/AI_DEVELOPER_GUIDE.md)** | Engineering handbook for AI agents and developers: non-negotiable rules, extension recipes, and common pitfalls. |

---

## 🗺️ Project Milestones & Roadmap

AxleEngine has undergone extensive architectural development and rigorous quality assurance:
* **Completed Milestones**: **Phase 1 through Phase 28** are 100% complete and fully verified.
* **Automated Test Suite**: **32 Comprehensive Headless Test Suites** (`AxleTests.exe`) validating all engine subsystems in CI/CD with a **100% pass rate**.
* **Upcoming Focus**: **Phase 29: Off-Road Terrain & Spline Track System**, followed by Skeletal Animation and VFX.

👉 **For the complete phase-by-phase development history, detailed feature checklists, and upcoming roadmap, see [ROADMAP.md](ROADMAP.md).**

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
│ STEP 4: PACKAGE INTO REUSABLE `.axle` PREFAB                                          │
│ • Click "Save Assembly" -> packages meshes, physics colliders, joints, and scripts    │
│ • Drag Assembly into ANY scene -> fully functional machine drives immediately!        │
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

### Build via CMake

```bash
# 1. Configure CMake in Release mode
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build all engine targets in parallel
cmake --build build --config Release --parallel

# 3. Launch the Axle Editor (WYSIWYG IDE)
./build/Editor/Release/AxleEditor.exe

# 4. Launch Standalone Game Player (Optional)
./build/Runtime/Release/AxleRuntime.exe

# 5. Run the Automated Headless Test Suite (32 Verified Suites)
./build/Editor/Release/AxleTests.exe
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
| **F5** | **Play Simulation** (Starts physics, scripts, and runtime vehicles) |
| **F6** | **Pause Simulation** (Freezes physics for inspection) |
| **F7 / Escape** | **Stop Simulation** (Restores pristine editor scene with zero physics drift) |
| **O** | Single Frame Step (1/60s tick when paused) |
| **F9 / Ctrl + B** | **Compile & Hot-Reload C++ Scripts** (`AxleScripts.dll`) |
| **F11** | Toggle Window Fullscreen / Maximize |
| **F1** | Toggle Jolt Physics Collision Wireframes |
| **F2** | **Toggle Camera Mode** (Game View / Primary Camera vs Eject Free-Fly Camera in Play Mode) |
| **C** | **Cycle Chase Camera Distance** (Near $\rightarrow$ Medium $\rightarrow$ Far preset) |
| **V** | **Cycle Camera Angle** (Third-Person Chase $\rightarrow$ Cockpit POV $\rightarrow$ Hood $\rightarrow$ Free Orbit) |
| **H** | **Toggle Racing HUD** Telemetry Overlay |
| **U** | **Toggle Speed Units** (KM/H $\leftrightarrow$ MPH) |

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

AxleEngine is open-source software licensed under the [MIT License](LICENSE).
