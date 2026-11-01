# Class Relationships: Current System

Reviewed: 2026-11-01. These diagrams show key relationships rather than every extracted type; see the [class index](CLASS_INDEX.md) for the wider declaration inventory.

## Notation

- `Base <|-- Derived`: inheritance.
- `Owner *-- Value`: value or exclusive ownership.
- `Holder o-- Shared`: shared resource retention.
- `A --> B`: association/reference without an ownership claim.
- `A ..> B`: API use, creation or argument dependency.
- Multiplicity describes members/containers, not guaranteed runtime population. External types may use shortened labels.

## 1. Application and products

```mermaid
classDiagram
    class Application {
        +Run()
        +Close()
        +PushLayer(layer)
        +SetUIContext(context)
    }
    class Layer {
        +OnAttach()
        +OnUpdate(ts)
        +OnImGuiRender()
        +OnDetach()
    }
    class ApplicationUIContext {
        <<interface>>
        +Init(window)
        +Begin()
        +End()
        +Shutdown()
    }
    Application *-- Window : unique_ptr
    Application *-- LayerStack : value
    Application *-- ApplicationUIContext : unique_ptr
    LayerStack o-- "0..*" Layer : shared_ptr
    Layer <|-- EditorLayer
    Layer <|-- RuntimeLayer
    ApplicationUIContext <|-- ImGuiLayer
    Application ..> PhysicsEngine : init / shutdown
    Application ..> JobSystem : init / pinned tasks / shutdown
    Application ..> RenderCommand : OpenGL bootstrap
```

Application exclusively owns Window/UIContext and contains LayerStack; the stack retains shared layers. EditorLayer and RuntimeLayer derive from Layer, not Application.

Sources: [Engine/Core/include/Axle/Application.hpp](../../Engine/Core/include/Axle/Application.hpp), [Engine/Core/include/Axle/LayerStack.hpp](../../Engine/Core/include/Axle/LayerStack.hpp), [Engine/UI/include/Axle/ImGuiLayer.hpp](../../Engine/UI/include/Axle/ImGuiLayer.hpp).

## 2. Editor composition and contexts

```mermaid
classDiagram
    EditorLayer o-- Scene : edit / runtime / active shared_ptr
    EditorLayer *-- PhysicsScene : unique_ptr
    EditorLayer *-- SceneRenderer : unique_ptr
    EditorLayer *-- RHISceneRenderer : unique_ptr
    EditorLayer *-- RHIViewportDescriptor : unique_ptr
    EditorLayer *-- PerspectiveCameraController : unique_ptr
    EditorLayer *-- SceneHierarchyPanel : value
    EditorLayer *-- ContentBrowserPanel : value
    EditorLayer *-- AssetPreviewPanel : value
    EditorLayer *-- AssemblyEditorPanel : value
    EditorLayer *-- VFXEditorPanel : value
    EditorLayer *-- UICanvasDesignerPanel : value
    EditorLayer *-- HLSLStudioPanel : value declared in header
    SceneHierarchyPanel --> Scene : context pointer
    SceneHierarchyPanel --> PhysicsScene : context pointer
    AssetPreviewPanel --> Scene : spawn context
    AssemblyEditorPanel --> Scene : instantiate context
    VFXEditorPanel --> Scene : context pointer
    VFXEditorPanel *-- ParticleSystem : preview unique_ptr
    UICanvasDesignerPanel o-- UIDocument : preview shared_ptr
    UICanvasDesignerPanel *-- UIDataContext : mock data value
```

Editor owns panel values and scene/controller references. Panel context pointers observe the active scene and are updated on play/stop. Preview tools may own independent rendering resources.

Sources: [Editor/src/EditorLayer.hpp](../../Editor/src/EditorLayer.hpp), [Engine/UI/include/Axle/AssetPreviewPanel.hpp](../../Engine/UI/include/Axle/AssetPreviewPanel.hpp), [Engine/UI/include/Axle/VFXEditorPanel.hpp](../../Engine/UI/include/Axle/VFXEditorPanel.hpp).

## 3. ECS storage and entity facade

