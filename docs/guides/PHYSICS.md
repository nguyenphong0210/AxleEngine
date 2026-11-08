# Physics and Simulation Guide

Reviewed: **2026-11-06** (post OPT-03). See [SDD](../architecture/SDD.md#6-physics-and-vehicles) and the [documentation hub](../README.md).

## 1. World and scheduler lifecycle

PhysicsEngine initializes and shuts down Jolt at process level. PhysicsScene contains PhysicsSystem and owns its allocator and JobSystemEnkiTS adapter. The adapter routes Jolt tasks through the common enkiTS scheduler.

Platform/Application initializes the baseline systems, while simulation scenes and worlds are owned and stepped explicitly. Avoid mixing independent teardown with a live application or simulation context.

Sources: [PhysicsEngine](../../Engine/Physics/include/Axle/PhysicsEngine.hpp), [PhysicsScene](../../Engine/Physics/include/Axle/PhysicsScene.hpp), [job adapter](../../Engine/Physics/include/Axle/JobSystemEnkiTS.hpp).

`Axle::Physics` links only Core and Jolt. Graphics debug consumers live in
`Axle::PhysicsDebug`; Scene composition links that target. Physics emits through
`DrawDebug(JPH::DebugRenderer&, ...)` and does not include concrete GL/RHI renderers.
See [architecture acceptance](ARCHITECTURE_ACCEPTANCE.md) for the boundary/stress
gates and actual verification status.

## 2. Scene components and world APIs

RigidBody3D describes motion and runtime body identity; CollisionShape3D describes geometry and surface parameters. Scene lifecycle code combines them and synchronizes transforms. Changing authoring data does not automatically mutate a live body until synchronized.

PhysicsScene exposes body creation/destruction, transform/velocity access, activation, queries, constraints/listeners and gravity. Native Jolt access remains available; ownership and thread rules apply to those APIs as well.

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

`CastRay` takes origin, direction, maximum distance and an output result. `RayCastHit` exposes `HitBodyID`, `Position` and `Distance`.

## 4. Unified simulation driver and scheduling

Simulation execution is governed by a unified [SimulationDriver](../../Engine/Scene/include/Axle/Simulation/SimulationDriver.hpp), [SimulationClock](../../Engine/Scene/include/Axle/Simulation/SimulationClock.hpp), and [SimulationInputAdapter](../../Engine/Scene/include/Axle/Simulation/SimulationInputAdapter.hpp). Both the Editor and standalone Runtime share this exact pipeline, resolving the legacy update order divergence (defect `ECSJ-05`).

### Discrete simulation phases

The driver advances time in structured, deterministic phases:

1. **Clock accumulation**: `SimulationClock::Advance(rawDt)` scales time by speed multiplier, applies pause states, and accumulates elapsed time into 60Hz fixed ticks (`kFixedDeltaTime = 1.0f / 60.0f`). A hard cap (`kMaxFixedSubsteps = 4`) prevents spiral-of-death frame drops under load.
2. **Fixed substep loop** (for each accumulated tick):
   - `FramePhase::FixedControls`: Evaluates native script `OnFixedUpdate(fixedDt)` and controller inputs. The `SimulationInputAdapter` latches edge triggers across display frames until consumed by fixed ticks.
   - `FramePhase::Physics`: Calls `PhysicsScene::Step(fixedDt)` to advance the Jolt simulation.
   - `FramePhase::PostStepSync`: Synchronizes rigid body poses back to ECS `TransformComponent` and updates world transforms.
3. **Variable presentation frame**:
   - `FramePhase::FrameScripts`: Runs `OnUpdate(variableDt)` on active script instances.
   - `FramePhase::AnimationVFX`: Evaluates skeletal animation evaluators and particle emitters.
   - `FramePhase::LatePresentation`: Runs `OnLateUpdate(variableDt)` and generates camera view-projection matrices and render draw packets.

Single-frame stepping (`StepSingleFrame()`) executes exactly one fixed tick (60Hz) followed by variable presentation, giving bit-identical behavior between Editor stepping and Runtime simulation.

## 5. Character integration and retired Vehicle

`CharacterController` wraps Jolt `CharacterVirtual`. Its component supplies movement/jump data and synchronizes transforms/animation parameters.

The built-in Vehicle module and components were retired in `fa74981`; `be7b071` removed the remaining legacy payload holders and compatibility branches. Ordinary rigid-body and `CharacterController` behavior remains part of current correctness. The vehicle simulation replacement is planned as a standalone plugin in [OPT-07](../ROADMAP.md#opt-07---write-vehicle-as-a-complete-plugin).

## 6. Debug rendering and boundaries

`PhysicsDebugRenderer` and `RHIPhysicsDebugRenderer` adapt Jolt debug output to separate graphics paths. Future extraction will expose debug data through a pure contract consumed by a rendering bridge.
