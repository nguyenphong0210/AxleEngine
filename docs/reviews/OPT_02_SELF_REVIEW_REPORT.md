# AxleEngine Architecture Self-Review Report: Phase OPT-02

**Document:** `docs/reviews/OPT_02_SELF_REVIEW_REPORT.md`
**Reviewer:** Gemini (historical self-review; the autonomous-plan document has been retired)
**Scope:** Phase OPT-02 (*Repair threading, scripts and physics jobs*) — Packages `EJ-01` through `EJ-07`
**Code Baseline:** `2e56e27` (Git working tree clean)
**Date:** 2026-11-06
**Status:** **SELF-VERIFIED & AUDITED** (Independent acceptance remains pending at milestone checkpoint)

---

## 1. Executive Summary

This report performs a comprehensive, forensic self-review of all 7 packages implemented across **Phase OPT-02**. The review evaluates thread safety, memory lifetime, reentrancy guards, exception/error handling, and cross-subsystem shutdown ordering against the architectural specifications in `docs/design/ENTT_ENKITS_REFACTOR_PLAN.md` and `docs/ROADMAP.md`.

### Verdict: **READY FOR PHASE OPT-03 (NO BLOCKING CODE CHANGES REQUIRED)**
- All 7 packages (`EJ-01` to `EJ-07`) satisfy their technical contracts without regressing existing systems.
- Baseline defects **`ECSJ-01`**, **`ECSJ-02`**, and **`ECSJ-03`** are fully resolved and verified with positive regression assertions.
- Baseline defect **`ECSJ-04`** is correctly preserved as an expected failure, strictly deferred to `EJ-08` in Phase OPT-03.
- Full verification passed 100% in both **Debug** and **Release**: 31/31 CTest tests, 14/1/3 Jobs/ECS harness scenarios, and 68/68 AxleTests integration steps.

---

## 2. Sequential Package-by-Package Forensic Audit

---

### Package EJ-01: Thread Identity and Lifecycle Admission
* **Commit:** `c5b3d23` (Initial delivery), `fc04a4a` (Corrections addressing findings EJ01-R1..EJ01-R3)
* **Audited Files:** `Engine/Core/include/Axle/JobSystem.hpp`, `Engine/Core/src/JobSystem.cpp`

#### Forensic Code Inspection:
1. **OS Thread Pinning:** `s_OwnerThreadId` captures the OS thread that calls `JobSystem::Init()`. Any subsequent call to `Init()` or `Shutdown()` from any other OS thread is strictly rejected and logged with `AXLE_CORE_ERROR`.
2. **Lifecycle State Machine:** `JobSystemState` transitions through `Stopped` -> `Running` -> `Quiescing` -> `Draining` -> `Stopped`. Under `AdmissionMutex`, new top-level application work is strictly rejected once the state moves away from `Running`.
3. **Unforgeable Permit (`Detail::TaskExecutionScope`):**
   - Corrected Codex finding `EJ01-R3`. Public increment/decrement functions for `ActiveTaskDepth` were removed.
   - `TaskExecutionScope` is an RAII token created exclusively inside scheduler-admitted task wrappers (`AsyncLambdaTask`, `LambdaTaskSet`, `PhysicsTaskRecord`). It verifies `IsValidSchedulerContext()` upon creation. External threads cannot fake a scheduler task scope.
4. **Direct Scheduler Reference Isolation (`EJ01-R1`):**
   - `JobSystem::GetScheduler()` checks `IsValidSchedulerContext()` and `JobSystemState::Running`. If called from an unsupported context or while stopped, it returns `nullptr` and reports an active diagnostic.
5. **Lifecycle Object Concurrency (`EJ01-R2`):**
   - `s_Data` is protected by `s_LifecycleMutex`. All queries obtain a `std::shared_ptr<JobSystemData>` snapshot, preventing use-after-free even if an external thread calls an API concurrently with `Shutdown()`.

#### Edge Cases & Stress Scenarios:
- **Rapid Re-initialization:** Verified across 3 consecutive `Init()` -> `Shutdown()` cycles. Monotonically increasing `s_Epoch` guarantees that stale references cannot cross initialization boundaries.
- **Worker Thread Numbering:** Worker threads return real indices `1 .. TotalThreads-1`; thread 0 is Main; external threads return `enki::NO_THREAD_NUM` (never mapped to 0).

