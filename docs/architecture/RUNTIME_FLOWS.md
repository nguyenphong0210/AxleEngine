# Execution Flows: Current System

Reviewed: **2026-11-06** (post OPT-03). Diagrams describe current runtime sequence and subsystem interactions across Editor and Runtime targets.

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

Editor Main handles cook/export before normal UI startup and requires an explicit project for cooking. Development startup selects the build-configured Samples project unless overridden by an explicit project or `--empty-project`. Backend settings come from CLI/project with fallback.

Sources: [Editor/src/Main.cpp](../../Editor/src/Main.cpp), [Engine/Platform/src/Application.cpp](../../Engine/Platform/src/Application.cpp), [Engine/Platform/src/LayerStack.cpp](../../Engine/Platform/src/LayerStack.cpp).

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

Sources: [Engine/Platform/src/Application.cpp](../../Engine/Platform/src/Application.cpp), [Engine/UI/src/ImGuiLayer.cpp](../../Engine/UI/src/ImGuiLayer.cpp).

## 3. Editor play, pause and stop

```mermaid
stateDiagram-v2
    [*] --> Edit
    Edit --> Play: OnScenePlay / clone scene + runtime start
    Play --> Pause: OnScenePause
    Pause --> Play: resume
    Pause --> Pause: requested single frame step
    Play --> Edit: OnSceneStop / runtime stop + restore edit context
    Pause --> Edit: OnSceneStop
```
```mermaid
sequenceDiagram
    participant EL as EditorLayer
    participant Copy as Scene Copy
    participant Runtime as Runtime Scene
    participant Panels
    participant Driver as SimulationDriver
    participant Reload as ScriptHotReloadManager
    EL->>EL: CompileAndHotReloadAssets
    EL->>Copy: Copy editor scene
    Copy-->>EL: new runtime Scene
    Note over Copy: preserve entity UUIDs; remap registry handles/references as needed
    EL->>EL: active scene = runtime scene
    EL->>Panels: SetContext and clear selection
    EL->>Reload: sync input and ImGui context
    EL->>Driver: Start(runtimeScene, physicsScene)
    Note over EL: state = Play
    EL->>Driver: Stop(runtimeScene, physicsScene) on stop
    EL->>EL: release runtime and restore editor scene
    EL->>Panels: restore edit context
```

Scene::Copy preserves UUID identity and creates new registry handles. On stop, the editor restores its original Scene, restores physics bodies and panel contexts. Single-step execution delegates to `SimulationDriver::StepSingleFrame()`.

Sources: [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp), [Engine/Scene/src/Scene.cpp](../../Engine/Scene/src/Scene.cpp), [Engine/Scene/src/Simulation/SimulationDriver.cpp](../../Engine/Scene/src/Simulation/SimulationDriver.cpp).

## 4. Unified SimulationDriver frame flow (shared Editor & Runtime)

Both Editor Play mode and standalone Runtime step simulation via the shared `SimulationDriver`:

```mermaid
sequenceDiagram
    participant Caller as EditorLayer / RuntimeLayer
    participant Driver as SimulationDriver
    participant Clock as SimulationClock
    participant Input as SimulationInputAdapter
    participant Scene as Scene / Scripts
    participant Physics as PhysicsScene
    Caller->>Driver: AdvanceFrame(rawDt, scene, physicsScene)
    Driver->>Clock: Advance(rawDt)
    Note over Clock: scale by speed, handle pause, accumulate ticks (cap max 4)
    loop while Clock.TakeFixedStep()
        Driver->>Scene: FramePhase::FixedControls (OnFixedUpdate)
        Note over Scene,Input: read edge-latched key/mouse/controller states
        Driver->>Physics: FramePhase::Physics Step(fixedDt)
        Driver->>Scene: FramePhase::PostStepSync (sync poses to TransformComponent)
    end
    Driver->>Scene: FramePhase::FrameScripts (OnUpdate variableDt)
    Driver->>Scene: FramePhase::AnimationVFX (evaluate animators & particle emitters)
    Driver->>Scene: FramePhase::LatePresentation (OnLateUpdate & camera matrices)
```

The unified driver resolves legacy defect `ECSJ-05`. Editor single-step executes exactly one fixed tick (60Hz) followed by presentation, identical to standalone Runtime behavior.

Sources: [Engine/Scene/include/Axle/Simulation/SimulationDriver.hpp](../../Engine/Scene/include/Axle/Simulation/SimulationDriver.hpp), [Engine/Scene/include/Axle/Simulation/SimulationClock.hpp](../../Engine/Scene/include/Axle/Simulation/SimulationClock.hpp), [Engine/Scene/include/Axle/Simulation/SimulationInputAdapter.hpp](../../Engine/Scene/include/Axle/Simulation/SimulationInputAdapter.hpp).

## 5. Discrete simulation phases

