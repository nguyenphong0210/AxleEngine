# Physics and Simulation Guide

Reviewed: **2026-11-01**. See [SDD](../architecture/SDD.md#6-physics-and-vehicles) and the [documentation hub](../README.md).

## 1. World and scheduler lifecycle

PhysicsEngine initializes/shuts down Jolt at process level. PhysicsScene contains PhysicsSystem and owns its allocator and JobSystemEnkiTS adapter. The adapter routes Jolt tasks through the common enkiTS scheduler.

Application currently initializes PhysicsEngine and JobSystem; application layers create and step worlds. Avoid mixing independent teardown with a live application context.

Sources: [PhysicsEngine](../../Engine/Physics/include/Axle/PhysicsEngine.hpp), [PhysicsScene](../../Engine/Physics/include/Axle/PhysicsScene.hpp), [job adapter](../../Engine/Physics/include/Axle/JobSystemEnkiTS.hpp).

## 2. Scene components and world APIs

RigidBody3D describes motion and runtime body identity; CollisionShape3D describes geometry/surface parameters. Scene lifecycle code combines them and synchronizes transforms. Use current declarations for shape fields and defaults. Changing authoring data does not automatically establish a matching live-world update.

PhysicsScene exposes body creation/destruction, transform/velocity access, activation, queries, constraints/listeners and gravity. Native Jolt access remains available; ownership/thread rules apply to those APIs too.

## 3. Body creation and raycasts

This fragment assumes an initialized physics context and an existing PhysicsScene named `world`:

```cpp
Axle::BoxBodyProps props;
props.Position = { 0.0f, 2.0f, 0.0f };
props.HalfExtent = { 0.5f, 0.5f, 0.5f };
props.IsDynamic = true;
Axle::BodyID body = world.CreateBox(props);

Axle::RayCastHit hit;
if (world.CastRay(glm::vec3(0.0f, 10.0f, 0.0f),
                  glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, hit)) {
    Axle::BodyID hitBody = hit.HitBodyID;
    float distance = hit.Distance;
}

world.DestroyBody(body);
```

CastRay takes origin, direction, maximum distance and an output result. RayCastHit exposes HitBodyID, Position and Distance. Use these actual fields instead of older RayCast/Normal/Fraction examples.

## 4. Simulation ordering

PhysicsScene::Step is separate from Scene::OnUpdateRuntime. Editor currently updates Scene before physics and scales physics time separately; Runtime steps physics first. A paused Editor single-step advances physics in the inspected branch without the Play-only Scene update.

This affects input, telemetry, characters, animation and VFX. See [current sequences](../architecture/RUNTIME_FLOWS.md#4-different-editor-and-runtime-update-order) before changing scheduling. Fixed-step and consistent pause/time-scale behavior are proposed improvements, not descriptions of one shared current scheduler.

## 5. Character and vehicle integration

CharacterController wraps Jolt CharacterVirtual. Its component supplies movement/jump data and synchronizes transforms/animation parameters. Vehicle wraps chassis/wheel constraints and exposes input, reset and telemetry. VehicleAudio/VehicleVFX are independent adapters updated through Scene.

Consult [VehicleConfig](../../Engine/Vehicle/include/Axle/VehicleConfig.hpp) and [Vehicle.cpp](../../Engine/Vehicle/src/Vehicle.cpp) for units and constraint settings. Spring frequency and damping ratio are not spring/damping constants without the relevant mass/unit conversion.

## 6. Debug rendering and boundaries

PhysicsDebugRenderer and RHIPhysicsDebugRenderer adapt Jolt debug output to separate graphics paths. They currently live in Physics and bring graphics dependencies into the target. Future extraction can expose debug data through a contract consumed by a rendering bridge.

No threading/collision benchmark or backend parity result is supplied here. Validate world/body lifecycle, queries and integrated behavior under the actual scheduler/backend configuration being changed.
