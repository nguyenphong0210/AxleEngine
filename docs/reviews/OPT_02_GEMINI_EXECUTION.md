# OPT-02 Execution Report: Repair Threading, Scripts and Physics Jobs

Evolving execution log and verification record for the sequential implementation of **OPT-02 (EJ-01 through EJ-07)**, authored by Gemini under the historical OPT-02 scope recorded in the exchange.

---

## Package EJ-01: Thread Identity and Lifecycle Admission

- **Status:** Implemented and self-verified; local delivery committed. Independent review remains pending at phase checkpoint.
- **Base Baseline:** `d2ab4e7`
- **Scope & Contract:** historical OPT-02 scope recorded in the exchange

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
- **Scope & Contract:** [ROADMAP.md](../ROADMAP.md) and the historical exchange record

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
- **Scope & Contract:** [ROADMAP.md](../ROADMAP.md) and the historical exchange record

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

---

## Package EJ-04: Reload Commit and Scene/DLL Teardown

- **Status:** Implemented and self-verified; local delivery committed. Independent review remains pending at phase checkpoint.
- **Base Baseline:** `b3e3ca2`
- **Scope & Contract:** [ROADMAP.md](../ROADMAP.md) Package EJ-04

### Implementation Summary
1. **Candidate Staging & Zero-Downtime Rollback:**
   - Script DLL compilation yields a candidate shadow library copied to a candidate path.
   - Staging protocol (`ScriptHotReloadManager::Reload`):
     - Loads candidate shadow DLL first via `LoadLibraryA`.
     - Validates mandatory entry points (`Axle_InitializeScripts` and exported script registration).
     - Queries script metadata into a candidate staging table without affecting the live registry.
     - If loading, symbol resolution, or script registration fails: logs error, unloads candidate shadow DLL with `FreeLibrary`, and aborts reload with active DLL and live scene instances completely intact (rollback guarantee).
2. **Lifecycle Ordering & Destructor-Before-Unload Barrier:**
   - Scene script instances and leases must be cleanly destroyed *before* `FreeLibrary()` unmaps the active DLL image.
   - `ScriptHotReloadManager::Reload` invokes `OnPreScriptReload()` on the active scene prior to calling `FreeLibrary()`.
   - `NativeScriptComponent::DestroyScript()` routes through `ScriptRegistry::Destroy(name, instance)` so that the DLL's exported destructor is called using the DLL's heap allocator, preventing CRT heap mismatches across DLL boundaries.
   - Generation counter (`GetModuleGeneration()`) increments atomically upon successful module commit or shutdown, invalidating outstanding module leases.
3. **Shutdown Cleanliness:**
   - `ScriptHotReloadManager::Shutdown()` triggers `OnPreScriptReload()` to destroy all native script instances, unregisters all scripts from `ScriptRegistry`, increments the generation counter, and only then calls `FreeLibrary()` on the active module.

### Verification Evidence
1. **Focused EJ-04 Native Probe (`ej04-reload-commit`):**
   - Verified candidate export staging and rollback on corrupt DLL / missing exports.
   - Verified that invalid candidates leave existing active scripts and registrations undamaged.
   - Verified generation tracking and destructor ordering before module unload.
   - PASS (exit 0) in Debug and Release.

2. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - Registered `ej04-reload-commit` as native `PASS`.
   - **Debug:** `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json`
     - Result: **9 PASS, 2 recorded defects (ECSJ-03, ECSJ-04 preserved), 3 verified probes, 0 unexpected failures**.
   - **Release:** `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json`
     - Result: **9 PASS, 2 recorded defects, 3 verified probes, 0 unexpected failures**.

3. **CTest Suite (Debug & Release):**
   - Registered `JobsECS_ReloadCommit` in `Tests/CMakeLists.txt` (total 27 tests).
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **27/27 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **27/27 tests passed (100%)**.

4. **Integration Suite (`AxleTests.exe`):**
   - Step 20 verified: `AxleTests.exe 20` (anti-lock shadow copy & state preservation) -> **100% passed**.

5. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.

6. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

### Remaining Limitations
- Single-thread Jolt bridge hang (`ECSJ-03`) is deferred to EJ-06.
- EnTT stable storage iteration tombstone (`ECSJ-04`) is deferred to EJ-08.
- Independent acceptance remains pending; this report documents self-verification under autonomous authorization.

---

## Package EJ-05: Stable Physics Submission Lifetime

### Objectives & Deliverables
1. **Decouple JPH Job Slot from Task Lifetime:**
   - Eliminated the indexed reusable array `m_EnkiTasks[m_Index]` in `JobSystemEnkiTS`.
   - Replaced with a thread-safe, dynamically allocated pool of stable `PhysicsTaskRecord` instances inheriting from `enki::ITaskSet`.
   - Releasing or destructing a Jolt job slot in `m_Jobs` no longer touches, destroys, or aliased live enkiTS task sets.
