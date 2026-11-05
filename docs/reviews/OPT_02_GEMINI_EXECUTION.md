# OPT-02 Execution Report: Repair Threading, Scripts and Physics Jobs

Evolving execution log and verification record for the sequential implementation of **OPT-02 (EJ-01 through EJ-07)**, authored by Gemini under the authorized autonomous architecture plan ([GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md](../agents/handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md)).

---

## Package EJ-01: Thread Identity and Lifecycle Admission

- **Status:** Implemented and self-verified; local delivery committed. Independent review remains pending at phase checkpoint.
- **Base Baseline:** `d2ab4e7`
- **Scope & Contract:** [OPT_02_EJ01_GEMINI.md](../agents/handoffs/OPT_02_EJ01_GEMINI.md)

### Implementation Summary
1. **OS Owner Identity & `IsMainThread()`:**
   - Established `s_OwnerThreadId` upon `JobSystem::Init()` on the engine's main thread.
   - `IsMainThread()` verifies `std::this_thread::get_id() == s_OwnerThreadId`, completely eliminating the flawed fallback where `enki::NO_THREAD_NUM` mapped to 0 and caused external threads to impersonate the main thread.
   - Retains owner identity across stopped and reinitialized states.
2. **External Thread Protection & NO_THREAD_NUM Preservation:**
   - `GetCurrentThreadIndex()` returns `enki::NO_THREAD_NUM` (`0xFFFFFFFF`) directly for unregistered external callers and when stopped (never zero).
   - `IsValidSchedulerContext()` strictly requires the calling thread to be either the OS owner thread, an active task permit holder, or a registered scheduler thread.
   - Unsupported external calls to `Dispatch`, `ParallelFor`, `Wait`, `WaitForAll`, and `RunPinnedTasks` emit active diagnostics (`AXLE_CORE_ERROR`) in both Debug and Release without crashing or entering scheduler arrays.
3. **Lifecycle States & Initialization Epochs:**
   - Formalized states: `Stopped`, `Running`, `Quiescing`, `Draining`.
   - Epoch counter `s_Epoch` increments atomically on each successful `Init()`.
   - `Init()` and `Shutdown()` belong exclusively to the OS owner thread; calls from external threads are rejected with active diagnostics.
   - Separated thread count queries: `GetTotalThreadCount()` (all task threads including main thread), `GetDedicatedWorkerThreadCount()` (background workers created by scheduler), and `GetWorkerThreadCount()` (maintained for compatibility with existing tests).
4. **Admission Synchronization vs Shutdown:**
   - Synchronized submission admission against shutdown using `AdmissionMutex`.
   - New application submissions are rejected once shutdown enters `Quiescing`.
   - Preserves progress for already-admitted task graphs and declared internal children via `t_ActiveTaskDepth` execution permits.
   - `AdmissionMutex` is never held while executing task bodies, waiting, or destroying callable captures.

### Verification Evidence

1. **Focused Scenarios (`AxleJobsECSTests`):**
   - `ecsj-02-thread-identity`: Fixed and verified PASS (exit 0) in Debug and Release.
   - `ej01-lifecycle-admission`: Deterministic 7-stage invariant suite verifying:
     - Stopped state defaults (`NO_THREAD_NUM`, zero counts).
     - Main thread owner initialization (`Init(2)` -> total 2, workers 1, epoch 1).
     - External thread identity and rejection of `Init`, `Shutdown`, `Dispatch`, `ParallelFor`, `Wait`, `WaitForAll`, `RunPinnedTasks`.
     - Worker thread execution context inside admitted task (`idx < 2`, not main thread, valid context).
     - Stopped state owner preservation after `Shutdown()`.
     - Repeated initialization epochs (`Init(1)` -> total 1, workers 0, epoch 2).
     - Barrier-coordinated admission vs shutdown: task A progresses and submits child task C during draining; external new submissions rejected; safe reinitialization.
     - PASS (exit 0) in Debug and Release.

2. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - **Debug:**
     - Command: `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json`
     - Summary: **6 PASS, 3 recorded defects (ECSJ-01, ECSJ-03, ECSJ-04 preserved), 3 verified probes, 0 unexpected failures**.
   - **Release:**
     - Command: `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json`
     - Summary: **6 PASS, 3 recorded defects, 3 verified probes, 0 unexpected failures**.

3. **CTest Suite (Debug & Release):**
   - Registered 23 tests, including `JobsECS_ThreadIdentity` and `JobsECS_LifecycleAdmission`.
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **23/23 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **23/23 tests passed (100%)**.

