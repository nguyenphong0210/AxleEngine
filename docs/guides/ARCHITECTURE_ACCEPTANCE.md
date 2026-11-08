# Architecture contracts and acceptance

The [roadmap](../ROADMAP.md) owns package status. The
[OPT-10 record](../reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md) owns current evidence
and open gates. Historical design proposals and dated dependency inventories do
not describe additional released work.

## Boundaries

`Axle::Core` owns jobs, identity and CPU IO. `Axle::SceneData` owns registry,
hierarchy, transforms and generic extension persistence. `Axle::Asset` owns CPU
formats, registry and import/cook interpretation. These targets do not require
native device creation. `Axle::Physics` links Core and Jolt, with no graphics link.
`Axle::PhysicsDebug` integrates Physics with Renderer/RHI above that boundary;
`PhysicsScene::DrawDebug(JPH::DebugRenderer&, ...)` emits vendor primitives into
the caller's consumer. Scene composition links that adapter. There is no concrete
OpenGL debug overload in Physics.

Renderer owns `Axle/RHIECSStreamer.hpp`; the old RHI forwarding header is removed.
RHI cannot include Renderer, Scene, EnTT or Jolt. The default architecture scan
has no baseline allowances. It inspects declared source/CMake unions, including
inactive branches, nested helpers, relative includes and known vendor roots.
Unresolved dynamic composition is a coverage error, never a validated violation.
The external Runtime graph is separately validated by the frozen build/receipt
and configured/link/import checks. This conservative scanner is not a C++ parser
or a sandbox for arbitrary third-party build scripts.

## Shared lifecycle and seams

| Contract | Implemented owner/API | Consumers |
| --- | --- | --- |
| Deferred completions | Core `OperationOwner`, `OwnerToken`, `PostToMainThread`, `PumpMainThreadCompletions` | Compiler, plugin build/restart, product safe boundaries |
| Task completion | Core `JobHandle`, epochs and admission; Physics `JobSystemEnkiTS` stable records | Scene, Jolt and plugin task leases |
| Structural mutation | SceneData `SceneCommandBuffer`, scene lifetime/epoch validation | Editor/Runtime and plugin component hooks |
| Transform publication | SceneData `UpdateWorldTransforms`, `UpdateWorldTransformsParallel` | Read-only extraction and completed parallel queries |
| Simulation | Scene `SimulationDriver`, `SimulationClock`, `SimulationInputAdapter` | Both actual Editor Play and Runtime update loops |
| Plugin ownership | `PluginProjectSession`, RuntimeServices/EditorServices and native host gateways | Selected Editor DLLs and selected static Runtime modules |
| Render ownership | `RenderService`, immutable extracted values, RHI resource/code leases and `RHIPresentationContext` | Scene, private views, thumbnails, debug and Canvas |
| Artifact identity | Frozen Runtime request, materialized snapshot, generated registration and matching receipt | UI/CLI cook/export and packaged player preflight |

Plugin callbacks use the existing simulation phases and owner/service leases.
They do not create another scheduler, fixed clock, device or presentation owner.
Captured callbacks/destructors and submitted graphics work retain code until CPU,
scene and GPU owners have retired. Native activation is startup-only; scripts use
their separate validated ABI **4** generation-swap contract.

The default clock admits at most 0.25 seconds of wall time and eight fixed ticks
per frame, at a 1/60-second fixed interval. Clamp drops excess whole ticks and
retains fractional remainder; Drop discards the overloaded frame's interval.
Invalid dt is diagnosed. Pause/resume and zero speed do not accumulate gameplay
backlog. A requested paused Step executes one complete fixed tick. Input edges
are consumed once; paused UI events do not replay on resume. Controls run before
physics, followed by body/telemetry synchronization and late presentation.

Native assets embed UUIDs; raw files use canonical root-relative paths. No
per-asset `.meta`, `.uid`, `.import` substitute or authored root registry is used.
Discovery rebuilds the in-memory index; generated caches remain ignored. See the
[asset contract](../reference/ASSET_FORMAT.md) and
[plugin authoring guide](../design/PLUGIN_SDK_AUTHORING.md).

## Reproduce acceptance

```powershell
cmake -S . -B build-opt10 -DAXLE_ENABLE_VFX_GPU_TESTS=ON
python scripts/run_architecture_acceptance.py --build build-opt10 --output build-opt10/acceptance
```

The default runner rebuilds Editor, Runtime and AxleAllTests in Debug/Release,
runs full CTest, default integration, repeated stress/real-script-DLL lifecycles,
the typed jobs harness, actual product simulation parity, physical D3D12/Vulkan/GL
rendering/failure/Canvas/presentation checks, and five detached exported products.
The exporter must have its matching SDK package built first. A new output run
directory records exact argv/cwd, source identity, logs, exit codes and executable
hashes; edits during a run invalidate that run's source identity.

`--stages build unit native export` selects work explicitly. Omitted stages yield
INCOMPLETE (exit 2), a failed check or timeout yields FAIL (exit 1), and all
automated stages passing yields exit 0. Missing binaries are NOT RUN. Intentional
negative tests require their exact exit and proof marker; crashes and setup
errors do not count. Watchdogs terminate only their launched process tree.
The default stops after the first mandatory failure and still publishes the
evidence report; `--keep-going` collects independent gates for fault investigation.

Native UI observations and independent correction review are separate gates.
The runner cannot manufacture them or set architecture acceptance itself.
Current-source evidence must map to FOUND-01..21 in the master plan and the
applicable EJ/PLG rows. The optional plugin domain features deferred by the user
remain deferred; foundation composition does not complete vehicle handling,
terrain sculpting, track generation, advanced audio/VFX or rendering OPT-11.
There is no backend-default change or unmeasured performance claim.