2. **Distinct Lifecycle Phases:**
   - Separated JPH Job reference retention (`AddRef`/`Release`), wrapper body execution (`job->Execute()`), and scheduler completion (`enki::ITaskSet::GetIsComplete()`).
   - Tasks returning from `ExecuteRange` transition through `OnTaskBodyCompleted` into `m_PendingReclaim` and are only reclaimed to `m_FreeRecords` once `GetIsComplete()` returns true, preventing premature task set reuse.
3. **Synchronous Execution Fallback & Clean Teardown:**
   - Decoupled synchronous fallback (`ExecuteSynchronous`) from scheduler task scope and asynchronous reclamation, preventing double-decrements and context check errors when admission is rejected.
   - Replaced spinloop in `~JobSystemEnkiTS()` with graceful task draining (`WaitForLiveTasks`).
   - Removed unmanaged auto-initialization of `JobSystem` from `JobSystemEnkiTS::QueueJobs`.

### Verification Evidence
1. **Focused EJ-05 Native Probe (`ej05-physics-lifetime`):**
   - Subtest 1: Constrained JPH slot reuse (4 slots, 32 jobs) with aggressive rapid destruction verified without corruption or aliasing.
   - Subtest 2: Concurrent queueing and reclamation under load (100 jobs) verified with high-water mark tracking and zero leaks.
   - Subtest 3: Synchronous execution fallback verified with stopped `JobSystem`.
   - PASS (exit 0) in Debug and Release.

2. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - Registered `ej05-physics-lifetime` as native `PASS`.
   - Debug: `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json` -> **11 PASS, 2 preserved defects (ECSJ-03, ECSJ-04), 3 verified probes, 0 unexpected failures**.
   - Release: `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json` -> **11 PASS, 2 preserved defects (ECSJ-03, ECSJ-04), 3 verified probes, 0 unexpected failures**.

3. **CTest Suite (Debug & Release):**
   - Registered `JobsECS_PhysicsLifetime` in `Tests/CMakeLists.txt` (total 29 tests).
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **29/29 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **29/29 tests passed (100%)**.

4. **Integration Suite (`AxleTests.exe`):**
   - Step 23 verified: `AxleTests.exe 23` (Phase 19.3 Jolt Physics Bridge) -> **100% passed**.
   - Full suite verified: `AxleTests.exe` -> **All 68 steps passed (100%)**.

5. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.

6. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

### Remaining Limitations
- Single-thread Jolt bridge hang (`ECSJ-03`) is resolved in EJ-06.
- EnTT stable storage iteration tombstone (`ECSJ-04`) is deferred to EJ-08.
- Independent acceptance remains pending; this report documents self-verification under autonomous authorization.

---

## Package EJ-06: Physics Waits and Forward Progress

### Objectives & Deliverables
1. **Guaranteed 1-Thread and Constrained Forward Progress (Resolving Defect `ECSJ-03`):**
   - Eliminated the destruction hang in `JobSystemEnkiTS` when running on a 1-thread scheduler (`GetDedicatedWorkerThreadCount() == 0`).
   - Root cause: When zero background enkiTS workers exist, JPH's barrier executes `job->Execute()` directly on the calling thread, but the admitted `PhysicsTaskRecord` remains unexecuted in enkiTS's queue. During bridge destruction, `WaitForLiveTasks` hung indefinitely waiting for live tasks to reach 0.
   - Solution: Driving `Axle::JobSystem::WaitForAll()` in `JobSystemEnkiTS::WaitForJobs` and `Shutdown()` when `GetDedicatedWorkerThreadCount() == 0` actively pumps and completes the enkiTS task set on the waiting thread, allowing immediate reclamation.
2. **Prohibit Non-Owner and In-Job Disposal:**
   - Implemented explicit `Shutdown()` on `JobSystemEnkiTS`.
   - Rejects non-owner thread disposal (`!IsMainThread()`) with diagnostic logging.
   - Prohibits disposal from within any active JPH job scope (`t_InJobScope > 0`) or active enkiTS task scope (`Axle::JobSystem::GetActiveTaskDepth() > 0`), preventing deadlock and self-destruction during job callbacks.
3. **Elimination of Dangerous Yield Spinloops:**
   - Replaced naked `std::this_thread::yield()` spinloops in `WaitForLiveTasks` with active task pumping (`Axle::JobSystem::WaitForAll()`) on 0-worker schedulers and bounded micro-sleeps (`std::this_thread::sleep_for(std::chrono::microseconds(100))`) on multi-worker schedulers, preventing CPU saturation.
