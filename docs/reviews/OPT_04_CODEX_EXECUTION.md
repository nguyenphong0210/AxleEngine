Latest user steering (2026-11-07): implement all P41-01..11, then perform one consolidated final review. This supersedes intermediate review checkpoints; package verification and native acceptance gates remain required.

# OPT-04 Codex execution record

Date: 2026-11-07. Starting clean HEAD: `977f68c42755f1cfc0042854dc06dc74fc8cebc8`.
Scope: P41-01 through P41-11 only. No push or OPT-05 implementation.

Implementation and self-verification are separate from the consolidated final
fresh-context review requested by the user. That review has not completed.
Gemini has not supplied supplementary feedback. Two preliminary Codex readers
provided advisory source audits; these are not checkpoint reviews. A subsequent
advisory request failed because the reviewer account hit its usage limit.

## Environment and evidence

- Windows x64; physical NVIDIA GeForce RTX 3070, NVIDIA driver reported by GL as
  610.88. Native tests reject software adapters and backend fallback.
- NRI remains clean at `6998e3fbbaed1b132f386a9d49743a2481003487`, API 190.
  No vendor source upgrade/edit was made. The initial build directory had no
  CMake cache; normal configuration populated the pinned build dependencies.
- Generated scripts, logs and JSON results are under ignored
  `build/agent-runs/OPT-04/`.
- Graphics API validation is **NOT RUN**: the host has no Vulkan Khronos
  validation layer, and D3D12/DXGI debug prerequisites are unavailable. Vulkan
  layer enumeration was checked directly against the installed loader. An
  initial D3D12 debug request failed in `CreateDXGIFactory2` with `0x887A0001`.
  The Engine now rejects unavailable requested validation instead of accepting
  NRI's silent Vulkan layer filtering. Native operation tests separately enable
  embedded NRI validation. These tests do not establish API-validation acceptance
  for P41-07 or close any later gate requiring those layers.
- Builds retain the existing `LNK4098` CRT-library conflict warning. No claim is
  made that this warning has been resolved by OPT-04.

## P41-01 — backend readiness and startup transaction

Implemented and self-verified. Consolidated separate-context review remains due after P41-11.
Local implementation commit: `aba9a3c`.

Device, interfaces and queue remain private candidates until every required
operation succeeds. Native readiness and context readiness are explicit; generic
backend selection cannot authorize NRI resource calls. Explicit GL and fallback
GL now use the same selection/product/resource path. Strict selection survives
CLI argument order and cannot fall through to a manually selected GL backend.

Product composition owns the complete startup attempt. Window, renderer and UI
failure unwind before another backend is attempted. UI device objects are created
and checked during startup. Vulkan window construction checks each native step
and destroys partial state safely. Late Editor failure destroys panel owners while
their UI/window/device still exist. GLFW state and input context are cleared on
failed startup/teardown. Renderer readiness is checked for both product paths.
Runtime continues to declare its existing GL scene-rendering capability; its
full modern renderer cutover remains P41-19.

The production `ScopedRHIFault` seam injects a result at a named native operation;
all surrounding native creation and cleanup remain real. `AxleRHIFoundationTests`
covers policy, required native interfaces/queue, strict rejection, physical backend
recovery, explicit/fallback GL mesh pixels and nine partial Vulkan UI construction
failures followed by successful real presentation. Editor verification additionally
injects rejection after UI readiness and after the full renderer attaches.

Reproduction commands (run after building the changed targets):

```powershell
cmake -S . -B build
cmake --build build --config Debug --target AxleRHIFoundationTests AxleRenderUnitTests AxleEditor AxleRuntime --parallel 6 -- /p:UseMultiToolTask=true /p:CL_MPCount=4
cmake --build build --config Release --target AxleRHIFoundationTests AxleRenderUnitTests AxleEditor AxleRuntime --parallel 6 -- /p:UseMultiToolTask=true /p:CL_MPCount=4
ctest --test-dir build -C Debug -R '^(RenderUnitTests|RenderHarnessFailure|RHIFoundationPolicy)$' --output-on-failure
ctest --test-dir build -C Release -R '^(RenderUnitTests|RenderHarnessFailure|RHIFoundationPolicy)$' --output-on-failure
# Repeat for Debug and Release, each mode as a separate process:
build/Tests/Debug/AxleRHIFoundationTests.exe policy
build/Tests/Debug/AxleRHIFoundationTests.exe dx12
build/Tests/Debug/AxleRHIFoundationTests.exe vulkan
build/Tests/Debug/AxleRHIFoundationTests.exe gl
build/Tests/Debug/AxleRHIFoundationTests.exe vulkan-window
```

