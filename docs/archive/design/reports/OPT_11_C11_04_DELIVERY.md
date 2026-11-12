# OPT-11 C11-04 delivery

## Result

C11-04 implements the current safe path: independent primary command buffers
and owner-controlled batch submission. The engine does not advertise worker
thread recording yet because the current `RHICommandBuffer` resource tracker is
owner-thread bound and the pinned NRI headers provide no portable secondary
execute/merge operation.

## Implementation

- `RHIParallelRecording.hpp/.cpp` returns an explicit probe result and diagnostic.
- `RHIPrimaryCommandBatch` allocates independent primary command buffers and
  submits them in one deterministic `SubmitBatch` call. Allocation, state
  tracking and submission remain on the render owner.
- A failed or ambiguous batch submission poisons the batch and cannot be
  retried; `Reset()` is available only for a completed successful batch.
- `RHIRecordingPlan::BuildLevels()` validates dependencies, rejects cycles and
  computes deterministic topological levels.
- `BuildCpuPreparationBatches()` keeps graph order, groups thread-safe work and
  isolates unsafe passes for serial owner preparation. It does not call native
  RHI APIs from worker jobs.
- `RHIDeviceCapabilities::SupportsParallelCommandRecording` remains `false`
  until a future worker recording lease and backend validation prove safe
  multi-threaded recording. Secondary merge/hybrid recording is deferred.

## Verification

`AxleRenderUnitTests.exe` covers the primary-batch selection, deterministic DAG
levels, unsafe-pass isolation and cycle rejection. `AxleRHIStateTests.exe dx12`
creates two native primary command buffers and submits them as one batch.
Debug and Release builds pass; `python scripts/check_architecture.py` passes
with no violations.
