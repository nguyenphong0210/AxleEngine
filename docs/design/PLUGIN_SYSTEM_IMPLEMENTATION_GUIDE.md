# Plugin System Implementation Guide

Date: **2026-11-04**. Status: **proposed algorithms and interfaces; not implemented**.
The current assignment is documentation only. Source baseline and selected product
behavior are in the [architecture](PLUGIN_SYSTEM_ARCHITECTURE.md); package order
and evidence gates are in the [plan](PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md).

The examples below express contracts, not copy-ready definitions. Use the vendored
EnTT 3.14.0, enkiTS and Jolt APIs actually present in the checkout. Do not infer
implementation authority from a code fence or execute placeholder commands.

## 1. First implementation checkpoint

Start with PLG-00..04 only when assigned. Produce a metadata/resolution/build-plan
fixture before changing current product linkage. Then prove the shared-host and
DLL component lifetime boundary in PLG-05..09. The first plugin fixture is deliberately
unrelated to vehicles: it creates a component with real heap-backed state, displays
it in an Inspector and updates it through the common scene scheduler.

Do not start by deleting `Engine/Vehicle`, changing all libraries to `SHARED`,
copying `EditorLayer.cpp` vehicle branches into a DLL, or adding `#ifdef VEHICLE`
around every caller. A clean Vehicle rewrite follows after the common system works.

## 2. Minimum model and authoritative owners

| Proposed model | Owner | Contains |
| --- | --- | --- |
| PluginCatalog | Project/session discovery service | Metadata and diagnostics, no loaded library handles |
| PluginSelection | Project configuration | Explicit stable plugin IDs and schema version |
| PluginResolution | Immutable build request/session snapshot | Effective graph, reasons, versions, module kinds/order |
| PluginBuildReceipt | Build artifact store | Exact inputs, output identities, hashes and completion status |
| PluginSession | Application composition | Loaded modules, services and owner-scoped registrations |
| SceneExtensionSet | Each scene/preview runtime | Per-plugin instances associated with that registry |
| PluginDraft | Plugins window view model | Editable selection, validation and pending/active comparisons |
| PluginBuildOperation | Editor build coordinator | Frozen request, owned process, cancellation and persistent log |

Catalog, project configuration and UI drafts do not own live EnTT components.
Receipts do not retain DLL handles; active runtime objects retain a module lease
from PluginSession. Avoid a global static plugin registry per DLL.

## 3. Discovery and resolution algorithms

Discovery is read-only:

```text
for each configured root, in stable order:
    enumerate manifest paths without evaluating code
    canonicalize path; detect path aliases and duplicate IDs
    parse strict schema; attach diagnostics to invalid entries
    validate declared paths, module names, kinds and installed version
return sorted catalog + diagnostics
```

Do not traverse arbitrary source/build directories looking for DLLs. An invalid
unselected plugin remains visible as invalid; it does not prevent unrelated selected
plugins from resolving. A duplicate selected ID cannot be resolved by scan order.

Resolve against one catalog snapshot:

```text
queue explicit IDs in sorted order
visit each plugin and required plugin dependency:
    validate existence, installed version, SDK/platform and Engine/project direction
    record every reason edge; deduplicate nodes, not reasons
    detect visiting -> visiting cycle and report its full path
validate module dependency names, Runtime/Editor restrictions and module cycles
derive Editor DLL plan and shipping static Runtime plan
topologically order with stable ID/module-name tie breaks
serialize canonical resolution and calculate identity
```

Version one does not fetch packages or backtrack through several installed
versions. Optional integrations must not become undeclared includes. Either the
feature declares a dependency or a separately registered capability adapter makes
its absence a tested supported state.

When unchecking dependency D, compute the reverse dependent closure before
changing Draft. Present `Disable A and B as well` or Cancel. Apply commits a
validated complete draft; a checkbox handler never directly stops a live module.

## 4. Handshake and Engine SDK

