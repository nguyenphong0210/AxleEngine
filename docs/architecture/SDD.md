# Software Design Description: Current System

> **Dated source snapshot:** this survey predates the 2026-11-04 Vehicle
> retirement and later sample cleanup. It is retained for architectural context,
> not current API/feature support. Follow the [optimization roadmap](../ROADMAP.md)
> for execution; unavailable source paths below are historical references.

Status: **AS-IS**, reviewed 2026-11-01. See [class diagrams](CLASS_RELATIONSHIPS.md), [execution flows](RUNTIME_FLOWS.md) and the [class index](CLASS_INDEX.md).

## 1. Ownership conventions

Value members and unique pointers have lifetimes attached to their owner. Shared pointers retain shared resources. Observer pointers and entity handles do not keep their referenced objects alive. Native GPU/OS handles may be explicitly owned by wrappers even when represented as raw pointers; inspect allocation/destruction rather than inferring ownership from syntax.

## 2. Application and layers

Sources: [Application](../../Engine/Core/include/Axle/Application.hpp), [implementation](../../Engine/Core/src/Application.cpp), [LayerStack](../../Engine/Core/include/Axle/LayerStack.hpp), [UI contract](../../Engine/Core/include/Axle/ApplicationUI.hpp).

| Type | Responsibility | Lifetime/relationship |
|---|---|---|
| ApplicationSpecification | Window settings and graphics backend | Value configuration; default OpenGL |
| Application | Logging/jobs/physics bootstrap, window creation, callbacks and loop | unique_ptr Window/UIContext, value LayerStack, static instance pointer |
| Layer | Attach/update/UI/detach/resize/drop hooks | Base of EditorLayer and RuntimeLayer |
| LayerStack | Store layers and dispatch attach/detach | vector of shared_ptr Layer |
| ApplicationUIContext | Init/Shutdown/Begin/End contract | Init takes GLFWwindow* |
| ImGuiLayer | Concrete UI context for supported backends | Defined in AxleUI |

Run resets scroll, polls window events, executes pinned jobs, computes delta time, updates layers, optionally runs the UI frame and swaps window buffers. PushLayer attaches immediately; pop/clear detach. Application destruction clears layers, shuts down UI, PhysicsEngine and JobSystem; members such as Window are destroyed afterward according to C++ member lifetime rules.

Application therefore currently contains concrete upper-subsystem orchestration inside Core.

## 3. Platform and foundation services

Sources: [Platform headers](../../Engine/Platform/include/Axle), [Core headers](../../Engine/Core/include/Axle).

- Window manages GLFWwindow, WindowData and callbacks, and exposes the native pointer, backend and cursor controls. Event records carry callback data.
- Input exposes static accessors and an InputContext containing delegated function pointers. The script DLL can use host-side input functions instead of unsynchronized private static state.
- FileDialogs implements native OS interactions. This is not a general engine-wide event bus.
- Log retains core/client spdlog loggers and supports additional sinks. EditorConsoleSink feeds the editor console.
- JobSystem provides Dispatch, Wait, ParallelFor and main-thread dispatch on enkiTS. JobHandle retains an ICompletable through shared ownership. The ParallelFor implementation dispatches and waits; a parallel operation is not necessarily asynchronous to its caller.
- FileSystem retains the active asset directory and a mounted Package in implementation-level static state. Reads use package entries when present and otherwise resolve disk/resource paths.

## 4. Scene and Entity

Sources: [Scene](../../Engine/Scene/include/Axle/Scene.hpp), [Entity](../../Engine/Scene/include/Axle/Entity.hpp), [Components](../../Engine/Scene/include/Axle/Components.hpp), [ParallelECS](../../Engine/Scene/include/Axle/ParallelECS.hpp).

Scene owns entt::registry, RuntimeVehicleData records and UIDataContext; it observes a PhysicsScene. Its API combines entity creation/destruction/duplication, hierarchy, transforms, runtime/script lifecycle, copying and rendering operations.

