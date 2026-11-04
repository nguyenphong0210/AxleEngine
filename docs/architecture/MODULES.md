# Subsystems and Module Reference

> **Dated source snapshot:** this survey predates the 2026-11-04 Vehicle
> retirement and later sample cleanup. It is retained for architectural context,
> not current API/feature support. Follow the [optimization roadmap](../ROADMAP.md)
> for execution; unavailable source paths below are historical references.

Reviewed: 2026-11-01. This quick reference describes current responsibilities and important boundaries. See [SAD](SAD.md), [SDD](SDD.md), [class diagrams](CLASS_RELATIONSHIPS.md) and the [complete dependency map](DEPENDENCY_MAP.md).

## 1. Engine/Core

Application, Layer/LayerStack, Log, UUID, JobSystem, FileSystem and Package. Application currently introduces upward Platform/Renderer/Physics dependencies. UUID uses a random-number generator; it is not documented here as cryptographically secure.

Source: [Engine/Core](../../Engine/Core).

## 2. Engine/Platform

Window/Input, event records and FileDialogs. Native GLFW types and graphics backend selection remain visible in the public API.

Source: [Engine/Platform](../../Engine/Platform).

## 3. Engine/Renderer

OpenGL RenderCommand, shader/buffer/VAO/texture/framebuffer, material, shadow, cubemap, skybox, camera and skinning resources. SceneRenderer orchestration lives in Engine/Scene.

Source: [Engine/Renderer](../../Engine/Renderer).

## 4. Engine/Audio

AudioEngine, AudioClip, AudioSource and AudioListener wrap miniaudio. Sources hold shared clip metadata and an owned sound object. Scene and VehicleAudio provide integration.

Source: [Engine/Audio](../../Engine/Audio).

## 5. Engine/Physics

PhysicsEngine/PhysicsScene, Jolt body/query/constraint APIs, CharacterController and the enkiTS job adapter. OpenGL/RHI debug renderers currently bring graphics and asset dependencies into this target.

Source: [Engine/Physics](../../Engine/Physics).

## 6. Engine/Scene

Scene/Entity, component storage, hierarchy/transforms, ParallelECS, scene/assembly serialization, native scripting, simulation and both scene rendering paths. SceneSerializerContext callbacks extend serialization for plugins.

Source: [Engine/Scene](../../Engine/Scene).

## 7. Engine/Asset

AssetMetadata/Serializer, static AssetManager registry/resource caches, AssetDSL, mesh import/serialization, TextureDescriptor, TerrainAsset and AssetCooker. Identity, GPU loading and tools are not yet separate targets.

Source: [Engine/Asset](../../Engine/Asset).

## 8. Engine/Project

ProjectConfig, static active Project and ProjectSerializer manage .axleproj settings and asset-path configuration.

Source: [Engine/Project](../../Engine/Project).

## 9. Engine/UI

Two targets share this directory: AxleUI2D provides documents/nodes/layout/binding/font/canvas geometry; AxleUI provides ImGui integration, inspectors, previews, content browser, UI/VFX designers and HLSLStudioPanel. Canvas2D currently retains CPU geometry rather than submitting it to the GPU.

Source: [Engine/UI](../../Engine/UI).

## 10. Engine/Vehicle (Gameplay Toolkit)

Vehicle wraps Jolt chassis/wheel constraints and exposes telemetry. VehicleAudio and VehicleVFX independently consume telemetry. Scene currently links this toolkit unconditionally.

Source: `Engine/Vehicle` (historical path; unavailable in this checkout).

## 11. Editor Application

EditorLayer composes scenes, physics, renderers/viewports, panels, project IO, import/export and script compile/reload. Play uses a cloned registry preserving entity UUIDs; Stop restores the edit scene. HLSL Studio open callbacks and UI rendering are wired in EditorLayer.

Source: [Editor](../../Editor).

## 12. Standalone Runtime Player

RuntimeLayer loads a project/package/start scene, rebuilds procedural plugin resources and updates simulation/rendering/UI. It currently uses the OpenGL SceneRenderer. Plugin/script/resource staging dependencies still connect its build to editor tooling.

Source: [Runtime](../../Runtime).

## 13. Engine/RHI

NRI device/surface/swapchain, resources, commands/barriers, DXC compiler/reflection, pipelines, descriptors/bindless, streaming and material/light helpers. RHIECSStreamer currently introduces EnTT at this low-level boundary.

Source: [Engine/RHI](../../Engine/RHI).

## 14. Engine/Animation

Skeletons, clips/channels/keyframes, poses, blend spaces, state machines, Animator, skeletal mesh/import. CPU evaluation, graphics upload and import currently share a target.

Source: [Engine/Animation](../../Engine/Animation).

## 15. Engine/VFX

ParticlePool SoA, emitter configuration/serialization, ParticleSystem, forces/noise/collision and OpenGL/RHI particle renderers. Scene components and vehicle/character effects integrate this subsystem.

Source: [Engine/VFX](../../Engine/VFX).

## 16. Spline and Terrain Plugins

Static libraries contain procedural data/generation, colliders, serialization and authoring in the same targets. Terrain depends on Spline; both publicly depend on AxleUI. They are not a general dynamic plugin loading system.

Source: [Plugins](../../Plugins).

## Tests and build composition

Test targets are defined in Tests/CMakeLists.txt and excluded from the default/Editor build. Tests/src/Test.cpp selects legacy groups; dedicated CPU/build/GPU targets register with CTest. Tests stage their own fixture copies and script DLL without an Editor or Runtime dependency. See [test layout](../../Tests/README.md) and [build workflows](../guides/BUILD_WORKFLOWS.md) for current commands and verification limits.

For proposed optional builds, runtime/editor separation and layering enforcement, see [Target Layered Architecture](TARGET_LAYERED_ARCHITECTURE.md).
