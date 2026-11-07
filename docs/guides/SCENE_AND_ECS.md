# Scene and ECS Guide

Reviewed: **2026-11-07** (OPT-02/03 corrections). See the [independent review](../reviews/OPT_02_03_CODEX_REVIEW.md), [SDD](../architecture/SDD.md#4-scene-and-entity) and the [documentation hub](../README.md).

## 1. Identity, SceneData, and storage

`SceneData` owns an `entt::registry` and manages entity storage, hierarchy relationships, and transform propagation without coupling to concrete graphics or physics runtimes. `Scene` provides the high-level coordinator facade.

`Axle::SceneData` is a separate target in `Engine/SceneData`, linked only to Core.
Use `DataComponents.hpp` and `SceneDataDocument` for CPU construction/copy and
structural document loading with passive system/plugin sections. Concrete codecs
are installed by Scene composition; generic data tools do not initialize them implicitly.

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

Validate handles with `if (entity)` before use. A weak lifetime token and reset epoch invalidate handles on entity destruction, SceneData Clear, or owner destruction. `GetScene()` returns null for generic SceneData.

## 2. Component families

Basic identity, transform and hierarchy types live in [DataComponents.hpp](../../Engine/SceneData/include/Axle/DataComponents.hpp). Concrete system types are exposed by [Components.hpp](../../Engine/Scene/include/Axle/Components.hpp):

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
`GetWorldTransform()` reads the last completed cache without mutation. After changing authored local values, explicitly call `UpdateWorldTransforms()` for the forest, or `RecomputeWorldTransform()` for one entity and its ancestors, before reading world matrices. Reparenting publishes the changed entity path; publish the forest once after a batch before reading descendants. `GetTransformRevision()` advances when an evaluation completes; pair a revision with its scene identity when switching scenes. Parent links are authoritative and inverse child lists are rebuilt before disjoint subtree evaluation.

## 4. Scenes, assemblies and play-mode clones

- **SceneSerializer**: Persists scenes to `.axlescene` format using embedded native UUIDs and raw root-relative asset paths.
- **AssemblySerializer**: Persists reusable sub-hierarchies and handles entity reference remapping upon instantiation.
- **Scene::Copy**: Creates an isolated `SceneData` copy for Editor Play while preserving UUIDs and mapping parent-child handles.
- **Play/Stop lifecycle**: On stopping Play mode, runtime state is released and the pristine authoring scene is restored.

Editor Play owns a separate physics world. Runtime body IDs are not copied with
authored values; the runtime bridge creates bodies in the destination world.

`SceneSerializerContext` supplies caller-installed script and component callbacks. Keep extension hooks explicit to avoid upward dependencies from `SceneData`.

## 5. Parallel iteration and command buffer mutation boundaries

Parallel ECS iteration dispatches work across workers via `JobSystem`:

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
Never call `CreateEntity`, `DestroyEntity`, `AddComponent`, or `RemoveComponent` directly inside parallel worker callbacks or active iteration loops. Instead, record structural changes into a [SceneCommandBuffer](../../Engine/SceneData/include/Axle/SceneCommandBuffer.hpp):

```cpp
Axle::SceneCommandBuffer cmdBuffer;
cmdBuffer.DestroyEntity(deadEntity);
cmdBuffer.EmplaceOrReplace<Axle::TagComponent>(targetEntity, "Updated");

// Flush synchronously at the end of the frame or phase:
cmdBuffer.Execute(scene);
```

## 6. Extension checklist

Follow [component extension guidance](DEVELOPMENT.md#recipe-1-adding-a-new-ecs-component). Review serialization, clone/duplicate, destroy, runtime start/update/stop and inspector behavior together.
