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

## Remaining slices

C11-05.3 placement, C11-05.4 interval packing, C11-05.5 alias barriers/backend
proof, C11-05.6 metrics and consolidated review remain open at this checkpoint.