```mermaid
classDiagram
    class Scene {
        +CreateEntity(name)
        +DestroyEntity(entity, physics)
        +SetParent(child, parent, keepWorld)
        +OnRuntimeStart(physics)
        +OnUpdateRuntime(ts, physics)
        +OnRuntimeStop(physics)
        +Copy(scene)
    }
    class Entity {
        -entityHandle
        -Scene scene
        +AddComponent()
        +GetComponent()
        +GetChildren()
    }
    class EnttRegistry {
        <<external>>
    }
    Scene *-- EnttRegistry : registry value
    Scene *-- UIDataContext : value
    Scene *-- "0..*" RuntimeVehicleData : vector values
    RuntimeVehicleData *-- Vehicle : unique_ptr
    Scene --> PhysicsScene : runtime observer
    Entity --> Scene : non owning pointer
    EnttRegistry *-- "0..*" TransformComponent : component storage
    EnttRegistry *-- "0..*" HierarchyComponent : component storage
    EnttRegistry *-- "0..*" NativeScriptComponent : component storage
    EnttRegistry *-- "0..*" MeshInstance3D : component storage
    HierarchyComponent --> EnttRegistry : entity handles
    ParallelECS ..> EnttRegistry : views
    ParallelECS ..> JobSystem : dispatch
```

Scene owns the registry and runtime vehicle records. Entity is a non-owning facade. Hierarchy links store entity handles; they do not own another registry.

Sources: [Engine/Scene/include/Axle/Scene.hpp](../../Engine/Scene/include/Axle/Scene.hpp), [Engine/Scene/include/Axle/Entity.hpp](../../Engine/Scene/include/Axle/Entity.hpp), [Engine/Scene/include/Axle/Components.hpp](../../Engine/Scene/include/Axle/Components.hpp).

## 4. Native scripting

```mermaid
classDiagram
    ScriptableEntity *-- Entity : value facade
    ScriptableEntity <|-- CarController
    ScriptableEntity <|-- RacingHUD
    ScriptableEntity <|-- ThirdPersonController
    ScriptableEntity <|-- ThirdPersonCamera
    ScriptableEntity <|-- VehicleChaseCamera
    ScriptableEntity <|-- RotatorScript
    NativeScriptComponent --> ScriptableEntity : Instance lifecycle via callbacks
    NativeScriptComponent ..> ScriptRegistry : Bind
    ScriptRegistry ..> ScriptMetadata : static registry
    ScriptMetadata *-- "0..*" ScriptField : field records
    ScriptFieldRegistry *-- "0..*" ScriptField : vector
    ScriptHotReloadManager --> Scene : weak_ptr active scene
    ScriptHotReloadManager ..> ScriptRegistry : register / clear dynamic
    ScriptHotReloadManager ..> ScriptModuleRegistry : DLL export bridge
    EditorLayer ..> ScriptCompiler : compile request
    EditorLayer ..> ScriptHotReloadManager : reload after compile
```

NativeScriptComponent manages instance lifecycle through callbacks despite using a raw pointer. Script metadata/registration differs between host and DLL builds. Sample projects also contain some script names shown here.

Sources: [Engine/Scene/include/Axle/ScriptableEntity.hpp](../../Engine/Scene/include/Axle/ScriptableEntity.hpp), [Engine/Scene/include/Axle/ScriptRegistry.hpp](../../Engine/Scene/include/Axle/ScriptRegistry.hpp), [Engine/Scene/include/Axle/ScriptModule.hpp](../../Engine/Scene/include/Axle/ScriptModule.hpp), [Editor/src/Scripts/ScriptsModule.cpp](../../Editor/src/Scripts/ScriptsModule.cpp).

## 5. Physics, jobs and vehicles

