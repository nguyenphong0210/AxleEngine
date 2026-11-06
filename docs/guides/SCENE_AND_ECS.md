# Scene and ECS Guide

Reviewed: **2026-11-06** (post OPT-03). See [SDD](../architecture/SDD.md#4-scene-and-entity), [class relationships](../architecture/CLASS_RELATIONSHIPS.md#3-ecs-storage-and-entity-facade) and the [documentation hub](../README.md).

## 1. Identity, SceneData, and storage

`SceneData` owns an `entt::registry` and manages entity storage, hierarchy relationships, and transform propagation without coupling to concrete graphics or physics runtimes. `Scene` provides the high-level coordinator facade.

`Entity` is a lightweight value facade wrapping an `entt::entity` and a non-owning pointer to its `SceneData`/`Scene`. Registry-local handles and persistent 64-bit UUIDs are distinct; `IDComponent` stores the persistent UUID.

```cpp
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>

Axle::Scene scene;
Axle::Entity player = scene.CreateEntity("Player");
auto& transform = player.GetComponent<Axle::TransformComponent>();
transform.Translation = { 0.0f, 1.0f, 0.0f };
```

Validate handles with `if (entity)` before use. `Entity` does not keep `Scene` alive and becomes invalid when its underlying entity is destroyed.

## 2. Component families

Built-in types share [Components.hpp](../../Engine/Scene/include/Axle/Components.hpp):

| Family | Representative types |
|---|---|
| Identity/hierarchy | IDComponent, TagComponent, TransformComponent, HierarchyComponent |
| Graphics | MeshInstance3D, MaterialComponent, CameraComponent, DirectionalLightComponent, PointLightComponent, SkyboxComponent |
| Physics | RigidBody3D, CollisionShape3D, CharacterControllerComponent |
| Scripts/audio | NativeScriptComponent, AudioSourceComponent, AudioListenerComponent |
| Animation | SkeletalMeshComponent, AnimatorComponent |
| VFX | ParticleEmitterComponent, ParticleEmitterStack, CharacterVFXComponent |
| UI/assemblies | UICanvasComponent, AssemblyComponent |

MeshComponent aliases MeshInstance3D. Material assets use the pure CPU descriptor workflow. Retired Vehicle components are absent; vehicle simulation is planned as a standalone plugin in [OPT-07](../ROADMAP.md#opt-07---write-vehicle-as-a-complete-plugin).

## 3. Transforms, hierarchy, and revisions

`TransformComponent` stores local `Translation`, quaternion `Rotation`, and `Scale`. World matrices follow:

$$M_{world} = M_{parent\ world} M_{local}, \qquad M_{local} = T R S.$$

### Hierarchy operations and cycle safety
Use `Entity::SetParent` or `Scene::SetParent` for relationship updates and `keepWorldPosition` preservation. Parent/child relationships are strictly validated with Floyd cycle detection to prevent recursive infinite loops.

```cpp
Axle::Entity vehicle = scene.CreateEntity("Vehicle");
Axle::Entity camera = scene.CreateEntity("Camera");
camera.SetParent(vehicle, true);

for (Axle::Entity child : vehicle.GetChildren()) {
    auto& childTransform = child.GetComponent<Axle::TransformComponent>();
    // Work with child local transform.
}
```

### Monotonic transform revisions
Transform evaluation is read-only and side-effect free via `GetWorldTransform()`. Scene data tracks a monotonic transform revision counter via `GetTransformRevision()`. Downstream consumers (renderers, physics post-step sync, spatial audio) inspect this revision to skip redundant matrix re-evaluations when transforms are stationary.

## 4. Scenes, assemblies and play-mode clones

- **SceneSerializer**: Persists scenes to `.axlescene` format using embedded native UUIDs and raw root-relative asset paths.
- **AssemblySerializer**: Persists reusable sub-hierarchies and handles entity reference remapping upon instantiation.
- **Scene::Copy**: Creates an isolated `SceneData` copy for Editor Play while preserving UUIDs and mapping parent-child handles.
- **Play/Stop lifecycle**: On stopping Play mode, runtime state is released and the pristine authoring scene is restored.

`SceneSerializerContext` supplies caller-installed script and component callbacks. Keep extension hooks explicit to avoid upward dependencies from `SceneData`.

## 5. Parallel iteration and command buffer mutation boundaries

Parallel ECS iteration dispatches work across workers via `JobSystemEnkiTS`:

```cpp
#include <Axle/ParallelECS.hpp>

Axle::ParallelECS::ForEach<Axle::TransformComponent>(
    scene.GetRegistry(),
    [](Axle::TransformComponent& transform) {
        // Independent per-component work.
        transform.Scale = glm::max(transform.Scale, glm::vec3(0.001f));
    }
);
```

### Tombstone safety
Iterators handle EnTT view chunks correctly and explicitly skip tombstones/destroyed entities during parallel iterations.

### Structural mutation boundaries (`SceneCommandBuffer`)
Never call `CreateEntity`, `DestroyEntity`, `AddComponent`, or `RemoveComponent` directly inside parallel worker callbacks or active iteration loops. Instead, record structural changes into a [SceneCommandBuffer](../../Engine/Scene/include/Axle/SceneCommandBuffer.hpp):

```cpp
Axle::SceneCommandBuffer cmdBuffer;
cmdBuffer.DestroyEntity(deadEntity);
cmdBuffer.AddComponent<Axle::ParticleEmitterComponent>(targetEntity, emitterProps);

// Flush synchronously at the end of the frame or phase:
cmdBuffer.Execute(scene);
```

## 6. Extension checklist

Follow [component extension guidance](DEVELOPMENT.md#recipe-1-adding-a-new-ecs-component). Review serialization, clone/duplicate, destroy, runtime start/update/stop and inspector behavior together.