```mermaid
flowchart TD
    Clock[1. SimulationClock Tick Accumulation] --> FixedControls[2. FramePhase::FixedControls: OnFixedUpdate & Latched Inputs]
    FixedControls --> Physics[3. FramePhase::Physics: Jolt Step]
    Physics --> PostStepSync[4. FramePhase::PostStepSync: RigidBody to TransformComponent]
    PostStepSync --> MoreSubsteps{More fixed ticks?}
    MoreSubsteps -- Yes --> FixedControls
    MoreSubsteps -- No --> FrameScripts[5. FramePhase::FrameScripts: Script OnUpdate]
    FrameScripts --> AnimVFX[6. FramePhase::AnimationVFX: Skeletal Anim & Particles]
    AnimVFX --> LatePres[7. FramePhase::LatePresentation: OnLateUpdate & Viewport Render Packets]
```

Retired Vehicle components are eliminated from the update pipeline. General rigid bodies and `CharacterController` are synchronized deterministically during `FixedControls` and `PostStepSync`.

Sources: [Engine/Scene/src/Simulation/SimulationDriver.cpp](../../Engine/Scene/src/Simulation/SimulationDriver.cpp).

## 6. Standalone loading and shutdown

```mermaid
sequenceDiagram
    participant Main as Runtime Main
    participant App as Application
    participant RL as RuntimeLayer
    participant FS as FileSystem
    participant Proj as Project
    participant Ser as SceneSerializer
    participant Driver as SimulationDriver
    participant Scene
    Main->>App: construct default graphics spec
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
    RL->>Driver: Start(scene, physicsScene)
    Main->>App: Run
    App->>RL: OnDetach during shutdown
    RL->>Driver: Stop(scene, physicsScene)
    RL->>RL: reset scene/physics/renderer and shutdown scripts/audio
```

Runtime uses the clean Platform `Application` specification. It loads packages or project scenes, initializes audio/physics, and runs simulation via `SimulationDriver`.

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
        Reload->>DLL: Validate GetScriptABIVersion() == 2
        Reload->>DLL: Axle_InitializeScripts callback
        DLL-->>Reload: script metadata registrations
        Reload->>Scene: OnPostScriptReload
        Reload->>Reload: cleanup obsolete shadow copies
    end
```

Script compilation runs asynchronously on a worker thread. Shadow copying avoids Windows DLL file locks (`LNK1104`). The runtime validates `AXLE_SCRIPT_ABI_VERSION = 2` before registering script types.

Sources: [Engine/Scene/src/ScriptCompiler.cpp](../../Engine/Scene/src/ScriptCompiler.cpp), [Engine/Scene/src/ScriptHotReloadManager.cpp](../../Engine/Scene/src/ScriptHotReloadManager.cpp), [Editor/src/Scripts/ScriptsModule.cpp](../../Editor/src/Scripts/ScriptsModule.cpp), [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp).

## 8. Pure CPU asset registry and package cooking

```mermaid
sequenceDiagram
    participant Consumer as Scene or Editor Consumer
    participant AM as AssetManager (Pure CPU)
    participant FS as FileSystem / Loader
    Consumer->>AM: lookup by path or UUID
    alt UUID or path already registered
        AM-->>Consumer: cached CPU AssetDocument / descriptor
    else load from disk / package
        AM->>FS: read .axleasset / raw media
        Note over AM,FS: read embedded native UUID or assign root-relative path
        AM->>AM: register UUID and path
        AM-->>Consumer: CPU AssetDocument
    end
```
```mermaid
flowchart LR
    Settings[CookSettings] --> Cooker[AssetCooker]
    Project[Project config] --> Cooker
    Assets[Asset directory - embedded UUIDs] --> Cooker
    Resources[Shader resources] --> Cooker
    Cooker --> Bake[Optional mesh bake]
    Bake --> Writer[PackageWriter]
    Cooker --> Writer
    Writer --> Pak[Data.axlepak header + chunks + TOC]
    Cooker --> Files[Copy runtime / scripts / project files as configured]
```

Native descriptors embed 64-bit UUIDs directly; raw assets use root-relative paths. `AssetManager` is a pure CPU registry without GPU dependencies. GPU caches (`MaterialCache`, `RHIMaterial`) reside in renderer bridges.

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

Input processing occurs before creating or updating canvas layouts. Scoped data contexts combine scene state with reflected script variables. `Canvas2D` outputs CPU vertices and indices for batch submission.

Sources: [Runtime/src/RuntimeLayer.cpp](../../Runtime/src/RuntimeLayer.cpp), [Engine/UI/src/UI2D/UIDocument.cpp](../../Engine/UI/src/UI2D/UIDocument.cpp), [Engine/UI/src/UI2D/Canvas2D.cpp](../../Engine/UI/src/UI2D/Canvas2D.cpp).