```mermaid
classDiagram
    class JoltJobSystemWithBarrier {
        <<external>>
    }
    class JoltDebugRendererSimple {
        <<external>>
    }
    PhysicsScene *-- JoltPhysicsSystem : value
    PhysicsScene *-- JobSystemEnkiTS : unique_ptr
    PhysicsScene *-- JoltTempAllocator : unique_ptr
    JoltJobSystemWithBarrier <|-- JobSystemEnkiTS
    JobSystemEnkiTS ..> JobSystem : enkiTS scheduler
    JoltDebugRendererSimple <|-- PhysicsDebugRenderer
    JoltDebugRendererSimple <|-- RHIPhysicsDebugRenderer
    PhysicsScene ..> PhysicsDebugRenderer : debug output
    CharacterController --> PhysicsScene : world access
    Vehicle --> PhysicsScene : observer
    Vehicle --> JoltVehicleConstraint : JPH Ref
    Vehicle --> VehicleConfig : config value
    Vehicle ..> VehicleTelemetryData : produces
    VehicleAudio ..> VehicleTelemetryData : consumes
    VehicleAudio o-- "0..*" AudioSource : shared_ptr
    VehicleVFX ..> VehicleTelemetryData : consumes
    VehicleVFX o-- "0..*" ParticleSystem : shared_ptr
```

Vehicle observes PhysicsScene and holds Jolt references. VehicleAudio/VFX consume telemetry; Vehicle does not directly own them. The RHI debug renderer also privately inherits an initialization helper omitted from this overview.

Sources: [Engine/Physics/include/Axle/PhysicsScene.hpp](../../Engine/Physics/include/Axle/PhysicsScene.hpp), [Engine/Physics/include/Axle/JobSystemEnkiTS.hpp](../../Engine/Physics/include/Axle/JobSystemEnkiTS.hpp), [Engine/Vehicle/include/Axle/Vehicle.hpp](../../Engine/Vehicle/include/Axle/Vehicle.hpp).

## 6. Parallel OpenGL and RHI rendering

```mermaid
classDiagram
    SceneRenderer ..> Scene : render argument
    SceneRenderer o-- Shader : shared_ptr
    SceneRenderer o-- ShadowMap : shared_ptr
    SceneRenderer o-- Skybox : shared_ptr
    SceneRenderer o-- SkinningPipeline : shared_ptr
    SceneRenderer o-- ParticleRenderer : shared_ptr
    RHISceneRenderer ..> Scene : render argument
    RHISceneRenderer --> RHIDevice : device observer
    RHISceneRenderer ..> RHIMeshRegistry : mesh lookup
    RHISceneRenderer *-- RHIParticleRenderer : unique_ptr
    RHISceneRenderer *-- RHIPhysicsDebugRenderer : unique_ptr
    RHISceneRenderer *-- RHIObjectPicker : unique_ptr
    RHISceneRenderer ..> SceneRenderSettings : shared settings type
    SceneRenderer ..> SceneRenderSettings : settings argument
    MeshInstance3D o-- VertexArray : OpenGL shared_ptr
    MeshInstance3D o-- RHIMesh : RHI shared_ptr
    MeshInstance3D o-- RHIMaterial : RHI shared_ptr
    MaterialComponent o-- Material : shared_ptr
```

The scene renderers are independent concrete classes, not a base/derived pair. RHISceneRenderer owns its particle/debug/picker helpers. Components retain both OpenGL and RHI resources.

Sources: [Engine/Scene/include/Axle/SceneRenderer.hpp](../../Engine/Scene/include/Axle/SceneRenderer.hpp), [Engine/Scene/include/Axle/RHISceneRenderer.hpp](../../Engine/Scene/include/Axle/RHISceneRenderer.hpp), [Engine/Scene/include/Axle/Components.hpp](../../Engine/Scene/include/Axle/Components.hpp).

## 7. RHI resource dependencies

```mermaid
classDiagram
    RHIDevice *-- NRIInterfaces : value
    RHIDevice --> NRIDevice : managed native handle
    RHIDevice --> NRIQueue : native queue
    RHISwapChain --> RHISurface : observer pointer
    RHISwapChain *-- "0..*" FrameInFlight : vector values
    RHISwapChain *-- "0..*" SwapChainTextureEntry : vector values
    RHISwapChain ..> RHIDevice : native resource APIs
    RHIBuffer ..> RHIDevice : allocation
    RHITexture ..> RHIDevice : allocation
    RHICommandBuffer ..> RHIDevice : command APIs
    RHIStreamer ..> RHIDevice : streaming
    RHIECSStreamer ..> RHIStreamer : uploads
    RHIECSStreamer ..> EnttRegistry : template view input
    RHIMaterial o-- RHITexture : texture maps
    RHIMaterial ..> RHIDescriptorPool : bindings
    RHIShaderCompiler ..> RHIShaderReflection : compile result
    RHIPipelineCache ..> RHIShaderBytecode : pipeline inputs
```

