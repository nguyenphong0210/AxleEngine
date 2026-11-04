# EnTT and enkiTS Integration Review

Reviewed: **2026-11-04**. Source baseline:
**08a20ae2affefac8babef7ac2d2ff2401997252b**.
Scope: existing ECS, jobs, Jolt bridge, scene lifecycle and actual product callers.
This is an audit with temporary native probes, **not a production fix** or a
release of the [plugin implementation plan](../design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md).

## 1. Assessment

The choice of EnTT plus enkiTS is compatible with this engine. Scene owns its
registry, the Engine provides a shared task scheduler, and Jolt routes work through
that scheduler rather than creating another physics worker pool. The synchronous
ParallelFor helpers keep stack tasks alive until their scheduler wait completes.
Terrain chunk geometry is built into separate per-chunk output before GPU upload.
Those are useful foundations and do not require replacing either library.

The integration has concrete correctness problems: three high-priority findings
around main-thread delivery, script reload and Jolt/enki completion, plus two
medium-priority findings in generic ECS iteration and product scheduling.
Four offending behaviors were reproduced in a newly compiled native probe;
Editor/Runtime schedule divergence was verified in source, not a gameplay capture.

| ID | Priority | Finding | Evidence |
| --- | --- | --- | --- |
| ECSJ-01 | P1 | Script compiler callback reloads scene/DLL state on a background thread | Callback thread reproduced; actual Editor reload path traced |
| ECSJ-02 | P1 | External threads are classified as main and execute main-thread callbacks immediately | Native reproduction |
| ECSJ-03 | P1 | Jolt completion is mistaken for enki task completion; bridge destruction can hang or race task storage | Single-thread hang reproduced; other lifetime windows source-confirmed |
| ECSJ-04 | P2 | Parallel ForEach bypasses EnTT tombstone filtering for stable single-component storage | Native reproduction; latent generic-helper bug |
| ECSJ-05 | P2 | Editor and Runtime execute scripts/physics/readback in different orders | Product call-order inspection |

P1 means potential crash, data race or indefinite wait in a supported integration
path; it does not mean every current session fails. P2 findings have narrower
conditions or primarily affect correctness/parity. No measured performance loss,
ThreadSanitizer result or native Editor crash is claimed by this review.

## 2. ECSJ-01: script compilation completion crosses the scene thread boundary

**Sources:** [ScriptCompiler](../../Engine/Scene/src/ScriptCompiler.cpp), lines
141-145; [Editor callback](../../Editor/src/EditorLayer.cpp), lines 776-802;
[scene reload hooks](../../Engine/Scene/src/Scene.cpp), lines 1284-1322;
[DLL reload](../../Engine/Scene/src/ScriptHotReloadManager.cpp), lines 100-125.

`CompileAsync` creates a `std::thread`, runs the compile process there and directly
invokes `onComplete(result)` on that thread. The production Editor callback calls
`Reload(m_Scene, ...)`, changes script instances/factories and unloads the old DLL.
It also writes Editor notification strings/timers that the main thread reads.
No scene-thread handoff or matching synchronization surrounds this callback.

**Trigger:** complete a script build while the Editor is drawing, updating scripts
or changing the active scene. The callback can destroy an instance or unload code
while the main thread is using it. Even outside Play, notification state and scene
reload operations still cross the ownership boundary.

**Expected:** only compilation/process/log collection happens on the worker.
Completion is delivered at a safe Editor owning-thread boundary, with a valid
session/scene owner and no script invocation concurrently using the old DLL.

**Native evidence:** the probe compiles the current `ScriptCompiler.cpp` and uses
a successful isolated CMake fixture target. Its callback reports:

```text
started=1 callbackRan=1 compilationSucceeded=1 callbackOnMain=0
```

This proves the callback thread. The fixture target does not build a gameplay DLL;
the unsafe real Editor reload is established by the production caller, not executed
in the probe. A concurrent reload crash/data race was not induced.

**Related owner lifetime:** Editor `OnDetach` waits for its Runtime export future
but does not cancel/join the script compiler before clearing scenes and shutting
down the reload manager. The completion captures raw `this`; joining only in the
compiler singleton destructor does not protect EditorLayer lifetime. This is an
additional source-confirmed teardown risk within the same completion contract.

**Repair direction:** deliver an immutable completion result to an owning-thread
queue; validate operation/session identity, preserve DLL lifetime through all live
instances and cancel/join or invalidate callbacks before Editor detach/project switch.
Fix ECSJ-02 before using the current DispatchToMainThread as that handoff.

