# Project plugin authoring

The native Editor workflow uses selected DLL modules. OPT-09 adds selected static
Runtime composition and snapshot-bound packaged content. Both products use the
same SDK registration, service and lifetime boundaries. See
[architecture acceptance](../guides/ARCHITECTURE_ACCEPTANCE.md) for the implemented
seams, reproduction commands and separately required UI/review evidence.

## Create and activate a plugin

Build `AxleEditor` for the desired configuration. Its `AxlePluginSDKPackage`
dependency produces `build/PluginSDK/<configuration>` with matched headers,
CMake helpers, import libraries and shared host DLLs. Use the same Windows x64
MSVC compiler, CRT and configuration as the Editor.

In a saved project, open **Edit > Plugins > Create Plugin...**. Enter a display
name; Folder/module and Plugin ID are suggested automatically and remain editable.
Create writes the Runtime component, dependent Editor Inspector, manifest, CMake
and README under `<project>/Plugins/<module>`, then shows the new plugin in the
list. It uses the template packaged with Editor and requires no Python at runtime.
It preserves pending selections and does not enable or load code automatically.
Select the new plugin, Apply, Build selected plugins and Restart Now to activate it.

Creation is disabled during Play/build/export/restart and requires Save Project As
for an untitled project. Existing folders, duplicate IDs/module names and invalid
names are rejected. Files are staged and validated before nonoverwriting publication;
a project lock prevents concurrent Editor creators from publishing conflicting IDs.
Cancel creates no files. Plugin IDs remain persistent authored identities.

The repository also provides a command-line generator:

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

## Standalone composition and content

OPT-09 exports configure an Editor-free product from the saved project's selected
Runtime graph. Keep Runtime code on the SDK boundary; `axle_add_plugin_module`
builds the same source as a DLL in Editor and a static library in the player.
Static builds rename entrypoints automatically. Do not hardcode paths to original
source installations or add private Engine, imported/shared or unselected library
dependencies. Both implementation and interface links are validated.

Author optional plugin resources under `Content/`. Export mounts selected content
at `Plugins/<plugin-id>/` in the project's asset namespace; use those canonical
paths in authored references. Unselected plugin installations/content are excluded.
Project gameplay sources belong under project `Scripts/`: headers are included in
the generated registration unit, and `.cpp` files are linked into the executable.
Use `AXLE_REGISTER_SCRIPT` and the supported host APIs; shipping has no script DLL.

The Editor Export action and `AxleEditor --project=<file> --export <directory>`
build and preflight the exact player before publishing. `--cook` produces data
only from the same frozen selection/content rules. Add
`--runtime-configuration=Debug` or `--runtime-configuration=Release` to select the
player configuration independently of the running Editor's configuration.
A changed source, selection,
asset or mismatched receipt requires a fresh export. `AxleRuntime --check-content`
checks the compiled plugin/script requirements without creating a GPU/window.
Existing authored output directories cannot be used as export targets. Previous
successful export directories carry `AxleExport.receipt` and are replaced only
after a new export succeeds.

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
