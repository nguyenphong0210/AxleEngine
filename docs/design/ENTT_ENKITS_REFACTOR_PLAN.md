# EnTT/enkiTS Refactor Plan

> **Execution:** follow [OPT-00..11](../ROADMAP.md). This document is a
> technical contract, not a separate phase queue. Old Phase/G labels and
> source-audit baselines are retained for traceability. Early Vehicle retirement
> is delivered; acceptance and new plugin implementation remain open.

Date: **2026-11-04**. Source baseline:
**84ad556678693c33039703801ea013d2d2686913**.
Status: **technical contract; EJ-00 assigned to Gemini under the
[initial OPT-01 handoff](../agents/handoffs/OPT_01_AF00_EJ00_GEMINI.md) on 2026-11-05;
implementation/review pending; EJ-01..17 remain unreleased**.

The user requests a detailed plan following the
[EnTT/enkiTS review](../architecture/ENTT_ENKITS_REVIEW.md). This task changes
documentation only at its original baseline. The current handoff releases EJ-00
fixtures and harness work only. Keep EnTT, enkiTS and Jolt.
No production fault fixes, Vehicle deletion, plugin implementation or vendor upgrade
are performed by this document. The [exchange](../agents/CODEX_GEMINI_EXCHANGE.json)
owns the next actual assignment.

This is the **Phase 42 ECS/jobs workstream**, retaining all EJ identifiers under
the [Architecture Foundation master plan](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md).
That later coordinator formalizes phase navigation and cross-track prerequisites;
clean architecture now precedes feature expansion. Phase numbers do not require
finishing every rendering feature before a thread/lifetime safety prerequisite.
Its accepted results become prerequisites at the relevant
[plugin gates](PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) and
[rendering seams](RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md). This domain plan's
internal dependencies do not replace the master's additional cross-track edges.

## 1. Outcomes and limits

The completed refactor must provide:

1. Explicit owner-thread identity and a main-thread queue that never runs Scene/UI
   work on an external worker or reenters it during a scheduler wait.
2. Script compilation as background work, with owner-validated completion and
   DLL/instance changes committed on the scene thread at a safe boundary.
3. Jolt wrapper storage whose lifetime and reuse follow actual enki completion,
   including forward progress with one scheduler thread.
4. Correct EnTT view iteration for supported storage policies, explicit structural
   mutation barriers and documented transform-cache ownership.
5. One tested fixed-step simulation schedule used by Editor Play and Runtime,
   with explicit frame callbacks, pause/step/speed and shutdown behavior.
6. Permanent Debug/Release regressions for the previously reproduced faults.

Do not replace the ECS, create a second task pool, build an automatic task graph
from arbitrary component accesses, rewrite the renderer, remove old Vehicle,
implement a plugin loader or add networking determinism as part of this track.
The new Vehicle remains separate PLG work. Old built-in Vehicle was retired
early in `fa74981`; `be7b071` removes the remaining compatibility data. Old-format
support is not required by the 2026-11-05 user clarification. Preserve
existing Character/audio/VFX/animation behavior through explicit phase adapters.

## 2. Findings to work mapping

The review records historical evidence. Do not relabel its fault observations as
fixed without rebuilt current-source regression results.

| Finding | Root problem | Packages that repair it | Required acceptance |
| --- | --- | --- | --- |
| ECSJ-01 / P1 | Compiler invokes scene/DLL/UI completion on its worker; raw owner can outlive detach | EJ-02, EJ-03, EJ-04 | THREAD-03, SCRIPT-01..04, LIFE-02 |
| ECSJ-02 / P1 | NO_THREAD_NUM becomes zero; external callbacks run inline | EJ-01, EJ-02 | THREAD-01..04 |
| ECSJ-03 / P1 | Jolt completion and an in-callback counter control enki task reuse/destruction | EJ-05, EJ-06, EJ-07 | BRIDGE-01..05, LIFE-01 |
| ECSJ-04 / P2 | Raw single-storage path visits tombstones | EJ-08 | ECS-01..03 |
| ECSJ-05 / P2 | Products put combined control/readback on opposite sides of physics | EJ-11, EJ-12, EJ-13, EJ-14 | SIM-01..06 |
| Review caveats | Registry command bypasses Scene cleanup; const transform query writes caches | EJ-09, EJ-10 | ECS-04..06 |

The last row is preventive ownership work, not an additional reproduced production
crash. Preserve that distinction in delivery reports.

## 3. Target ownership and threading rules

| Owner | Owns | Must not do |
| --- | --- | --- |
| Application runtime services | One scheduler, owner-thread identity, scheduler epoch and main-thread executor | Be statically duplicated into future plugin DLLs |
| Editor session | Compiler operation IDs, active scene identity, completion ownership and reload requests | Let worker callbacks capture/use unguarded EditorLayer pointers |
| Scene runtime | Registry, phase/mutation authority, simulation clock and physics adapter | Permit arbitrary worker structural changes or render-time simulation |
| Jolt bridge | Stable enki submission records, retained Jolt job refs and actual-completion tracking | Reuse a task because a Jolt slot is free or its body returned |
| Parallel batch | Prepared views/entity list and a joined group of disjoint data jobs | Outlive its view/storage or perform conflicting insertion/removal |
| Render/UI extraction | A completed read-only transform/telemetry snapshot or valid read phase | Mutate shared transform caches from an apparently const query |

An OS owner-thread check and permission to mutate a scene are separate facts.
enki can execute jobs on the main thread while it waits. Such execution does not
grant a job permission to reload scripts, pump UI or structurally mutate the scene.

Use a small execution/phase guard at supported Engine entry points. It checks
owner identity, scene/session generation, active parallel batches and phase.
Raw `GetRegistry()` access remains an explicitly advanced API with the same
caller obligations; adding a guard does not make arbitrary EnTT calls thread-safe.