## 3. ECSJ-02: unregistered threads become main thread zero

**Sources:** [JobSystem](../../Engine/Core/src/JobSystem.cpp), lines 90-98 and
187-204; [vendored scheduler API](../../ThirdParty/enkiTS/src/TaskScheduler.h),
thread-index/external-thread documentation and lines 334-354.

The wrapper translates `enki::NO_THREAD_NUM` to `0`. `IsMainThread()` then tests
that translated value, so a normal external `std::thread` is reported as main.
`DispatchToMainThread` sees true and runs the callback inline on that external
thread instead of queuing it.

**Trigger:** any initialized Engine process receiving work from a non-enki thread,
such as a compiler worker, dialog worker or project plugin thread, uses this API.

**Expected:** an unregistered thread remains distinguishable from the scheduler
creator/main thread. Main-thread delivery enqueues from external threads and is
consumed only by the real owning thread.

**Native evidence:** production JobSystem compiled into the probe, initialized
with two scheduler threads, followed by one external `std::thread`:

```text
external enki index=4294967295 wrapper index=0
incorrectlyMain=1 calledBeforePump=1 callbackOnMain=0
```

The worker was joined before inspecting the results, so this observation does not
depend on a data race in the test. The callback ran before the main-thread pump.

**Repair direction:** preserve the invalid thread index and explicitly record/check
the owning OS thread. Define behavior before initialization/after shutdown. The
mutex-protected main-thread queue can accept external-thread delivery without
registering that thread with enki. Separately validate scheduler operations:
Dispatch/ParallelFor/Wait cannot safely call enki APIs from arbitrary unregistered
threads merely because the wrapper labels them zero. Test those API contracts too.

## 4. ECSJ-03: the Jolt bridge releases resources before enki completion

**Sources:** [bridge](../../Engine/Physics/src/JobSystemEnkiTS.cpp), lines 21-25,
88-126; [enki scheduler completion](../../ThirdParty/enkiTS/src/TaskScheduler.cpp),
lines 605-619 and 628-638; [Jolt barrier contract](../../ThirdParty/JoltPhysics/Jolt/Core/JobSystem.h),
the `WaitForJobs` integration notes; [barrier implementation](../../ThirdParty/JoltPhysics/Jolt/Core/JobSystemWithBarrier.cpp).

There are two distinct completion events:

1. Jolt's `Job::Execute` has completed and its barrier is satisfied.
2. The enki `ITaskSet::ExecuteRange` wrapper has returned and enki has completed
   its own accesses to that task's running count/dependencies.

The bridge waits only for the Jolt barrier. It decrements `m_ActiveEnkiTasks`
inside ExecuteRange, and its destructor spins on that counter. That is earlier
than enki's post-return accesses to task memory. In addition, `job->Release()` can
free the Jolt slot inside ExecuteRange; another job can reuse the same indexed
`m_EnkiTasks[index]` before the old enki submission is complete.

**Deterministic trigger:** initialize `JobSystem::Init(1)`, create a bridge and job,
add the job to a Jolt barrier, wait for that barrier and destroy the bridge.
Jolt's barrier can execute the Jolt job itself. Its queued enki wrapper is still
pending; with no worker to run it, the destructor's yield loop never reduces the
active counter. One scheduler thread is a supported public configuration.

**Observed:** the process prints `Jolt barrier returned; ran=1` and
`Destroying bridge`, then times out after five seconds. Only this owned probe
process was terminated by the test runner.

**Control:** an otherwise identical probe explicitly calls `JobSystem::WaitForAll`
before bridge destruction; it prints `Bridge destruction returned` and exits zero.
This isolates pending enki wrapper work as the cause. It is a diagnostic control,
not a recommendation to use global WaitForAll indiscriminately in physics code.

**Other source-confirmed windows:** on multi-threaded schedulers, destroying the
bridge after its counter reaches zero can free `m_EnkiTasks` while enki is still
finishing the task. Early indexed-task reuse violates enki's requirement that a
task is complete before resubmission. A multi-threaded UAF/reuse crash was not
reproduced in this review; do not present it as an observed crash.

**Repair direction:** make actual enki completion govern wrapper storage lifetime,
slot reuse and owner teardown. Track/drain the bridge's own submissions, including
wrappers for Jolt jobs that the barrier executed directly. Preserve forward
progress with one scheduler thread and do not execute unrelated physics work
under an inappropriate barrier. Add constrained-pool reuse and shutdown stress
regressions; a counter decremented from inside ExecuteRange is insufficient.