Entity is a value facade containing entt::entity and Scene*. Its component methods operate on the Scene registry. It does not own Scene or a private component list. Its boolean conversion validates the pointer/handle and registry membership.

TransformComponent stores translation, quaternion rotation, scale and cached local/world matrices. HierarchyComponent stores parent/children handles and inheritance settings. Scene/Entity APIs handle reparenting and cycle checks; direct disconnected field edits can bypass those invariants.

ParallelECS supplies template helpers for registry views, JobSystem work and deferred structural commands. Its presence does not mean every Scene loop is parallelized.

| Component family | Main types | Integration state |
|---|---|---|
| Identity/hierarchy | IDComponent, TagComponent, TransformComponent, HierarchyComponent | UUIDs, transforms and entity links |
| Rendering | MeshInstance3D, MaterialComponent, camera/lights/skybox | Both OpenGL and RHI resource references |
| Physics | RigidBody3D, CollisionShape3D, CharacterControllerComponent | Body/shape/controller runtime state |
| Scripts | NativeScriptComponent | Instance pointer, lifecycle callbacks, metadata/field buffers |
| Vehicle | VehicleComponent, WheelComponent, VehicleAudioComponent, VehicleVFXComponent | Inputs, telemetry and runtime adapters |
| Animation | SkeletalMeshComponent, AnimatorComponent | Shared mesh/skeleton/animator resources |
| VFX | ParticleEmitterStack/Component, CharacterVFXComponent | Emitter configuration and runtime systems |
| UI | UICanvasComponent | Asset path, shared document, datasource UUID and ordering |
| Prefabs | AssemblyComponent | Assembly association |

MeshComponent is a compatibility alias of MeshInstance3D, not a subclass. Scene::Copy explicitly copies supported component families and rebuilds relationships/resources as needed. It preserves entity UUIDs while creating handles in a new registry. Additions to component types must consider copy, serialization, inspection and lifecycle paths together.

## 5. Runtime integration and update contract

Source: [Scene.cpp](../../Engine/Scene/src/Scene.cpp).

OnRuntimeStart creates or restores physics, vehicle, audio, character and script resources from components. OnUpdateRuntime updates scripts, vehicle input/poses/telemetry, vehicle audio/VFX, spatial sources/listener, characters and animation parameters, animators, then particle emitters. OnRuntimeStop releases runtime state. OnImGuiRender forwards script UI hooks.

PhysicsScene::Step is called by the application layer, outside OnUpdateRuntime. Current Editor Play runs Scene update before physics; standalone Runtime runs physics before Scene update. Editor scales the physics timestep separately from the Scene update timestep. These differences must be resolved deliberately if scheduling is extracted.

## 6. Physics and vehicles

Sources: [Physics headers](../../Engine/Physics/include/Axle), `Vehicle headers` (historical path; unavailable in this checkout).

| Type | Design role |
|---|---|
| PhysicsEngine | Process-level Jolt initialization/shutdown, currently called by Application |
| PhysicsScene | Value Jolt PhysicsSystem, exclusive allocator/job adapter, body/query/constraint/debug APIs |
| JobSystemEnkiTS | JPH::JobSystemWithBarrier implementation routing jobs through the shared scheduler |
| CharacterController | Jolt CharacterVirtual wrapper with movement, grounding and step behavior |
| PhysicsDebugRenderer | Jolt DebugRendererSimple adapter to OpenGL line resources |
| RHIPhysicsDebugRenderer | Jolt debug adapter with RHI resources and a Jolt initialization helper |
| Vehicle | PhysicsScene observer, Jolt constraint/collision tester references, chassis and controller handles |
| VehicleAudio | Shared audio sources driven by telemetry/driver input |
| VehicleVFX | Shared particle systems driven by telemetry/driver input |

Scene's RuntimeVehicleData exclusively owns Vehicle instances and retains wheel entity handles. Vehicle itself does not own VehicleAudio or VehicleVFX; Scene updates those adapters through components. Debug rendering currently ties the Physics target to graphics and asset dependencies.