Run Editor commands from its executable directory. Each uses
`--verify-empty-startup`, plus one of:

- `--rhi=dx12 --strict-rhi`, `--rhi=vulkan --strict-rhi`, or `--rhi=gl --strict-rhi`.
- `--rhi=dx12 --verify-startup-rejection=CreateDevice` (complete GL fallback).
- `--rhi=dx12 --verify-startup-rejection=ProductUI` or `ProductRenderer`.
- `--rhi=dx12 --strict-rhi --verify-startup-rejection=CreateDevice` (expected exit 1).

All positive cases must exit 0; the negative strict case must exit 1. Logs and
case-by-case exits are in `p41-01-{debug,release}-results.json` and
`p41-01-editor-{debug,release}-results.json`. Build logs are
`p41-01-final-build-{debug,release}.log`; CTest logs are
`p41-01-ctest-{debug,release}.log`. Earlier incomplete/stale-build attempts are
not acceptance evidence. The final current-source runs all passed:

| Check | Debug | Release |
| --- | --- | --- |
| Four changed targets above | exit 0 | exit 0 |
| Five foundation modes | 5/5, exit 0 | 5/5, exit 0 |
| Seven Editor startup/rollback cases | 7/7 expected exits | 7/7 expected exits |
| Focused CTest | 3/3, exit 0 | 3/3, exit 0 |
| Real Editor/Runtime simulation probe | PASS, exit 0 | PASS, exit 0 |

Simulation was rerun using `python scripts/run_simulation_product_probe.py
build/Editor/<Config>/AxleEditor.exe build/Runtime/<Config>/AxleRuntime.exe
--report build/agent-runs/OPT-04/p41-01-product-simulation-<config>.json`.
Architecture AF-04 check passed (377 files, 18 CMake files); baseline tooling
regressions passed 18/18. `git diff --check` passed. The pinned NRI working tree
was rechecked clean. API-validation acceptance remains NOT RUN as recorded above.

## P41-02 - Transactional resource construction

Starting from `aba9a3c`. Texture and buffer creation now separates real wrapper,
allocation, binding, required views and uploads. Candidates clean up on rejection
and failed replacement preserves the old complete object. Descriptors are checked
before narrowing, including queried extent/format/sample/memory limits. D3D12
depth sampling checks the native SRV alias that the pinned NRI bridge actually
uses, rather than mistaking the DSV format's sampling bits for SRV capability.

Buffer placement borrows memory; successful adoption transfers ownership, failed
adoption leaves it with the caller. Group replacement is transactional. Map/update
ranges use checked subtraction and nested mappings honor their starting offset.
The pinned NRI helper uploads a whole buffer: device-local partial updates now
reject explicitly instead of reading beyond the source or ignoring the offset.
Existing production partial updates use host-visible buffers; no supported caller
was migrated to a smaller device upload. Mesh vertex/index uploads, sampler,
descriptor-pool and layout replacement preserve prior objects. Layout descriptor
ranges are deep-copied. Streamed meshes check staging results before recording.

Viewport color/depth/GUI registration, picking pairs and scene MSAA/post-processing
target sets are candidates. Late snapshot allocation failure abandons recording.
Recording/submission retirement and device ownership remain P41-05/06/10 work;
this package does not claim those lifetimes are already solved.

`AxleRHIResourceTests` is active in both configurations: policy, physical D3D12
and Vulkan passed initial current-library runs (all exits 0), with embedded NRI
validation. Tests cover every texture/buffer allocation/binding/view boundary,
real pixel and byte readbacks after failed replacement, invalid descriptors,
borrow/adopt/group ownership, helper rollback and partial streamed-mesh failure.
Logs: `p41-02-{debug,release}-{policy,dx12,vulkan}.log` and corresponding results
JSON under `build/agent-runs/OPT-04/`. No unexpected NRI errors in those runs.

`AxleRHIResourceViewTests` adds real native windows/ImGui registration and production
scene rendering, mesh pixel preservation, MSAA target recovery and picking-pair
rollback. All final Debug/Release checks passed after rebuilding the changed targets:

| Check | Debug | Release |
| --- | --- | --- |
| Resource tests (policy, physical D3D12/Vulkan) | 3/3, exit 0 | 3/3, exit 0 |
| Native view/product resource tests | 2/2, exit 0 | 2/2, exit 0 |
| P41-01 foundation regression modes | 5/5, exit 0 | 5/5, exit 0 |
| Editor startup/rollback regression cases | 7/7 expected exits | 7/7 expected exits |
| Focused rendering/material/VFX CTest | 6/6, exit 0 | 6/6, exit 0 |
| Physical VFX GPU regression, both APIs | 2/2, exit 0 | 2/2, exit 0 |