The first implementation targets the current Windows x64 toolchain. Verify against
vendored EnTT 3.14.0/enkiTS/Jolt, not assumed APIs from a newer upstream release.

## 4. Thread identity, completion and scheduler lifecycle

### Identity and admission

Record the owning OS thread explicitly when application runtime services start.
`GetCurrentThreadIndex()` preserves an invalid/unregistered index; it never maps
it to main thread zero. Init/Shutdown run on the owner thread, and invalid/reentrant
lifecycle calls return an active diagnostic in Release as well as Debug.

Separate public contracts:

- Main-thread posting accepts ordinary external producer threads.
- Scheduler Dispatch/ParallelFor/Wait accept only the owner and registered scheduler
  execution contexts. Unsupported external callers receive a defined error rather
  than indexing scheduler arrays with NO_THREAD_NUM.
- If a real use case requires external scheduler participation, add an explicit
  RAII registration token using the local enki API. Do not register every arbitrary
  thread automatically or implicitly give it Scene ownership.
- Raw enki task submission remains an advanced caller-owned-lifetime interface;
  prefer the Engine's tracked owning task handle for asynchronous work.

Proposed lifecycle: `Stopped -> Running -> Quiescing -> Draining -> Stopped`, with
a new epoch at each start. Reject new application submissions after Quiescing.
Admission and active submitters must be synchronized so Shutdown cannot destroy
scheduler state between a successful admission check and actual enqueue.
Do not hold the admission lock while running a task, callback, wait or destructor.
Already admitted task graphs can complete their declared internal children under
their retained execution permit; rejecting those children must not break a wait.

### Deferred main-thread executor

Introduce an always-deferred posting API, illustratively:

```cpp
PostResult PostToMainThread(OperationOwner owner, Completion completion);
void PumpMainThreadCompletions(); // Owner thread, safe frame boundary only.
```

Names are proposed. Store copied/moved immutable results; callbacks carry a weak
operation/session token, not just a raw `this`. Invalidation occurs on the owner
thread before detach/project change. Owner identity, operation generation and
scheduler epoch are independent values and must all match where relevant.

Posting from the owner thread also enqueues. This prevents a completion becoming
an inline scene mutation when enki steals a task on main inside Physics Step.
Pump swaps one bounded FIFO batch under the queue lock, then releases the lock
before invocation. Work posted during that batch runs on a later pump, avoiding
recursive/unbounded draining. Define per-frame item/time limits and retain the
rest in order. Discard invalid-owner entries without dereferencing their captures.

Preserve explicit cross-thread producer publication order; no ordering guarantee
is invented for genuinely concurrent producers. Lifecycle-critical completion
cannot be silently dropped on queue capacity exhaustion: return a visible failure
and keep the operation result available for the owner's polling/recovery path.

Separate this pump from enki's `RunPinnedTasks`. Audit/migrate the old
`DispatchToMainThread` inline semantics; a temporary compatibility wrapper may
forward to deferred posting, with documented timing and tests. It is not a second
queue with different ownership behavior.

Queued callbacks also retain the code generation needed to destroy their callable
and captures. Invalidating a target does not make a DLL-defined callable destructor
safe after unload. Release/discard those captures under the same module lifetime
contract as execution. Owning async task records distinguish Prepared, Submitted
and Completed: `GetIsComplete()` is true on a never-submitted enki task and is not
by itself permission to retire a record that is still being published.

### Teardown

Freeze session producers and invalidate completion owners before clearing scenes.
Cancel/join compiler/process operations without requiring their completion callbacks
to execute. Drain admitted jobs while Scene/physics resources they need still exist.
Release scenes, script instances and deferred resource owners before unloading code.
Finish the scheduler's own completion accesses before freeing task storage or
unregistering Jolt types/factory. The final scheduler/Jolt/UI shutdown order follows
the verified ownership graph; a blanket late WaitForAll after resource destruction
does not repair that graph.

## 5. Compiler and reload contract

Replace mutable background reads of compiler settings with an immutable request:
workspace, target, configuration, output DLL, operation/session IDs and expected
script ABI/build identity. Worker code owns process execution/log collection only.
Protect process-handle/cancel publication; do not let Cancel race a closed/reused
Windows handle. Reuse the owned-process approach where appropriate without coupling
script compilation to Runtime export configuration.

Proposed operation states:

```text
Idle -> Compiling -> CompletedPendingDelivery -> Delivered/Discarded
                 -> Failed/CancelledPendingDelivery -> Delivered/Discarded
```

`IsCompiling` becoming false is not proof that an old callback has finished.
Recompile, project switch and detach must account for pending delivery explicitly.
Never join the compiler from its own completion worker; join from its owner.

The owner queues successful results for reload only at a barrier with no active
script calls, parallel scene jobs or iterator leases. First stage the new shadow
DLL and validate required exports/ABI/metadata while retaining the old module.
Metadata registration must target a staging registry, not overwrite the live global
ScriptRegistry merely by loading or validating the candidate.
EJ-04 establishes a minimal script ABI/build-identity handshake as part of candidate
validation; EJ-11 increments that ABI when adding virtual callbacks. This is a
script-module contract, not implementation of the general plugin SDK/loader.

Then capture supported reflected field state, detach all script instances/factories
that can reference the old generation, install the candidate metadata and rebind.
Track all affected edit/Play/preview scenes, not only whichever `m_Scene` is active.
No function pointer, callback, task, inspector or field destructor may outlive its
owning module. Script code may not create untracked long-running callbacks across
reload; reject reload or retain its generation until those owners are released.

