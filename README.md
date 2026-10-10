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
- **Level 0 - `Engine/Core`**: Application lifecycle orchestrator (`Axle::Application`, `Axle::Layer`, `Axle::LayerStack`), primitive typedefs (`Axle::f32`, `Axle::u32`), high-performance multithreaded logging via **spdlog** (`Axle::Log`, `AXLE_CORE_*`), frame timing (`Axle::Timestep`), and debugging assertion macros.
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
  - [x] Define `TagComponent`, `TransformComponent`, `MeshComponent`, and `RigidBodyComponent`.
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
  - [x] Properties / Inspector Panel (Tag rename, Transform X/Y/Z controls, Mesh & Material color/pattern, RigidBody with Friction & Restitution live tweaking, Sleeping controls).
  - [x] Vehicle Live Tuner Panel (springs, dampers, steering, torque, 4-wheel telemetry table).
  - [x] Engine Console Panel (spdlog integration, log levels, search & auto-scroll).
  - [x] Procedural Primitive Meshes: Plane, Cube, Sphere, Cylinder, Cone with procedural shader patterns.
- [ ] **Phase 6: Interactive 3D Viewport & Transform Gizmos *(Recommended Next)***
  - [ ] **3D Transform Gizmo (ImGuizmo)**: Manipulate Entity Translation (`W`), Rotation (`E`), and Scale (`R`) directly inside the 3D Viewport with interactive coordinate arrows.
  - [ ] **Viewport Mouse Picking**: Click directly on 3D objects in the viewport to select them in the Hierarchy & Inspector.
  - [ ] **Grid & World Coordinates**: Render a dynamic infinite grid floor with metric ruler lines in the editor.
- [ ] **Phase 7: Automotive Test Playground & Obstacle Course**
  - [ ] **Jump Ramps & Inclines**: Multi-height ramps to test vehicle airborne physics and suspension landing.
  - [ ] **Washboard & Bumps Track**: Rhythmic road bumps to test suspension oscillation and damping.
  - [ ] **Banked Curved Track**: Oval / circuit track with elevated cambered turns.
  - [ ] **Dynamic Destruction / Crash Barriers**: Stack of boxes, wooden crates, and tire barriers to test high-speed impacts.
- [ ] **Phase 8: 3D Asset Loading (`Engine/Asset`)**
  - [ ] Integrate mesh loading (fastgltf / tinyobjloader) for detailed car body and wheel models.
  - [ ] Texture mapping (diffuse, normal maps) via `stb_image`.
  - [ ] Replace procedural shapes on car with realistic vehicle 3D model.
- [ ] **Phase 9: Scene Serialization & Project Management**
  - [ ] Scene save/load system via YAML (`.axle` scene files).
  - [ ] Save track layouts, obstacle arrangements, and vehicle tuning presets.
  - [ ] Shortcuts `Ctrl+S` (Save Scene), `Ctrl+O` (Open Scene), `Ctrl+N` (New Scene).
- [ ] **Phase 10: Lighting, Shadows & Environmental Realism**
  - [ ] Directional Shadow Mapping (Real-time dynamic shadows cast by vehicle and obstacles).
  - [ ] PBR or Blinn-Phong material shading with specular highlights on car paint.
  - [ ] Atmospheric Skybox (Cubemap or procedural gradient sky).
- [ ] **Phase 11: Audio System (`Engine/Audio`)**
  - [ ] Engine sound synthesis (RPM-dependent pitch/frequency).
  - [ ] Tire squeal on slip/drift, collision impact sounds, suspension compression thuds.

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

# Execute Sandbox
./build/Sandbox/Release/SandboxApp.exe
```

---

## 🎮 Controls

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
