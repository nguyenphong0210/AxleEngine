# OPT-11 C11-05 Delivery Record

Implementation covers C11-05.1 through C11-05.6. Final regression and independent
review evidence are recorded below; later OPT-11 packages are outside this release.

## C11-05.1

CPU planner delivered in `4783c25`; see [the slice report](OPT_11_C11_05_1_DELIVERY.md).

## C11-05.2 - Committed native realization

Implemented `RHITransientResources` and `RenderTransientFrame`. The owner creates
candidate textures/buffers, queries actual memory requirements, recompiles the
value plan, records declared uses and publishes the generation only after submit
success. Imports retain their exact generation and are checked against actual
native descriptions. Duplicate imported aliases reject the candidate.

The last submitted generation survives candidate failure/abandon. GPU submission
pins independently retain resources after graph wrapper destruction. Ambiguous
submit poisons the pending generation and cannot be retried or abandoned into
normal reuse. No per-frame wait was added to preparation/recording/submission;
the fixture uses checked completion via the existing conservative RHI wait policy.

Native fixtures exercise a buffer upload, transient color clear, export into an
imported output, image/buffer readback, replacement, stale device epoch, wrong
owner, false import metadata, wrong pass order, callback exception, failure at
each creation stage and failed native submit. Callback code is trusted owner
code and must record only its declared work without ending/submitting the borrowed
command wrapper itself.

Executed on NVIDIA GeForce RTX 3070, NRI pinned at `6998e3fb`:

- Debug and Release builds: `cmake --build build --config CONFIG --target
  AxleTransientGraphTests AxleRenderUnitTests --parallel 6`, exit 0.
- Both configurations: `AxleTransientGraphTests.exe dx12`, `vulkan`,
  `dx12_fail_submit`, `vulkan_fail_submit`: 8/8 process outcomes PASS, exit 0.
- Both configurations: CTest selection
  `^(RenderUnitTests|TransientGraphPolicy|RHIStatePolicy)$`: 3/3 PASS, exit 0.
- `python scripts/check_architecture.py`: PASS (533 files, 25 CMake files).
- `git diff --check`: PASS.

Logs: `build/c11_05_2_debug_build.log`, `build/c11_05_2_release_build.log`,
`build/c11_05_2_{dx12,vulkan,dx12_fail_submit,vulkan_fail_submit}.log` and
`build/c11_05_2_release_{dx12,vulkan,dx12_fail_submit,vulkan_fail_submit}.log`.
NRI validation is enabled; the fixture requests native API validation when
available and checks zero validation errors through teardown.

Native backing requirements for this tiny mixed fixture total 131072 bytes on
D3D12 and 768 bytes on Vulkan. These are queried resource backing sizes, not
whole-device budget measurements or a claim that either backend is generally
more memory-efficient.

## C11-05.3 - Placed resources and memory ownership

Added query-only texture/buffer requirement probes. They validate descriptions,
create unbound native objects and query requirements without allocating backing
memory. `RHITexture::InitPlaced` now supports the transactional create/bind/view
path already used for buffers. Registered memory generations become transitive
dependencies of every bound texture/buffer generation.

`RHITransientResources::InitPlaced` allocates explicit blocks, checks queried
types/alignment/dedicated restrictions and binds resources through NRI. Explicit
shared blocks additionally require identical descriptions/requirements and the
native resource-aliasing feature. Callers remain responsible for lifetime/barrier
proof; graph execution at this slice uses only distinct blocks. No capability is
enabled solely from the presence of function pointers.

The graph executor has an explicit placed-unaliased mode. The existing committed
path remains the default. Native tests cover query probes without backing
allocation, misaligned texture placement, depth view creation/failure, partial
memory allocation rollback, exact-once memory retirement, and retention after the
original memory/texture wrapper is released.

