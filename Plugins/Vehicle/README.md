# AxleEngine Vehicle Plugin (`axle.vehicle`)

The Vehicle plugin provides a physically simulated, Jolt-backed four-wheeled vehicle system for AxleEngine, implemented entirely as an independent native plugin without polluting the core Engine SDK.

## Architecture

- **Manifest:** `Vehicle.axleplugin` declaring schema version 1, runtime module `VehicleRuntime`, and editor module `VehicleEditor`.
- **Runtime Module (`VehicleRuntime`):**
  - Component types:
    - `axle.vehicle/VehicleChassis`: Authored chassis configuration (`ChassisConfig`) and decoupled runtime telemetry (`ChassisRuntimeState`).
    - `axle.vehicle/VehicleWheel`: Authored per-wheel setup (`WheelConfig`) and decoupled wheel telemetry (`WheelRuntimeState`).
  - Codecs: `SerializeChassis`, `DeserializeChassis`, `SerializeWheel`, `DeserializeWheel` with version headers (`chassis-v1`, `wheel-v1`), validation checks, and entity reference remapping.
  - Physics Backend: `VehiclePhysics` managing Jolt `JPH::VehicleConstraint`, `JPH::WheeledVehicleController`, suspension springs, differential powertrain, driver inputs, and post-step synchronization.
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

- **Play:** The scene is cloned for runtime simulation; Jolt physics constraint and body are initialized on the runtime physics scene.
- **Pause/Step:** Simulation clock advances fixed steps without mutating the original authored scene.
- **Stop:** The simulation scene is torn down, and the original authored scene is completely restored with zero lingering state.