Use an exported C entrypoint for each DLL, with a narrow descriptor exchange.
Illustrative layout, with omitted service function signatures:

```cpp
struct AxlePluginHostV1 {
    uint32_t StructSize;
    uint32_t ApiVersion;
    const char* SdkFingerprint; // UTF-8; host-owned, valid for session lifetime.
    void* HostContext;
    // Function pointers for diagnostics, services and owner-scoped registration.
};

struct AxlePluginModuleV1 {
    uint32_t StructSize;
    uint32_t ApiVersion;
    const char* PluginId;       // Module-owned; valid until unload.
    const char* ModuleName;
    const char* SdkFingerprint;
    int32_t (*Start)(const AxlePluginHostV1*, void** instance);
    void (*Stop)(void* instance);
};

// A DLL exports one well-known query symbol. No Engine work in DllMain.
extern "C" int32_t AxlePlugin_QueryV1(AxlePluginModuleV1* out);
```

The implementation must finish the ABI specification: calling convention, packing,
nullability, structure version/size evolution, string lengths, returned-error
lifetime, initialization-failure ownership and exception containment. Catch plugin
exceptions inside module entrypoints; callbacks must have the same declared policy.
Do not pass `std::string`, `std::function` or an owning STL container through the
C handshake. Return a plugin-owned opaque instance with its matching Stop/destroy.

Host services can expose typed C++ SDK interfaces after compatibility validation.
That SDK is tied to the toolchain/build fingerprint. A C entrypoint does not make
`entt::registry`, Jolt objects or ImGui ABI-independent. Public C++ interfaces and
vendor includes must be explicitly reviewed usage requirements, not every Engine
static target exported transitively.

Keep shared state behind exported host accessors. In particular, audit Project's
inline static active project, Application singleton, Log, JobSystem, ScriptRegistry,
RHI device/mesh/resource state and ImGui globals. Prefer passing per-session and
per-scene service references; shared accessors must resolve to one storage location.

Build Jolt's shared variant only for the Editor host/plugin graph and publish
matching import definitions. Verify allocator/factory/type registration is unique.
The shared host owns initialization and shutdown. Never link a second static Jolt
copy into VehicleRuntime.dll while passing it host physics objects.

## 5. Module loading and registration transactions

```text
validate receipt hash, module metadata and declared SDK/build configuration
load prerequisites from the exact staged receipt directories
load selected DLL; query descriptor and compare identity again
create owner-scoped registration transaction
call Start(host services, transaction)
validate registered component IDs, phase dependencies and service requirements
commit registrations and append module to Active order
on failure: unwind only this transaction and initialized dependents in reverse order
```

Windows loaders run DLL initialization while loading; checking the query result
cannot protect against a misbehaving static initializer. The build contract forbids
Engine side effects in static initialization, and native fixture tests verify it.
Receipts are compatibility/provenance checks, not a sandbox for untrusted binaries.

Use explicit selected DLL paths and scoped dependency search paths. Keep dependency
handles for the full active lifetime. Two plugins importing the same module resolve
to the same loaded artifact/instance, not two copies under different temporary names.
Version one does not load a newer generation into an existing scene session.

Define failed-Start ownership explicitly: return a null instance after complete
local cleanup, or a non-null partial instance that the host must pass to Stop.
The host always unwinds the registration transaction as well. Do not allow
background work or external callbacks to escape an uncommitted Start; any bounded
initialization jobs must be joined before Start returns or fails.

Each registration receives an owner ID and removable token. Component codecs,
Inspector callbacks, menu actions, phase systems, render extraction and datasource
actions all use that mechanism. Do not retain anonymous lambdas with raw module
pointers in global registries that cannot identify their owner.

## 6. EnTT storage, clones and module leases

The scene's registry remains authoritative. A plugin can use typed views directly
after receiving its scene context and the supported SDK. Core consumers do not
need to include that component type to enumerate registered descriptors.