## 7. OpenGL rendering

Sources: [Renderer headers](../../Engine/Renderer/include/Axle), [SceneRenderer](../../Engine/Scene/include/Axle/SceneRenderer.hpp).

RenderCommand wraps context/render operations. Shader, buffers, VertexArray, Texture2D, Framebuffer, ShadowMap, Cubemap and Skybox represent graphics resources. Material contains shading properties, texture maps and schema/custom-property information. PerspectiveCamera holds matrices; its controller integrates input. SkinningPipeline uploads bone palettes.

SceneRenderer belongs to the Scene target. It retains shared shaders, shadow/skybox, skinning and particle renderers and orchestrates scene passes. RenderScene accepts Scene, camera, optional framebuffer, physics/debug renderer and SceneRenderSettings.

Scene still implements geometry/shadow/skeletal/particle rendering helpers. SceneRenderSettings exposes postprocessing options, but matching settings names do not establish identical behavior on all backends.

## 8. RHI and RHI scene rendering

Sources: [RHI headers](../../Engine/RHI/include/Axle/RHI), [RHISceneRenderer](../../Engine/Scene/include/Axle/RHISceneRenderer.hpp).

| Types | Responsibility |
|---|---|
| RHIDevice, NRIInterfaces | Adapter/device/capability/queue discovery and NRI interface access |
| RHISurface, RHISwapChain | Native surface, acquire/present, resize, frame fences/allocators |
| RHIBuffer, RHIMemoryManager, RHITexture | GPU resource allocation, upload/readback and state |
| RHICommandBuffer, barrier/state tracker types | Recording and synchronization |
| RHIShaderCompiler/Reflection, RHIPipelineLayout/Cache | DXC compilation, reflection and pipeline layout/cache |
| RHIStreamer, RHIECSStreamer | GPU streaming and an EnTT view adapter |
| RHIMesh, RHIMaterial, RHISampler | Mesh/material resources and sampling |
| RHIDescriptorPool, RHIBindlessRegistry | Descriptor allocation and bindless indices |
| RHIMultiLightBuffer | Light record packing/upload |
| RHISwitcher | Backend configuration and fallback |
| RHISceneRenderer | Scene extraction, graphics/postprocessing passes, targets, particles/debug/picking |
| RHIMeshRegistry, RHIObjectPicker | Scene mesh cache and entity ID picking/readback |

RHISceneRenderer is independent of SceneRenderer; it includes that header to share settings rather than inheriting from the class. It owns particle/debug/picking helpers and intermediate targets. RHIDevice has singleton-style Get access but also a public constructor. NRI types in public APIs make the current RHI a relatively direct NRI wrapper, not a fully vendor-neutral interface.

## 9. Assets, DSL and package storage

Sources: [Asset headers](../../Engine/Asset/include/Axle), [Package](../../Engine/Core/include/Axle/Package.hpp), [FileSystem](../../Engine/Core/src/FileSystem.cpp).

- AssetMetadata stores UUID/type/path. AssetMetadataSerializer reads/writes companion metadata.
- AssetManager is a static registry/cache service with UUID/path maps, shared Texture2D/Material/RHIMaterial caches and a recursive mutex. Missing metadata generates a new UUID.
- AssetProperty, AssetBlock and AssetDocument form a typed DSL tree: value properties, shared child blocks and a shared root. AssetDSL handles parsing/serialization; domain loaders supply interpretation.
- AssetSerializer handles mesh text/binary, model conversion and mesh manifests. This is distinct from PackageWriter's .axlepak container.
- TextureDescriptor, TerrainAsset, ParticleEmitterSerializer, Material and UIParser are domain-specific consumers rather than one universal object serializer.
- AssetCooker traverses files, optionally bakes meshes, writes packages and stages distribution files using CookSettings/CookResult.
- Package indexes entries by path/UUID; PackageWriter gathers pending entries and writes header/chunks/table/checksums. Current chunk writing is uncompressed.