Before the commit point, candidate failure leaves the old module/state active.
After the commit point, arbitrary user script side effects cannot be rolled back
perfectly. If rebinding fails, retain code lifetimes, stop the affected simulation
and expose a recoverable reload diagnostic; do not promise transactional reversal
of arbitrary gameplay. Never unload either module while still referenced.

Editor detach invalidates delivery owners, cancels/joins compilation, discards
pending results, stops instances and only then shuts down the reload manager.
The safe result channel must not require main-thread pumping while that owner is
blocked in join. Build failure changes diagnostics, not the live scene/DLL.

## 6. Jolt/enki bridge redesign

### Submission storage

Use stable submission records independent of Jolt's reusable job-slot index:

```text
SubmissionRecord
  stable EnkiTask object (no enki dependents in the initial design)
  retained Jolt Job reference until the wrapper finishes using it
  bridge/session ownership and admitted-submission identity
  submission/body-complete/enki-complete state for diagnostics
```

Do not put a growing vector of task objects in storage that relocates on append.
Use stable heap/pool addresses and a synchronized collection of record owners.
The bridge owns the record collection; any record backpointer is valid through
the bridge's final drain. Do not create a mutual strong-reference cycle between
the bridge and its records as a substitute for that ownership proof.
Publish a fully initialized retained record before scheduling, because enki may
execute synchronously when its queue is full. Handle admission failure by releasing
the queued job reference without leaking a half-submitted record.

ExecuteRange runs the retained Jolt job once and releases that job reference only
after it no longer accesses the job. Freeing/reusing a Jolt slot must not reuse the
enki wrapper record. The bridge/record itself stays alive after ExecuteRange returns
until the scheduler declares the wrapper complete.

In the vendored scheduler, `GetIsComplete()` with acquire ordering can establish
that the task is no longer touched by the no-dependent completion path. If enki
dependent nodes/completion actions are added later, their storage/lifetime requires
a separately proven completion contract; do not delete a dependency graph simply
because the producer count reached zero.

Retire completed records outside collection locks before invoking captured
destructors. Do not keep every completed submission until end of frame/bridge
destruction; bounded reclamation must operate during sustained physics workloads.
Jolt's own fixed job pool and the Engine's submission capacity are separate limits.

### Waiting and forward progress

`WaitForJobs` must satisfy Jolt's barrier and account for enki wrappers associated
with the completed work, including wrappers whose Jolt body the barrier ran itself.
Track admitted submissions/generations explicitly; do not call a global WaitForAll
as a substitute for per-bridge ownership. Outstanding jobs from another world or
unrelated producer do not belong to this wait.

Use owner/registered-thread waits that make progress with one scheduler thread.
No destructor busy-yield loop can depend on a nonexistent worker. enki WaitforTask
can execute other runnable tasks: verify the surrounding physics lock/reentrancy
contract before using it. All generic jobs must obey the no-direct-live-Scene/
PhysicsScene-access rule and deferred main-thread posting. Do not pump Editor/UI
callbacks from inside a physics wait.

EJ-06 is an explicit design/proof gate: demonstrate the chosen targeted wait or
controlled scheduling policy with the actual Jolt barriers/locks and unrelated
queued work. If isolation cannot be proven, introduce a constrained physics
execution lane/priority policy with tests, or adjust the adapter boundary. Do not
close the package based only on the single-thread example passing.

Dispose a live bridge on its owner outside an executing physics job. A non-owner
release must request owner-side disposal or return a defined error; it cannot free
storage underneath a scheduler call. Stop new bridge submissions before its final
drain. Finish or safely retain the entire record set before freeing its tasks.
Diagnostics/timeouts may detect a stuck job; they must not free a still-running
task and report successful shutdown. Arbitrary nonterminating jobs cannot be made
safely cancellable by forcibly deleting their records.

## 7. ECS iteration, mutations and transform ownership

### Correct iteration first

Make the initial parallel ForEach path materialize matching entity IDs through
normal EnTT view iteration on the owning thread, then partition that valid list.
This preserves filtering, tombstones and supported empty-component behavior.
Join all tasks before the list/view/storage can go out of scope. Prepare required
storage before dispatch, and forbid conflicting insertion/removal until join.

This adds temporary memory/collection cost; measure it after correctness. A later
fast path is allowed only for a documented storage policy with an equivalence test
and measured benefit. Do not assume every pool uses packed swap-and-pop deletion.
ForEachEntity must document uniqueness of input entities or deduplicate them before
parallel writes. Duplicate IDs cannot silently cause concurrent writes to one component.

Use explicit read/write declarations at scheduled system boundaries, not a general
reflection-based access inference system. Registry mutation from arbitrary callbacks
remains unsupported; typed command recording is the supported path.

### Scene-aware structural commands

Keep a low-level registry-only command buffer if tests/tools need it, but name and
document its lack of Scene lifecycle behavior. Add scene-aware destroy/add/remove/
reparent commands whose apply path uses Scene's cleanup/ownership rules.
They carry scene epoch and versioned entity handle/UUID identity as appropriate.
Reject commands for a different scene/generation even when numeric entity IDs match.

Apply only after batches join and active view leases end. A mutex on the command
vector is not a registry lock. Define per-producer sequence numbers and an explicit
merge order where reproducibility matters; arbitrary worker lock acquisition order
is not deterministic command order. Conflicts such as destroy-then-edit become
reported no-ops/errors according to a tested policy, not use-after-free.

Do not hold a component reference across recursive destruction or structural
changes that can relocate packed storage. Reacquire by entity identity at apply
boundaries. Verify hierarchy, script and physics cleanup on the scene-aware path.
Generic plugin component descriptors remain PLG work, but these lifecycle seams
must not be incompatible with that later extension registry.

### Transform caches