Persistent component identity is a namespaced schema ID, for example
`axle.vehicle/chassis-v1`. It is distinct from EnTT runtime type indices and from
an entity UUID. Validate aliases before accepting input. Compiler-generated type
names/hashes are not an authored-format versioning strategy.

For clone/duplicate:

```text
allocate destination entities and build complete source-handle -> destination mapping
copy foundation authored data and UUIDs according to operation semantics
invoke each present registered component codec's clone/remap operation
allocate fresh plugin runtime instances only when the destination enters simulation
retain unresolved payloads passively; reject unsafe opaque-reference remaps
```

Differentiate full scene Copy for Play (preserve authored identity in another
registry) from DuplicateEntity (new authored identity). Multi-scene contexts must
not confuse equal numeric `entt::entity` values from different registries.

Never copy live body/constraint IDs, task state, audio voices, GPU owners or Vehicle
pointers as authored state. Copying the same `shared_ptr` to live simulation state
is not a deep clone. Test the source and clone can reset/stop independently.

Module leases must cover storage implementations, not only component instances.
An empty DLL-created pool may still contain DLL vtables/destructors. At shutdown,
destroy SceneExtensionSet and every registry/pool while its DLL and dependencies
remain loaded. Include histories, private previews and script-reflection metadata
in the ownership graph. If the implementation cannot prove safe release, retain
the module until controlled process teardown rather than calling FreeLibrary early.

## 7. Serializer extension and raw data

Reuse SceneSerializerContext callback seams where useful, but replace single-slot
domain composition with deterministic owner-scoped codecs. Inspect all Assembly
APIs separately: Serialize/Deserialize, LoadAssemblyData/SaveAssemblyData and
preview instantiation must preserve component extensions too.

Suggested parsed section model:

```text
entity authored UUID
component stable type ID + owner plugin ID + schema version
raw section bytes (retained until a codec successfully claims and validates it)
decoded authored payload, only when a matching active codec is available
required runtime plugin/dependency metadata
```

Preserve order/bytes for unknown sections within the surrounding serializer's
document model. Do not reconstruct unknown blocks by trimming every line.
Malformed known data remains preserved with diagnostics; codec failure must not
consume it or partially instantiate simulation state.

A missing codec is not permission to create a generic rigid body for a former
Vehicle chassis. The load/Play preflight checks unresolved required components and
body ownership. Cook scans the actual assets the current packaging policy includes,
not only whichever scene happens to be open in Editor.

New Vehicle uses new component IDs. Legacy tags remain unclaimed raw data after
old-code retirement. A future converter should preview mapped values/warnings,
write a new asset or explicit undoable edit, and never overwrite originals merely
because a project was opened.

## 8. Scheduling and resource contribution seams

Define a small ordered phase set with explicit before/after dependencies. Do not
launch a task per entity or use plugin discovery order as simulation order.
Separate control preparation from post-Jolt synchronization. Fixed dt, accumulator
clamp, pause and single-step are shared product semantics.

Structural mutations happen at the owning scene thread's barriers. Prepare typed
storage/views before parallel reads, partition independent writes and wait on
dependencies before consumers. Creating a new storage from a const/worker path is
not assumed safe. EnTT atomic configuration is not a global registry mutex.

Plugin tasks use the Engine JobSystem with an owner/session generation. The task
and its captures hold module leases. Main-thread continuations check the owner is
still active and the scene generation matches before applying changes. Task bodies
and completion/destruction callbacks must all be finished before unloading code.

Render extraction contributes engine-owned or leased draw data, particle systems
and debug primitives; SceneRenderer must not call `GetRuntimeVehicles`. Renderer
consumption and retirement retain whatever plugin code/resources remain necessary.
Prefer host-owned immutable data with no plugin callback in the GPU submission
path. Do not add a new shader pipeline or claim Phase 41 renderer parity here.

## 9. Plugins window implementation model

