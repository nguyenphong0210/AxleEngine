# Phase 39 follow-up review and acceptance

> **Archived delivery/review:** Evidence retains its recorded revision and verification limits; archival does not establish acceptance.
> Follow the [optimization roadmap](../../../ROADMAP.md) for current work;
> recorded instructions/commands below describe the original task.

Date: 2026-11-02. Reviewed Gemini baseline: `1de558c` (27 commits after the handoff).
Authoring follow-up: 2026-11-03; see the neutral workspace delivery below and the
[authoring guide](../deferred/VFX_EDITOR_AUTHORING_GUIDE.md).
Status: corrective implementation and automated/physical backend verification complete; interactive UI/OpenGL acceptance remains open.

The original commit subjects covered A1-J4, but several production paths still
used legacy adapters or placeholders. Passing the original CPU suites did not
establish editor-to-runtime or GPU acceptance. Phase 39 remains open for the
interactive editor checklist below; Phase 40 has not started.

## Corrective implementation

| Area | Finding and resulting behavior | Regression evidence |
| --- | --- | --- |
| Definitions and scenes (A/B/E) | Component/panel adapters discarded IDs, seed, transforms, trigger-only and resource-enable flags. Adapters now retain complete definitions. Inline scenes use the canonical versioned AssetDSL payload, encoded as a single hex scalar for the existing line-based scene parser. | Production component/panel save-reopen and complete scene round trips. |
| Atomic saves and inactive modules (B2/G) | Copy-overwrite was not atomic, and disabled modules/force fields omitted authored values. A shared Windows/POSIX replacement helper now preserves the old file on failure. Both modern and legacy containers share the complete module codec, including inactive values. | Locked-destination save preserves old bytes and dirty state; inactive lighting/noise/collision/shape/forces and full-width material identity round-trip. |
| Shared versus unique storage (B3/E3) | References could be silently replaced by a stale inline snapshot. A valid referenced asset now supplies the definition; a complete fallback is used only when the source is unavailable. Make Unique owns an independent copy. Applying a working copy to an entity explicitly detaches its source binding. | Modify the source after saving a scene; reference follows it, unique copy does not; missing source still loads a complete fallback. |
| History and dirty state (E/G) | Redo restored the undo snapshot, clean state used history depth, and panel edits bypassed history. Commands now restore before/after definitions and selection, use unique revision identities, and coalesce module drags in transactions. | Value assertions after undo/redo, selection restoration, branch-after-save dirty state, grouped edits, discard, and panel integration. |
| Authoring workflow (E/F/G/I) | The document is now connected to module edits, emitter duplication/removal/reorder and scene Apply. Asset documents survive unrelated scene selection. Open/Save As, diagnostics, unsaved New/Open/close handling, texture search/drag/disable/clear, material/OBJ resource pickers, authored transforms, force fields and collision boxes are available. | CPU document/panel tests; interactive acceptance remains NOT RUN. |
| Playback (C) | Reapplying config rearmed bursts; paused seeking suppressed emission; seeking to a loop boundary could never complete. Config sync preserves scheduling, transport pauses the clock, and seek ticks remain monotonic. Autoplay and manual start retain their emission windows. Several duration crossings in one update have bounded burst work. | Real panel seek versus stepped simulation, unchanged/unrelated config edits, autoplay/manual start, duration and multi-loop scheduling tests. |
| Runtime graphs (H) | Dispatch used names only, inherited color vanished after the next update, inherited births were missing, and failed spawns could generate births. Dispatch prefers stable IDs, bounds generations/events/spawns, preserves color multipliers, and converts event coordinates between simulation spaces. Legacy direct child APIs reject cycles and duplicate direct aliases. | Renamed ID targets, trigger-only suppression, inherited tint after update, real/failed Birth events and legacy cycle checks. |
| Mesh and physics (I) | Authored mesh paths now bind a real triangle sampler at stack initialization. Scene Physics now binds the actual Jolt raycast service, including surface normals. Local particles collide in world coordinates and convert back. | CDF sampling tests, production physics callback test against a real Jolt plane, local-space bounce and unbind checks. |
| RHI preview (D2) | The RHI entry point called the OpenGL framebuffer path. It now renders panel-owned systems through a private scene and `RHISceneRenderer` into the viewport's real RHI targets. A private mesh grid follows the grid toggle. Teardown releases preview GPU resources before device shutdown. | Shared production renderer GPU tests; actual editor presentation/interaction remains NOT RUN. |
| Particle drawing (D/F) | The material adapter was unused; depth used a white placeholder. Production draws now resolve material tint, albedo/normal resources, emission, UVs, blend mode and cutoff, and sample a separate opaque-depth snapshot. Alpha sorting uses camera depth, local transforms include scale, and draw resources use independent descriptor pools and appropriately sized instance buffers. | Physical DX12/Vulkan readbacks described below. |
| Curve persistence (B/G) | Serializer omitted Bezier time coordinates and linear keyframes. Full control points/keys now round-trip, and Bezier evaluation solves the time coordinate. Keyed interpolation can be edited or explicitly replaced by Bezier editing. Single-cell flipbook settings are preserved too. | Complete canonical round trips with asymmetric handles and multi-key curves. |
| Acceptance (J) | The GPU function initialized a device and returned success without drawing; Step 102 ignored its return value. It now renders, reads pixels, validates behavior, saves captures and fails the caller on errors. A separate opt-in GPU CTest target is available. CPU fixture writes use unique temporary directories. | CPU tests, physical backend tests and historical runner checks. |

