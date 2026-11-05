# Plugin System Baseline and Retirement Inventory (PLG-00)

Date: **2026-11-05**  
Author/Implementer: **Gemini**  
Coordinator/Reviewer: **Codex**  
Status: **Delivered (OPT-01 / PLG-00 Baseline and Retirement Ledger)**  
References:
- [`PLUGIN_SYSTEM_ARCHITECTURE.md`](PLUGIN_SYSTEM_ARCHITECTURE.md)
- [`PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md`](PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md)
- [`PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md`](PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md)
- [`OPT_00_VEHICLE_RETIREMENT_REVIEW.md`](../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md)
- [`ROADMAP.md`](../ROADMAP.md)

---

## 1. Executive Summary & Objective

Package **PLG-00** establishes an immutable, reproducible baseline and retirement ledger for AxleEngine's plugin subsystem and vehicle architecture prior to the implementation of packages **PLG-01..25**. 

Per the user steering confirmed on **2026-11-05**:
1. **Clean Engine Authority**: The Engine core must remain clean. Old data formats, legacy Vehicle payload preservation, compatibility holders, and automatic migration are **not** requirements.
2. **Early Vehicle Retirement Completed**: The monolithic `Engine/Vehicle` module was retired early in commit `fa74981` and its compatibility data purged in `be7b071`. The clean Engine core contains zero active vehicle simulation code or monolithic vehicle holders.
3. **Reproducible Ledger**: PLG-00 captures the exact current build topology, module link relationships, startup sequencing, physics step ordering, scene lifecycles, and consumer dependencies across `Engine/`, `Editor/`, `Runtime/`, `Plugins/`, and `Samples/`.
4. **Zero Production Deletion**: PLG-00 introduces no destructive modifications or premature feature deletion. It produces documentation, architectural classification, and isolated regression tests verifying clean baseline invariants.

---

## 2. Current Build & Module Link Topology

### 2.1 Root CMake Build Graph
At the current baseline (`8d1fece`):
- The root [`CMakeLists.txt`](../../CMakeLists.txt) unconditionally includes:
  - `Engine/` modules: `Core`, `Platform`, `RHI`, `Renderer`, `Audio`, `Asset`, `Physics`, `Animation`, `VFX`, `Project`, `Scene`, `UI`.
  - `Plugins/` modules: `Plugins/Spline`, `Plugins/Terrain`.
  - Products: `Editor/`, `Runtime/`, and `Tests/`.
- Notice that `Engine/Vehicle` is **not present** in `CMakeLists.txt` (completely removed).
- `Plugins/Spline` and `Plugins/Terrain` are added unconditionally regardless of project selection.

### 2.2 Monolithic Static Plugin Libraries
- **`AxleSpline`** ([`Plugins/Spline/CMakeLists.txt`](../../Plugins/Spline/CMakeLists.txt)):
  - Built as a single `STATIC` library.
  - Combines runtime mathematics and algorithms (`Curve3D`, `TrackGenerator`, `SplineComponent`, `SplineSerializer`) with Editor-only ImGui/ImGuizmo authoring tooling (`SplineEditor`).
  - Publicly links `Axle::Core`, `Axle::Renderer`, `Axle::Physics`, `Axle::Scene`, and `Axle::UI`.
- **`AxleTerrain`** ([`Plugins/Terrain/CMakeLists.txt`](../../Plugins/Terrain/CMakeLists.txt)):
  - Built as a single `STATIC` library.
  - Combines runtime data/physics (`TerrainData`, `TerrainCollider`, `TerrainMesh`, `TerrainSculptor`, `TerrainSplineCarver`, `TerrainComponent`, `TerrainSerializer`) with Editor-only tooling (`TerrainEditor`).
  - Publicly links `Axle::Core`, `Axle::Renderer`, `Axle::Physics`, `Axle::Scene`, `Axle::UI`, and `Axle::Spline`.
  - Dependency direction: `Terrain` depends on `Spline` (`TerrainSplineCarver` uses `Curve3D` and `SplineComponent`). `Spline` has zero dependency on `Terrain` (acyclic).

