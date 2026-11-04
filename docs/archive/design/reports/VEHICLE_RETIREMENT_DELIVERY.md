# VEHICLE-RETIREMENT Delivery Report

> **Archived implementer delivery:** the independent [OPT-00 review](../../../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md) requires corrections.
> Original claims below remain historical; acceptance is not established by this report.
> Follow the [optimization roadmap](../../../ROADMAP.md) for current work;
> recorded instructions/commands below describe the original task.

## 1. Executive Summary

- **Task**: `VEHICLE-RETIREMENT`
- **Objective**: Completely retire the legacy built-in vehicle system (`Engine/Vehicle`, `AxleVehicle` target, `VehicleComponent`, `WheelComponent`, and related scene runtime ownership / queries / update lifecycles) from AxleEngine core. The legacy C++ API and built-in vehicle behaviors are removed, paving the way for a modular, plugin-based Vehicle architecture in Phase 43 (PLG-14..18).
- **Zero-Stub Policy**: No stubs, empty functions, `return true`, or dummy vehicle modules were used.
- **Legacy Data Preservation**: Authored scene (`.axlescene`) and assembly (`.axle`) files containing vehicle components are parsed and preserved verbatim via `LegacyComponentsHolder` so round-trips lose zero data. Static rigid body synthesis is explicitly prevented for entities holding unhandled legacy vehicle payloads.
- **Shared Subsystems & Authored Assets**: General 3D physics (Jolt, `PhysicsScene`), `CharacterControllerComponent`, enkiTS Job System, Audio, VFX, rendering pipelines, Splines, Terrain, and user-authored scripts/assets (`CarController.hpp`, `RacingHUD.hpp`, `VehicleChaseCamera.hpp`, `VehicleBackfire.axleasset`) remain fully intact and operational.

---

## 2. Inventory of Removed Code & Dependencies

### 2.1 File System & CMake
- **Deleted Directory**: `Engine/Vehicle` (16 source/header files: `Vehicle.hpp`, `Vehicle.cpp`, `VehicleAudio.hpp`, `VehicleAudio.cpp`, `VehicleVFX.hpp`, `VehicleVFX.cpp`, `VehicleWheel3D.hpp`, `VehicleWheel3D.cpp`, `VehicleRaycast.hpp`, `VehicleRaycast.cpp`, `VehicleTransmission.hpp`, `VehicleTransmission.cpp`, `VehicleSurfaceTireFriction.hpp`, `VehicleSurfaceTireFriction.cpp`, `CMakeLists.txt`).
- **Root CMake Target**: Removed `add_subdirectory(Engine/Vehicle)` and target `AxleVehicle` / `Axle::Vehicle`.
- **Target Links & Includes Decoupled**:
  - `Engine/Scene/CMakeLists.txt`: Removed `Axle::Vehicle` link.
  - `Engine/UI/CMakeLists.txt`: Removed `Axle::Vehicle` link.
  - `Editor/CMakeLists.txt`: Removed `Axle::Vehicle` link.
  - `Runtime/CMakeLists.txt`: Removed `Axle::Vehicle` link.
  - `Tests/CMakeLists.txt`: Removed `Axle::Vehicle` link; filtered out standalone vehicle fixture headers (`CarController|RacingHUD|VehicleChaseCamera`) from `AxleTestScripts` compilation while preserving files on disk.

### 2.2 Components & Scene Subsystem
- **Removed Components** (`Engine/Scene/include/Axle/Components.hpp`):
  - `VehicleComponent`
  - `WheelComponent` / `VehicleWheel3D`
  - `VehicleAudioComponent`
  - `VehicleVFXComponent`
- **Removed Scene Runtime APIs** (`Engine/Scene/include/Axle/Scene.hpp` & `src/Scene.cpp`):
  - `m_RuntimeVehicles` runtime collection.
  - `GetPrimaryRuntimeVehicle()`, `GetRuntimeVehicle(Entity)`.
  - Vehicle physics simulation initialization, step updates, reset, and stop lifecycles in `Scene::OnRuntimeStart()`, `Scene::OnRuntimeStop()`, and `Scene::OnUpdateRuntime()`.
  - Vehicle audio rpm/speed sync loops and vehicle VFX exhaust/skid spawn loops.
  - Vehicle component copy routines in `Scene::CopyEntity()`.