Stable metadata identity is a required workflow invariant. The current Git ignore policy for .meta files is a separate risk documented in the SAD.

## 10. Native scripts and DLL boundary

Sources: [Scene scripting headers](../../Engine/Scene/include/Axle), [exports](../../Editor/src/Scripts/ScriptsModule.cpp).

| Type | Contract |
|---|---|
| ScriptableEntity | Entity facade and virtual create/update/UI/destroy hooks |
| NativeScriptComponent | Script name, raw instance, factory/destroy callbacks and reflected field buffers |
| ScriptFieldRegistry | Collects reflection metadata |
| ScriptMetadata | Name, factory, destroyer and fields |
| ScriptRegistry | Host registration, lookup, binding and factory lifecycle |
| ScriptModuleRegistry | DLL-side registration under AXLE_SCRIPTS_DLL |
| ScriptHotReloadManager | Source/shadow DLL paths, native module handle, weak Scene and reload workflow |
| ScriptCompiler | Build-process worker, status, cancellation, logs and compiler diagnostics |

AXLE_REGISTER_SCRIPT selects host or module registration through compile definitions. Exported functions initialize/shutdown scripts, report count and synchronize input/ImGui context. Although symbol names use C linkage, metadata includes C++ types; this does not guarantee ABI portability across toolchains.

Instances must be destroyed before module unload using the correct lifecycle callbacks. Reload preserves field state, unloads the old module, loads a shadow copy, registers metadata and recreates instances. Failure after unload is not guaranteed to roll back. Windows-specific implementation does not establish Linux parity.

CompileAsync calls its completion callback directly on its worker. The current editor callback reloads scene scripts and updates editor state there; it does not visibly marshal this path through JobSystem's main-thread queue.

## 11. Animation

Sources: [Animation headers](../../Engine/Animation/include/Axle).

Skeleton contains bones/hierarchy. AnimationClip holds channels and keyframes. AnimationPose holds BoneTransform values and computes global/skinning matrices. BlendSpace1D retains clip points and samples by parameter.

AnimationState references a clip or blend space; AnimationTransition contains condition/crossfade settings. AnimationStateMachine owns state/transition/parameter containers and its current pose. Animator retains shared skeleton/state machine and fallback pose/matrices and exposes UploadToPipeline.

SkeletalMesh contains CPU mesh data and a shared VertexArray; SkeletalMeshImporter loads FBX/glTF resources. CPU evaluation, import and GPU upload are currently in the same module.

## 12. Audio and particles

Sources: [Audio headers](../../Engine/Audio/include/Axle), [VFX headers](../../Engine/VFX/include/Axle), [ParticleSystem](../../Engine/VFX/src/ParticleSystem.cpp).

AudioEngine is a service wrapping miniaudio. AudioClip retains file/format/stream metadata. AudioSource retains a shared clip and exclusively owned ma_sound; AudioListener is a static listener API. Scene synchronizes source/listener position, direction and velocity. VehicleAudio is a gameplay adapter.

ParticleSystem contains configuration and ParticlePool by value, named shared child systems and sub-emitter callbacks. ParticlePool uses SoA vectors for position, velocity, age, lifetime, color and other attributes. Simulation evaluates configured ParticleForceField records, CurlNoise and ParticleCollisionSolver. ParticleForceFieldManager exists as a helper but is not the direct force-evaluation call in the surveyed ParticleSystem loop.

ParticleRenderer and RHIParticleRenderer consume simulation data; neither derives from ParticleSystem. Scene emitter components connect descriptor/configuration to runtime systems. VFXEditorPanel has independent preview systems/renderers and observes scene selection.

## 13. UI2D, binding and authoring

Sources: [UI types](../../Engine/UI/include/Axle/UI), [Canvas2D](../../Engine/UI/src/UI2D/Canvas2D.cpp), [Runtime UI](../../Runtime/src/RuntimeLayer.cpp).