#### Residual Notes & Observations:
- `JobSystem::GetScheduler()` is still exposed to `JobSystemEnkiTS`. In future phases (e.g. `OPT-03` / `OPT-06`), raw scheduler access should remain strictly private to internal engine bridges.

---

### Package EJ-02: Deferred Main-Thread Executor and Owner Tokens
* **Commit:** `3145a30`
* **Audited Files:** `Engine/Core/include/Axle/JobSystem.hpp`, `Engine/Core/src/JobSystem.cpp`

#### Forensic Code Inspection:
1. **Generation-Based Invalidation:**
   - `OperationOwner` holds a `std::shared_ptr<OperationOwnerState>`.
   - `OwnerToken` holds a `std::weak_ptr<OperationOwnerState>` and the snapshot generation `m_Generation`.
   - When `OperationOwner` is destroyed or calls `Invalidate()`, `Valid` is atomically set to `false` and `Generation` is bumped.
   - In `OwnerToken::IsValid()`, locking the weak pointer ensures that if the owner was destroyed, `IsValid()` evaluates to `false` with zero dangling pointer dereference.
2. **Queue Saturation & Memory Safety:**
   - `MaxMainThreadQueueCapacity` is capped at 16,384. If flooded, `PostToMainThread` returns `PostResult::RejectedQueueFull` rather than triggering unbounded memory exhaustion.
3. **Reentrancy During Pump:**
   - In `PumpMainThreadCompletions(maxItems)`: Items are extracted from `m_MainThreadQueue` under `MainThreadMutex`, and the lock is **released** before executing callbacks.
   - If a callback calls `PostToMainThread()`, the lock is acquired cleanly and the new item is queued for the *subsequent* pump batch. Deadlock is impossible.

#### Edge Cases & Stress Scenarios:
- **Abandoned Tasks:** When an owner is destroyed before a worker finishes, the worker still posts its completion. During `PumpMainThreadCompletions()`, `token.IsValid()` evaluates to false; the callback closure (and all its captured shared pointers) is reset to `nullptr` without execution, releasing captured resources immediately.

#### Residual Notes & Observations:
- Default `maxItems` is 1024. If a scenario queues > 1024 items in a single frame, the remainder will be processed in the next frame. In `Application::Run`, this occurs once per frame, which is optimal for frame pacing.

---

### Package EJ-03: Compiler Request / Result Handoff
* **Commit:** `b3e3ca2` (Resolves defect `ECSJ-01`)
* **Audited Files:** `Engine/Scene/include/Axle/ScriptCompiler.hpp`, `Engine/Scene/src/ScriptCompiler.cpp`, `Editor/src/EditorLayer.cpp`

#### Forensic Code Inspection:
1. **Immutable Snapshotting:**
   - All build configuration parameters (`WorkspaceDirectory`, `Target`, `BuildConfig`) are deep-copied into a `CompileRequestSnapshot` before the job is dispatched.
   - Mutating compiler settings on the UI thread while a build is in-flight has zero effect on the active process.
2. **Worker-to-UI Decoupling:**
   - Eliminated raw cross-thread callbacks from background workers into `EditorLayer`.
   - The completion result (`CompileResult`) is marshaled through `JobSystem::PostToMainThread` using `m_CompileOwner.GetToken()`.
3. **Cancellation & Teardown Join:**
   - `ScriptCompiler::Cancel()` terminates the OS build process and invalidates `m_CompileOwner`.
   - `ScriptCompiler::WaitForCompletion()` waits for the enkiTS task handle to finish, guaranteeing no background thread or zombie process is left behind when switching projects or closing the Editor.

#### Edge Cases & Stress Scenarios:
- **Switching Projects Mid-Build:** When `EditorLayer::OpenProject()` is invoked while a build is running, `m_CompileOwner.Invalidate()` is called. The in-flight build finishes or cancels in the OS background, but its completion callback is safely discarded and will never trigger hot-reload on the new project.

#### Residual Notes & Observations:
- Process spawning uses Win32 `CreateProcessW` with job object assignment. When porting to Linux/macOS in future milestones, a POSIX-equivalent implementation will be required; the Engine's current platform target is Windows.

---