## Reproduction

From the repository root, with MSVC and the project's native dependencies:

```powershell
cmake -S . -B build -DAXLE_ENABLE_VFX_GPU_TESTS=ON
cmake --build build --config Release --target AxleVFXUnitTests AxleVFXGPUTests AxleTests AxleMaterialUnitTests --parallel 12
ctest --test-dir build -C Release --output-on-failure
```

GPU CTest registration is off by default. Enabling it explicitly requires real
supported hardware; the tests reject software adapters and backend fallback.
`AxleVFXGPUTests` uses `AXLE_VFX_TEST_BACKEND=D3D12` or `Vulkan`.
`AXLE_VFX_CAPTURE_DIR` optionally redirects captures outside project assets.
Default artifacts are in `build/vfx-captures/d3d12` and `build/vfx-captures/vulkan`.

The GPU fixtures cover independent textures/instance data with reversed draw
order, texture replacement/disable/missing fallback, material tint/cutout/
emission/UV transforms, multiply blending, normal maps, live lighting and near/
far fading, actual opaque-depth intersection, MSAA, HDR tone mapping, RGBA16F
readback, odd-sized targets with row padding, and seeded reference save/reopen.
Reference effects exercise alpha and additive draws. Captures use isolated
fixtures; neither the user's scene nor the checked-in VFX assets is overwritten.

Reports contain adapter/backend, target dimensions, sample count and measured
CPU wall time around rendering/submission/queue completion. They are not GPU
timestamp measurements or a general engine performance benchmark.

## Final validation record

- Release builds: `AxleVFXUnitTests`, `AxleVFXGPUTests`, `AxleTests`,
  `AxleMaterialUnitTests`, and their `AxleEditor`/`AxleRuntime` dependencies: PASS.
- CTest: **4/4 PASS** (Material CPU, VFX CPU with seven suites, physical D3D12 VFX,
  physical Vulkan VFX), NVIDIA GeForce RTX 3070, fallback disabled.
- Historical Step 102 with requested physical D3D12 acceptance: PASS.
- Material Step 106 regression: PASS on both D3D12 and Vulkan.
- Negative checks: invalid Step 102 backend fails the runner; a file-valued
  capture directory is rejected without modifying that file.
- Windows locked-destination test: failed replacement preserves saved bytes and
  dirty work; retry after unlocking succeeds.
- The pre-existing MSVC `LNK4098` CRT linkage warning remains. The fixture
  integer-to-float warning was corrected; the final build has no C++ compiler warnings.
- Five captured SDR fixtures (descriptor isolation, soft depth and all three
  reference effects) match byte for byte between D3D12 and Vulkan on this RTX 3070.
  The comparison record is `build/vfx-captures/backend-parity.json`.
- Actual editor interaction and OpenGL execution: **NOT RUN**.

Code commits after the Gemini baseline:

| Commit | Scope |
| --- | --- |
| `ba731ae` | Complete component/scene definition adapters |
| `051d066` | Correct document snapshots and unique history revisions |
| `aa8110f` | Burst schedule and inherited event fixes |
| `1dbbe04` | Production material/depth wiring and updated buffer layout contract |
| `46b9707` | Complete module persistence, validation, legacy codec and atomic saves |
| `d997fbc` | Physics service, local collisions and playback behavior |
| `513257b` | Integrated document UI, transport, pickers and RHI preview |
| `a709c10` | Camera-depth sorting, lighting and draw-resource allocation |
| `c59eaa3` | Real GPU fixtures, CTest target and failure propagation |

