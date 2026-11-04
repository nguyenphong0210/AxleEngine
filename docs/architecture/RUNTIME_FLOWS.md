# Execution Flows: Current System

> **Dated source snapshot:** this survey predates the 2026-11-04 Vehicle
> retirement and later sample cleanup. It is retained for architectural context,
> not current API/feature support. Follow the [optimization roadmap](../ROADMAP.md)
> for execution; unavailable source paths below are historical references.

Reviewed: 2026-11-01; startup paths updated 2026-11-03. Diagrams preserve architecture-relevant call order while omitting widget and command-level details. They describe source paths; execution success was not measured.

## 1. Editor startup and backend selection

```mermaid
sequenceDiagram
    actor User
    participant Main as Editor Main
    participant Config as ProjectSerializer
    participant Switch as RHISwitcher
    participant App as Application
    participant UI as ImGuiLayer
    participant Layer as EditorLayer
    User->>Main: launch with CLI/project
    alt --export or --cook
        Main->>Main: Log Init and AssetCooker CookProject
        Main-->>User: exit code
    else normal editor
        Main->>Switch: ParseCommandLine
        opt selected project and no backend CLI
            Main->>Config: Deserialize project settings
            Config-->>Main: RHIBackend
        end
        opt backend is not OpenGL
            Main->>Switch: ResolveAndInitialize
            Switch-->>Main: selected API or fallback OpenGL
        end
        Main->>Main: activate explicit/configured development project or create untitled workspace
        Main->>App: construct with GraphicsBackend
        App->>App: init Log, Jobs, Physics, Window
        opt OpenGL selected
            App->>App: RenderCommand Init and viewport
        end
        Main->>App: SetUIContext ImGuiLayer
        App->>UI: Init native window
        Main->>App: PushLayer EditorLayer
        App->>Layer: OnAttach (selected start scene or empty scene)
        Main->>App: Run
    end
```

Editor Main handles cook/export before normal UI startup and requires an explicit project for cooking. Development startup selects the build-configured Samples project unless overridden by an explicit project or `--empty-project`. Empty mode has zero entities/assets; nearby project discovery is disabled. Backend settings come from CLI/project with fallback. UI backend availability and headless device capabilities are different concerns.

Sources: [Editor/src/Main.cpp](../../Editor/src/Main.cpp), [Engine/Core/src/Application.cpp](../../Engine/Core/src/Application.cpp), [Engine/Core/src/LayerStack.cpp](../../Engine/Core/src/LayerStack.cpp).

## 2. Shared application loop

```mermaid
sequenceDiagram
    participant App as Application
    participant Input
    participant Window
    participant Jobs as JobSystem
    participant Layer
    participant UI as ApplicationUIContext
    loop running and window not closed
        App->>Input: reset scroll
        App->>Window: PollEvents
        App->>Jobs: RunPinnedTasks
        App->>App: glfwGetTime and delta calculation
        loop each layer
            App->>Layer: OnUpdate(ts)
        end
        opt UI context installed
            App->>UI: Begin
            loop each layer
                App->>Layer: OnImGuiRender
            end
            App->>UI: End
        end
        App->>Window: SwapBuffers
    end
```

Layer updates run before the optional UI frame. For native RHI paths, UI/RHI integration performs additional command/presentation work; the common SwapBuffers call does not mean every backend presents through OpenGL.

Sources: [Engine/Core/src/Application.cpp](../../Engine/Core/src/Application.cpp), [Engine/UI/src/ImGuiLayer.cpp](../../Engine/UI/src/ImGuiLayer.cpp).

## 3. Editor play, pause and stop

```mermaid
stateDiagram-v2
    [*] --> Edit
    Edit --> Play: OnScenePlay / clone scene + runtime start
    Play --> Pause: OnScenePause
    Pause --> Play: resume
    Pause --> Pause: requested single physics step
    Play --> Edit: OnSceneStop / runtime stop + restore edit context
    Pause --> Edit: OnSceneStop
```
```mermaid
sequenceDiagram
    participant EL as EditorLayer
    participant Copy as Scene Copy
    participant Runtime as Runtime Scene
    participant Panels
    participant Reload as ScriptHotReloadManager
    EL->>EL: CompileAndHotReloadAssets
    EL->>Copy: Copy editor scene
    Copy-->>EL: new runtime Scene
    Note over Copy: preserve entity UUIDs; remap registry handles/references as needed
    EL->>EL: active scene = runtime scene
    EL->>Panels: SetContext and clear selection
    EL->>Reload: sync input and ImGui context
    EL->>Runtime: OnRuntimeStart physics
    Note over EL: state = Play
    EL->>Runtime: OnRuntimeStop on stop
    EL->>EL: release runtime and restore editor scene
    EL->>Panels: restore edit context
```

