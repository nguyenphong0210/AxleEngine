# OPT-11 C11-06: Lifetime Stress Execution Plan

Released by the user for Codex implementation after C11-05. Baseline `7b9db0c`.
This package covers stress fixtures and necessary corrections, not C11-07 rollout,
full hybrid, vendor upgrades or scene migration to the transient allocator.

## Architecture and invariants

Immutable snapshots retain CPU/module payloads through queue cancellation or
consumer completion. Container mutexes protect admission and accounting; they
must not protect execution of arbitrary payload destructors. Destruction may
query the same container. Plugin code is kept by `NativePlugin` code leases and
its owner-thread retirement pump. Logical Stop revokes admission; final module
Stop/unload waits for every payload lease.

RenderWorker owns native rendering on its dedicated thread. Accepted requests
have exactly one terminal result, with bounded queued/results/in-flight storage.
Stop rejects further admission, cancels queued requests and joins active work.
Exceptions must become a failure result/state, never escape a thread entry point.
CPU task completion is distinct from GPU completion. Native resource dependencies
and submission pins must outlive wrappers and only retire after checked completion
or confirmed device-loss policy. No per-frame production WaitIdle is introduced.

## Sequential slices and exit checks

1. **C11-06.1: Snapshot/module cancellation.** Deterministic replacement,
   PopLatest, drain and destructor-release cases; reentrant cleanup regression;
   multiple producers/consumer plus Stop race with exact lease accounting.
   Correct container cleanup boundaries if required. CPU tests must not require
   a GPU and must have a process timeout for deadlocks.
2. **C11-06.2: Worker/plugin lifecycle.** Repeated physical backend start, pressure,
   producer/Stop races, result reconciliation, restart rejection until result
   drain, exception handling and first-error preservation. Exercise real module
   retention with queued/active CPU work and native resource cleanup; distinguish
   static-module tests from actual DLL unload evidence.
3. **C11-06.3: Native resource stress.** Fixed-seed mixed transient generations,
   size changes, import reload, abandoned recording, allocation/barrier failures,
   delayed completion, submit quarantine, device-loss simulation and epoch restart.
   Verify actual buffer/pixel data, stale-handle rejection and bounded teardown.
   Include real swapchain resize/minimize/recovery stress using existing presenter.
4. **C11-06.4: Consolidated verification and handoff.** Debug/Release, strict
   physical D3D12/Vulkan with native/NRI validation, relevant existing regression
   suites, architecture and exchange validation. One separate-context final review
   and focused correction recheck if needed. Record exact commands/logs, counts,
   hardware/driver, observed limits and unavailable checks.

Each slice produces a coherent local commit. Gemini receives the consolidated
package handoff at the end; no intermediate response is needed.

## Test design

Use deterministic gates (condition variables/latches) to force lifecycle overlap;
do not rely on sleep timing to prove a race was exercised. Every accepted item
must reconcile to a terminal result or explicit cancellation. Use unique payload
IDs, atomic construction/destruction counts and weak references to establish
exact-once release. Reentrant destructors query production APIs to detect lock
inversion directly. Stress loops have finite iteration counts, deadlines and CTest
timeouts; native waits use the existing bounded RHI policy.

Native fault injection occurs at real operation boundaries. Simulated loss is
injected only before new native work after previously submitted work is drained;
it is not evidence of a physical GPU reset. Validation errors, leaked generations,
deadlock, lost accepted requests and callbacks after unload block acceptance.
Unsupported native recording/hybrid remains unsupported, never a fake passing
parallel-recording test. Multi-vendor/driver and sanitizer coverage are recorded
separately from the available Windows GPU evidence.