Debug native graph modes `dx12`, `vulkan`, `dx12_placed`, `vulkan_placed`,
`dx12_placed_fail_submit`, `vulkan_placed_fail_submit` pass. The existing native
`AxleRHIResourceTests.exe dx12` and `vulkan` regression suites also pass.
Debug CTest selection `^(RenderUnitTests|TransientGraphPolicy|RHIResourcePolicy|RHIStatePolicy)$`
passes 4/4. Release rebuilt the graph, render-unit and resource targets; placed
normal/failure modes pass on both backends (4/4), and the same CTest selection
passes 4/4. All commands exit 0.

Logs: `build/c11_05_3_debug_build.log`, `build/c11_05_3_debug_fixture_build.log`,
`build/c11_05_3_*.log`, `build/c11_05_3_resources_{dx12,vulkan}.log`.

## C11-05.4 - CPU interval packing

The pure compiler now accepts an explicit `AllowAliasing` planning option.
Default compilation preserves the earlier unaliased block IDs. Opt-in planning
sorts closed lifetimes by `(first, last, resource ID)` and uses deterministic
first-fit coloring. Only opted-in, non-dedicated resources with equal conservative
compatibility keys share a block, and only when the previous last pass strictly
precedes the next first pass. Events retain the immediate previous occupant.

Plans distinguish unaliased bytes, packed backing bytes, block count and logical
alias events. No native alias capability is enabled by this compiler option.
CPU tests cover equal-size incompatible formats/types/alignment, dedicated and
non-opted-in resources, touching intervals, three-occupant chains and default
fallback. All 120 generated graphs are additionally checked against an independent
maximum-live-interval oracle and pairwise overlap checks.

Debug and Release RenderUnitTests pass after rebuilding (exit 0). Logs:
`build/c11_05_4_{debug,release}_{build,tests}.log`.

## C11-05.5 - Validated alias activation

The RHI implements a conservative alias contract through NRI global barriers.
The previous whole resource must already be in copy-source read state, the next
must be unused, both must share a factory-owned block, and activation must occur
outside rendering. A global all-command dependency waits for previous reads
before the next clear/copy. No layout transition or write flush is added to the
previous occupant at this alias boundary. Normal next-resource initialization
then occurs through the transactional state journal.

This path requires Vulkan or D3D12 enhanced barriers. Pinned NRI's legacy D3D12
global barrier implementation is UAV-only, so that path stays unsupported and
uses committed fallback. No raw backend escape hatch or NRI modification is used.

An explicit `RHITransientResources::ProbeAliasing()` performs real unaliased and
aliased texture/buffer recording, checks every pixel/word, checks validation and
retirement, and proves lower requested native backing size before setting the
device capability. This is an explicit startup/diagnostic operation, not a
per-frame GPU wait. Flags reset with the device epoch.

`RenderTransientFrame::AliasingIfValidated` falls back to committed allocation
before realization when unverified. With positive proof it re-queries requirements,
filters supported resource uses, recompiles packing and records each alias event
before the next occupant. Initial native support is identical RGBA8_UNORM,
single-mip/layer/sample 2D color targets and device-local buffers, with complete
first clear/copy and final copy-read. Dedicated, imported and other resource/use
classes stay unaliased. Aliased views cannot escape their declared pass through
the graph getter; persistent outputs must be imported generations.

Debug and Release `dx12_alias`, `vulkan_alias`, `dx12_alias_fail_submit` and
`vulkan_alias_fail_submit` all pass with both NRI and native API validation on.
Tests include failed probe, pre-use activation rejection, unsupported texture
class, failed alias barrier, failed submit, recovery and exact image/buffer parity.

The startup probe requests 262144 -> 131072 backing bytes on D3D12 and
1536 -> 768 on Vulkan for its identical workload. Graph planning uses additional
alignment rounding; its conservative byte estimate must be distinguished from
the native allocation request totals. Whole-device physical VRAM/budget and
performance improvements are not inferred from these fixture numbers.

Logs: `build/c11_05_5_{debug,release}_build.log`,
`build/c11_05_5_{dx12,vulkan}_alias*.log`, and
`build/c11_05_5_release_{dx12,vulkan}_alias*.log`.

Technical basis:

