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
  - [x] **Step 1: Core SceneSerializer (`.axlescene`)**: Built-in structured YAML-like scene serializer saving and restoring complete ECS scene state (UUIDs, TagComponents, TransformComponents, MeshComponents with builtin/custom models, Jolt RigidBodyComponents with shape detection, HierarchyComponents parent-child links, and NativeScriptComponents).
  - [x] **Step 2: Interactive Save/Open Workflows**: Connect `Ctrl+S` (Save Scene), `Ctrl+Shift+S` (Save Scene As), `Ctrl+O` (Open Scene), `Ctrl+N` (New Scene) with native OS file dialogs (`Axle::FileDialogs`), dynamic window title indicator (`Axle Engine - Project - [SceneName.axlescene]`), and full Main Menu Bar integration (`File -> New / Open / Save / Save As / Exit`).
  - [x] **Step 3: Content Browser Scene Loading**: Double-click scene files (`.axlescene`) in Content Browser to instantly load them into the active Viewport, with right-click context menu ("Open Scene in Viewport"), dedicated Scene clapperboard icon (`SCN`), and drag-and-drop scene loading onto the Viewport.
  - [x] **Step 4: Project Configuration System (`.axleproj`)**: Project file storing project name, asset directory root, and start scene with `File -> New Project`, `File -> Open Project` (`Ctrl+Shift+O`), `File -> Save Project`, Content Browser `.axleproj` project blueprint icon (`PRJ`), and dynamic window title synchronization.
- [ ] **Phase 11: Lighting, Dynamic Shadows & PBR Materials**
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
  - [ ] **Atmospheric Skybox**:
    - [x] **Sub-step 4.1**: Procedural atmospheric Rayleigh / Mie scattering skybox pipeline (`Axle::Skybox`), camera-centric unit cube renderer at infinity (`glDepthFunc(GL_LEQUAL)`, $z/w = 1.0$), analytical sharp sun disk with anti-aliasing, sunset/sunrise golden horizon warming, night darkening with procedural twinkling stars, and full HDR ACES filmic tone mapping.
    - [ ] **Sub-step 4.2**: Dynamic Sun Light synchronization & Inspector controls (Atmospheric Turbidity, Zenith, Horizon, Ground colors, Sun Disk Size & Glow) with scene serialization.
    - [ ] **Sub-step 4.3**: Ambient hemisphere irradiance calculation from atmospheric sky.
- [ ] **Phase 12: Audio System (`Engine/Audio`)**
  - [ ] Engine sound synthesis (RPM-dependent pitch/frequency).
  - [ ] Tire squeal on slip/drift, collision impact sounds, suspension compression thuds.
- [ ] **Phase 13: Off-Road Terrain & Spline Track System (`Engine/Terrain` & `Engine/UI`)**
  - [ ] **Heightfield Physics Engine**: High-performance off-road surface using `JPH::HeightFieldShape` for realistic wheel travel and suspension damping on irregular soil.
  - [ ] **Interactive Sculpting Tool (Editor)**: Viewport circular brushes (Raise / Lower / Smooth / Flatten) with real-time vertex buffer and Jolt collision updates.
  - [ ] **Splatmap Texture Painting**: Layer blending for Grass, Dirt, Gravel, Mud, and Asphalt with slope-based triplanar material shaders.
  - [ ] **Chunked Grid & LOD**: Chunk-based terrain mesh division with distance-dependent level of detail for large-scale open worlds.
  - [ ] **Spline Road & Track Generator**: Extrude 3D race track and barrier meshes dynamically along Bézier / Catmull-Rom spline paths (modular Godot-style track builder).
- [ ] **Phase 14: Gameplay Polish & Realistic Vehicle Rigging (`Engine/Vehicle` & Content)**
  - [ ] **Realistic Vehicle Chassis & Multi-Part Car Rigging**: Bind imported 3D car models (e.g. Volkswagen Jetta GLI multi-part package) directly to Jolt Physics vehicle simulation — synchronizing chassis body, animated spinning/steering wheels, and functional toggleable headlights/taillights.
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

# Execute Sandbox
./build/Sandbox/Release/SandboxApp.exe
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