## Remaining acceptance and explicit limits

- [ ] Exercise the actual editor on DX12 and Vulkan: open/save/new/close prompts,
  resource search and drag/drop, rename/duplicate/reorder/remove/solo, narrow
  layouts, curve/color/alpha editing, undo/redo and dirty markers.
- [ ] Compare the displayed preview with a placed scene instance, including scene
  switching, deletion, Apply, Make Unique, and renderer teardown/reopen.
- [ ] Perform visual review of all three reference effects with intended art
  textures. The automated tests deliberately permit procedural missing-resource
  fallbacks and do not establish artistic quality.
- [ ] Run a separate OpenGL regression session. DX12/Vulkan evidence does not
  establish OpenGL feature parity.
- MSAA retains real depth testing but disables soft intersections until a
  compatible multisample depth resolve is implemented; it never samples the
  active writable DSV or pretends a white map is scene depth.
- Particle materials support the documented billboard subset. Arbitrary custom
  shader programs and advanced PBR surface models remain outside this phase;
  emission currently uses color/intensity rather than an emission texture.
- Mesh emission currently imports OBJ triangle geometry. Preview physics queries
  the bound scene without advancing that scene's physics simulation; standalone
  preview can use authored floor/planes/boxes instead.
- Authoring limits are 256 emitters, 1,000,000 particles per emitter and 4,000,000
  particles across one effect. Numeric/enum/range checks run before asset-driven
  pool allocation. Authored event dispatch caps four generations, 500 events,
  2,000 spawns per dispatch and 200 spawns per event.
- Referenced definitions are resolved on load/reopen or explicit Reload Shared Effect. Saving an effect does not
  imply a global live hot-reload service for all running instances. Applying a
  working copy is explicit and produces an inline entity definition.

The original [completion plan](../deferred/VFX_PHASE_39_COMPLETION_PLAN.md) and
[implementation guide](../deferred/VFX_PHASE_39_GEMINI_IMPLEMENTATION_GUIDE.md) remain the
scope/design references. Use this report and actual source names for current
implementation/verification status.


## Follow-up: Create VFX crash and allocation recovery (2026-11-03)

A user-provided crash log after Content Browser Create VFX records an access
violation in NRI D3D12 `CommandBufferD3D12::SetVertexBuffers`, reading the native
resource in `BufferD3D12::GetDeviceAddress`. The new `.axleasset` had already been
written. This identifies the failing draw path; it does not establish why the
original native allocation failed, since that driver/allocation diagnostic was
not captured. Ordinary continuous emission also passed before this correction.

A deterministic fault-injection regression exposed a matching production defect:
NRI's committed creation can return a wrapper before memory allocation fails.
`RHIBuffer::Init` returned false but retained the unbound wrapper; `IsValid` then
returned true. The particle renderer destroyed its old instance buffer before
growth and subsequently used size alone to decide whether to retry. A following
frame could bind the retained wrapper with no native resource, matching the
reported null-resource stack. The new test failed before the correction with
`Failed vertex allocation does not publish an unbound NRI buffer`.

Corrections:

- Release partially created committed buffers and reset their descriptor on
  shutdown. A failed allocation exposes neither a usable handle nor stale size.
- Grow particle capacity geometrically, allocate a replacement transactionally,
  retain the previous buffer on failure, retry next frame and skip draws whose
  instance/constant upload failed. Other emitters continue rendering.
- Both Create VFX menu routes report creation failures; a failed creation cannot
  select an empty asset or display a successful-create notification.
- Use nonthrowing filesystem checks/directory creation in these VFX paths. The
  new invalid-directory CPU fixture initially terminated under the inherited
  `_HAS_EXCEPTIONS=0` setting; error_code overloads now report the error safely.

Verification:

```powershell
cmake --build build --config Release --target AxleVFXUnitTests AxleVFXGPUTests AxleEditor AxleTests AxleMaterialUnitTests --parallel 12
ctest --test-dir build -C Release --output-on-failure
```