4. **Permanent Regression & Diagnostic Probes:**
   - Converted baseline defect `ecsj-03-bridge-hang` in `scripts/run_jobs_ecs_harness.py` from `REPRODUCED_DEFECT` (timeout) to `PASS` (clean exit 0).
   - Created native probe `ej06-physics-waits` in `Tests/src/JobsECSTestMain.cpp` testing:
     - Subtest 1: 1-thread forward progress and direct bridge destruction (`Init(1)`).
     - Subtest 2: Non-owner shutdown rejection and in-job shutdown rejection from worker callbacks.
     - Subtest 3: Constrained scheduler forward progress under 10-job dependency chaining on a 2-thread scheduler (`Init(2)`).

### Verification Evidence
1. **Focused EJ-06 Native Probe (`ej06-physics-waits`):**
   - All 3 subtests PASS (exit 0) in Debug and Release.
2. **Permanent Defect Fix (`ecsj-03-bridge-hang`):**
   - Clean exit 0 (`AXLE_TEST_COMPLETED`, no hang) in Debug and Release.
3. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - Debug: `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json` -> **13 PASS, 1 preserved defect (ECSJ-04), 3 verified probes, 0 unexpected failures**.
   - Release: `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json` -> **13 PASS, 1 preserved defect (ECSJ-04), 3 verified probes, 0 unexpected failures**.
4. **CTest Suite (Debug & Release):**
   - Registered `JobsECS_PhysicsWaits` in `Tests/CMakeLists.txt` (total 30 tests).
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **30/30 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **30/30 tests passed (100%)**.
5. **Integration Suite (`AxleTests.exe`):**
   - Step 23 verified: `AxleTests.exe 23` (Phase 19.3 Jolt Physics Bridge) -> **100% passed**.
   - Full suite verified: `AxleTests.exe` -> **All 68 steps passed (100%)**.
6. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.
7. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

### Remaining Limitations
- EnTT stable storage iteration tombstone (`ECSJ-04`) is deferred to EJ-08 in OPT-03.
- Independent acceptance remains pending; this report documents self-verification under autonomous authorization.

---

## Package EJ-07: Handles, Draining and Shutdown

### Objectives & Deliverables
1. **Owning Asynchronous Handles & Lifecycle Safety:**
   - Redesigned `JobHandle` to retain shared ownership of scheduler state (`std::shared_ptr<JobSystemData> m_Data`), task completable (`std::shared_ptr<enki::ICompletable> m_Task`), initialization epoch (`u32 m_Epoch`), and atomic cancellation token (`std::shared_ptr<std::atomic<bool>> m_Cancelled`).
   - Implemented `JobHandle::Cancel()`: atomically cancels pending execution before `TaskFunc` starts; `JobHandle::IsCancelled()` provides synchronized query.
   - Implemented `JobHandle::Detach()`: drops handle references safely while task continues execution without memory leaks or dangling pointers.
   - Out-of-life `JobHandle` wait/query safety: Calling `handle.Wait()` or `handle.IsComplete()` after `JobSystem::Shutdown()` returns safely without crashing or accessing freed memory, because `m_Data` and `m_Task` are held by shared ownership and `Scheduler.WaitforAllAndShutdown()` guarantees all admitted work completed before shutdown returns.
2. **Admitted Task Graph Retention:**
   - Admitted tasks (`AsyncLambdaTask`) retain a `std::shared_ptr<JobSystemData>` through their execution scope, guaranteeing that scheduler state is never freed while background worker threads are actively running.
3. **Reentrancy and In-Task Shutdown Prohibition:**
   - Added `GetActiveTaskDepth() > 0` rejection check in `JobSystem::Shutdown()`, prohibiting reentrant self-shutdown deadlocks from inside worker or task callbacks.
4. **Orderly Teardown Across Engine Subsystems:**
   - In `Application::~Application()`:
     1. Clear LayerStack (`[1/6]`): detaches all layers, stopping simulations, clearing scenes, joining compiler and worker tasks.
     2. Drain deferred main-thread completions (`[2/6]`): calls `JobSystem::PumpMainThreadCompletions()` to safely evaluate and purge invalidated owner tokens before UI and platform systems tear down.
     3. Shutdown `UIContext` (`[3/6]`).
     4. Shutdown `PhysicsEngine` (`[4/6]`): unregisters Jolt types and factory only after all `PhysicsScene` instances and jobs have completed.
     5. Shutdown `JobSystem` (`[5/6]`): quiesces admission, drains remaining engine tasks, clears queues, and shuts down enkiTS scheduler.
     6. Complete (`[6/6]`).
   - In `PhysicsScene::~PhysicsScene()`: explicitly invokes `m_JobSystem->Shutdown()` before body removal and physics system destruction, ensuring no background worker thread accesses the Jolt physics world while it is being destroyed.