- [D3D12 enhanced resource aliasing](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html#resource-aliasing):
  synchronization, write visibility and subsequent resource initialization.
- [Vulkan memory aliasing](https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-memory-aliasing):
  alias binding and access restrictions.
- Pinned NRI `Source/{D3D12,VK}/CommandBuffer*.hpp` translates global barriers;
  `Source/VK/DeviceVK.hpp` enables image alias creation flags.

## C11-05.6 - Diagnostics and integration boundary

`RenderTransientFrame::GetDiagnostics()` exposes an owner-thread value snapshot:
requested mode, actual allocation mode, epoch/generation, lifecycle, completion
token, first failure, resource/block/event counts and fallback reason. Native
requested bytes sum each resource's queried requirement without sharing;
allocated backing bytes sum actual factory allocation requests after sharing;
estimated plan bytes retain conservative alignment rounding. Pending diagnostics
also expose the last published generation's retained backing bytes. These figures
exclude imported resources, readback fixtures, driver overhead and other device
allocations; they are not a physical VRAM counter or a complete in-flight peak.
Existing RHI completion/retirement remains authoritative for in-flight generations.

Failed/poisoned candidates preserve their first diagnostic. Submit failure keeps
the previous published generation, quarantines pending resources, and rejects
retry/abandon. Snapshots become invalid after device epoch retirement. The test
checks metrics while prepared, recording, ready, submitted and poisoned, including
previous-generation retention and exact backing-byte comparisons.

The production API is implemented in Renderer/RHI and exercised through real
native fixtures. Existing `RenderGraph`/`RHISceneRenderer` callers and persistent
`RHIViewTargets` keep their imported-resource behavior. No Editor/Runtime scene
has been migrated wholesale to transient ownership, and no scene-wide VRAM/FPS
improvement is claimed. Callers opt in by declaring graph-owned scratch resources;
history, UI and persistent outputs remain imported exact generations.

Owner integration sequence:

1. Declare resources/passes and compile the pure `RenderTransientGraph` plan.
2. At an explicit startup/diagnostic point, optionally run
   `RHITransientResources::ProbeAliasing()`. It performs synchronous native proof;
   do not call it per frame. Unsupported or unverified devices retain fallback.
3. `Prepare(plan, imports, AllocationMode::AliasingIfValidated)` creates an
   unpublished candidate and rebuilds the plan from its declarations/native
   requirements. Caller-supplied precomputed allocation fields are not trusted.
4. Follow `GetPlan()->PassOrder`, calling `Record(id, callback)` once per pass.
   Callbacks record only declared work, including a complete initial clear/copy;
   they must not submit/reset/end commands, reenter the frame, or export aliased
   views. `Get(id)` permits packed transient access only in its declared pass.
5. Call `Submit()` after all passes; only success publishes a new generation.
   Pre-submit errors may be abandoned; ambiguous submit requires device-policy
   recovery. Export persistent results into imported outputs before lifetime end.
6. Inspect `GetDiagnostics()` and existing RHI retirement/completion diagnostics.

Native alias support remains deliberately restricted: identical non-dedicated
RGBA8_UNORM 2D color targets with one mip/layer/sample and device-local buffers,
complete initial clear/copy, final whole-resource copy read, single graphics
queue. Other classes remain unaliased. D3D12 requires enhanced barriers; Vulkan
requires native alias primitives and positive proof. OpenGL transient allocation
through this native API is unsupported; existing OpenGL/imported rendering stays
on its established path. Hybrid worker recording, NRI upgrades/forks, cross-queue
aliasing, C11-06 stress and C11-07 rollout are not included.

## Final verification

Hardware: NVIDIA GeForce RTX 3070, NVIDIA driver 610.88, Windows; pinned NRI
`6998e3fbbaed1b132f386a9d49743a2481003487`. D3D12 uses ID3D12Device14/enhanced
barriers; Vulkan reports API v1.4. Both graph fixtures report NRI validation ON
and native API validation ON and check zero validation errors through shutdown.
D3D12 reports the existing `CheckFeatureSupport(options22)` warning; this is not
silently classified as a validation error or hidden.

All commands below completed with exit 0 against final production source. The
last Recording/Ready fixture assertions were rebuilt and rerun in all four alias
normal/failure modes in both configurations after the wider matrix completed.

| Gate | Debug | Release |
| --- | --- | --- |
| Build `AxleTransientGraphTests AxleRenderUnitTests AxleRHIResourceTests AxleRHIStateTests AxleRHIRetirementTests AxleRHIFrameTests` | PASS | PASS |
| Native graph: committed, placed, alias, each normal and submit failure, D3D12 + Vulkan | 12/12 PASS | 12/12 PASS |
| Existing native resource, state, retirement and frame suites, D3D12 + Vulkan | 8/8 PASS | 8/8 PASS |
| CTest render units/harness failure and transient/resource/state/retirement policy | 6/6 PASS | 6/6 PASS |
| Native/NRI graph validation and exact readback | PASS | PASS |

Build command: `cmake --build build --config CONFIG --target` followed by the
six targets above and `--parallel 6`. Each native executable was invoked from
`build/Tests/CONFIG/`. Graph modes are `dx12`, `vulkan`, `dx12_placed`,
`vulkan_placed`, `dx12_alias`, `vulkan_alias`, and each corresponding
`_fail_submit` mode. Existing suites are `AxleRHIResourceTests`, `AxleRHIStateTests`,
`AxleRHIRetirementTests`, `AxleRHIFrameTests`, each with `dx12` and `vulkan`.
CTest command: `ctest --test-dir build -C CONFIG -R
'^(RenderUnitTests|RenderHarnessFailure|TransientGraphPolicy|RHIResourcePolicy|RHIStatePolicy|RHIRetirementPolicy)$'
--output-on-failure`.

Final graph and startup-probe allocation-request totals agree in both builds:

| Backend | Unaliased backing | Aliased backing | Reduction within fixture |
| --- | ---: | ---: | ---: |
| D3D12 enhanced barriers | 262144 bytes | 131072 bytes | 50% |
| Vulkan | 1536 bytes | 768 bytes | 50% |

`python scripts/check_architecture.py` passes (534 source files, 25 CMake files).
`git diff --check` passes. Final logs:
`build/c11_05_final_{debug,release}_build.log`,
`build/c11_05_6_{debug,release}_fixture_build.log`,
`build/c11_05_final_{Debug,Release}_graph_MODE.log`,
`build/c11_05_final_{Debug,Release}_AxleRHI{Resource,State,Retirement,Frame}Tests_BACKEND.log`,
`build/c11_05_final_{Debug,Release}_ctest.log`.

### Consolidated independent review

The separate-context Codex reviewer `c11_05_final_review` inspected the full diff
from `488123d`, including pending .6 metrics, native ownership/barrier translation,
failure/retirement paths, actual callers and final lifecycle assertions/docs.
It reported no P1/P2 findings and no findings in its focused final recheck.
It independently ran Debug `RenderUnitTests` and `TransientGraphPolicy` (2/2
PASS) and `git diff --check` (PASS). It did not rebuild or independently execute
the native matrix. The native evidence above is the implementer's executed
verification, not a Gemini review or an independent native rerun.

Package status: implemented and verified within the conservative supported class;
required separate-context source review complete. No required C11-05 backend row
is NOT RUN on this machine. Multi-vendor/other-driver coverage, scene benchmarks,
windowed product rollout, broader alias classes and full hybrid remain unmeasured
or outside this released package. C11-06/C11-07 are not accepted by this record.

### Slice commits

- C11-05.1: `4783c25` - pure transient planner (`488123d` delivery record).
- C11-05.2: `73c9340` - committed native generation and transactional execution.
- C11-05.3: `8393f64` - placement/query probes and borrowed-memory ownership.
- C11-05.4: `cf453db` - deterministic interval packing and alias events.
- C11-05.5: `b65af0a` - conservative native alias barriers and parity capability gate.
- C11-05.6: this diagnostics, fixture validation and handoff commit; its hash is
  recorded by the subsequent exchange delivery without a self-referential hash.