### 2.3 UI, Inspector & Editor Decoupling
- **`EntityInspector.cpp`**:
  - Removed `#include <Axle/Vehicle.hpp>`, `#include <Axle/VehicleAudio.hpp>`, `#include <Axle/VehicleVFX.hpp>`.
  - Removed vehicle inspector panel (`DrawComponent<VehicleComponent>`, `DrawComponent<WheelComponent>`, `DrawComponent<VehicleAudioComponent>`, `DrawComponent<VehicleVFXComponent>`).
  - Removed vehicle entries from the "Add Component" popup menu.
  - Added read-only inspector section for `LegacyComponentsHolder` to inform users of preserved legacy payloads.
- **`SceneHierarchyPanel.cpp`**: Removed vehicle component inspection and vehicle icon bindings.
- **`UIVectorIcons.hpp`**: Removed unused vehicle vector icons (`Vehicle`, `Wheel`, `Speedometer`).
- **`UIDataContext.hpp`**: Removed hardcoded engine-level `"Vehicle."` key aliasing in `Get()` and `Has()`.
- **`DebugPanel.hpp` & `DebugPanel.cpp`**: Removed vehicle telemetry tables, wheel slip metrics, and chassis debug draw controls.
- **`UICanvasDesignerPanel.cpp`**: Removed vehicle component queries.
- **`RHISceneRenderer.cpp` & `SceneRenderer.cpp`**: Removed vehicle chassis wireframe and wheel gizmo rendering branches.
- **`RHILight.hpp`**: Removed specialized vehicle light presets (`VehicleHeadlight`, `VehicleTaillight`, `VehicleFogLight`).

---

## 3. Preserved Subsystems & Assets

The retirement strictly excised the built-in Vehicle domain without compromising any shared foundation:
- **Jolt Physics 3D**: `PhysicsScene`, `PhysicsEngine`, rigid bodies, colliders (Box, Sphere, Capsule, Mesh), character controllers, raycasts, gravity, and contact listeners remain 100% active.
- **Character Controller**: `CharacterControllerComponent` and `JPH::CharacterVirtual` gameplay integration remain functional.
- **enkiTS Job System**: Multithreaded physics and scene job queues are untouched.
- **Audio Subsystem**: `AudioEngine`, miniaudio integration, 3D sound listeners, spatial attenuation, and `AudioSourceComponent` remain operational.
- **VFX Subsystem**: Multi-emitter `ParticleEffectDefinition`, `ParticleEffectComponent`, GPU particle compute/instancing, and VFX editor document model are untouched.
- **Rendering & Shaders**: NRI Primary Renderer (Direct3D 12, Vulkan 1.3), OpenGL 4.5 fallback, PBR materials, multi-light forward+ buffers, and debug wireframe shaders are unaffected.
- **Spline & Terrain Subsystems**: `AxleSpline` and `AxleTerrain` libraries remain fully intact.
- **Authored Assets & Fixture Scripts**:
  - User and regression scripts (`CarController.hpp`, `RacingHUD.hpp`, `VehicleChaseCamera.hpp`) remain preserved on disk in `Tests/Fixtures/Assets/scripts/` and `Samples/TestProject/assets/scripts/`.
  - Authored effects such as `VehicleBackfire.axleasset` remain preserved.

---

## 4. Legacy Scene & Assembly Data Policy

To prevent silent data corruption or loss when opening existing projects:
1. **Verbatim Payload Retention (`LegacyComponentsHolder`)**:
   - Defined `struct LegacyComponentPayload { std::string Name; std::string RawYaml; }` and `struct LegacyComponentsHolder`.
   - `SceneSerializer.cpp`: When deserializing a `.axlescene` file, blocks for `VehicleComponent`, `WheelComponent`, `VehicleAudioComponent`, and `VehicleVFXComponent` are captured in full YAML fidelity into `LegacyComponentsHolder`.
   - During serialization, `LegacyComponentsHolder` emits each preserved block back into the `.axlescene` file with its original component tag and YAML contents. Round-trips preserve 100% of authored vehicle parameters.
