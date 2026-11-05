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

## 5. Character integration and retired Vehicle

CharacterController wraps Jolt CharacterVirtual. Its component supplies movement/jump data and synchronizes transforms/animation parameters.

The built-in Vehicle module and components were retired in `fa74981`; `be7b071`
removed the remaining legacy payload holders and compatibility branches. The user
requires no old-format support or legacy-specific body suppression. Ordinary
rigid-body and CharacterController behavior remains part of current correctness.
No current Vehicle simulation is provided. The replacement is planned as a complete
plugin in [OPT-07](../ROADMAP.md#opt-07---write-vehicle-as-a-complete-plugin).
Clean-removal delivery claims await independent verification.
Existing general physics and CharacterController remain separate from that rewrite.

## 6. Debug rendering and boundaries

PhysicsDebugRenderer and RHIPhysicsDebugRenderer adapt Jolt debug output to separate graphics paths. They currently live in Physics and bring graphics dependencies into the target. Future extraction can expose debug data through a contract consumed by a rendering bridge.

No threading/collision benchmark or backend parity result is supplied here. Validate world/body lifecycle, queries and integrated behavior under the actual scheduler/backend configuration being changed.