Release build passed; the pre-existing LNK4098 linkage warning remains. All four
CTest entries passed: MaterialUnitTests, VFXUnitTests, VFXGPU_D3D12 and
VFXGPU_Vulkan, using the physical RTX 3070 for both GPU entries. Added CPU checks
create/load/instantiate a real temporary VFX asset, preserve an existing asset
on repeated Create, and reject invalid destination directories. Added GPU checks
cover unbound partial-creation cleanup, successful retry, one failed growth draw
with another emitter still visible, next-frame pixel recovery and 180 continuously
simulated/rendered frames. Allocation failure is injected through a scoped test
interface override; ordinary rendering/readback still uses the real device.

Final build/test logs: `build/vfx-create-final-regression-build.log` and
`build/vfx-create-final-regression-tests.log`; the pre-fix failure is recorded in
`build/vfx-create-failure-before.log`. Step 102 also passed with
`AXLE_VFX_TEST_BACKEND=D3D12` and captures outside project assets; its log is
`build/vfx-create-step102.log`. These are local generated evidence.

The actual right-click menu was **NOT RUN** interactively: the Computer Use native
pipe was unavailable. The user's created asset and crash log were preserved.
This follow-up does not close Phase 39's remaining interactive/OpenGL acceptance.


## Follow-up: device removal and thumbnail retirement (2026-11-03)

The later-supplied 09:24 log reports `D3D12MA::CreateResource3` returning
`0x887A0005`, which is `DXGI_ERROR_DEVICE_REMOVED`, not an out-of-memory code
([Microsoft DXGI reference](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error)).
Consequently the buffer-allocation error is downstream of a lost DX12 device;
the previous correction prevents the subsequent invalid-buffer CPU access but
cannot recover a removed device or establish what originally removed it.

Source inspection exposed a separate concrete lifetime defect in the Create path:
Content Browser builds thumbnail images into the current ImGui draw lists, then
Create -> RefreshThumbnails -> Clear immediately destroys their textures and
recycles their descriptors before those draw lists are submitted. Individual
thumbnail invalidation had the same problem. A new test failed on the original
code because a new green thumbnail received the descriptor still referenced by
the queued red image. This invalid lifetime is a plausible GPU-fault source in
the reported workflow; the historical device-removal event was not deliberately
reproduced because doing so could reset the user's GPU/other applications.

The corrected cache removes entries immediately but transfers texture ownership
and descriptor release into ImGuiLayer's retirement queue. End drains it only
after UI submission and GPU completion; Shutdown drains it before destroying the
backend. Synchronization is required only on frames with pending retirements.
OpenGL deletion is similarly ordered after UI draws. External D3D12 texture
registration now rejects heap exhaustion instead of copying beyond 1024 slots.

The physical D3D12 test initializes the actual ImGui backend without a native
window, queues a red image, clears the production cache and registers green.
It verifies distinct handles, submits real ImGui draw data into an offscreen
RHI target and reads back red pixels. The next frame invalidates green and
verifies its queued pixels stay green. Heap exhaustion is checked separately.
This tests the production resource/draw path, not just a callback or draw count.
Vulkan still runs the particle renderer regressions; this new native ImGui pixel
fixture is D3D12-only. Actual context-menu mouse input and OpenGL remain NOT RUN.

Verification commands:

```powershell
cmake --build build --config Release --target AxleVFXGPUTests AxleVFXUnitTests AxleTests --parallel 12
ctest --test-dir build -C Release --output-on-failure
```

Logs: `build/thumbnail-retirement-before.log` captures the pre-fix failure;
`build/thumbnail-retirement-final-build.log` and
`build/thumbnail-retirement-final-tests.log` record final verification. The test
build also rebuilds the Editor/Runtime dependencies. No automatic DX12 device
recreation or general device-loss recovery is claimed by this change.

Result: Release build and all four CTest entries PASS on the physical RTX 3070
(Material CPU, VFX CPU, particle D3D12 and Vulkan). Step 102 also PASS with
`AXLE_VFX_TEST_BACKEND=D3D12`; see `build/thumbnail-retirement-step102.log`.
The existing LNK4098 link warning remains. The thumbnail ImGui readback and heap
bounds checks execute as part of the D3D12 GPU fixture.

## Neutral modular authoring delivery (2026-11-03)