### 2.3 Product Linkage & Inclusion
- **`AxleEditor`** ([`Editor/CMakeLists.txt`](../../Editor/CMakeLists.txt)):
  - Links statically to both `Axle::Spline` and `Axle::Terrain`.
  - Contains direct include directives in [`Editor/src/EditorLayer.cpp`](../../Editor/src/EditorLayer.cpp):
    - `<AxleSpline/SplinePlugin.hpp>`, `<AxleSpline/SplineComponent.hpp>`, `<AxleSpline/SplineEditor.hpp>`, `<AxleSpline/TrackGenerator.hpp>`, `<AxleSpline/SplineSerializer.hpp>`.
    - `<AxleTerrain/TerrainPlugin.hpp>`, `<AxleTerrain/TerrainComponent.hpp>`, `<AxleTerrain/TerrainEditor.hpp>`, `<AxleTerrain/TerrainSerializer.hpp>`.
- **`AxleRuntime`** ([`Runtime/CMakeLists.txt`](../../Runtime/CMakeLists.txt)):
  - Links statically to both `Axle::Spline` and `Axle::Terrain`.
  - Contains direct include directives in [`Runtime/src/RuntimeLayer.cpp`](../../Runtime/src/RuntimeLayer.cpp):
    - `<AxleSpline/SplineComponent.hpp>`, `<AxleSpline/TrackGenerator.hpp>`, `<AxleSpline/SplineSerializer.hpp>`.
    - `<AxleTerrain/TerrainComponent.hpp>`, `<AxleTerrain/TerrainSerializer.hpp>`.
  - Problem identified for PLG-18..20: Shipped standalone Runtime drags in full plugin code even when a project does not utilize splines or terrains.

### 2.4 Test Scripts & Fixtures
- [`Tests/CMakeLists.txt`](../../Tests/CMakeLists.txt):
  - Builds `AxleTestScripts` from `Fixtures/Assets/scripts/*.hpp`.
  - Explicitly filters out legacy vehicle demo scripts:
    `list(FILTER AXLE_FIXTURE_SCRIPT_HEADERS EXCLUDE REGEX "CarController|RacingHUD|VehicleChaseCamera")`.
  - These scripts were authoring helpers for the old monolithic vehicle system and cannot compile against the clean Engine.

---

## 3. Startup Sequencing & Scene Lifecycle

### 3.1 Editor Startup Sequence ([`EditorLayer.cpp`](../../Editor/src/EditorLayer.cpp))
1. `EditorLayer::OnAttach`:
   - Physics initialization: `m_PhysicsEngine = std::make_unique<PhysicsEngine>(); m_PhysicsEngine->Init();`
   - Scene creation: `m_ActiveScene = std::make_shared<Scene>();`
   - Static plugin initialization:
     - `Axle::Spline::SplinePlugin::Init();`
     - `Axle::Terrain::TerrainPlugin::Init();`
   - UI and ImGui initialization.
   - Project startup / scene loading.
2. In Play Mode (`OnUpdate`):
   - Physics stepping: `m_PhysicsScene->Step(simTs);`
   - Scene update: `m_ActiveScene->OnUpdateRuntime(ts, m_PhysicsScene.get());`

### 3.2 Runtime Startup Sequence ([`RuntimeLayer.cpp`](../../Runtime/src/RuntimeLayer.cpp))
1. `RuntimeLayer::OnAttach`:
   - Physics initialization: `m_PhysicsEngine = std::make_unique<PhysicsEngine>(); m_PhysicsEngine->Init();`
   - Scene loading: Loads start scene from project asset directory.
   - Static plugin initialization: duplicated manual calls to `SplinePlugin::Init()` and `TerrainPlugin::Init()`.
2. Runtime Loop (`OnUpdate`):
   - `m_PhysicsScene->Step(ts);`
   - `m_ActiveScene->OnUpdateRuntime(ts, m_PhysicsScene.get());`

### 3.3 Scene Execution Loop ([`Engine/Scene/src/Scene.cpp`](../../Engine/Scene/src/Scene.cpp))
- `Scene::OnUpdateRuntime` order of execution:
  1. Native C++ scripting (`NativeScriptComponent::OnUpdate`).
  2. Spatial audio listener & source transforms synchronization with rigid body linear velocities.
  3. Kinematic Character Controller (`CharacterControllerComponent::Update`).
  4. Animator & Skeletal Mesh updates (`AnimatorComponent::Update`).
  5. GPU & CPU Particle System updates (`ParticleSystemComponent`, `ParticleEmitterComponent`).