Separate cache mutation from read-only world-transform access. Owner-phase writes
or a joined transform batch computes LocalMatrix/WorldMatrix; extraction/camera/
audio readers consume a completed revision. Replace the current const-cast write
in `GetWorldTransform` with either a pure read of valid cache or an explicitly named
owner-only recomputation operation during migration.

Validate/discover the forest on the owner thread. Parallel subtree jobs receive
disjoint validated roots and immutable hierarchy topology. Cycles, shared children,
invalid parents or a parent without a transform need defined diagnostics/fallback,
not unchecked recursion or silent stale caches. Preserve inheritance/scale semantics.
Do not add per-component locks or rebuild the hierarchy per individual read query.
Initially recompute at declared write-to-read boundaries rather than depending on
dirty flags that current public mutable component references cannot reliably set.
Callers such as reparenting that require immediate current world state use the
explicit owner-only recompute operation; they must not receive an old cached matrix
under an undocumented claim that it is current.

## 8. Shared simulation timing

### Proposed clock policy

These defaults are design decisions for implementation, not current engine settings:

| Setting/state | Proposed behavior |
| --- | --- |
| Fixed simulation dt | 1/60 second, double-precision accumulator and integer tick counter |
| Maximum wall dt admitted per frame | 0.25 second; nonfinite/negative values rejected with diagnostics |
| Maximum fixed ticks per frame | 8, with explicit dropped-time/tick counters |
| Simulation speed | Validated finite range 0..4 initially; one source for scripts/physics/animation/VFX time |
| Running with speed zero | No gameplay update/ticks; presentation/UI still run |
| Pause | Freeze simulation; clear accrued backlog, preserve committed state |
| Single step | Exactly one full fixed tick while paused; independent of speed and wall delta |
| Resume/reset/scene switch | No paused-time catch-up; clock/epoch reset is explicit |
| Private previews | Independent clocks; not advanced by the active game scene scheduler |

For a running frame: admit clamped wall dt, multiply by speed once, accumulate,
then consume up to eight fixed steps. If excess whole steps remain, drop those
whole steps, retain the fractional remainder and report dropped simulation time.
Do not feed the same time again next frame or pretend all admitted time was simulated.
Compute the tick plan before frame gameplay callbacks. Define
`FrameSimulationDelta = admittedWallDt * speed - droppedWholeStepTime`; carry
the fractional accumulator separately. Use that effective interval for frame-based
gameplay/presentation, fixed dt for tick callbacks, and wall dt separately for
Editor UI/unscaled device housekeeping. Do not scale any of these values twice.

Define exact-boundary numerical handling in clock tests; do not hide an off-by-one
tick behind a large arbitrary epsilon. Compare expected tick counts independently
of physics floating-point tolerances. This is frame-partition consistency, not
cross-machine/network deterministic Jolt simulation.

### Frame and tick phases

```text
Frame boundary:
  Pump validated completions -> apply scene commands -> collect input snapshot/plan ticks
  -> frame gameplay OnUpdate (when running, simulation-frame dt)
  -> zero or more complete fixed ticks
  -> presentation OnLateUpdate -> final world transforms/listener/UI extraction
  -> existing render/UI/present path

Each fixed tick:
  Apply tick commands -> OnFixedUpdate/control preparation
  -> Vehicle/Character pre-physics adapters -> Jolt Step(fixed dt)
  -> complete bridge submissions -> rigid-body/chassis/wheel synchronization
  -> telemetry/animation/VFX tick work -> world-transform completion
  -> publish tick state and queued presentation events
```

One simulation owner advances each system exactly once for its declared clock.
Audio device pumping stays frame-based; simulation-driven audio events/telemetry
are collected from fixed ticks and consumed without replaying them every render.
Presentation cameras/HUD and listener placement see final post-physics transforms.
Authoring previews keep their declared independent clocks. No new rendering
interpolation is required in this refactor; render the latest committed state.

Current ScriptableEntity exposes only OnUpdate. Preserve that frame callback's
role, add explicit OnFixedUpdate and OnLateUpdate contracts, and migrate the
known physics-driving versus camera/HUD scripts deliberately. Adding virtuals
changes the C++ script ABI: rebuild all script modules, version the ABI handshake
and reject old DLLs before using their vtables. Do not load an old AxleScripts.dll
against a new ScriptableEntity layout or silently call old OnUpdate N times/frame.

Held input applies to all applicable ticks. Edge input is consumed once by its
intended tick; frames with no tick retain pending game edges within a bounded
queue. Paused UI control events are not replayed as gameplay inputs on resume.
Single-step uses explicit latched game controls and performs the full post-step
sync, not just `PhysicsScene::Step`. Catch-up ticks do not multiply one key press.
While paused, frame OnUpdate is suspended; OnLateUpdate may observe the frozen
state with simulation dt zero. A requested single-step runs OnFixedUpdate and the
whole tick, then late presentation receives that one fixed interval. It does not
replay a frame OnUpdate or alter the paused state.

Frame OnUpdate cannot structurally mutate a registry being iterated; commands are
applied at the next declared barrier. For physics-driving code use OnFixedUpdate.
Document the one-frame versus tick command semantics and migrate sample controllers
accordingly. Do not implement root motion or new animation authoring in this task.

## 9. Packages, dependencies and gates

| Gate | Packages | Completion boundary |
| --- | --- | --- |
| G0 | EJ-00 | Permanent always-active fault fixtures and captured baseline |
| G1 | EJ-01..04 | Correct thread identity, deferred completion and safe script reload/teardown |
| G2 | EJ-05..07 | Correct bridge/task lifetime, forward progress and scheduler shutdown |
| G3 | EJ-08..10 | Valid parallel ECS iteration, scene-aware mutations and transform ownership |
| G4 | EJ-11..14 | Shared phase/clock implementation used by both products |
| G5 | EJ-15..17 | Stress/parity evidence, plugin/RHI seam agreement and maintained docs |