Final builds include AxleRHIResourceTests, AxleRHIResourceViewTests,
AxleRHIFoundationTests, AxleRenderUnitTests, AxleVFXGPUTests,
AxleMaterialUnitTests, AxleVFXUnitTests, AxleEditor and AxleRuntime; both exit 0.
Build logs: `p41-02-final-build-{debug,release}.log`. Commands/results/log paths
are recorded in `p41-02-final-{debug,release}-results.json`; the ignored runner
`verify02.py <Config>` reproduces the final test batch. Existing VFX allocation
failure probes now intercept allocation after real buffer-wrapper creation.

NRI validation exposed existing pipeline-layout visibility declarations using
`ALL` alongside fragment-only ranges. Scene/post-processing/picking/particle
layouts now specify the actual vertex/fragment stages; native production drawing
passes with embedded NRI validation and no unexpected NRI errors. The view test
initializes the real job owner before creating a Scene and tears it down normally.
Architecture AF-04, clean pinned NRI and `git diff --check` were rechecked.
P41-02 is implemented and self-verified; the final consolidated review remains open.

## P41-03 - Structured errors and bounded loss policy

Starting from P41-02 commit `0e8673e`. Implementation adds the first
GPU error (backend/operation/resource/native result/ambiguity/timeout), explicit
ready/rebuild/lost/stopping states, blocked admission after loss, and bounded
fence polling/checked queue drains. Unresolved shutdown retains native owners for
a checked retry. Texture/buffer/helper/command/swapchain cleanup uses this queue.

Commands reject unfinished/invalid batches and reconstruct abandoned recordings.
Swapchain completion values advance only after successful submit; present failure
retains that submission. Application consumes UI operational status. UI native
operations report failures, use bounded waits and retain the native backend/context
independently if a layer dies before an ambiguous submission can be drained.
The D3D12 descriptor callbacks have a separate closing owner. This remains a
transition to the resource-generation/recording/frame services in P41-05..10.

Final current-source Debug/Release builds and all package checks passed:

| Check | Debug | Release |
| --- | --- | --- |
| Error policy, native core/loss/timeout and real surface modes | 5/5, exit 0 | 5/5, exit 0 |
| Native UI destruction with unresolved drain | 2/2, exit 0 | 2/2, exit 0 |
| Resource boundary/readback regression modes | 3/3, exit 0 | 3/3, exit 0 |
| Production view/MSAA/mesh regression modes | 2/2, exit 0 | 2/2, exit 0 |
| Foundation regression modes | 5/5, exit 0 | 5/5, exit 0 |
| Editor startup/rollback regression cases | 7/7 expected exits | 7/7 expected exits |
| Focused rendering CTest | 5/5, exit 0 | 5/5, exit 0 |
| Physical VFX GPU regression, both APIs | 2/2, exit 0 | 2/2, exit 0 |

Build command is the P41-01 command extended with AxleRHIErrorTests,
AxleRHIResourceTests, AxleRHIResourceViewTests and AxleVFXGPUTests. Both builds
exit 0. Logs: `p41-03-final-build-{debug,release}.log`; exact test commands/results
are in the ignored `verify03.py <Config>` and
`p41-03-final-{debug,release}-results.json`. Architecture AF-04 passed (378 files,
18 CMake files); diff whitespace and clean pinned NRI were rechecked.

The destroyed-UI-owner test exposed and verified correction of viewport teardown
ordering: renderer-owned main viewport storage survives platform-hook removal,
then retires with its context after a checked drain. The test renders native font
and vertex storage, destroys the original layer, then drains its retained backend.
CRT assertions remain active and log failures without a modal dialog. Earlier
failed/stale attempts are not final evidence. API validation remains NOT RUN on
this host; embedded NRI validation is enabled for the native package fixtures.
P41-03 is self-verified; consolidated final review remains pending.

## P41-04 - Canonical primitive identity

All five authoring factories always create MeshInstance3D and assign an explicit
builtin URI, including CPU-only sessions. A Core-only builtin definition and the
shared Scene resolver serve native and GL paths. Scene/assembly persistence and
copies preserve the identity; renderer and picker no longer guess geometry from
entity names or replace missing references with a cube. A scene without an authored
mesh component/reference cannot be reconstructed safely from its name.

