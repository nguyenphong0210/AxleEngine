# Project plugin authoring

OPT-06 delivers the PLG-01..13 native Editor workflow. Selected static player
composition and packaged content mounting remain PLG-22/23. Editor export refuses
to bundle a player that cannot contain its selected native modules; data-only
cooking validates native scene/Assembly data against the active codecs first.

## Create and activate a plugin

Build `AxleEditor` for the desired configuration. Its `AxlePluginSDKPackage`
dependency produces `build/PluginSDK/<configuration>` with matched headers,
CMake helpers, import libraries and shared host DLLs. Use the same Windows x64
MSVC compiler, CRT and configuration as the Editor.

```powershell
python scripts/create_project_plugin.py D:\Projects\Example\Example.axleproj --id studio.mover --module StudioMover --name "Studio Mover"
```

The generator refuses an existing destination. It writes only
`<project>/Plugins/StudioMover`; no Engine source list or root CMake edit is needed.
The generated manifest, Runtime module, dependent Editor module and README are
the complete example. Plugin IDs are persistent authored identities; keep them
stable when renaming a display name.

Open **Edit > Plugins**, Refresh, select the plugin and Apply. Apply saves Desired
selection to the actual project file. Build selected plugins, then Restart Now.
Draft controls do not change the running Active graph. Native activation happens
once at process startup, including when a previously empty project gains its
first native plugin. Save Project As preserves project plugin sources and updates
the build/session path.

The build freezes selected source/CMake/header inputs, configures and compiles
real DLLs through the standalone SDK, validates their identities and publishes
an immutable generation. Source/include paths must use the frozen plugin roots,
generated build tree or matched SDK. Arbitrary native CMake is trusted author
code; this workflow is not an execution sandbox for hostile build scripts.
Unselected plugin CMake is never evaluated. Failed/cancelled builds preserve
previous generations. The window shows the current stage, log and cancellation.

Restart binds the project, plan, exact generation, receipt set, configuration,
toolchain and backend. The child validates these before loading, and acknowledges
only after five fully completed UI/present frames. The parent stays open on
compile, launch, handshake, native initialization or startup-render failure.
Recovery disables native activation and exposes selection/build/retry controls.
Clean shutdown removes its owned marker; abandoned markers require an explicit
retry. Markers held by other live Editors are left alone.

## Implement components and tools

Include `Axle/PluginSDK.hpp` and declare each selected module with
`axle_add_plugin_module`. The generated manifest declares the Editor module's
dependency on Runtime. The helper supplies exact descriptor identities; use
`AXLE_PLUGIN_DESCRIPTOR` rather than guessing SDK or compiler strings.

Plugins import the shared `AxleRuntimeHost`/`AxleEditorHost` gateways. They do not
link private Engine, EnTT, Jolt, enkiTS or ImGui implementations. Opaque entity
handles and POD service tables resolve to the canonical application Project,
scene registry, physics and scheduler. Inspector controls execute in the actual
host ImGui context. The example's Editor imports a narrow speed accessor from
Runtime; Runtime alone allocates, clones and destroys its nontrivial payload.

Register a versioned `ComponentType` with a stable `plugin-id/local-type` key.
Provide Create/Destroy/Serialize/Clone and required simulation callbacks. An
owned descriptor context transfers only on successful registration. Values,
descriptors, scene pools, callbacks, jobs and render submissions retain executable
code until their last use. Stop is ordered before final DLL release, with reverse
dependency teardown on the owner thread. Do not keep borrowed host/entity handles
past their validated owner/scene epoch.

Declare entity references and remap them in Clone. Whole-scene, subtree and
Assembly copies receive the complete remap before Start. Failure rolls back the
transaction. Missing required or incompatible data blocks Play/cook; unknown
optional payloads remain opaque, and opaque references cannot be copied unsafely.
Assemblies and private previews use the same owned codec path.

Run jobs through SubmitJob. Work callbacks touch only their owned job data;
live ECS, physics and UI access is rejected even when the owner OS thread steals
a scheduler task. Completion runs through the existing owner-token main-thread
queue. Document/build/restart transactions quiesce mutation-capable jobs and
script reloads before capture, and resume only after the freeze ends.

The template updates position during the Frame phase and submits a green debug
line. Scene extraction copies values and keeps code leases; recorded/submitted
RHI work retains those leases through GPU completion. Extend the existing phase
and rendering services rather than introducing another clock, scheduler or device.

## Verify your plugin

Create an entity, add the plugin component in the Inspector, edit Speed, save and
reopen the scene. Play uses a cloned runtime world; Stop preserves authored data.
Test duplicate/subtree/Assembly copy, failed Clone/Start, missing required schemas,
build errors, cancellation and recovery. SerializeToString is a read-only capture;
document writes occur only through explicit save operations. Save/Discard/Cancel
covers the scene, embedded UI and private Assembly/VFX/UI-source/HLSL documents.
Conflicting edited buffers targeting one file are rejected before overwriting it.

Repository evidence uses `PluginSessionTests` to generate/build this actual
template outside the Engine tree (including Unicode paths). The opt-in
`AxlePluginSubmissionTests <project> dx12|vulkan` checks real DLL ownership and
green pixels on a physical adapter. Native product probes are disposable-fixture
only; they are not a substitute for a plugin's own domain tests.