| Package | Depends on | Primary output |
| --- | --- | --- |
| EJ-00 | none | Baseline, permanent fixture targets and failure probe |
| EJ-01 | EJ-00 | Owner-thread identity and scheduler admission lifecycle |
| EJ-02 | EJ-01 | Deferred owner-scoped main-thread executor |
| EJ-03 | EJ-02 | Immutable compiler requests/results and owner delivery |
| EJ-04 | EJ-03 | Safe script reload barrier, module leases and detach cleanup |
| EJ-05 | EJ-00, EJ-01 | Stable Jolt submission records independent of job slots |
| EJ-06 | EJ-02, EJ-05 | Targeted bridge waits and single-thread/reentrancy proof |
| EJ-07 | EJ-04, EJ-06 | Asynchronous handles and complete shutdown ordering |
| EJ-08 | EJ-00, EJ-01 | Correct parallel view iteration and unique entity batches |
| EJ-09 | EJ-02, EJ-08 | Scene-aware structural commands and mutation barriers |
| EJ-10 | EJ-08, EJ-09 | Transform write/read phases and validated subtree batches |
| EJ-11 | EJ-04, EJ-06, EJ-09, EJ-10 | Shared runtime phase extraction and script callback ABI |
| EJ-12 | EJ-11 | Fixed clock, input consumption and pause/step/speed policy |
| EJ-13 | EJ-07, EJ-12 | Editor integration through shared runtime driver |
| EJ-14 | EJ-12, EJ-13 | Standalone integration and removal of duplicate step paths |
| EJ-15 | EJ-07, EJ-08, EJ-10, EJ-14 | Stress, fault-injection and product-parity evidence |
| EJ-16 | EJ-04, EJ-07, EJ-10, EJ-14 | Accepted plugin/RHI seam crosswalk |
| EJ-17 | EJ-15, EJ-16 | Final guide, evidence and obsolete-adapter retirement |

Execute in this order by default. The table describes technical prerequisites,
not authorization to start all packages or run concurrent writers.

### EJ-00 - Permanent baseline and meaningful harness

- Promote the four fault probes and bridge drain control from the review's ignored
  build directory into maintained tests that call production APIs.
- Use active checks in Debug/Release and isolate hang/crash cases in subprocesses
  with an owned watchdog. Add an intentional failure probe proving the harness fails.
- **Files:** `Tests/CMakeLists.txt`, proposed `Tests/src/JobsECSTestMain.cpp`, focused
  job/ECS/script/bridge fixtures and a baseline evidence report.
- **Verification:** rebuild the new target; capture current expected failures
  separately from normal passing CTest registration. Keep every discovered fault visible.
- **Done:** no reliance on machine-local probe files or disabled Release assertions.

### EJ-01 - Thread identity and lifecycle admission

- Preserve invalid enki indices, establish the OS owner, validate scheduler entry
  contexts and synchronize submit-versus-shutdown admission.
- Define stopped/reinitialized epoch behavior; remove accidental zero-thread identity
  fallback. Separate total scheduler threads from worker-count diagnostics.
- **Files:** `Engine/Core/include/Axle/JobSystem.hpp`, its implementation,
  Application runtime setup and the thread/lifecycle fixtures.
- **Verification:** owner/enki worker/external thread identities, Init(1/2), invalid
  external scheduler calls, admission races and reinitialization all have defined results.
- **Done:** ECSJ-02 identity fault is fixed; main delivery is completed by EJ-02.

### EJ-02 - Deferred main-thread executor and owner tokens

- Implement always-deferred owner-scoped posting/pumping, generation invalidation,
  bounded FIFO batches and explicit rejection/recovery on submission failure.
- Keep enki pinned work distinct; no implicit inline Scene callback during an enki
  wait. Audit/migrate existing DispatchToMainThread callers and compatibility timing.
- **Files:** JobSystem or a small Core executor source/header, Application frame
  boundary, operation-owner utility and executor tests.
- **Verification:** external and owner posts wait for the safe pump; nested posts
  run in later batches; cancelled/destroyed owners and previous epochs never execute.
- **Done:** ECSJ-02 callback reproduction passes with active thread assertions.

### EJ-03 - Compiler request/result handoff

- Snapshot compile settings, own/cancel the process safely and deliver results
  through the new executor or explicit owner polling. Remove raw worker-to-UI callbacks.
- Track pending delivery separately from process completion and keep old operation
  IDs from updating a new project's notifications or triggering reload.
- **Files:** ScriptCompiler header/source, EditorLayer compile integration and
  process/completion fixtures; reuse bounded process tooling where justified.
- **Verification:** success/failure/cancel completion runs on owner; request edits,
  repeat compile and detach with pending result do not race handles or call stale owners.
- **Done:** the ECSJ-01 callback-thread fault is fixed without moving build work to UI.

### EJ-04 - Reload commit and scene/DLL teardown

- Stage candidate exports/metadata, enter a scene/script barrier, preserve field
  state and retain module generations through all live objects/callbacks/tasks.
- Cover edit, Play and private scenes; invalidate/join the compiler before Editor
  detach and project/scene switches. Define recoverable failure at each reload stage.
- **Files:** ScriptHotReloadManager, ScriptRegistry, Scene reload hooks,
  EditorLayer teardown and real native script-DLL fixtures.
- **Verification:** actual script generation swap, missing export/failed load,
  compile then close/switch, active instance callbacks and destructor-before-unload ordering.
- **Done:** ECSJ-01 owner/DLL risks have current native evidence, not just a queue mock.

### EJ-05 - Stable physics submission lifetime

- Replace the indexed reusable EnkiTask array with stable owned records; distinguish
  Jolt job refs, wrapper body completion and actual enki completion.