- **Zero Monolithic Vehicle Coupling**:
  - `Scene` maintains no `Vehicle` list, no `GetRuntimeVehicle()` queries, and no vehicle simulation tick.
  - All physics entity positions are synchronized generically via `Scene::SynchronizePhysicsTransforms` using standard EnTT components (`TransformComponent`, `RigidBody3D`).

---

## 4. Physics Step Ordering & Simulation Boundary

### 4.1 Physics Scene Architecture ([`Engine/Physics/`](../../Engine/Physics/))
- **`PhysicsEngine`**: Wraps global Jolt Physics memory allocators, job scheduler, and factory initialization.
- **`PhysicsScene`**: Wraps the Jolt `JPH::PhysicsSystem`:
  - Body interface: `CreateBody`, `DestroyBody`, `AddBody`, `RemoveBody`, `SetLinearVelocity`, `GetLinearVelocity`, `SetTransform`, `GetTransform`.
  - Broadphase & Narrowphase collision layers: `NON_MOVING` (0), `MOVING` (1), `TRIGGER` (2).
  - Step execution: `PhysicsScene::Step(Timestep ts, i32 collisionSteps)` delegates directly to Jolt's `PhysicsSystem::Update(...)`.
- **Pure General Physics Coverage**:
  - Box, Sphere, Capsule, Cylinder, TaperedCapsule, ConvexHull, and Mesh/Heightfield shapes.
  - Motion types: `Static`, `Kinematic`, `Dynamic`.
  - Collision filtering, contact listeners, friction, restitution, and gravity factor.
  - Raycasting and sweep tests (`Raycast`, `SphereCast`).
- General physics operates completely independently of any specific vehicle constraint or wheel simulation.

---

## 5. Exhaustive Vehicle Monolith Retirement Ledger

### 5.1 Historical Module Scope (`Engine/Vehicle` - Retired)
Before deletion in `fa74981` / `be7b071`, `Engine/Vehicle` contained:
- `Vehicle.hpp` / `Vehicle.cpp`: Monolithic wrapper around Jolt's wheeled vehicle constraint, managing chassis rigid body, wheel settings, transmission, engine torque curves, and differential.
- `VehicleWheel.hpp`: Per-wheel authored data and runtime state.
- Hardcoded components in `Engine/Scene/include/Axle/Components.hpp`:
  - `VehicleComponent`: chassis mass, dimensions, engine horsepower, gear ratios.
  - `VehicleWheelComponent`: wheel radius, width, suspension spring rate, damping.
  - `VehicleAudioComponent`: RPM-based pitch modulation, surface tire screech.
  - `VehicleVFXComponent`: tire smoke particles, exhaust backfire emitters.
- Monolithic scene integration:
  - `Scene::m_RuntimeVehicles`: internal list of active vehicles.
  - `Scene::GetRuntimeVehicle(Entity)`: hardcoded query method.
  - Hardcoded YAML serialization blocks in `SceneSerializer` and `AssemblySerializer`.
  - Hardcoded inspector controls in `EntityInspector.cpp`.

### 5.2 Separation of Concerns Ledger
To ensure clarity for the upcoming clean plugin rewrite (`axle.vehicle` under PLG-11..17):