## 5. ECSJ-04: raw packed iteration includes tombstones

**Sources:** [ParallelECS](../../Engine/Scene/include/Axle/ParallelECS.hpp), lines
33-60; [EnTT view filtering](../../ThirdParty/entt/src/entt/entity/view.hpp).

The parallel ForEach path reads `view.handle()->data()` up to storage size. It
tests `view.contains(entity)` only when there are multiple component types.
For a single component with `static constexpr bool in_place_delete = true`,
EnTT retains tombstones in packed storage. Normal view iteration skips them;
the raw array path does not.

**Trigger:** create two entities with a pointer-stable component, remove one
component, then force the parallel branch with `minBatchSize=1`.

**Native evidence:** a fresh EnTT 3.14.0 registry and production ParallelECS header:

```text
sequential=1 parallel=2 invalidCallbacks=1
```

The callback checks entity/component validity without dereferencing missing data.
A component-reference callback could reach an invalid `view.get`, rather than
only receiving an extra entity callback.

**Impact limit:** inspected production components do not currently opt into this
storage policy, and product code primarily uses the hierarchy helper. General
ForEach/ForEachEntity exercise occurs in the milestone tests. This is a real
generic-helper bug, especially relevant to future plugin components, not evidence
that the current Transform iteration always returns invalid entities.

**Repair direction:** partition entities from valid view iteration or preserve all
required membership/tombstone filtering in the raw path. Test single/multiple
component views, stable storage, removals, empty components and both serial/parallel
branches against the same expected set. Document that callbacks must not perform
conflicting structural mutation during iteration.

## 6. ECSJ-05: products have different simulation ordering

**Sources:** [Editor update](../../Editor/src/EditorLayer.cpp), lines 1520-1566;
[Runtime update](../../Runtime/src/RuntimeLayer.cpp), lines 219-223;
[Scene runtime update](../../Engine/Scene/src/Scene.cpp), lines 982-1090;
[physics step](../../Engine/Physics/src/PhysicsScene.cpp), lines 57-63.

```text
Editor Play: scripts + vehicle input/readback -> physics Step
Runtime:     physics Step -> scripts + vehicle input/readback
```

Scene::OnUpdateRuntime combines pre-step controls with post-step pose/telemetry
readback. Neither product can place that combined call so both parts occur in
their proper phase. The current products place it on opposite sides of physics.
In Editor, readback can be from the previous step; in Runtime, script/control
changes can miss the current step. Editor one-step while paused advances physics
without calling the same runtime update path.

PhysicsScene forwards frame dt directly to Jolt. Editor simulation speed scales
the physics dt, while Scene::OnUpdateRuntime receives the original frame dt.
These are visible scheduling contracts that must be made explicit; this review
does not claim a measured numerical drift or determinism failure.

**Repair direction:** split PrePhysics controls, fixed physics step and PostPhysics
sync/telemetry; run one shared schedule in Editor and Runtime with explicit
pause/one-step/speed behavior. Keep the same ordering when moving Vehicle into
a plugin. A source move alone does not repair the current mismatch.

## 7. Other architectural limitations

These are design caveats, separate from the five prioritized findings:

- ParallelECS CommandBuffer protects its command vector, not the registry or
  execution phase. Its DestroyEntity calls raw `registry.destroy`, bypassing
  Scene script/physics/hierarchy cleanup. Current inspected uses are in tests;
  do not expose it as a complete scene destruction API without lifecycle hooks.
- `Scene::GetWorldTransform` is const but writes LocalMatrix/WorldMatrix through
  const_cast, including ancestor caches. Do not treat it as safe for arbitrary
  concurrent read-only queries. No current conflicting production call pair was
  established in this review.
- Scene's copy/destroy paths enumerate concrete components manually. The common
  Components header includes Vehicle/audio/VFX dependencies. Dynamic plugin
  component cloning and lifetime cannot be solved by EnTT registration alone.
- The scheduler singleton and several other services are in static libraries.
  Future DLLs must not embed independent copies while passing shared scene/physics
  objects across them. This is already addressed by the proposed plugin host gate;
  a new plugin DLL implementation was not tested here.
- The inspected milestone job/ECS tests rely heavily on C `assert`. Release builds
  defining NDEBUG remove these assertions. Their printed PASS text alone is not
  adequate Release correctness evidence; the review probes use active comparisons.

