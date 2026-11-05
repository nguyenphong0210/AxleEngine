# OPT-02 / EJ-01 independent review

Review baseline: `c5b3d23` (`feat(jobs): enforce thread identity, admission synchronization, and lifecycle epochs`), 2026-11-05. Reviewer: **Codex**.

The user message called this package “OPT 01 EJ 01”. The repository handoff and delivery identify it as **OPT-02 / EJ-01**; this review uses the repository identity.

Decision: **CHANGES_REQUIRED**. The focused lifecycle and identity behavior passes, but the public scheduler surface still permits bypassing the admission/context contract, and the scheduler data lifetime is not protected from ordinary external producers during shutdown.

## Findings

| ID / severity | Trigger and observed/source-confirmed behavior | Required correction |
| --- | --- | --- |
| EJ01-R1 / P1 | `JobSystem::GetScheduler()` returns the live `enki::TaskScheduler&` after checking only `s_Data`/`Running`; it never checks `IsValidSchedulerContext()` or admission. An ordinary external thread in the existing `ecsj-02-thread-identity` scenario calls `GetScheduler().GetThreadNum()` successfully and receives `NO_THREAD_NUM` without an invalid-context diagnostic. The same public reference can call `AddTaskSetToPipe`, `WaitforAll`, or retain the scheduler across shutdown, bypassing the wrapper admission barrier and risking use-after-free. | Remove the public raw reference from ordinary callers or gate it behind an owner/registered-context capability. Any supported raw submission must go through the synchronized admission path and have a lifetime-safe owner; unsupported external access must return a defined failure/diagnostic before scheduler access. Add a permanent external direct-access regression. |
| EJ01-R2 / P1 | `s_Data` is a non-atomic `std::unique_ptr`. `DispatchToMainThread()` checks `s_Data` and then later dereferences it to lock `MainThreadMutex`, while `Shutdown()` clears the queue and calls `s_Data.reset()` after draining. An external completion producer can pass the null check, be pre-empted while shutdown resets the pointer, and then dereference freed `JobSystemData`. The same check-then-use pattern exists in `GetCurrentThreadIndex()`, `GetState()`, the count queries, `Wait()`, and `TryAdmitTaskSet()`. This is source-confirmed; no MSVC thread sanitizer is available in this environment. | Protect the lifecycle object with a synchronized/shared lifetime snapshot (or an equivalent global lifecycle lock) so every operation retains the instance through its final dereference. Exercise an external producer racing shutdown and verify a defined drop/result with no crash or stale reference. |
| EJ01-R3 / P1 | `IncrementActiveTaskDepth()` and `DecrementActiveTaskDepth()` are public APIs. `IsValidSchedulerContext()` trusts `t_ActiveTaskDepth > 0` before checking scheduler registration. Any external thread can call `IncrementActiveTaskDepth()` and then `Dispatch()` while `Running`; the wrapper accepts the task as if it were an admitted scheduler callback. The permit is therefore forgeable and defeats the intended owner/worker admission rule. | Make the permit unforgeable to callers (private implementation state or an RAII token created only by `JobSystem` task execution), and validate the token/epoch when admitting internal children. Add an ordinary external self-grant regression that must be rejected in Debug and Release. |

The lifecycle test also does not attempt a submission during the actual `Quiescing` window; it only checks a child submission from task A and a dispatch after `Shutdown()` returns. That leaves the race/rejection boundary in R2 without a deterministic test even after the lifetime fix.

## Independent verification

The exact `c5b3d23` snapshot was built in a detached review worktree with the repository's third-party dependencies linked from the shared checkout:

| Check | Result |
| --- | --- |
| `cmake -S . -B build-review -G "Visual Studio 17 2022" -A x64` | Exit 0 |
| `cmake --build build-review --config Debug --target AxleJobsECSTests --parallel 6` | Exit 0; `AxleJobsECSTests.exe` rebuilt |
| `cmake --build build-review --config Release --target AxleJobsECSTests --parallel 6` | Exit 0; `AxleJobsECSTests.exe` rebuilt |
| `build-review/Tests/Debug/AxleJobsECSTests.exe ej01-lifecycle-admission` | Exit 0; `RESULT ej01-lifecycle-admission PASS` |
| `build-review/Tests/Release/AxleJobsECSTests.exe ej01-lifecycle-admission` | Exit 0; `RESULT ej01-lifecycle-admission PASS` |
| `git diff --check c5b3d23^ c5b3d23` | Exit 0 |

On the current shared HEAD (`3145a30`, which already contains EJ-02 on top of EJ-01), the independent regression run was:

| Check | Debug | Release |
| --- | --- | --- |
| `ctest --test-dir build -C <config> --output-on-failure` | 24/24, exit 0 | 24/24, exit 0 |
| `AxleJobsECSTests.exe ej01-lifecycle-admission` | Exit 0 | Exit 0 |
| `python scripts/check_architecture.py --root .` | PASS, 342 files / 17 CMake files | same |
| `python scripts/test_baseline_tools.py` | 18/18, exit 0 | same |

The current-HEAD jobs collector also completed with exit 0 and preserved the known ECSJ-01, ECSJ-03 and ECSJ-04 defects; its EJ-01 and EJ-02 scenarios passed. This is current-HEAD evidence, not a claim that EJ-02 belongs to the c5b3d23 review baseline. GPU, native interactive UI, export/player and MSVC thread-sanitizer checks were not run.

No production source fixes were made by this review. Keep EJ-01 pending and do not mark it accepted until EJ01-R1..R3 are corrected and the new race/direct-access regressions pass in Debug and Release.
