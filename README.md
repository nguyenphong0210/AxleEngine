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
- [ ] **Phase 4: Automotive Mechanics (`Engine/Vehicle`)**
  - [ ] Implement `VehicleConstraintWrapper` utilizing Jolt's vehicle mechanics.
  - [ ] Configure 4-wheel suspension rays, springs, and tire friction curves.
  - [ ] Build powertrain model (RPM, gear ratios, differential, acceleration/braking forces).
  - [ ] Wire vehicle telemetry (speedometer, tachometer, G-force) into ImGui.
- [ ] **Phase 5: 3D Asset Loading (`Engine/Asset`)**
  - [ ] Integrate mesh loading (fastgltf / tinyobjloader) for detailed car body and wheel models.
  - [ ] Texture mapping (diffuse, normal maps) via `stb_image`.
  - [ ] Replace debug cube meshes with realistic 3D vehicle assets.

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

| Input | Action |
| :--- | :--- |
| **Right Mouse Button + Drag** | Rotate / Look around (Free-Fly Camera) |
| **W / A / S / D** | Move Forward / Left / Backward / Right |
| **E / Space** | Move Up |
| **Q / Left Shift** | Move Down |
| **R** | Reset Falling Bodies |
| **F1** | Toggle Physics Collider Wireframe |
| **Escape** | Close Application |

---

## 📜 License
This project is open-source under the MIT License.