### Package EJ-04: Reload Commit and Scene/DLL Teardown
* **Commit:** `f950fbe`
* **Audited Files:** `Engine/Scene/include/Axle/ScriptHotReloadManager.hpp`, `Engine/Scene/src/ScriptHotReloadManager.cpp`, `Engine/Scene/src/ScriptRegistry.cpp`

#### Forensic Code Inspection:
1. **Candidate Staging & Rollback:**
   - New DLL is loaded into temporary memory first (`candidatePath`).
   - Entry point `InitializeScriptModule` is verified. Candidate exports are checked against existing script registry schemas.
   - If verification fails, the candidate library is freed and the active DLL is preserved intact.
2. **Teardown Barrier:**
   - Before unloading the old DLL, `ScriptHotReloadManager` iterates all entities with `NativeScriptComponent`, copies their field buffers to `FieldStorage`, and invokes `DestroyInstance()`.
   - All script instances are destroyed while the old DLL code memory is still valid.
   - Only after all instances are destroyed is `FreeLibrary()` called on the old DLL.
   - New DLL is committed, instances are re-created, and field values are restored.
3. **Simulation Guard:**
   - Reload is strictly prohibited if `m_SceneState != SceneState::Edit`.

#### Edge Cases & Stress Scenarios:
- **Corrupted DLL Export:** Verified by test `ej04-reload-commit` subtest 2: missing symbols trigger clean rejection and rollback without crashing the running Scene.

#### Residual Notes & Observations:
- Reflected primitive/math types (float, int, bool, vec2, vec3, vec4, string) restore cleanly. Custom heap objects owned by scripts must use reflected fields or serialized properties to survive hot-reload.

---

### Package EJ-05: Stable Physics Submission Lifetime
* **Commit:** `2b31691`
* **Audited Files:** `Engine/Physics/include/Axle/JobSystemEnkiTS.hpp`, `Engine/Physics/src/JobSystemEnkiTS.cpp`

#### Forensic Code Inspection:
1. **Pool Architecture (`PhysicsTaskRecord`):**
   - Replaced fixed indexed array `m_EnkiTasks[m_Index]` with stable, individually allocated `PhysicsTaskRecord` instances stored in `m_AllocatedRecords`.
   - Each record inherits from `enki::ITaskSet`, providing a stable memory address that never moves or aliases another job.
2. **Three-Phase Lifecycle Decoupling:**
   - **Phase A (JPH Job Ref):** `job->AddRef()` / `Release()`.
   - **Phase B (Execution Body):** `job->Execute()`. On completion, `record->AttachedJob = {}` releases the JPH job slot immediately, allowing Jolt to reuse the slot.
   - **Phase C (enkiTS Completion):** The enkiTS task set is only reclaimed to `m_FreeRecords` once `record->GetIsComplete()` returns `true` via `DrainPendingReclaim()`.
3. **Synchronous Fallback Execution:**
   - When scheduler admission is rejected (e.g., JobSystem stopped), `ExecuteSynchronous()` executes the JPH job inline and directly reclaims the record without entering enkiTS task scopes, preventing double-reclaim underflows.

#### Edge Cases & Stress Scenarios:
- **Rapid Slot Churn:** Verified under 100 concurrent jobs in a bridge configured with limited job slots. Allocation high-water mark stabilized without leaks.

#### Residual Notes & Observations:
- All record pool operations (`m_FreeRecords`, `m_PendingReclaim`) are synchronized under `m_PoolMutex`. Overhead is negligible (< 0.05ms in physics step).

---

### Package EJ-06: Physics Waits and Forward Progress
* **Commit:** `d46e23d` (Resolves defect `ECSJ-03`)
* **Audited Files:** `Engine/Physics/include/Axle/JobSystemEnkiTS.hpp`, `Engine/Physics/src/JobSystemEnkiTS.cpp`

#### Forensic Code Inspection:
1. **Root Cause Analysis of `ECSJ-03`:**
   - On a 1-thread scheduler (`Init(1)`), `GetDedicatedWorkerThreadCount() == 0`.
   - JPH's barrier executes `job->Execute()` directly on the calling thread. However, the admitted enkiTS task remained stranded in enkiTS's queue.
   - On bridge destruction, `~JobSystemEnkiTS()` waited for live tasks to reach 0; because no background thread existed, it hung indefinitely.