Canonical geometry follows existing GL authoring dimensions: unit cube/plane,
sphere radius 1, cylinder/cone radius 1 and height 2. The former modern builtin
sphere/cylinder/cone dimensions now agree with that contract. Names, transforms,
checker colors and existing static/dynamic Jolt defaults remain unchanged. Cache
shutdown releases shared ownership without invalidating a scene's live mesh.
Picker submission/drain failures are consumed before publishing readable results.

AxlePrimitiveTests exercises the actual authoring API, real Jolt bodies, all five
rename/duplicate/copy/save/reopen paths, assembly save/load/instantiate, native
production scene/picking pixels and GL pixels. Both Debug/Release passed policy,
physical D3D12, physical Vulkan, explicit GL and automatic GL fallback (all exits
0). All five explicit/fallback GL images are byte-identical. Focused CTest passed
1/1 in each configuration. Runner: run04.py <Config>; logs and results:
p41-04-{debug,release}-{policy,dx12,vulkan,gl,fallback}.log and results JSON.

Both configurations built AxlePrimitiveTests, AxleEditor and AxleRuntime (exit 0)
before the final pure-header relocation to Core. After relocation the primitive
target/dependencies rebuilt and all modes above reran. Final build evidence:
p41-04-verified-build-debug.log (exit 0) and p41-04-core-build-release.log (exit 0).
The initial Debug relocation build produced the executable but PowerShell treated
CMake's stderr diagnostic as a shell error; the direct-subprocess rerun returned
0. Architecture AF-04 and git diff --check passed; pinned NRI remains clean.
Embedded NRI validation was active; graphics API validation remains NOT RUN.
P41-04 is implemented/self-verified; the consolidated final review is pending.

## P41-05 - Device epochs and resource generations

Starting from P41-04 commit `59f12b6`. RHIResourceService owns a native device
context with a unique epoch, weak slot table and changing generations. Strong
leases retain complete resource payloads and their dependencies; native cleanup
is queued on release and runs on the render owner after checked completion.
Device close invalidates all remaining generations and destroys their native
payloads before destroying the actual owning NRI device. An unresolved process-exit
owner is quarantined rather than destroying in-flight storage.

Textures, buffers, sampler/layout/pool/pipeline generations, streamers, commands
and swapchains use this owner. Getters reject closed generations; stale wrapper
destruction after a new device opens cannot call the new device's interfaces.
Pipeline cache entries retain layouts and persistent renderer pipeline members
hold RHIResourceRef leases. Descriptor generations retain their layout, arena and
bound resource generations; arena reset builds a replacement. Group allocations
use typed memory leases held by their bound buffers. Raw externally borrowed memory
remains the external owner's responsibility until all buffer leases retire.

Live native UI owners close before their NRI device through a weak guarded owner
hook. Destroyed UI owners cannot be called by that hook. The builtin mesh registry
rebuilds stale device caches while preserving sharing inside the current epoch.
Raw native lookup remains a migration adapter: an address alone cannot detect
reuse; new persistent callers use the handle/lease contract.

AxleRHIIdentityTests covers stale handles, actual slot reuse, strong shared leases,
worker-thread release without native cleanup, owning-device close/reopen, all native
wrapper types, descriptor dependency/arena replacement, grouped memory and late
old-wrapper teardown. Native view tests also close a device while the original
UI and shared builtin mesh are still alive, reopen it, and render through a new
UI/mesh generation. Both physical backends pass with embedded NRI validation.

Final Debug/Release builds of AxleRHIIdentityTests, AxleRHIResourceTests,
AxleRHIErrorTests, AxlePrimitiveTests, AxleRHIResourceViewTests,
AxleRHIFoundationTests and AxleVFXGPUTests passed (exit 0). Build logs:
p41-05-final-build-{debug,release}.log. Each configuration then passed all 29
runner cases: identity 3, resources 3, errors 5, primitives 5, views 2, UI epoch/error
4, VFX GPU 2 and foundation 5. Commands/logs/exits are in run05.py <Config> --views,
more05.py <Config> and p41-05-{debug,release}{,-more}-results.json. Focused CTest
passed 5/5 in each configuration. Fresh explicit/fallback GL images match byte for
byte for all five primitives in both configurations (pixels05.py).

Log inspection caught an external-memory fixture freeing borrowed memory before
its now-deferred buffer generations retired. The fixture now explicitly retains
a buffer lease and checks the drain before its external owner frees the allocation.
Native diagnostics now have a process-wide atomic validation error count, and every
focused native test fails on any NRI error, including teardown. The final runs above
pass this check; earlier logs are not evidence. Architecture AF-04 passed (384 files,
18 CMake files), diff whitespace passed and pinned NRI is clean at 6998e3f.