Scene::Copy preserves UUID identity and creates new registry handles. On stop, the editor restores its original Scene, restores physics bodies and panel contexts. A requested paused step in the surveyed update branch advances physics; it does not invoke the Play-only Scene update there.

Sources: [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp), [Engine/Scene/src/Scene.cpp](../../Engine/Scene/src/Scene.cpp).

## 4. Different Editor and Runtime update order

```mermaid
sequenceDiagram
    participant EL as EditorLayer OnUpdate
    participant ES as Active Editor Scene
    participant EP as Editor PhysicsScene
    participant RL as RuntimeLayer OnUpdate
    participant RP as Runtime PhysicsScene
    participant RS as Runtime Scene
    Note over EL,EP: Editor Play path
    EL->>ES: OnUpdateRuntime(ts, physics)
    EL->>EL: VFX preview / camera update
    EL->>EP: Step(ts * simulationSpeed)
    EL->>EL: choose camera and render backend
    Note over RL,RS: Standalone unpaused path
    RL->>RP: Step(ts)
    RL->>RS: OnUpdateRuntime(ts, physics)
    RL->>RL: choose camera, SceneRenderer RenderScene, RenderUI
```

Editor Play calls OnUpdateRuntime before Step and scales physics time separately. Runtime calls Step before OnUpdateRuntime. These are observed sequences, not a recommended shared simulation contract.

Sources: [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp), [Runtime/src/RuntimeLayer.cpp](../../Runtime/src/RuntimeLayer.cpp).

## 5. Inside Scene runtime update

```mermaid
flowchart TD
    Start[OnUpdateRuntime] --> Scripts[Bind / create / OnCreate / OnUpdate scripts]
    Scripts --> Vehicle[Driver inputs + chassis/wheels pose + telemetry]
    Vehicle --> VA[VehicleAudio and VehicleVFX]
    VA --> Audio[Listener and spatial AudioSource synchronization]
    Audio --> Character[CharacterController update and transform]
    Character --> Params[Animator parameters from character]
    Params --> Anim[AnimatorComponent update]
    Anim --> Particles[ParticleEmitterComponent update]
```

PhysicsScene::Step is outside this method. Rendering/transform helpers also run through separate paths; this diagram is not a complete GPU/frame graph.

Sources: [Engine/Scene/src/Scene.cpp](../../Engine/Scene/src/Scene.cpp).

## 6. Standalone loading and shutdown

```mermaid
sequenceDiagram
    participant Main as Runtime Main
    participant App as Application
    participant RL as RuntimeLayer
    participant FS as FileSystem
    participant Proj as Project
    participant Ser as SceneSerializer
    participant Scene
    Main->>App: construct default graphics spec
    Note over Main,App: default backend OpenGL; no RHISwitcher bootstrap here
    Main->>App: PushLayer RuntimeLayer
    App->>RL: OnAttach
    RL->>RL: init Audio, scripts, Canvas, PhysicsScene, SceneRenderer
    RL->>FS: try package candidates
    RL->>Proj: load requested or discovered project
    RL->>RL: resolve configured StartScene only
    RL->>Ser: configure script and plugin callbacks
    alt scene found and parsed
        Ser->>Scene: create entities/components
        RL->>RL: rebuild spline/terrain VAOs and colliders
    else no authored start scene
        RL->>Scene: retain empty scene
    end
    RL->>Scene: OnRuntimeStart
    Main->>App: Run
    App->>RL: OnDetach during shutdown
    RL->>Scene: OnRuntimeStop
    RL->>RL: reset scene/physics/renderer and shutdown scripts/audio
```

Runtime uses the default OpenGL application specification. It installs plugin serializer callbacks, rebuilds authored procedural geometry/colliders, and retains an empty scene when no configured start scene loads. AssetManager::Init is not directly called in the inspected RuntimeLayer startup. SetupPauseMenu is defined but has no call in that file; this diagram does not assume it runs.

Sources: [Runtime/src/Main.cpp](../../Runtime/src/Main.cpp), [Runtime/src/RuntimeLayer.cpp](../../Runtime/src/RuntimeLayer.cpp).

## 7. Windows script compilation and reload