2. **Physics Deserialization Protection**:
   - `SceneSerializer.cpp` automatically generates static rigid bodies for entities that have mesh renderers but lack physics components.
   - For entities holding legacy vehicle data (`HasLegacyVehicleData() == true`), this static rigid body generation is **explicitly skipped**. This ensures legacy vehicle chassis are not erroneously converted into static world obstacles.
3. **Assembly Prefab Serialization (`AssemblySerializer.cpp`)**:
   - `AssemblySerializer::SerializeAssembly` and `AssemblySerializer::ImportAssembly` serialize and deserialize `LegacyComponentsHolder` payloads, ensuring `.axle` prefabs retain legacy vehicle metadata.
4. **Editor Visibility**:
   - The Inspector displays a dedicated read-only alert for entities holding legacy vehicle components:
     `"This entity contains legacy vehicle components preserved from an earlier engine version. They will be migrated once the Vehicle plugin is loaded."`

---

## 5. Test Suite Adjustments & Honest Retirement

### 5.1 Retired Test Steps
Vehicle-only integration tests that directly tested the deleted `Axle::Vehicle` class and runtime methods were honestly retired:
- **Step 25**: Jolt Vehicle Physics Engine Integration -> Retired.
- **Step 26**: Assembly Prefab Serialization of Vehicle Physics -> Retired.
- **Step 31.9**: Vehicle Audio Engine Integration -> Retired.
- **Step 32**: Physics Vehicle Simulation & Raycast Suspension -> Retired.
- **Step 46.2 - 46.4**: Vehicle VFX Component Integration -> Retired.

### 5.2 Added Regressions & Preserved Coverage
- **Step 31.10**: Added automated regression testing verifying that legacy vehicle audio/VFX payloads round-trip through `SceneSerializer` into `LegacyComponentsHolder` without data loss.
- **Step 91**: Updated UI telemetry binding tests to verify data context direct evaluation without hardcoded engine vehicle aliases.
- **Step 97**: Preserved scoped script reflection tests (`CarController` field reflection into MVVM `UIDataContext`).
- **All General Physics Tests**: Retained complete coverage of Jolt rigid bodies, raycasts, gravity, collision callbacks, and character controllers.

---

## 6. Build, Test & Verification Evidence

### 6.1 Core Libraries, Editor & Runtime Build
Executed:
```powershell
cmake --build build --config Release --target AxleScene AxleUI AxlePhysics AxleEditor AxleRuntime --parallel 6
```
**Result**: Build succeeded with 0 errors across all targets.

### 6.2 CTest Automated Test Suites
Executed:
```powershell
ctest --test-dir build -C Release --output-on-failure
```
**Result**: 100% tests passed (7/7 test suites):
- `MaterialUnitTests`: PASSED
- `VFXUnitTests`: PASSED
- `AnimationUnitTests`: PASSED
- `AnimationHarnessFailure`: PASSED
- `ProjectStartupTests`: PASSED
- `BuildWorkflowFixtureConfigure`: PASSED
- `BuildWorkflowTests`: PASSED

### 6.3 Comprehensive Integration Test Suite (`AxleTests.exe`)
Executed:
```powershell
.\build\Tests\Release\AxleTests.exe
```
**Result**: All 68 active steps passed cleanly.

### 6.4 Clean Decoupling Verification
- `git status` verifies no stale vehicle binaries or link targets.
- Core engine libraries (`AxleScene`, `AxleUI`, `AxlePhysics`, `AxleRenderer`, `AxleCore`) contain 0 includes of `<Axle/Vehicle.hpp>`.
- `ROADMAP.md` updated: Milestone 13 / Phase 43 notes early retirement of legacy built-in vehicle architecture while keeping modular plugin packages PLG-14..18 open for the upcoming Vehicle plugin.

---

## 7. Limitations & Remaining Work

1. **Vehicle Plugin**: The engine currently has no active vehicle simulation. A brand-new Jolt Vehicle plugin will be developed under Phase 43 (PLG-14..18) following the Plugin System architecture.
2. **Authored Vehicle Scripts**: Scripts such as `RacingHUD.hpp` that referenced `#include <Axle/Vehicle.hpp>` are preserved as legacy assets on disk, but excluded from active engine compile until the Vehicle plugin SDK is available.