The user selected **empty effect, manually add the first emitter**. That policy
now applies to panel New, Content Browser Create, saved effect definitions and
inline/referenced scene components. Explicitly empty components do not synthesize
a primary stack when initialized or copied. Default legacy component construction
continues to support existing callers. Add uses neutral settings and Duplicate
is an explicit operation. Fire/Smoke/Sparks/Dust/Exhaust authoring buttons and
effect-specific gradient shortcuts have been removed; gameplay factories and
acceptance reference assets remain available for explicit use.

| Change | Production behavior and coverage |
| --- | --- |
| Module state | Independent Emission, Shape, Size, Speed, Color, Alpha, Rotation, Forces, Texture Sheet and Sub Emitters flags reach the actual simulation, shared codec and document history. Noise/Collision reuse their existing enable flags. Missing flags in old assets preserve prior behavior. Disabled values are retained. |
| Initial color | Start Color is an independent white-default multiplier. Disabled Color over Lifetime uses Start Color alone; enabled color multiplies by the gradient. Both normal and inherited spawning, updates and file formats consume the property. |
| Generic module workflow | Search/filter, independent foldouts/toggles, type-matched reset/copy/paste, complete initial rotation/forces/burst/collision/sheet controls. Renderer binding resets/pastes consistently. Reset/paste restart preview; graph-invalid paste is rejected. |
| Document commands | Structural commands finish a property transaction first. Display/runtime names stay consistent without replacing live systems on each rename keystroke. Stable row IDs and row-wide context actions support duplicate names. |
| Event authoring | Add Link requires an existing authored child. Target selection/paste reject missing/self/cyclic references before publication; the former hardcoded Sparks fallback is gone. |
| Workspace | Resizable wide columns, compact tabs, wrapping transport, editable manual Emit count, configurable background/grid/live bounds, transformed billboard framing and two-axis camera pan. Preview settings remain transient. |
| Open/save/close | Project effect search, editable project-derived destination, reachable root-level dialogs even with Effect hidden, failed-save diagnostics and guarded dirty close. Tests render wide/narrow ImGui frames with all module bodies open and require zero ImGui errors. |
| Scene transfer | Public/tested creation and Apply paths validate complete definitions and honor chosen placement and authored autoplay. Clean saved effects create references; edited/unsaved effects and explicit Apply use inline data. Preview/scene systems remain independent. |

The eighth CPU suite, `Test_VFXAuthoringUnit.cpp`, covers these paths alongside
existing persistence/playback/graph/render-planning suites. It checks dormant
scene instances with Play on Awake disabled and their subsequent explicit Play.
Temporary test files are isolated from the project's authored assets.

The D3D12 GPU fixture now constructs the **actual VFXEditorPanel**, initializes
its production viewport/private scene, renders full workspace ImGui draw data
into an RHI target and reads back its viewport. Empty -> neutral red emitter ->
empty changes visible red-pixel coverage as expected. Region coverage is used
instead of a single center pixel because the reference grid crosses the emitter's
origin. The fixture writes `build/vfx-captures/d3d12/vfx-editor-workspace.ppm`;
its full workspace capture was visually inspected. Vulkan continues to run the
physical particle renderer/asset/scene acceptance fixture. Native VFX window
mouse/keyboard interaction and a separate OpenGL session remain **NOT RUN**.

Verification commands:

```powershell
cmake --build build --config Release --target AxleVFXUnitTests AxleVFXGPUTests AxleEditor AxleTests --parallel 12
ctest --test-dir build -C Release --output-on-failure
$env:AXLE_VFX_TEST_BACKEND = 'D3D12'
& build/Editor/Release/AxleTests.exe --step102
```

Evidence logs: `build/vfx-authoring-verified-build.log`,
`build/vfx-authoring-verified-tests.log` and
`build/vfx-authoring-verified-step102.log`. Release Editor/Runtime/test builds,
all four CTest entries and Step 102 pass on the physical RTX 3070. Existing
LNK4098 and APIENTRY redefinition warnings are not introduced by this delivery.
A transient Runtime post-build asset-directory copy failure was resolved by
retrying the directory copy and rebuilding; the final full build passes.

The [authoring guide](../deferred/VFX_EDITOR_AUTHORING_GUIDE.md) records supported operations
and the remaining capability backlog. Trails/ribbons, mesh-particle output,
rate-over-distance/prewarm/richer axis controls and GPU graph simulation are not
claimed by these changes. Phase 39 remains open for J4 interactive/OpenGL checks;
this is not a claim of complete Unity feature parity.