Dotted arrows indicate API use, not retained members. Native device/queue labels abbreviate NRI types. Wrappers may manage native handles explicitly.

Sources: [Engine/RHI/include/Axle/RHI/RHIDevice.hpp](../../Engine/RHI/include/Axle/RHI/RHIDevice.hpp), [Engine/RHI/include/Axle/RHI/RHISwapChain.hpp](../../Engine/RHI/include/Axle/RHI/RHISwapChain.hpp), [Engine/RHI/include/Axle/RHI/RHIECSStreamer.hpp](../../Engine/RHI/include/Axle/RHI/RHIECSStreamer.hpp).

## 8. Asset registry, DSL and packages

```mermaid
classDiagram
    AssetManager ..> AssetMetadataSerializer : load / create metadata
    AssetManager ..> AssetMetadata : static registry values
    AssetManager o-- Texture2D : static shared caches
    AssetManager o-- Material : static shared caches
    AssetManager o-- RHIMaterial : static shared caches
    AssetMetadata --> UUID : handle value
    AssetMetadataSerializer ..> AssetMetadata : serialize argument
    AssetDocument o-- AssetBlock : root shared_ptr
    AssetBlock o-- "0..*" AssetBlock : child shared_ptr
    AssetBlock *-- "0..*" AssetProperty : properties values
    AssetDSL ..> AssetDocument : parse / serialize
    AssetCooker ..> PackageWriter : creates local writer
    AssetCooker ..> AssetSerializer : mesh baking
    AssetCooker ..> AssetManager : metadata lookup
    FileSystem *-- Package : static unique_ptr in cpp
    Package *-- "0..*" PackageEntry : indexes by path and UUID
    PackageWriter ..> PackageEntry : pending entries
```

AssetManager caches are static, process-wide shared references. FileSystem owns the mounted Package through an implementation-level static unique_ptr. Package indexes store entry values rather than a single shared entry object.

Sources: [Engine/Asset/include/Axle/AssetManager.hpp](../../Engine/Asset/include/Axle/AssetManager.hpp), [Engine/Asset/include/Axle/AssetDSL.hpp](../../Engine/Asset/include/Axle/AssetDSL.hpp), [Engine/Core/src/FileSystem.cpp](../../Engine/Core/src/FileSystem.cpp), [Engine/Core/include/Axle/Package.hpp](../../Engine/Core/include/Axle/Package.hpp).

## 9. Animation

```mermaid
classDiagram
    Skeleton *-- "0..*" Bone : values
    AnimationClip *-- "0..*" AnimationChannel : values
    AnimationChannel *-- "0..*" Keyframe : keys
    AnimationPose *-- "0..*" BoneTransform : values
    Animator o-- Skeleton : shared_ptr
    Animator o-- AnimationStateMachine : shared_ptr
    Animator *-- AnimationPose : fallback value
    AnimationStateMachine *-- "0..*" AnimationState : state map
    AnimationStateMachine *-- "0..*" AnimationTransition : transition vector
    AnimationStateMachine *-- AnimationPose : current value
    AnimationState o-- AnimationClip : shared_ptr
    AnimationState o-- BlendSpace1D : shared_ptr
    BlendSpace1D *-- "0..*" BlendSpacePoint1D : values
    BlendSpacePoint1D o-- AnimationClip : shared_ptr
    Animator ..> SkinningPipeline : upload palette
    SkeletalMesh *-- SkeletalMeshData : value
    SkeletalMesh o-- VertexArray : shared_ptr
    SkeletalMeshData o-- Skeleton : shared_ptr
```

Animator holds shared skeleton/state machine references and a fallback pose. The state machine owns its current pose. AnimationState references clips/blend spaces; it does not inherit from them.