4. **Integration Suite (`AxleTests.exe`):**
   - Release: `build/Tests/Release/AxleTests.exe` -> **All 68 steps passed (100%)**.

5. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.

6. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

### Remaining Limitations
- Single-thread Jolt bridge hang (`ECSJ-03`) is deferred to EJ-06.
- Script compiler async completion on worker thread (`ECSJ-01`) is deferred to EJ-03.
- EnTT stable storage iteration tombstone (`ECSJ-04`) is deferred to EJ-08.
- Independent acceptance remains pending; this report documents self-verification under autonomous authorization.
---

## Package EJ-02: Deferred Main-Thread Executor and Owner Tokens

- **Status:** Implemented and self-verified; local delivery committed. Independent review remains pending at phase checkpoint.
- **Base Baseline:** `c5b3d23`
- **Scope & Contract:** [GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md](../agents/handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md) and [ROADMAP.md](../ROADMAP.md)

### Implementation Summary
1. **Always-Deferred Execution Semantics:**
   - Inlined execution on the main thread during `DispatchToMainThread` or `PostToMainThread` is completely prohibited. enkiTS work-stealing during `Wait` or physics steps can execute tasks on the main thread; executing callbacks inline under those conditions creates reentrant, concurrent mutations on Scene/UI data structures.
   - Every posted completion is enqueued to `MainThreadQueue` with bounded FIFO ordering (capacity 16,384) under `MainThreadMutex`.
2. **Owner Tokens & Generation-Based Invalidation (`OperationOwner`, `OwnerToken`):**
   - Added `OperationOwner` RAII controller and copyable `OwnerToken`.
   - Invalidation or destruction of `OperationOwner` marks the shared owner state invalid and bumps the generation counter atomically.
   - When the main thread pumps completions via `PumpMainThreadCompletions()`, expired or invalidated entries are discarded cleanly (`Callback = nullptr`) without invoking the closure or dereferencing invalidated captured state.
   - Added `OwnerToken::Global()` for perpetual engine-level background notifications.
3. **Bounded Non-Recursive Batch Draining (`PumpMainThreadCompletions`):**
   - Implemented `PumpMainThreadCompletions(u32 maxItems = 1024)` in `JobSystem`.
   - Swaps/slices a bounded batch from the queue under lock and executes callbacks outside the lock. Callbacks posting further main-thread work are safely queued for subsequent pump batches without recursion, deadlock, or unbounded frame starvation.
   - Integrates with `Application::Run()` at the main loop frame boundary immediately following `JobSystem::RunPinnedTasks()`.
4. **Epoch Validation:**
   - Queued completions record the active scheduler epoch (`s_Epoch`). Completions queued prior to `Shutdown()` are discarded if pumped in a subsequent scheduler epoch.
5. **Unified Compatibility (`DispatchToMainThread`):**
   - Deprecated raw, unsafe inline execution in `DispatchToMainThread`; routed directly through `PostToMainThread` to preserve all legacy callers while guaranteeing deferred execution.

### Verification Evidence

1. **Focused Scenarios (`AxleJobsECSTests`):**
   - `ej02-deferred-executor`: Comprehensive 7-stage deterministic suite verifying:
     - Main-thread post is never inline and only runs upon explicit pump (`mainPostRan` false until pump).
     - Worker thread cross-thread dispatch and guaranteed main-thread execution via pump.
     - Nested posting runs in subsequent pump batch (non-recursive batching).
     - RAII `OperationOwner` invalidation, reset/generation bumping, and destroyed-owner safe callback dropping without crashes or invocations.
     - Scheduler epoch invalidation (completions from prior epoch discarded in new epoch).
     - Bounded FIFO ordering preservation (e.g. 4-item pump followed by 6-item remainder).
     - Unified `DispatchToMainThread` compatibility verification.
     - PASS (exit 0) in Debug and Release.

2. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - Registered `ej02-deferred-executor` as permanent PASS native scenario.
   - **Debug:** `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json`
     - Result: **7 PASS, 3 recorded defects (ECSJ-01, ECSJ-03, ECSJ-04 preserved), 3 verified probes, 0 unexpected failures**.
   - **Release:** `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json`
     - Result: **7 PASS, 3 recorded defects, 3 verified probes, 0 unexpected failures**.

3. **CTest Suite (Debug & Release):**
   - Registered `JobsECS_DeferredExecutor` in `Tests/CMakeLists.txt` (total 24 tests).
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **24/24 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **24/24 tests passed (100%)**.

