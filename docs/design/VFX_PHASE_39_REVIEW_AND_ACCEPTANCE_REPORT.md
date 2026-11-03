# Phase 39 follow-up review and acceptance

Date: 2026-11-02. Reviewed Gemini baseline: `1de558c` (27 commits after the handoff).
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

The original [completion plan](VFX_PHASE_39_COMPLETION_PLAN.md) and
[implementation guide](VFX_PHASE_39_GEMINI_IMPLEMENTATION_GUIDE.md) remain the
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