- Add safe concurrent publication/reclamation, queue-full synchronous execution
  handling and explicit capacity behavior without depending on the old active counter.
- **Files:** JobSystemEnkiTS header/source and native bridge/pool fixtures.
- **Verification:** constrained Jolt pool reuse, task-address lifetime, concurrent
  queueing and scheduler completion occur before record release; track allocation high-water.
- **Done:** freeing a Jolt slot cannot reuse or destroy a live enki task.

### EJ-06 - Physics waits and forward progress

- Implement bridge-owned wait/drain accounting, including barrier-executed bodies.
  Prove scheduling/reentrancy under current Jolt locks before selecting a wait policy.
- Remove the destructor yield loop and prohibit non-owner/in-job disposal; never
  replace it with unrelated global WaitForAll or forced deletion after timeout.
- **Files:** JobSystemEnkiTS waits/destructor, PhysicsScene boundary checks,
  scheduler policy adapters only as needed and native wait fixtures.
- **Verification:** Init(1), two/many threads, empty jobs, dependent jobs, queued
  unrelated work, two worlds and shutdown; watchdog catches real deadlocks.
- **Done:** ECSJ-03 reproduction passes and no new nested physics/reentrancy deadlock.

### EJ-07 - Handles, draining and shutdown

- Make owning asynchronous handles and admitted task graphs retain scheduler state
  until complete. Keep raw caller-owned enki-task APIs explicitly constrained.
- Order producer invalidation, compiler join, scene/physics jobs, deferred owners,
  task reclamation, script modules and final scheduler/Jolt shutdown coherently.
- **Files:** JobSystem handles/lifecycle, Application, Editor/Runtime detach,
  PhysicsScene ownership and teardown fixtures.
- **Verification:** pending task/callback at close, completion racing quiescence,
  repeated startup/shutdown and destructor order, with no use of freed scheduler state.
- **Done:** shutdown is proven against actual completions and valid service lifetimes.

### EJ-08 - Parallel EnTT view correctness

- Partition IDs from valid view iteration initially; prepare storage, define empty
  component/const access behavior and require unique ForEachEntity write targets.
- Preserve callback signatures and synchronous join; structural changes are deferred.
- **Files:** `Engine/Scene/include/Axle/ParallelECS.hpp` and focused EnTT tests.
- **Verification:** serial/parallel set equivalence after removals with packed/stable
  pools, single/multiple components, empty components, sparse matches and duplicate input IDs.
- **Done:** ECSJ-04 tombstone reproduction passes without weakening callback validation.

### EJ-09 - Scene command and mutation boundaries

- Separate registry-only helpers from Scene lifecycle commands, validate scene/entity
  generations and define merge/conflict rules. Apply after all relevant batches join.
- Route destroy/reparent/add/remove through coherent script/physics/hierarchy cleanup;
  avoid holding packed component references across recursive mutations.
- **Files:** ParallelECS command utility, Scene/Entity mutation seams, proposed small
  SceneCommandBuffer and lifecycle fixtures.
- **Verification:** parent/child destroy, live script/body cleanup, stale command,
  destroy-then-edit, concurrent recording and wrong-scene numeric entity collisions.
- **Done:** a thread-safe command vector is no longer mistaken for a safe Scene mutation API.

### EJ-10 - Transform evaluation ownership

- Replace const-query cache writes with explicit write/recompute versus read APIs;
  publish a completed transform revision before parallel readers/extraction.
- Validate hierarchy forests and preserve inheritance behavior; use disjoint stable
  subtree jobs and a joined barrier instead of per-node locks.
- **Files:** Scene/Entity transform access, ParallelECS hierarchy helper,
  required camera/audio/render query callers and transform tests.
- **Verification:** serial/parallel transforms, inheritance/scale, invalid topology,
  parent removal, read-only queries and scene-switch revision invalidation.
- **Done:** transform readers cannot silently race ancestor cache writes.

### EJ-11 - Extract shared runtime phases and script API

- Split Scene's combined update into frame scripts, fixed controls, physics,
  post-step sync/telemetry, animation/VFX and late presentation operations.
- Add explicit script callbacks/ABI validation, migrate known controllers versus
  camera/HUD scripts and retain temporary Character phase adapters. Do not restore
  retired Vehicle adapters or old-format support.
- **Files:** Scene/ScriptableEntity, script module ABI/registry/build integration,
  Character call sites, explicit current-format sample/fixture scripts as required.
- **Verification:** recorded phase trace, same-tick controls/post-step poses, callback
  counts and rejection of an old ABI DLL; all changed scripts rebuild before use.
- **Done:** independently testable phases with no duplicate system update owner.

### EJ-12 - Fixed clock, inputs and pause/step

- Implement the declared fixed accumulator, overload counters and validated speed;
  capture/consume input consistently across zero/one/multiple ticks.
- Define complete paused single-step and separate private-preview/UI clocks.
- **Files:** proposed small Scene runtime clock/driver, input snapshot adapter,
  phase orchestration and clock/phase tests.
- **Verification:** 30/60/144 Hz frame partitions, exact boundaries, zero/negative/
  nonfinite dt, overload cap, speed 0/0.5/1/2/4, pause/resume/step/reset and input edges.
- **Done:** timing is a shared testable contract; physics tolerance is separate from tick counts.

### EJ-13 - Editor cutover

- Make Editor Play/Pause/Step use the shared driver; remove its standalone physics
  step and speed multiplication. Preserve selection, edit-scene restoration and tools.
- Pump validated completions at the agreed frame boundary; keep private asset/VFX/
  assembly previews on their own clocks and ensure GPU owners stay valid.
- **Files:** EditorLayer header/source, toolbar/state integration, Application boundary
  where needed, Editor/headless ImGui and native interaction fixtures.