UIDocument exclusively owns nodes in an arena of unique_ptr. Root/ID map and node parent/children are observer pointers; parents do not own child lifetimes. UINode is the polymorphic base for canvas, rectangle, text, image, progress, gauge and box-container nodes. UIButtonNode derives from UIRectangleNode. Legacy geometry and RectTransform coexist.

UIParser populates a document. UIPropertyBinding targets a node and records direct/interpolated/ternary expressions. UIDataContext stores variant values/actions and a parent observer for fallback; it also currently contains Vehicle.* aliases.

UIDocument evaluates binding, measures/arranges nodes, processes hit/focus/input and emits Canvas2D geometry. Canvas2D holds CPU vertex/index/clip/statistics buffers. EndCanvas updates statistics without GPU submission. Runtime RenderUI builds scoped datasource/script-field contexts and ends at EndCanvas; no upload/submit is visible in that function.

UICanvasComponent retains its document, asset path, datasource UUID and sorting/visibility. UICanvasDesignerPanel retains a document and mock context. UICanvasImGuiRenderer is a preview adapter; an ImGui preview does not prove runtime canvas GPU submission.

## 14. Spline and terrain

Sources: [Spline](../../Plugins/Spline/include/AxleSpline), [Terrain](../../Plugins/Terrain/include/AxleTerrain).

| Type | Responsibility |
|---|---|
| Curve3D | Nodes, evaluation, frames and arc-length data |
| SplineComponent | Value curve/settings, physics BodyID and shared TrackVAO |
| TrackGenerator | CPU track geometry, VAO creation and collider creation |
| SplineSerializer / SplineEditor | Component IO and authoring/gizmos |
| TerrainData | Heights/splat maps, coordinate transforms, sampling, generation and file IO |
| TerrainComponent | Data/chunks, shared CombinedVAO, BodyID and sculpt state |
| TerrainMesh / TerrainCollider | Graphics mesh and Jolt heightfield creation |
| TerrainSculptor | Brush edits and changed-region results |
| TerrainSplineCarver | Bridge between terrain data and spline curves |
| TerrainSerializer / TerrainEditor | Scene IO and authoring |

Plugin lifecycle helpers do not implement a general dynamic plugin discovery system. Runtime uses track/terrain APIs that build OpenGL VAOs. Both libraries combine runtime/editor source and link AxleUI.

## 15. Editor, project and tests

EditorLayer composes scenes, physics, cameras/renderers/viewports, panels, play controls, project/scene IO, import/export, compile/reload and backend settings. Panels receive observer contexts; preview panels may own separate resources.

HLSLStudioPanel retains source/saved-source, path, stage/entry settings and diagnostics. It calls RHIShaderCompiler::CompileForActiveBackend. EditorLayer wires open callbacks from the content browser and scene inspector and invokes the panel's UI rendering. Compilation success does not by itself demonstrate pipeline replacement.

Project contains ProjectConfig and a static active-project reference. ProjectSerializer retains a shared Project. There is no single EngineContext owning all currently static services.

AxleTests is defined under Editor, uses TestCommon and Test_Milestone files, selects groups with CLI flags and returns an exit code. It includes graphics/headless helpers. Tests currently depend on the Editor build and resource staging; first-party CMake does not register CTest tests.

## 16. Existing extension mechanisms

1. Layer lifecycle hooks.
2. Script registration, factory callbacks and reflection.
3. SceneSerializerContext callbacks for plugin serialization.
4. EnTT component storage/views and ParallelECS helpers.
5. UI node virtual properties/measure/draw and context actions.
6. Asset DSL trees interpreted by domain loaders.
7. Jolt adapters for scheduling and debug output.

These are concrete seams for incremental modularization. Adding a component still requires coordinated handling of copy, serialization, inspection and runtime lifecycle; the engine does not automatically discover every component or plugin type.