Sources: [Engine/Animation/include/Axle/Animator.hpp](../../Engine/Animation/include/Axle/Animator.hpp), [Engine/Animation/include/Axle/AnimationStateMachine.hpp](../../Engine/Animation/include/Axle/AnimationStateMachine.hpp), [Engine/Animation/include/Axle/SkeletalMesh.hpp](../../Engine/Animation/include/Axle/SkeletalMesh.hpp).

## 10. Audio and particle simulation

```mermaid
classDiagram
    AudioSource o-- AudioClip : shared_ptr
    AudioSource *-- MiniaudioSound : unique_ptr
    AudioSource ..> AudioEngine : playback service
    AudioListener ..> AudioEngine : listener service
    ParticleSystem *-- ParticleEmitterConfig : value
    ParticleSystem *-- ParticlePool : SoA value
    ParticleSystem o-- "0..*" ParticleSystem : named child systems
    ParticleSystem ..> ParticleCollisionSolver : simulation
    ParticleSystem ..> ParticleForceField : config fields evaluated
    ParticleSystem ..> CurlNoise : simulation
    ParticleRenderer ..> ParticleSystem : reads pool / config
    RHIParticleRenderer ..> ParticleSystem : reads pool / config
    ParticleEmitterStack o-- ParticleSystem : runtime shared_ptr
    ParticleEmitterComponent *-- "0..*" ParticleEmitterStack : stack values
    ParticleEmitterSerializer ..> ParticleEmitterConfig : descriptor IO
```

Particle renderers consume simulation state rather than inheriting from ParticleSystem. Force evaluation uses ParticleForceField records from config. MiniaudioSound denotes ma_sound.

Sources: [Engine/Audio/include/Axle/AudioSource.hpp](../../Engine/Audio/include/Axle/AudioSource.hpp), [Engine/VFX/include/Axle/ParticleSystem.hpp](../../Engine/VFX/include/Axle/ParticleSystem.hpp), [Engine/VFX/src/ParticleSystem.cpp](../../Engine/VFX/src/ParticleSystem.cpp), [Engine/Scene/include/Axle/Components.hpp](../../Engine/Scene/include/Axle/Components.hpp).

## 11. UI arena, tree and binding

```mermaid
classDiagram
    UIDocument *-- "0..*" UINode : unique_ptr arena
    UIDocument --> UINode : root and ID map observers
    UINode --> "0..1" UINode : Parent observer
    UINode --> "0..*" UINode : Children observers
    UINode *-- RectTransform : value
    UINode <|-- UICanvasNode
    UINode <|-- UIRectangleNode
    UINode <|-- UITextNode
    UINode <|-- UIImageNode
    UINode <|-- UIProgressBarNode
    UINode <|-- UIRadialGaugeNode
    UINode <|-- UIBoxContainerNode
    UIRectangleNode <|-- UIButtonNode
    UIDocument *-- "0..*" UIPropertyBinding : values
    UIPropertyBinding --> UINode : target observer
    UIDataContext --> UIDataContext : parent observer
    UIDocument ..> UIDataContext : evaluate / action lookup
    UIDocument ..> Canvas2D : emits geometry
    UIDocument o-- UIFont : default shared_ptr
    UIParser ..> UIDocument : populate
    UICanvasComponent o-- UIDocument : shared_ptr
```

UIDocument owns all nodes in its arena. Parent/children, root, ID lookup and binding targets are observers. Reload/clear must rebuild references into the node arena.

Sources: [Engine/UI/include/Axle/UI/UIDocument.hpp](../../Engine/UI/include/Axle/UI/UIDocument.hpp), [Engine/UI/include/Axle/UI/UINode.hpp](../../Engine/UI/include/Axle/UI/UINode.hpp), [Engine/UI/include/Axle/UI/UIDataContext.hpp](../../Engine/UI/include/Axle/UI/UIDataContext.hpp).

## 12. Spline, terrain and serializer extensions