```mermaid
sequenceDiagram
    participant EL as EditorLayer
    participant Compiler as ScriptCompiler
    participant Build as Build process
    participant Reload as ScriptHotReloadManager
    participant Scene
    participant DLL as Shadow-copy DLL
    EL->>Compiler: CompileAsync callback
    Compiler->>Build: run compile on worker thread
    Build-->>Compiler: exit code and diagnostics
    Compiler-->>EL: completion handling
    opt compilation successful and reload requested
        EL->>Reload: Reload(activeScene, runtimeFlag)
        Reload->>Scene: OnPreScriptReload
        Reload->>Reload: shutdown old module, clear metadata, unload
        Reload->>Reload: copy source DLL to timestamped path
        Reload->>DLL: LoadLibrary and sync contexts
        Reload->>DLL: Axle_InitializeScripts callback
        DLL-->>Reload: script metadata registrations
        Reload->>Scene: OnPostScriptReload
        Reload->>Reload: cleanup obsolete shadow copies
    end
```

CompileAsync invokes completion on the worker thread; the editor callback calls reload directly. The logical sequence does not imply a main-thread handoff. Copy/load/export failures after unloading the old DLL are not guaranteed to roll back.

Sources: [Engine/Scene/src/ScriptCompiler.cpp](../../Engine/Scene/src/ScriptCompiler.cpp), [Engine/Scene/src/ScriptHotReloadManager.cpp](../../Engine/Scene/src/ScriptHotReloadManager.cpp), [Editor/src/Scripts/ScriptsModule.cpp](../../Editor/src/Scripts/ScriptsModule.cpp), [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp).

## 8. Asset metadata, resources and cooking

```mermaid
sequenceDiagram
    participant Consumer as Scene or editor consumer
    participant AM as AssetManager
    participant Meta as AssetMetadataSerializer
    participant Loader as Material / Texture loader
    Consumer->>AM: lookup by path or UUID
    alt UUID/path cached
        AM-->>Consumer: shared resource
    else path metadata resolution
        AM->>Meta: read companion meta
        alt missing or invalid meta
            AM->>AM: generate random UUID
            AM->>Meta: write companion meta
        end
        AM->>Loader: load resource from descriptor/media
        Loader-->>AM: resource
        AM->>AM: cache by path and UUID
        AM-->>Consumer: shared resource
    end
```
```mermaid
flowchart LR
    Settings[CookSettings] --> Cooker[AssetCooker]
    Project[Project config] --> Cooker
    Assets[Asset directory and metadata] --> Cooker
    Resources[Shader resources] --> Cooker
    Cooker --> Bake[Optional mesh bake]
    Bake --> Writer[PackageWriter]
    Cooker --> Writer
    Writer --> Pak[Data.axlepak header + chunks + TOC]
    Cooker --> Files[Copy runtime / scripts / project files as configured]
```

A missing/invalid companion metadata file causes a new UUID. AssetCooker traverses the asset directory rather than an established referenced-asset closure. PackageWriter currently writes uncompressed data.

Sources: [Engine/Asset/src/AssetManager.cpp](../../Engine/Asset/src/AssetManager.cpp), [Engine/Asset/src/AssetCooker.cpp](../../Engine/Asset/src/AssetCooker.cpp), [Engine/Core/src/Package.cpp](../../Engine/Core/src/Package.cpp).

## 9. Runtime UI binding and geometry

```mermaid
sequenceDiagram
    participant RL as RuntimeLayer RenderUI
    participant Comp as UICanvasComponent
    participant Doc as UIDocument
    participant Context as Scoped UIDataContext
    participant Canvas as Canvas2D
    RL->>RL: collect visible canvases and sort
    RL->>RL: process input against existing documents
    RL->>Canvas: BeginCanvas viewport
    loop each sorted canvas
        alt Document absent
            RL->>Doc: create and LoadFromFile
        else document exists
            RL->>Doc: CheckAndHotReload
        end
        RL->>Comp: GetDataSourceEntity
        RL->>Context: parent = scene context; copy reflected script fields
        RL->>Doc: EvaluateBindings scoped context
        RL->>Doc: UpdateLayout viewport
        RL->>Doc: Render Canvas
        Doc->>Canvas: DrawRect / DrawText / sprites etc.
    end
    RL->>Canvas: EndCanvas
    Note over Canvas: retains CPU vertices/indices and statistics; no GPU submit in EndCanvas
```

Input processing occurs before this frame creates/loads/layouts missing documents. Scoped contexts combine scene values with reflected script fields. EndCanvas retains CPU geometry/statistics without submitting GPU work in this path.

Sources: [Runtime/src/RuntimeLayer.cpp](../../Runtime/src/RuntimeLayer.cpp), [Engine/UI/src/UI2D/UIDocument.cpp](../../Engine/UI/src/UI2D/UIDocument.cpp), [Engine/UI/src/UI2D/Canvas2D.cpp](../../Engine/UI/src/UI2D/Canvas2D.cpp).