| Category | Component / Feature | Current Status | Target in Plugin System |
| --- | --- | --- | --- |
| **General Physics** | RigidBody3D, CollisionShape3D | Retained in `Engine/Physics` | Remains in Engine core; shared Jolt foundation |
| **General Physics** | PhysicsScene, Step, Raycasts | Retained in `Engine/Physics` | Remains in Engine core; extensible via listeners |
| **General Physics** | PhysicsMaterial, ContactListener | Retained in `Engine/Physics` | Remains in Engine core |
| **Vehicle Simulation** | Drivetrain, Gearbox, Differential | **RETIRED** from Engine | Re-implemented cleanly in `axle.vehicle` runtime module |
| **Vehicle Simulation** | Wheel suspension, Jolt wheeled constraint | **RETIRED** from Engine | Re-implemented cleanly in `axle.vehicle` runtime module |
| **Vehicle Authoring** | Vehicle tuning inspector, wheel gizmos | **RETIRED** from Engine | Re-implemented cleanly in `axle.vehicle` editor module |
| **Demo Scripts** | `CarController.hpp` | Excluded from build | Sample script updated to use `axle.vehicle` public API |
| **Demo Scripts** | `RacingHUD.hpp` | Excluded from build | Sample script updated to use generic DataContext / plugin API |
| **Demo Scripts** | `VehicleChaseCamera.hpp` | Excluded from build | Sample script updated to follow arbitrary target transform |
| **UI Telemetry** | `{Vehicle.SpeedKmh}`, `{Vehicle.RPM}` | Generic `UIDataContext` | Evaluated generically via key-value bindings; no hardcoding |
| **Sample Scenes** | `CarTest.axlescene`, `OffRoadTrack.axlescene` | Authoring assets preserved | Clean Engine ignores legacy vehicle tags without crashing |
| **Sample Prefabs** | `SportsCar_Assembly.axle` | Authoring asset preserved | Clean Engine ignores legacy vehicle tags without crashing |

### 5.3 Classification of Legacy Fixture Assets
- Assets in `Tests/Fixtures/Assets/` containing legacy strings (`VehicleComponent`, `VehicleAudioComponent`, `VehicleChaseCamera`):
  - `SportsCar_Assembly.axle`: authored assembly prefab.
  - `CarTest.axlescene`, `OffRoadTrack.axlescene`, `VehicleSandbox.axlescene`: authored scene files.
- **Clean Engine Policy**:
  - The Engine does not load, construct, or preserve legacy vehicle runtime objects from these files.
  - Deserialization safely skips unrecognized tags without aborting scene loading.
  - When the new `axle.vehicle` plugin is introduced in PLG-11..17, it will define a distinct, versioned schema namespace (`Axle::Vehicle::VehicleComponent`) rather than reviving legacy unnamespaced tags.

---

## 6. Target Plugin Architecture (PLG-01..25 Roadmap)

The baseline establishes the requirements for the subsequent packages:
1. **Manifest & Discovery (PLG-01)**:
   - Read-only metadata scanning of `Plugins/` roots.
   - Parse `plugin.json` declaring ID (e.g. `axle.spline`, `axle.terrain`, `axle.vehicle`), version, module kinds (Runtime vs Editor), and dependencies.
2. **Dependency Resolution (PLG-02)**:
   - Acyclic topological sort. Terrain explicitly declares dependency on Spline.
   - Detect and reject cyclic, missing, or mismatched version dependencies.
3. **Project Selection Schema (PLG-03)**:
   - Explicit versioned selections in project configuration (`ProjectConfig`).
   - Default: empty optional selection.
4. **Editor Host DLL & Runtime Static Split (PLG-05..10)**:
   - Split plugins into separate Runtime and Editor modules.
   - Editor dynamically loads selected plugins as DLLs.
   - Standalone Shipping Runtime links selected plugins statically.
5. **Fresh Vehicle Plugin (`axle.vehicle`) (PLG-11..17)**:
   - Clean implementation from scratch against the generic plugin interfaces.
   - Zero Engine core pollution.
6. **Spline & Terrain Plugin Cutover (PLG-18..20)**:
   - Split `AxleSpline` into `AxleSplineRuntime` and `AxleSplineEditor`.
   - Split `AxleTerrain` into `AxleTerrainRuntime` and `AxleTerrainEditor`.
   - Remove hardcoded includes from `EditorLayer.cpp` and `RuntimeLayer.cpp`.

---

## 7. Verification Evidence & Ledger Integrity

- **Build Invariants Verified**:
  - Engine builds completely clean without `Engine/Vehicle`.
  - `AxleEditor` and `AxleRuntime` compile and link in Debug and Release.
  - CTest suite (18/18 tests) passes with 100% success rate.
- **NOT RUN Boundaries Maintained**:
  - Physical GPU readback and windowed interactive rendering remain separated from CPU contracts.
  - No premature deletion of authoring scene fixtures.