Keep a view model alive across frames. Initialize Draft once on opening/project
change, not by resetting it from ProjectConfig every render. Key each row by
stable plugin ID; filter/search must not change widget identity or selection.

Show both Desired and Active state. Example statuses: Disabled; Required by X;
Build required; Building; Restart pending; Active; Incompatible; Initialization
failed. Distinguish manifest parse errors from compiler errors and missing runtime
content. Details show the complete dependency reason chain and source origin.

Use one build coordinator shared with export to prevent overlapping configuration
mutations. The UI polls an operation result without blocking frame rendering; all
ImGui/scene mutations remain on the owning thread. Progress and logs come from
actual configure/build processes, not invented percentage completion.

Apply actions:

```text
validate Draft -> atomically save Desired -> derive Pending difference
if restart requested:
    run all registered dirty-document guards; Cancel stops here
    capture frozen build/startup request; lock configuration edits
    build/validate missing modules into a new artifact set
    on success publish receipt and launch explicit project/backend/receipt
    only then close the old session through normal teardown
    on failure keep current session and Pending state, expose Retry/Revert
```

If launching the new process fails, do not close the old one. Coordinate a startup
acknowledgment so a failed new initialization can produce recovery rather than
silently replacing the user's working session. A bounded restart token prevents
automatic restart loops. Dirty guards run again if the user edits documents during
the build; alternatively lock document edits for that operation with a visible reason.
Do not silently save only the scene while discarding an unsaved VFX/assembly document.

Selecting another project with a different effective DLL graph also requires a
new process/session; do not hot-unload the first project's modules. If the graph
is identical, a same-process scene switch still requires complete scene teardown.

## 10. Build keys, receipts and process ownership

Canonical resolution includes explicit/effective IDs, exact versions/source roots,
module graph and target platform. Build identity adds Engine/SDK identity, toolset,
architecture, CRT, Debug/Release, generator and relevant compile options. A module
receipt includes source and build-input identities plus dependency artifact hashes.

Use a separate product directory for Editor DLLs and static shipping builds.
Reuse valid host artifacts and dependency-aware incremental outputs; selection
changes do not imply recompiling the entire Engine. Source changes still require
normal compilation even when the selected ID set is unchanged.

The process helper accepts an executable and argument vector, owns child processes,
supports cancellation and drains their output to a log. On Windows preserve current
CreateProcess quoting/job-object behavior; use hidden build processes. Build worker
threads must not call Editor UI or read mutable active-project globals.

Publish only a complete validated output set using a staging/receipt transaction.
Previous loaded DLLs remain untouched; new artifacts use another generation path.
Do not clean arbitrary project folders or delete a cached artifact still leased
by another running Editor. Store logs and Intermediate outputs outside tracked source.

Shipping registration generation must reference every selected module explicitly:

```cpp
// Generated only for the resolved Runtime module list; names are illustrative.
void RegisterProjectRuntime(Host& host) {
    RegisterVehicleRuntime(host);
    RegisterRacingAIRuntime(host);
    RegisterProjectScripts(host);
}
```

This creates linker-visible references; do not depend on static object constructors
being retained from archives. Enforce registration order and rollback through the
same owner/lifecycle contracts as DLLs. Do not generate references for Editor modules.

## 11. Scripts, content and export

Project gameplay source declares the plugin Runtime APIs it consumes. Its Editor
DLL imports the host and required plugin DLLs. Its shipping static module links
the corresponding static APIs. Generate script registration for both products;
do not always link `Axle::UI` or `Axle::Vehicle` as the current Editor script target does.

This changes the current Runtime packaging requirement for `AxleScripts.dll`.
Update AssetCooker, RuntimeBuild, startup and workflow fixtures together at the
proper gate. Keep the old shipped layout working until that bounded cutover; never
ship a player that expects a removed DLL or includes an unselected plugin through it.