P41-05 is implemented/self-verified. Recording/submission pinning is P41-06 work;
graphics API validation and the consolidated final review remain open. Product
executables/full phase gates will be rebuilt at the phase checkpoint.

## P41-06 - Recorded/submitted leases and unified retirement

Starting from P41-05 commit `02236e6`. Command recording now pins transitive
resource generations at barriers, attachments, binding and streamed-copy boundaries.
Snapshots retain views, descriptors, layouts, arenas, backing allocations and the
native commands/allocator. Central graphics-queue submission publishes an
epoch/value completion token only after successful submit. Failed submits retain
ambiguous snapshots until checked drain; abandoned recordings release their pins
when reset/destroyed. Duplicate/raw invalid batches and foreign queues reject.
Full buffer updates publish a replacement when recorded/in-flight; mapped storage
cannot be reused while pinned. Conservative whole-queue waits remain for P41-08.

GuiTextureLease acquires the CPU draw-list pin before publishing its image ID.
Registrations own texture and independent descriptor-arena generations. D3D12
descriptor callbacks own shared arena state; Vulkan uses an external shared pool
and copied native cleanup functions. The native ImGui backend itself has a lease
covering fonts, vertex/index storage and renderer context when recorded through the
RHI bridge. Destroying the original UI owner preserves that independent payload
until completion/abandon. Another UI recording cannot overwrite that frame storage.

Inspector, thumbnails, preview icons and viewport registration use leased tokens;
their teardown no longer invokes a destroyed UI owner. Saved registration factories
use weak owner guards. Deferred-release handles refer to the device owner/frame
queue rather than the panel/layer and drain nested callbacks outside iteration.
Viewport replacement retains the old GUI generation and uses a saved factory
without dereferencing a dead UI pointer. Full per-view history/MSAA ownership is
still P41-10 work; shared presentation is still P41-09 work.

AxleRHIRetirementTests verifies transitive CPU recording pins, transfer, nested
retirement, real original GPU pixels after replacing a source before submit,
unobserved completion, mapped reuse rejection, completed slot reuse, abandon and
ambiguous failed submission on both physical APIs. Windowed GUI fixtures verify
reload/close after image publication, queue drain before UI submit, original GPU
pixels, submit/abandon, saved handles after owner destruction and no NRI errors.
The D3D12 RHI UI bridge additionally renders the original red image after its UI
object is destroyed before command submission. The Vulkan RHI UI pipeline/target
composition is part of P41-09; this package's Vulkan GUI test uses its real native
windowed presenter. No cross-API interop/presentation acceptance is inferred.

Final Debug/Release builds of AxleRHIRetirementTests, AxleRHIIdentityTests,
AxleRHIResourceTests, AxleRHIErrorTests, AxleRHIResourceViewTests,
AxlePrimitiveTests, AxleRHIFoundationTests and AxleVFXGPUTests passed (exit 0).
Logs: p41-06-final-build-{debug,release}.log. Each configuration passed 34 runner
cases: run06.py <Config> --views (18), more06.py <Config> (13) and retire06.py
<Config> (3). Corresponding p41-06-{debug,release}{,-more,-retirement}-results.json
record exits and logs. Focused CTest passed 6/6 in both configurations. All five
fresh explicit/fallback GL images remain byte-identical in each configuration.
Native test mains enforce zero NRI validation errors through teardown; final logs
contain none. Architecture AF-04 passed (385 files, 18 CMake files), diff whitespace
passed and pinned NRI remains clean. An Inspector backend-selection regression
found by the real VFX/Inspector fixture was fixed before these final runs.

P41-06 is implemented/self-verified. Graphics API validation remains NOT RUN;
the consolidated separate-context review is pending after P41-11. No intermediate
parallel review session was opened in this run. Full product/phase gates remain open.

## Remaining work

P41-07..11 remain. Next: integrate generation/range-aware pending resource state
with successful submission, then frame slots, shared presentation, complete view
targets and central CPU decode/upload/cache ownership. Continue through P41-11,
then one consolidated final review.

Full phase builds, CTest, integration, jobs/ECS, final native product simulation, GPU
readbacks and final window/lifetime checks remain open. API-validation prerequisites
must be available before claiming dependent P41-07 acceptance. The pre-existing
OPT-00 manual/full-product gap remains separate.