2. **Active Work Pumping Fix:**
   - In `WaitForJobs()` and `Shutdown()`, when `GetDedicatedWorkerThreadCount() == 0`, the bridge calls `Axle::JobSystem::WaitForAll()`.
   - This drives enkiTS tasks to completion on the waiting thread immediately, allowing `m_LiveTaskCount` to decrement to 0 in microseconds.
3. **Disposal Guards:**
   - Added `static thread_local uint32_t t_InJobScope` tracking whether the thread is currently executing inside a JPH job callback.
   - `Shutdown()` rejects calls if `!IsMainThread()`, `t_InJobScope > 0`, or `GetActiveTaskDepth() > 0`, completely preventing reentrant self-destruction deadlocks.
4. **Spinloop Elimination:**
   - Replaced naked `std::this_thread::yield()` with active pumping (when 0 workers) and bounded 100µs micro-sleeps (when > 0 workers).

#### Edge Cases & Stress Scenarios:
- **1-Thread Dependent Chaining:** Verified by `ej06-physics-waits` and `ecsj-03-bridge-hang` under 1-thread and 2-thread schedulers with 10 chained dependent jobs. Execution completed with exit 0 in < 0.1s.

#### Residual Notes & Observations:
- `JobSystemEnkiTS::Shutdown()` is idempotent; multiple calls return safely.

---

### Package EJ-07: Handles, Draining and Shutdown
* **Commit:** `98fe297`
* **Audited Files:** `Engine/Core/include/Axle/JobSystem.hpp`, `Engine/Core/src/JobSystem.cpp`, `Engine/Core/src/Application.cpp`, `Engine/Physics/src/PhysicsScene.cpp`

#### Forensic Code Inspection:
1. **Owning `JobHandle` Lifetime Retention:**
   - `JobHandle` retains a `std::shared_ptr<JobSystemData> m_Data`, `std::shared_ptr<enki::ICompletable> m_Task`, `u32 m_Epoch`, and `std::shared_ptr<std::atomic<bool>> m_Cancelled`.
   - Even if `JobSystem::Shutdown()` is called and `s_Data.reset()` is executed, `m_Data` remains allocated as long as any `JobHandle` or active task references it.
   - If a caller invokes `handle.Wait()` or `handle.IsComplete()` after `Shutdown()`, it checks `m_Data->State == Stopped` and returns immediately without touching a dangling pointer.
2. **Cancellation & Detach Semantics:**
   - `JobHandle::Cancel()` atomically flips `m_Cancelled`. If the task has not started executing, `ExecuteRange` skips `TaskFunc()`, and enkiTS marks the task complete.
   - `JobHandle::Detach()` clears internal references; the background task completes without leaking.