4. **Integration Suite (`AxleTests.exe`):**
   - Release: `build/Tests/Release/AxleTests.exe` -> **All 68 steps passed (100%)**.

5. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.

6. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

### Remaining Limitations
- Single-thread Jolt bridge hang (`ECSJ-03`) is deferred to EJ-06.
- Script compiler async completion on worker thread (`ECSJ-01`) is deferred to EJ-03 (which will consume EJ-02 `PostToMainThread`).
- EnTT stable storage iteration tombstone (`ECSJ-04`) is deferred to EJ-08.
- Independent acceptance remains pending; this report documents self-verification under autonomous authorization.

---

## Package EJ-03: Compiler Request/Result Handoff

- **Status:** Implemented and self-verified; local delivery committed. Independent review remains pending at phase checkpoint.
- **Base Baseline:** `3145a30`
- **Scope & Contract:** [GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md](../agents/handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md) and [ROADMAP.md](../ROADMAP.md)

### Implementation Summary
1. **Immutable Request Snapshot (`CompileRequest`):**
   - Added `CompileRequest` struct snapshotting `WorkspaceDirectory`, `BuildConfig`, `Target`, `OwnerToken`, and monotonic `RequestId` at submission time.
   - Background worker thread executes against the immutable value snapshot; mutations to `ScriptCompiler` configuration while compilation is in-flight have zero effect on in-flight builds.
2. **Safe Windows Process Management & Cancellation:**
   - Synchronized process handle (`m_ProcessHandle`) and cancellation flags under dedicated `m_ProcessMutex`.
   - Guaranteed atomic check before and after process creation; child process handles are never closed while `TerminateProcess` operates on them, completely preventing handle recycling races.
3. **Main-Thread Deferred Completion & Owner Invalidation (Defect `ECSJ-01` Resolved):**
   - Replaced direct worker thread completion invocation with `JobSystem::PostToMainThread(request.Owner, ...)`.
   - When compilation completes, the callback is queued and dispatched strictly on the engine's main thread during `JobSystem::PumpMainThreadCompletions()`.
   - Integrated `OperationOwner m_CompileOwner` in `EditorLayer`. If `EditorLayer` detaches or project switches before compilation finishes, `m_CompileOwner.Invalidate()` ensures the completion is discarded without dangling pointer dereference or invalid state reload.
   - For offline test harnesses where `JobSystem` is uninitialized, completions are safely buffered in a fallback queue and pumped upon caller `WaitForCompletion()`.

### Verification Evidence

1. **Focused Scenarios (`AxleJobsECSTests`):**
   - `ecsj-01-compiler-thread`: **Defect ECSJ-01 resolved and verified PASS (exit 0)** in Debug and Release (`callbackOnMain` is 1).
   - `ej03-compiler-handoff`: Deterministic suite verifying:
     - Request immutability: mutating `ScriptCompiler` target/config after submission does not corrupt in-flight build.
     - Owner invalidation: `OperationOwner::Invalidate()` during build safely suppresses callback invocation during pump.
     - Cooperative cancellation: `Cancel()` cleanly aborts process execution without leaks or hangs.
     - PASS (exit 0) in Debug and Release.

2. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - Updated `ecsj-01-compiler-thread` to expected `PASS` and registered `ej03-compiler-handoff` as native `PASS`.
   - **Debug:** `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json`
     - Result: **8 PASS, 2 recorded defects (ECSJ-03, ECSJ-04 preserved), 3 verified probes, 0 unexpected failures**.
   - **Release:** `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json`
     - Result: **8 PASS, 2 recorded defects, 3 verified probes, 0 unexpected failures**.

3. **CTest Suite (Debug & Release):**
   - Registered `JobsECS_CompilerThread` and `JobsECS_CompilerHandoff` in `Tests/CMakeLists.txt` (total 26 tests).
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **26/26 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **26/26 tests passed (100%)**.

4. **Integration Suite (`AxleTests.exe`):**
   - Step 21 verified: `AxleTests.exe 21` -> **100% passed**.
   - Full 68-step integration verified.

5. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.

6. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

### Remaining Limitations
- Single-thread Jolt bridge hang (`ECSJ-03`) is deferred to EJ-06.
- EnTT stable storage iteration tombstone (`ECSJ-04`) is deferred to EJ-08.
- Independent acceptance remains pending; this report documents self-verification under autonomous authorization.
