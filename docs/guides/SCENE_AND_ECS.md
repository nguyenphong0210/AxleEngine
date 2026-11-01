# Scene and ECS Guide

Reviewed: **2026-11-01**. See [SDD](../architecture/SDD.md#4-scene-and-entity), [class relationships](../architecture/CLASS_RELATIONSHIPS.md#3-ecs-storage-and-entity-facade) and the [documentation hub](../README.md).

## 1. Identity and storage

Scene owns an entt::registry. Entity is a value facade containing an entt::entity and a non-owning Scene pointer. Registry-local handles and persistent 64-bit UUIDs are distinct; IDComponent stores the latter.

CreateEntity adds identity, tag, transform and hierarchy components:

```cpp
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>

Axle::Scene scene;
Axle::Entity player = scene.CreateEntity("Player");
auto& transform = player.GetComponent<Axle::TransformComponent>();
transform.Translation = { 0.0f, 1.0f, 0.0f };
```

Validate handles and required components before use. Entity does not keep Scene alive and becomes invalid when its underlying entity is destroyed.

## 2. Component families

Built-in types share [Components.hpp](../../Engine/Scene/include/Axle/Components.hpp), a coupling point discussed in the [assessment](../architecture/ASSESSMENT.md).

| Family | Representative types |
|---|---|
| Identity/hierarchy | IDComponent, TagComponent, TransformComponent, HierarchyComponent |
| Graphics | MeshInstance3D, MaterialComponent, CameraComponent, light and skybox components |
| Physics | RigidBody3D, CollisionShape3D, CharacterControllerComponent |
| Scripts/audio | NativeScriptComponent, AudioSourceComponent, AudioListenerComponent |
| Vehicles | VehicleComponent, WheelComponent, VehicleAudioComponent, VehicleVFXComponent |
| Animation | SkeletalMeshComponent, AnimatorComponent |
| VFX | ParticleEmitterComponent, ParticleEmitterStack, CharacterVFXComponent |
| UI/assemblies | UICanvasComponent, AssemblyComponent |

Use actual declarations for constructors/defaults. MeshComponent aliases MeshInstance3D. Material assets use the current descriptor workflow; legacy extensions do not define a universal material schema.

## 3. Transforms and hierarchy

TransformComponent stores translation, quaternion rotation, scale and cached matrices. With transform inheritance:

$$M_{world} = M_{parent\ world} M_{local}, \qquad M_{local} = T R S.$$

Use Entity::SetParent or Scene::SetParent for cycle checks, relationship updates and keepWorldPosition policy. Parent/children are handles, not owning objects.

```cpp
Axle::Entity vehicle = scene.CreateEntity("Vehicle");
Axle::Entity camera = scene.CreateEntity("Camera");
camera.SetParent(vehicle, true);

for (Axle::Entity child : vehicle.GetChildren()) {
    auto& childTransform = child.GetComponent<Axle::TransformComponent>();
    // Work with the child's local transform.
}
```

Scene propagates world transforms through UpdateWorldTransforms/UpdateWorldTransformsParallel. Construct quaternion rotations with GLM quaternion operations rather than treating Rotation as Euler storage.

## 4. Scenes, assemblies and play-mode clones

- SceneSerializer persists the level through the current human-readable writer/parser.
- AssemblySerializer persists reusable hierarchies and implements instantiation identity/reference mapping.
- Scene::Copy creates a separate registry for Editor Play while preserving UUIDs and mapping handles/references into the clone.
- Stop releases runtime state, restores the original Scene/physics bodies and rebinds panels.

These operations have different identity policies. Do not replace explicit clone logic with a raw memory copy. See [Scene.cpp](../../Engine/Scene/src/Scene.cpp) and the [play/stop sequence](../architecture/RUNTIME_FLOWS.md#3-editor-play-pause-and-stop).

SceneSerializerContext supplies caller-installed script/plugin callbacks. Keep extension hooks explicit instead of making Scene depend on every plugin.

## 5. Parallel iteration

ParallelECS dispatches matching-view work through JobSystem and has sequential fallbacks. Supported callbacks are documented in [ParallelECS.hpp](../../Engine/Scene/include/Axle/ParallelECS.hpp).

```cpp
#include <Axle/ParallelECS.hpp>

Axle::ParallelECS::ForEach<Axle::TransformComponent>(
    scene.GetRegistry(),
    [](Axle::TransformComponent& transform) {
        // Independent per-entity work only.
        transform.Scale = glm::max(transform.Scale, glm::vec3(0.001f));
    }
);
```

Avoid unsynchronized shared writes or structural registry changes from worker callbacks. Use supported deferred commands where appropriate and check the thread contract of called subsystems. Physics queries and graphics calls do not become safe merely by placing them in a parallel loop.

## 6. Extension checklist

Follow [component extension guidance](DEVELOPMENT.md#recipe-1-adding-a-new-ecs-component). Review serialization, clone/duplicate, destroy, runtime start/update/stop and inspector behavior together. Domain component extraction is part of the [target layered design](../architecture/TARGET_LAYERED_ARCHITECTURE.md).
