# AxleEngine Vehicle Plugin (`axle.vehicle`)

The accepted native foundation owns vehicle data, codecs, Inspectors and common
scene lifecycle through the matched opaque SDK. Detailed driving, tuning and
audio/VFX completeness are deferred by the user's foundation-first scope.
`VehiclePhysics` and the audio/VFX adapters remain development prototypes tested
separately; they are not currently connected to the SDK DLL's Start/Update path.

## Architecture

- **Manifest:** `Vehicle.axleplugin` declaring schema version 1, runtime module `VehicleRuntime`, and editor module `VehicleEditor`.
- **Runtime Module (`VehicleRuntime`):**
  - Component types:
    - `axle.vehicle/VehicleChassis`: Authored chassis configuration (`ChassisConfig`) and decoupled runtime telemetry (`ChassisRuntimeState`).
    - `axle.vehicle/VehicleWheel`: Authored per-wheel setup (`WheelConfig`) and decoupled wheel telemetry (`WheelRuntimeState`).
  - Codecs: `SerializeChassis`, `DeserializeChassis`, `SerializeWheel`, `DeserializeWheel` with version headers (`chassis-v1`, `wheel-v1`), validation checks, and entity reference remapping.
  - Preserved development prototype: `VehiclePhysics` manages Jolt constraints and powertrain in its isolated tests. The native DLL does not create another/private Jolt backend.
- **Editor Module (`VehicleEditor`):**
  - Component Inspectors for Chassis and Wheels.
  - Real-time diagnostic validation for out-of-range configurations.
  - Real-time telemetry readouts (speed, RPM, gear, ground contact, suspension travel, slip).
  - Runtime reset trigger.

## Authoring a Vehicle in Editor

1. Create a root Chassis Entity:
   - Add `TransformComponent`.
   - Add component `axle.vehicle/VehicleChassis`.
   - Set mass, half-extents (width, height, length), center of mass offset, and engine torque settings.
2. Create 4 child Wheel Entities (FL, FR, RL, RR):
   - Add `TransformComponent` with local offsets.
   - Attach as children under the Chassis Entity in `HierarchyComponent`.
   - Add component `axle.vehicle/VehicleWheel`.
   - Set `WheelIndex` (0: Front Left, 1: Front Right, 2: Rear Left, 3: Rear Right).
   - Link `ChassisEntity` to the parent Chassis entity ID.
3. Save the scene (`.axlescene`). The plugin's entity reference remapping ensures that duplicating or prefab-instantiating the vehicle remaps all wheel-to-chassis entity IDs deterministically.

## Editor Play Lifecycle

- **Play:** Owned data is cloned into the runtime scene; SDK callbacks update telemetry and contribute debug lines through canonical host services. Native driving constraints are not created by this foundation.
- **Pause/Step:** Simulation clock advances fixed steps without mutating the original authored scene.
- **Stop:** The simulation scene is torn down, and the original authored scene is completely restored with zero lingering state.