```mermaid
classDiagram
    SplineComponent *-- Curve3D : value
    Curve3D *-- "0..*" SplineNode : values
    SplineComponent o-- VertexArray : TrackVAO
    TrackGenerator ..> Curve3D : extrusion frames
    TrackGenerator ..> SplineComponent : settings
    TrackGenerator ..> PhysicsScene : collider creation
    SplineEditor ..> SplineComponent : authoring
    SplineSerializer ..> SplineComponent : IO
    TerrainComponent *-- TerrainData : value
    TerrainComponent *-- "0..*" TerrainChunk : values
    TerrainComponent o-- VertexArray : CombinedVAO
    TerrainMesh ..> TerrainData : mesh generation
    TerrainCollider ..> TerrainData : heightfield
    TerrainCollider ..> PhysicsScene : body creation
    TerrainSculptor ..> TerrainData : brush writes
    TerrainSplineCarver ..> TerrainData : carving
    TerrainSplineCarver ..> Curve3D : path
    TerrainEditor ..> TerrainComponent : authoring
    SceneSerializer *-- SceneSerializerContext : value
    SceneSerializer --> Scene : observer
    SceneSerializer --> PhysicsScene : observer
    SceneSerializerContext ..> SplineSerializer : caller installed callback
    SceneSerializerContext ..> TerrainSerializer : caller installed callback
```

Serializer callbacks are installed by callers in Editor/Runtime; these edges do not establish a Scene target dependency on plugin libraries. Geometry/collider creation is still backend/world specific.

Sources: [Plugins/Spline/include/AxleSpline/SplineComponent.hpp](../../Plugins/Spline/include/AxleSpline/SplineComponent.hpp), [Plugins/Terrain/include/AxleTerrain/TerrainComponent.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainComponent.hpp), [Engine/Scene/include/Axle/SceneSerializer.hpp](../../Engine/Scene/include/Axle/SceneSerializer.hpp), [Runtime/src/RuntimeLayer.cpp](../../Runtime/src/RuntimeLayer.cpp).

## 13. Project and standalone runtime

```mermaid
classDiagram
    Project *-- ProjectConfig : value
    ProjectSerializer o-- Project : shared_ptr
    RuntimeLayer o-- Scene : shared_ptr
    RuntimeLayer *-- PhysicsScene : unique_ptr
    RuntimeLayer *-- SceneRenderer : unique_ptr
    RuntimeLayer *-- PerspectiveCamera : value
    RuntimeLayer *-- Canvas2D : value
    RuntimeLayer ..> Project : load / active config
    RuntimeLayer ..> ScriptHotReloadManager : initialize module
    RuntimeLayer ..> SceneSerializer : load startup scene
    RuntimeLayer ..> TrackGenerator : build track resources
    RuntimeLayer ..> TerrainMesh : build terrain resources
    RuntimeLayer ..> TerrainCollider : build collision
```

RuntimeLayer owns SceneRenderer, not RHISceneRenderer. ProjectSerializer retains a shared Project; active project state is static.

Sources: [Engine/Project/include/Axle/Project.hpp](../../Engine/Project/include/Axle/Project.hpp), [Engine/Project/include/Axle/ProjectSerializer.hpp](../../Engine/Project/include/Axle/ProjectSerializer.hpp), [Runtime/src/RuntimeLayer.hpp](../../Runtime/src/RuntimeLayer.hpp).

## 14. HLSL Studio

```mermaid
classDiagram
    HLSLStudioPanel ..> RHIShaderCompiler : CompileForActiveBackend
    HLSLStudioPanel ..> RHIShaderCompileDesc : stage settings
    HLSLStudioPanel ..> RHIShaderCompileResult : diagnostics and bytecode size
    ContentBrowserPanel ..> HLSLStudioPanel : default template generation
    EditorLayer *-- HLSLStudioPanel : header value member
```

EditorLayer wires content-browser and inspector open callbacks and renders the panel. CompileForActiveBackend validates the selected shader stages; pipeline replacement is a separate concern.

Sources: [Engine/UI/include/Axle/HLSLStudioPanel.hpp](../../Engine/UI/include/Axle/HLSLStudioPanel.hpp), [Engine/UI/src/HLSLStudioPanel.cpp](../../Engine/UI/src/HLSLStudioPanel.cpp), [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp).
