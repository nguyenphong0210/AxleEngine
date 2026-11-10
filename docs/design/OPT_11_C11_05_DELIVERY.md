# OPT-11 C11-05 Delivery Record

This is an incremental execution record. The complete C11-05 assignment is in
progress; a completed slice is not acceptance of the remaining native/alias gates.

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

## Remaining slices

C11-05.5 alias barriers/backend proof, C11-05.6 metrics and consolidated review
remain open at this checkpoint.