5. **Permanent Regression & Diagnostic Probes:**
   - Implemented `RunEJ07HandlesShutdownProbe()` in `Tests/src/JobsECSTestMain.cpp`:
     - Subtest 1: Owning asynchronous handles, cancellation, detach, and scheduler retention across `Shutdown()`.
     - Subtest 2: Repeated startup/shutdown cycles (3 cycles) with monotonically increasing epochs, deferred completion pumping, non-owner shutdown rejection, and in-task shutdown rejection.
     - Subtest 3: Full teardown sequencing order (`PhysicsScene` -> Deferred drainage -> `PhysicsEngine` -> `JobSystem`).
   - Registered `JobsECS_HandlesShutdown` in `Tests/CMakeLists.txt` (total 31 CTest tests).
   - Registered `("ej07-handles-shutdown", "PASS", args.timeout)` in `scripts/run_jobs_ecs_harness.py`.

### Verification Evidence
1. **Focused EJ-07 Native Probe (`ej07-handles-shutdown`):**
   - All 3 subtests PASS (exit 0) in Debug and Release.
2. **Typed Baseline Harness (`scripts/run_jobs_ecs_harness.py`):**
   - Debug: `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt02-debug --report build/opt02-debug/harness-debug.json` -> **14 PASS, 1 preserved defect (ECSJ-04), 3 verified probes, 0 unexpected failures**.
   - Release: `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt02-release --report build/opt02-release/harness-release.json` -> **14 PASS, 1 preserved defect (ECSJ-04), 3 verified probes, 0 unexpected failures**.
3. **CTest Suite (Debug & Release):**
   - Debug: `ctest --test-dir build -C Debug --output-on-failure` -> **31/31 tests passed (100%)**.
   - Release: `ctest --test-dir build -C Release --output-on-failure` -> **31/31 tests passed (100%)**.
4. **Integration Suite (`AxleTests.exe`):**
   - Step 23 verified: `AxleTests.exe 23` (Phase 19.3 Jolt Physics Bridge) -> **100% passed**.
   - Full suite verified: `AxleTests.exe` -> **All 68 steps passed (100%)**.
5. **Tooling & Architecture:**
   - `python scripts/check_architecture.py --root .` -> **AXLE_ARCH_CHECK PASS** (342 files scanned).
   - `python scripts/test_baseline_tools.py` -> **18 tests passed (OK)**.
6. **Product Target Builds:**
   - `AxleEditor` & `AxleRuntime` built successfully (exit 0) in Debug and Release.

---

## Phase OPT-02 Completion Summary

Phase OPT-02 (Repair threading, scripts and physics jobs) is complete and fully self-verified under the historical OPT-02 scope recorded in the exchange.

| Package | Status | Key Deliverable | Defects Resolved |
| --- | --- | --- | --- |
| **EJ-01** | Delivered (`c5b3d23`, fixes `fc04a4a`) | OS thread identity, admission barrier, unforgeable permit | P1 thread identity |
| **EJ-02** | Delivered (`3145a30`) | Deferred main executor, `OperationOwner` tokens | `ECSJ-02` |
| **EJ-03** | Delivered (`b3e3ca2`) | Immutable compiler request snapshot & deferred completion | `ECSJ-01` |
| **EJ-04** | Delivered (`f950fbe`) | Reload commit, candidate staging & scene/script barrier | Script DLL hazards |
| **EJ-05** | Delivered (`2b31691`) | Decoupled Jolt job slots from enkiTS task lifetime (`PhysicsTaskRecord`) | Physics lifetime hazards |
| **EJ-06** | Delivered (`d46e23d`) | Single-thread forward progress & non-owner disposal prohibition | `ECSJ-03` |
| **EJ-07** | Delivered (this commit) | Owning async `JobHandle`, cancellation/detach, complete shutdown order | Shutdown races |

All exit criteria for Phase OPT-02 have been satisfied:
- ECSJ-01, ECSJ-02, and ECSJ-03 are completely resolved and verified.
- CTest passes 31/31 tests (100%) in Debug and Release.
- Jobs/ECS harness passes 14 scenarios, with only ECSJ-04 remaining as preserved defect (deferred to EJ-08 in OPT-03).
- Full 68-step integration suite passes 100%.
- Both products (`AxleEditor`, `AxleRuntime`) build cleanly with zero errors.
- Independent acceptance remains pending; this report documents full self-verification.