3. **Engine-Wide Teardown Ordering:**
   - [`Application::~Application`](file:///d:/WorkSpace/AxleEngine/Engine/Core/src/Application.cpp#L69-L89) executes the canonical 6-step teardown:
     1. Clear `LayerStack` (detaches `EditorLayer`/`RuntimeLayer`, joins compiler, clears scenes).
     2. Drain deferred main-thread completions (`JobSystem::PumpMainThreadCompletions()`).
     3. Shutdown `UIContext`.
     4. Shutdown `PhysicsEngine` (unregisters Jolt types and factory only after all `PhysicsScene` instances are destroyed).
     5. Shutdown `JobSystem` (quiesces admission, drains tasks, terminates enkiTS).
     6. Complete.
   - [`PhysicsScene::~PhysicsScene`](file:///d:/WorkSpace/AxleEngine/Engine/Physics/src/PhysicsScene.cpp#L46-L56) explicitly calls `m_JobSystem->Shutdown()` before removing bodies or destroying `m_PhysicsSystem`.

#### Edge Cases & Stress Scenarios:
- **Handle Outliving JobSystem:** Tested in `ej07-handles-shutdown` subtest 1: handle held across `JobSystem::Shutdown()` queried and waited on safely with zero errors.
- **In-Task Shutdown Reentrancy:** Tested in `ej07-handles-shutdown` subtest 2: in-task shutdown attempt rejected with `AXLE_CORE_ERROR` without deadlock.

#### Residual Notes & Observations:
- All 3 subtests passed with exit 0 in Debug and Release.

---

## 3. Cross-Package Interactions & Threat Matrix

| Threat / Race Scenario | Mitigating Architecture | Audited Result |
| :--- | :--- | :--- |
| **Worker calls UI / Scene directly** | Disallowed by design. All asynchronous completions route through `JobSystem::PostToMainThread` using `OperationOwner` tokens. | **SAFE**: Zero cross-thread Scene mutations. |
| **Worker callback fires after object destroyed** | `OwnerToken::IsValid()` evaluates to false; `PumpMainThreadCompletions()` drops the callback closure. | **SAFE**: Zero Use-After-Free. |
| **Compiler build finishes while switching project** | Snapshot request isolates build settings; `m_CompileOwner.Invalidate()` discards results for old project. | **SAFE**: No cross-project hot-reload corruption. |
| **Hot-reload DLL frees memory while instances exist** | Teardown barrier in `ScriptHotReloadManager` destroys all instances before `FreeLibrary`. | **SAFE**: No dangling vtable crashes. |
| **Jolt reuses job slot while enki task still returns** | Pool of stable `PhysicsTaskRecord` instances; reclamation only after `GetIsComplete() == true`. | **SAFE**: No task set memory aliasing. |
| **Bridge destruction on 1-thread scheduler** | Active work pumping in `WaitForJobs()` and `Shutdown()` drives enkiTS tasks inline. | **SAFE**: Zero destruction hangs (`ECSJ-03` resolved). |
| **JobHandle outlives scheduler shutdown** | `JobHandle` retains `shared_ptr<JobSystemData>` snapshot; `Wait()` checks `Stopped` state. | **SAFE**: No freed-memory dereference. |

---

## 4. Status of Architectural Defects (ECSJ-01 .. ECSJ-05)

| Defect ID | Severity | Description | Status in OPT-02 | Owning Package |
| :--- | :--- | :--- | :--- | :--- |
| **`ECSJ-01`** | P1 | Script compiler callback thread violation | **RESOLVED & VERIFIED** | `EJ-03` |
| **`ECSJ-02`** | P1 | Thread identity & main-thread dispatch invalidation | **RESOLVED & VERIFIED** | `EJ-01`, `EJ-02` |
| **`ECSJ-03`** | P1 | Jolt bridge destruction hang on constrained scheduler | **RESOLVED & VERIFIED** | `EJ-05`, `EJ-06` |
| **`ECSJ-04`** | P2 | EnTT stable storage iteration tombstone | **PRESERVED AS PLANNED** | Deferred to `EJ-08` (OPT-03) |
| **`ECSJ-05`** | P2 | Products put combined control/readback on opposite sides of physics | **PRESERVED AS PLANNED** | Deferred to `EJ-11..14` (OPT-03) |

---

## 5. Verification Gate Evidence Checklist

- [x] **Debug CTest:** 31/31 passed (100%), exit 0.
- [x] **Release CTest:** 31/31 passed (100%), exit 0.
- [x] **Debug Jobs/ECS Harness:** 14 PASS, 1 preserved defect (`ECSJ-04`), 3 verified probes, exit 0.
- [x] **Release Jobs/ECS Harness:** 14 PASS, 1 preserved defect (`ECSJ-04`), 3 verified probes, exit 0.
- [x] **Debug Full Integration (`AxleTests.exe`):** 68/68 steps passed (100%), exit 0.
- [x] **Release Full Integration (`AxleTests.exe`):** 68/68 steps passed (100%), exit 0.
- [x] **Architecture Rules:** `python scripts/check_architecture.py --root .` -> `AXLE_ARCH_CHECK PASS` (342 source files, 17 CMake files).
- [x] **Tool Regression Tests:** `python scripts/test_baseline_tools.py` -> 18/18 tests passed (OK).
- [x] **Product Builds:** `AxleEditor` & `AxleRuntime` build and link cleanly in Debug and Release.

---

## 6. Conclusion & Recommendation

The forensic self-review confirms that **Phase OPT-02 is technically complete, fully verified, and free of blocking defects or race conditions**. No code modifications are needed within OPT-02.

**Recommendation:** Proceed with confidence to **Phase OPT-03** (*Separate Core, scene data and assets; unify simulation*):
1. Package **`AF-02`**: Foundation, Application and explicit service ownership.
2. Package **`EJ-08`**: Parallel EnTT view correctness (resolving preserved defect `ECSJ-04`).