- **Verification:** Play/Pause/Step/Stop/speed/reset, script compile while interacting,
  scene/project switch and unaffected private preview controls; native UI evidence separate.
- **Done:** Editor cannot bypass the shared tick or mutate Scene from a compile worker.

### EJ-14 - Runtime cutover and product parity

- Make Runtime call the same driver/phase policy, remove reversed script/physics
  ordering and keep input/camera/UI extraction after the proper boundaries.
- Retire temporary duplicate step paths; invoke the exact target Runtime build
  rather than borrowing an old executable from a previous export.
- **Files:** RuntimeLayer header/source, shared driver adapters and parity/player fixtures.
- **Verification:** same scene/input/tick trace and callback counts as Editor mode;
  physics/control states compared with documented tolerances at matching ticks.
- **Done:** ECSJ-05 is repaired in both code paths; one-sided Editor success is insufficient.

### EJ-15 - Stress, failure and regression acceptance

- Run the matrix below in Debug/Release, using actual DLLs, Jolt and product drivers.
  Add fault injection for queued completions, malformed inputs and partial reloads.
- Use bounded owned subprocess watchdogs; record sanitizers only where the toolchain
  supports them. Neither timeout nor unavailable instrumentation counts as PASS.
- **Files:** maintained Tests fixtures, optional sanitizer configuration and an evidence report.
- **Verification:** repeated compile/close, constrained bridge slots, thread counts
  1/2/hardware, batch removals and 30/60/144 Hz product partitions with fixed inputs.
- **Done:** every mandatory functional row has reproducible current evidence and honest limits.

### EJ-16 - Plugin and rendering seam agreement

- Cross-map accepted owner tokens, task handles, Scene mutation and simulation phases
  to PLG/P41 APIs. Export shared accessors later; do not create DLL hosts in this package.
- Reuse accepted renderer retirement/frame contracts without inventing competing
  resource leases or moving Jolt scheduling into a renderer.
- **Files:** integration reference/guide updates and only minimal exercised adapter
  changes justified by accepted packages; production DLL implementation remains PLG scope.
- **Verification:** dependency/include/lifetime review, no new singleton or scheduler,
  no second sim clock and no Runtime dependency on Editor tooling.
- **Done:** future Vehicle can register to these phases without redoing this refactor.

### EJ-17 - Documentation and final delivery

- Update implemented job/ECS/scripting/simulation/build guides and link exact APIs,
  defaults, breaking script ABI changes and input/clock semantics.
- Remove temporary duplicate adapters after caller migration; preserve historical
  review results as historical and record current fixes separately.
- **Files:** maintained guides/architecture, roadmap and exchange; delivery report.
- **Verification:** documentation/links agree with built targets and actual results;
  clean explicit staging, coherent local commits and no push unless requested.
- **Done:** no pending mandatory acceptance hidden by a completed checklist.

## 10. Acceptance matrix

| ID | Scenario | Required result |
| --- | --- | --- |
| THREAD-01 | Main, enki worker and external thread identity | Real owner only is main; invalid index stays invalid |
| THREAD-02 | External/owner posts, nested posts | Deferred safe-boundary delivery and bounded batch semantics |
| THREAD-03 | Compiler completion and stale owner | Owner-thread UI/reload or explicit discard; no raw stale owner access |
| THREAD-04 | External scheduler calls/shutdown admission | Defined rejection or explicit registration; no invalid scheduler indexing |
| SCRIPT-01 | Real DLL generation swap | All old instances/callbacks destroyed before unloading old code |
| SCRIPT-02 | Candidate missing/bad exports/ABI | No old-scene destruction before validation; actionable diagnostics |
| SCRIPT-03 | Compile then close/switch/recompile | No callback into old scene/Editor and no worker self-join |
| SCRIPT-04 | Candidate bind failure after commit | Safe stopped state with retained code lifetimes, no false rollback claim |
| BRIDGE-01 | Init(1), Jolt barrier body already done | Wrapper drains and bridge destruction completes |
| BRIDGE-02 | Constrained pool and rapid reuse | Jolt slot reuse never reuses live enki record storage |
| BRIDGE-03 | Queue-full/nested submissions | Synchronous execution publication/lifetime is safe |
| BRIDGE-04 | Two worlds and unrelated queued tasks | Targeted ownership; no wrong-world/reentrant Scene mutation |
| BRIDGE-05 | Sustained workload | Completed records reclaimed; bounded live high-water, no leaked job refs |
| LIFE-01 | Pending tasks and scheduler shutdown | No free before actual scheduler completion; epochs cannot cross |
| LIFE-02 | Compiler/Scene/module teardown | Invalidated delivery, joined worker and valid destructor order |
| ECS-01 | Packed/stable single/multiple component pools | Exact valid-entity set matches sequential behavior |
| ECS-02 | Empty/const/sparse views and removals | Supported signatures work with no missing component access |
| ECS-03 | Repeated ForEachEntity input ID | Defined dedup/rejection; no concurrent duplicate write |
| ECS-04 | Scene command lifecycle | Hierarchy/script/physics cleanup once; stale/wrong-scene commands rejected |
| ECS-05 | Transform writes and parallel reads | Completed revision; no implicit const cache mutation |
| ECS-06 | Invalid/disjoint hierarchy batches | No recursion loop/shared-child write race; explicit diagnostics |
| SIM-01 | Shared phase trace | Controls before Step and all body/wheel telemetry readback after Step |
| SIM-02 | Frame partitions 30/60/144 Hz | Equal scheduled tick/callback/input counts for equal admitted simulation |
| SIM-03 | Pause/Step/resume/speed zero | No backlog; one full requested tick; no game update at zero speed |
| SIM-04 | dt validation and overload | Declared clamp/drop/remainder behavior and diagnostic counters |
| SIM-05 | Input edges and catch-up ticks | One consumption; paused UI events never replay as game inputs |
| SIM-06 | Actual Editor/Runtime fixture | Matching new tick contract/state within declared physics tolerance |
| REG-01 | Existing independent suites | Relevant project/material/VFX/animation/physics checks remain meaningful |
| REG-02 | Default build separation | Editor/default excludes tests and Runtime; explicit targets remain available |