Mount content with stable plugin-root identities, such as
`plugin://axle.vehicle/...` (a proposed asset namespace, not a network protocol).
Keep authored asset UUIDs and referenced dependencies; avoid absolute source paths
inside packages. Selected Editor-only content is excluded. Missing required runtime
content fails preflight; optional missing assets need documented fallback behavior.

Export freezes project/configuration, resolved modules, scripts and asset inputs.
Build and cook bind to that snapshot. Verify player hash/registration/receipt and
packaged plugin requirements before publishing the output directory. A changed
Desired selection cannot reuse a different player merely because its filename is
`AxleRuntime.exe`. Existing successful output survives compiler/cook/cancel failures.

## 12. Greenfield Vehicle implementation notes

Start from a tiny authored chassis and four wheels in a headless Jolt world.
The old implementation is a feature reference, not a source/API preservation rule.
Make supported units and coordinate axes explicit; validate finite positive mass,
wheel radius, suspension travel, axle layout and engine/transmission settings.

Treat input commands, authored config and telemetry as distinct data. Clamp input
and define reset behavior for velocity, gear, controls, wheel synchronization and
audio/VFX state. Editing unsupported physical configuration during Play should be
disabled or applied through a deliberate rebuild command at a safe barrier.

Create body/constraint state transactionally and register physics callbacks only
after successful setup. Stop/unregister before destroying referenced bodies. Repeated
start/stop, failed creation and deleting a wheel/chassis must not leave listeners,
constraints or bodies referencing freed state. Query services identify a scene and
entity explicitly; no new global 'primary vehicle' in Engine.

VehicleEditor registers Inspector/add actions, validated setup commands, debug
contributions and telemetry. Keep camera/input/HUD/reset bindings in explicit
plugin integration or sample gameplay modules. Engine UIDataContext remains generic.
Audio/VFX come from scoped telemetry adapters with a single update owner per scene.

## 13. Test fixtures that prove the boundaries

Use actual production APIs and libraries; fixture success must not be hardcoded.

| Fixture | Checks |
| --- | --- |
| Manifest-only poison plugin | Discovery/empty build never loads DLL or evaluates unselected configure script |
| External Counter plugin | Real component with heap state, Inspector and increment system outside Engine tree |
| Shared-state probe | Host/plugin/script DLL see identical project/jobs/physics/device/UI owners |
| Pool/destructor probe | Component and empty storage destructors run before DLL unload |
| Failed registration plugin | Late Start failure removes earlier owner registrations and instances |
| Delayed-task plugin | Main-thread continuation/worker teardown cannot touch closed scenes |
| Unknown-data fixture | Scene and all assembly paths preserve unavailable raw sections |
| Fresh Vehicle world | Real Jolt contact/controls/reset/stop with declared tolerances |
| Static project player | Same components/scripts and required assets work without Editor/source checkout |
| Stale export fixture | Wrong selection/source/SDK receipt is rejected; old successful output retained |

Use always-active assertions in Release and a deliberate failure probe. Distinguish
metadata CPU, native DLL, physics, actual submitted GPU readback, native UI input and
packaged player checks. Do not call a mock DLL test a successful binary boundary test.

Prove exclusion through generated target/source/link closures, registration lists,
build logs and binary imports. Absence of one symbol after optimization alone is
insufficient; dormant code behind a flag also fails the optional-plugin contract.

## 14. Scoped handoff template

When implementation is explicitly assigned, record in the exchange:

```text
Package/range: PLG-xx only
Base commit and current git status:
Owner and expected files:
Accepted prerequisite evidence:
Concrete behavior and supported limits:
Permanent fixtures and exact build/run commands:
Forbidden scope / overlapping work:
Delivery commits, log paths, exit codes:
Reported versus independently verified results:
Remaining native UI/GPU/player gates:
```

Do not generate an assignment for another agent from this template alone. Preserve
the current documentation-only boundary until the user releases implementation.
Commit verified changes locally with explicit staging; no push unless requested.