Healthy current boundaries should remain: one registry per scene, one Engine task
scheduler, CPU chunk work separated from GPU upload, and explicit waiting for
stack-allocated ParallelFor tasks. Avoid replacing EnTT/enkiTS to compensate for
missing ownership rules in their wrappers.

## 8. Native evidence and reproducibility

Machine-local artifacts are under `build/ecs-review-probe/` and are ignored by Git.
The probe compiles current production JobSystem.cpp, Log.cpp,
JobSystemEnkiTS.cpp, PhysicsEngine.cpp and ScriptCompiler.cpp; it includes the
production ParallelECS header. It links the existing matching Release Jolt/enkiTS/
spdlog libraries and imports the current generated include/definition configuration.
It runs without Editor, a window, renderer or project asset mutation.

Toolchain: MSVC 19.44.35228.0, Visual Studio 17 2022 generator, Windows x64,
Windows SDK 10.0.22621.0, Release. Compiler definitions match the inspected
Release physics/scene targets, including Jolt SIMD/profile settings and CRT mode.

Executed this session:

```powershell
cmake -S build/ecs-review-probe -B build/ecs-review-probe/out -G "Visual Studio 17 2022" -A x64
cmake -S build/ecs-review-probe/script-workspace -B build/ecs-review-probe/script-workspace/build -G "Visual Studio 17 2022" -A x64
cmake --build build/ecs-review-probe/out --config Release --parallel 4
python build/ecs-review-probe/run_probes.py external-thread stable-storage bridge-one-thread bridge-drained
python build/ecs-review-probe/run_probes.py script-callback
```

Both configure operations and the probe builds exited zero. Modes were run in
separate processes. Exit 1 below is the deliberately reported offending behavior,
not a passing engine test. The runner records child results and exits zero once
the observations are collected.

| Mode | Result | Local log |
| --- | --- | --- |
| external-thread | exit 1; callback ran on external thread | `build/ecs-review-probe/external-thread.log` |
| stable-storage | exit 1; one invalid callback | `build/ecs-review-probe/stable-storage.log` |
| bridge-one-thread | five-second timeout at bridge destructor; owned process killed | `build/ecs-review-probe/bridge-one-thread.log` |
| bridge-drained | exit 0 after explicit scheduler drain | `build/ecs-review-probe/bridge-drained.log` |
| script-callback | exit 1; successful fixture build callback on worker | `build/ecs-review-probe/script-callback.log` |

Build log: `build/ecs-review-probe/build.log`. Probe source/CMake/runner and
`results.json` remain machine-local diagnostic artifacts, not permanent regressions.
The command block is reproducible while those artifacts exist; a clean checkout
needs the fixture recreated from the contracts above or permanent regressions added
in an explicitly assigned fix task. A transient runner error while decoding timeout
output was corrected; the listed logs are from the completed corrected runs.

**Not run:** ThreadSanitizer/AddressSanitizer, native Editor hot-reload interaction,
concurrent DLL-unload crash, multi-threaded bridge reuse stress, physical GPU checks,
full historical milestone suite, packaged-player simulation parity or performance
benchmarks. No permanent source/test changes or production fixes were made.

## 9. Suggested correction order

1. Correct main-thread identity/queue contract (ECSJ-02), then marshal compiler
   completion and protect Editor/scene/DLL owner lifetime (ECSJ-01).
2. Correct actual enki completion, bridge task reuse and single-thread progress
   (ECSJ-03), with permanent native lifetime/constrained-pool regressions.
3. Repair generic view iteration for the actual supported EnTT storage policies
   (ECSJ-04), using always-active serial/parallel equivalence checks.
4. Define shared PrePhysics/Step/PostPhysics scheduling, pause/step/speed and
   structural-mutation barriers (ECSJ-05), then reuse that contract in plugin work.

These are recommended bounded hardening tasks, not newly released implementation
packages or a change to the user's rendering/plugin backlog priority.

## 10. Library references

Local vendored sources define the reviewed behavior. The current upstream
[EnTT ECS documentation](https://skypjack.github.io/entt/md_docs_2md_2entity.html)
explains registry concurrency and iteration constraints but describes a newer
release than the local 3.14.0. The
[enkiTS scheduler header](https://github.com/dougbinks/enkiTS/blob/master/src/TaskScheduler.h)
documents scheduler-thread, external-thread and completion contracts; compare
against the local header before applying APIs. Neither library automatically
provides Engine scene ownership, DLL lifetime or application phase scheduling.
