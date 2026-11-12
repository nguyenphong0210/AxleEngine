# OPT-11 C11-05.1: Pure Transient Graph Planner

**Date:** 2026-11-10.
**Implementation baseline:** `ad58438`.
**Status:** Implemented and self-verified; consolidated C11-05 independent review
and all later native/aliasing gates remain open.

## Scope

The user selected C11-05 before full hybrid. This slice adds a CPU-only companion
planner, `RenderTransientGraph`, beside the existing imported-texture RenderGraph.
The existing graph API and native callers are unchanged. No NRI source, native
allocation, capability flag, worker recording or secondary/bundle path changes.

## Delivered behavior

- Imported, transient texture and transient buffer descriptions contain values
  only. Plans own snapshots of descriptions and pass declarations.
- Explicit pass dependencies may reference forward pass IDs. RAW/WAR/WAW hazards
  preserve declaration-order resource versions. Stable Kahn ordering selects the
  smallest declaration index among ready passes.
- ReadWrite uses do not create self-dependencies. Cycles, bad IDs, duplicate
  names/uses, unknown enums and unsupported queues reject the candidate.
- Whole-resource use validation checks resource kind, descriptor usage,
  attachment load/store and defined contents. Transients begin undefined; a
  Write/Preserve declaration promises complete initialization.
- Lifetimes use closed intervals in the compiled execution order. Imported
  resources never enter lifetime packing or allocation statistics.
- Compatibility keys retain kind, shape, format, mip/layer/sample count, usage,
  memory location/type, stride/raw mode, alignment, granularity and dedicated
  requirements. The initial transient mode is device-local on one graphics queue.
- Each transient receives a distinct abstract block, even for disjoint lifetimes
  or `AllowAlias` requests. Ignored alias requests are reported explicitly.
- Checked payload, alignment and aggregate arithmetic reject overflow.
- Compilation uses a temporary plan; validation failures preserve the destination.
  Successful compilation clears a separately supplied error string.

## Memory accounting boundary

`MemoryRequirements` is supplied CPU metadata, not proof of a backend capability.
Texture payload size is a descriptor sanity lower bound, never a native allocation
query. `RequestedBytes`, `PlannedAllocationBytes` and `PlannedPeakBytes` are plan
values, not measured committed VRAM. Planned peak equals all simultaneously owned
unaliased candidate blocks, not merely the sum of resources live at one pass.

Before future native realization, the RHI owner must query actual requirements,
validate hardware limits/format support and bind them to the exact description and
device epoch. Native resource-generation/retirement and alias barrier proof are
not delivered by this planner.

## Verification executed

All commands below ran locally against this slice; exit codes were zero.

| Gate | Command | Result/log |
| --- | --- | --- |
| Debug baseline build | `cmake --build build --config Debug --target AxleRenderUnitTests AxleRHIStateTests --parallel 6` | `build/c11_05_1_baseline_debug_build.log` |
| Debug baseline tests | `ctest --test-dir build -C Debug -R '^(RenderUnitTests\|RHIStatePolicy)$' --output-on-failure` | 2/2; `build/c11_05_1_baseline_debug_tests.log` |
| Release baseline build | Same targets with `--config Release` | `build/c11_05_1_baseline_release_build.log` |
| Release baseline tests | Same baseline CTest selection with `-C Release` | 2/2; `build/c11_05_1_baseline_release_tests.log` |
| Debug changed build | Same targets with `--config Debug` | `build/c11_05_1_debug_build.log` |
| Debug changed tests | `ctest --test-dir build -C Debug -R '^(RenderUnitTests\|RHIStatePolicy\|RenderHarnessFailure)$' --output-on-failure` | 3/3; `build/c11_05_1_debug_tests.log` |
| Release changed build | Same targets with `--config Release` | `build/c11_05_1_release_build.log` |
| Release changed tests | Same changed CTest selection with `-C Release` | 3/3; `build/c11_05_1_release_tests.log` |
| Architecture | `python scripts/check_architecture.py` | PASS; 529 files, 25 CMake files |
| Exchange | `python scripts/agent_exchange.py validate` | PASS |
| Whitespace | `git diff --check` | PASS |

The focused tests include mixed textures/buffers/imports, RAW/WAR/WAW,
ReadWrite, forward dependencies, overlapping/touching/disjoint intervals,
undefined/LOAD/discarded reads, alias exclusion, malformed descriptors,
alignment/granularity, arithmetic overflow, unchanged output after failure,
snapshot ownership and repeatability across 120 fixed-seed generated graphs.

The Debug render executable links with an LNK4098 runtime-library warning; build
and tests completed successfully. This slice does not modify runtime linkage.
Build logs remain generated local artifacts under `build/`.

## Open gates and next slice

C11-05.2 will realize committed transient resources on the RHI owner with
transactional candidate creation, generation leases and GPU completion retirement.
Do not reuse abstract block IDs as native allocations without those checks.

Native transient D3D12/Vulkan creation, placed textures/buffers, alias barriers,
readback parity, measured VRAM reduction, device-loss/submit rollback and final
independent C11-05 review are NOT RUN/not implemented in this CPU slice.
Full hybrid and an NRI fork remain deferred; no push is part of this delivery.