Use event latches/barriers to force lifetime interleavings rather than relying on
random sleeps. Assert happens-before/lifetime order and owner IDs, not an exact
thread scheduling order. Keep failure probes active under NDEBUG. An automatic
mock callback test cannot substitute for the real DLL teardown rows.

## 11. Build, evidence and measurement

Proposed new targets: `AxleJobsECSTests`, a separate native script/bridge integration
target if needed, and CTest label `ecs-jobs`. These names are **not available yet**.
EJ-00 establishes the actual targets under Tests as EXCLUDE_FROM_ALL, using immutable
small fixtures and local writable workspaces. Include them in AxleAllTests without
adding Editor/Runtime as transitive test dependencies.

Future command examples after target implementation:

```powershell
cmake --build build --config Debug --target AxleJobsECSTests --parallel 6
ctest --test-dir build -C Debug -L ecs-jobs --output-on-failure
cmake --build build --config Release --target AxleJobsECSTests --parallel 6
ctest --test-dir build -C Release -L ecs-jobs --output-on-failure
cmake --build build --config Release --target AxleEditor AxleRuntime --parallel 6
```

The last command deliberately builds both products for acceptance; it does not
change default build behavior. Test fixtures may invoke the actual Editor/Runtime
as opt-in external runs after they are built, rather than making CPU tests build them.
Do not run this block during the current documentation-only task.

Report baseline and after-change allocation/task high-water, queue depth/latency,
wait time and per-phase/tick counts with machine/config/workload. Correctness comes
first; no speedup is assumed. In particular, valid-ID collection may cost more than
the old raw pool loop, and stable submission records need measured reclamation costs.
Use bounded representative scenes, not a giant synthetic test that misses ownership.

Sanitizers and profilers are supplemental evidence. Windows MSVC tooling does not
automatically provide a full data-race detector; do not label a build race-free
because ASan found no memory error. A Linux/Clang/TSan run, if added and supported,
must be separately configured/executed and never inferred from Windows results.

## 12. Plugin/rendering crosswalk and sequencing

| Existing package/seam | Refactor prerequisite or shared result |
| --- | --- |
| PLG-00 baseline | Reuse review plus EJ-00 results; rebase against current source |
| PLG-05/06 shared hosts/loader | EJ-01..04 and EJ-07 ownership/completion contracts before moving state across DLLs |
| PLG-07 scene extensions | EJ-08..10 mutation/storage/transform rules; preserve plugin-owned destructor lifetime |
| PLG-09/15 scheduling/new Vehicle | EJ-05..07 and EJ-11..14 accepted bridge and fixed runtime driver |
| PLG-11/12 build/restart | EJ-02..04 owner-scoped operations and detach, extended for plugin restart |
| PLG-21 scripts | Reuse script ABI/result/reload contract; add product-specific DLL/static linkage there |
| P41 worker/frame/resource seams | Reuse owner/drain and accepted GPU retirement contracts; completion tokens stay distinct |
| P41-19 Runtime integration | One simulation driver; rendering integration cannot reverse script/physics order |
| P41 animation/extraction seams | Declare update clock and read completed transforms; avoid a second animation/VFX owner |

Metadata discovery and unrelated rendering work need not wait for this entire
track. Required ownership gates must pass before their risky consumer is enabled.
When P41 or PLG already supplies an accepted equivalent, adapt/reuse it and record
the evidence instead of implementing a competing API. Keep overlapping source
ownership in the exchange; no implicit whole-phase release or parallel writer.

Follow the [optimization roadmap](../ROADMAP.md): EJ-00 establishes permanent
fixtures in OPT-01; EJ-01..07 then close the P1 thread/compiler/reload/bridge
hazards in OPT-02 before broad ECS/simulation changes in OPT-03.
Do not combine every P1 repair and fixed-step behavior change into one commit.

## 13. Migration and stop conditions

| Condition | Response |
| --- | --- |
| A reproduced fault no longer appears at a newer baseline | Recheck actual source/evidence; do not manufacture the old failure |
| Bridge wait can execute unsafe unrelated work under Jolt locks | Stop EJ-06 cutover and prove isolation; passing Init(1) alone is insufficient |
| A DLL instance/callback has unknown ownership | Retain its code generation; block unload and add the missing owner contract |
| New virtual callbacks mismatch an old script DLL | Reject before binding; rebuild scripts as part of the same accepted package |
| New clock changes controller/camera behavior | Classify/migrate callbacks and compare tick traces; do not hide it as a pure move |
| Refactor starts implementing generic plugin reflection/loader | Keep only exercised seams here and hand off the remaining work to PLG |
| Native UI/player/sanitizer prerequisites unavailable | Mark the affected evidence NOT RUN and leave its acceptance open |

Keep each accepted package runnable and locally committed with explicit staging.
Use normal commits/reverts for recovery; preserve authored files and other agents'
work. Do not maintain two permanent scene clocks or silently fall back to unsafe
legacy scheduler behavior if new validation rejects a call.

## 14. Planning verification boundary

This document is based on the inspected source and historical native reproductions
in the review. Its proposed APIs, defaults, targets and algorithms have not been
compiled or benchmarked. The documentation task validates links, package/finding
coverage, dependency acyclicity, gate ranges and code-fence/example consistency.
Implementation acceptance requires the rebuilt permanent tests and actual product
checks above; neither the plan nor an old successful control run fixes current code.
