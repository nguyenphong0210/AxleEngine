# OPT-02 and OPT-03 independent review and corrections

Reviewer: **Codex**. Date: **2026-11-07**. Review baseline: `0eeb734`.
Scope: Gemini's EJ-01..EJ-14 and AF-02..AF-04 deliveries, including authorized
repairs. Later OPT packages, old-format compatibility and retired Vehicle work
are outside this assignment. The checkout was clean when review began.

Decision: **ACCEPTED after verified Codex corrections** for OPT-02 and OPT-03.
The independent review found **12 groups of defects (9 P1, 3 P2)** in Gemini's
baseline; all findings below are FIXED. Gemini's execution reports remain
historical implementer evidence. The repairs and current-source gates below are
Codex-authored and Codex-verified; no separate post-fix independent review is claimed.

Implementation commits:
- `e351399ba08874019a6421fb877113c878423f96`: scheduler, completion, compiler process-tree and physics lifetime repairs.
- `3a671163abc6cbbfc0ed02630dc20578f6cdc80b`: real SceneData boundary, scene/module ownership, clock/input and product simulation corrections, with permanent regressions.

These commits are local; no push was performed.

## Findings and corrections

| ID | Severity / packages | Source-confirmed problem | Correction and regression |
| --- | --- | --- | --- |
| OPT23-R1 | P1 / EJ-01, EJ-02, EJ-07 | Submission held `AdmissionMutex` across enki enqueue, which can execute user code synchronously when full. Nested submission could deadlock. An async task entered reclamation before publication, and task/queue captures could be destroyed under locks. The owner-only completion pump also allowed recursive or in-task invocation. | Count in-flight publication without holding the mutex during execution; retain a submitted flag through actual completion; retire captures outside locks; separate pinned work from the bounded, non-recursive safe-boundary completion pump. Queue-saturation, nested producer, pump and capture regressions exercise these rules. Cancellation now uses one atomic pending/running/cancelled transition. |
| OPT23-R2 | P1 / EJ-05, EJ-06 | Single-thread physics used unrelated global `WaitForAll`; a two-second drain timeout was ignored before task storage destruction. Worker occupancy also needed an isolated forward-progress path. Exhausting the Jolt job pool spun indefinitely. | Run ready physics jobs synchronously without a dedicated worker. Reserve enki's high priority lane for physics wrappers; normal submissions use the low lane, and owner waits execute only physics wrappers. Reclamation requires both completed publication and actual enki completion. Shutdown retains storage until drained; invalid disposal is diagnosed before destruction. Recover transient capacity held by queued enki wrappers after Jolt has already finished their barrier. Genuinely held/exhausted capacity returns an invalid handle with an active diagnostic. Regressions include one thread, two worlds, an occupied worker, constrained capacity and the existing bridge harness. |
| OPT23-R3 | P1 / EJ-03 | Compiler completion ignored queue rejection; the offline path ignored owner invalidation. Joining compilation pumped unrelated callbacks. Cancellation terminated the immediate process without owning its descendants. | Retain an exactly-once owner-pollable result, validate tokens in both delivery paths, restrict join/start to the operation owner, and poll from the Editor's frame boundary. Launch CMake directly, assign its suspended process to a kill-on-close Windows Job Object before resume, and cancel the process tree. The compiler fixture fills the executor, invalidates an offline owner, and cancels after a real descendant reports readiness. |
| OPT23-R4 | P1 / EJ-04, EJ-11 | Reload only rebound the supplied active scene, leaving edit/preview factories pointing into an unloaded DLL. Candidate metadata was not validated; post-commit factory failure lacked a safe stopped state. Deferred scene commands could retain old module callables. | Register every concrete Scene with the reload manager; validate ABI, compiler/configuration identity, metadata names, counts and destroyers before touching the active generation. Drain admitted jobs and discard old scene commands while code is loaded. Destroy old instances and metadata before unload; rebind all edit/Play/private scenes. A bind failure retains the committed module and faults/stops simulation. Reject external initialization/reload/shutdown before reading or changing module state. Real DLL fixtures verify field preservation, destructor ordering, rejection and failure recovery. Script ABI is now **4**; rebuild script modules. |
| OPT23-R5 | P1 / EJ-13, EJ-14 | Both products synchronized `SetPaused` every frame, clearing fractional time even while running. Editor Step was cleared by the same synchronization. Play from Pause cloned a new world, and edit/Play scenes shared body IDs/world state. | Pause/resume are idempotent transitions; Step survives per-frame state synchronization and is independent of wall dt/speed. Resume preserves the current runtime scene. Editor Play owns a separate physics world, copies authored body values without runtime IDs, and restores the original edit world on Stop. Opt-in native probes call the actual product `OnUpdate` paths at 30/60/144 Hz and check input, callback counts, poses, pause, step, resume, zero speed and restoration. |
| OPT23-R6 | P1 / EJ-11, EJ-12 | Tick input never sampled keyboard/button state in production. The script DLL kept the hardware context while the host switched to tick snapshots. Short taps erased their own press edges, and the migrated compiled Character controller/camera still ran in both old and new callbacks. | Poll supported hardware keys/buttons, retain press/release/scroll edges across zero-tick frames, consume them once, preserve cursor/viewport delegation and restore input scopes on unwind. Synchronize the DLL at callback boundaries and expose tick edge queries. Remove duplicate `OnUpdate` execution from the compiled ThirdPerson controller/camera fixtures. Retired Vehicle fixtures remain excluded. |
| OPT23-R7 | P2 / EJ-12 | Gameplay frame callbacks received raw dt, including speed zero and invalid dt; overload behavior did not implement the declared wall-time/eight-tick policy. Speed was unbounded and single-step depended on positive wall dt. | One validated, scaled effective frame interval feeds gameplay/presentation. Admit at most 0.25 seconds, execute at most eight ticks, retain fractional remainder and account for dropped wall/simulation time and ticks. Validate speed 0..4, retain numerical boundary debt and cap the roundoff allowance even in long sessions. Explicit paused steps bypass wall dt validation. |
| OPT23-R8 | P1 / EJ-09 | Scene commands did not enforce an owner/iteration boundary; entity handles could become valid again after registry Clear. Component removal/replacement bypassed native-script and physics teardown. | Entity handles retain a weak scene lifetime and reset epoch. Supported structural APIs reject external/in-task/iterating mutations; command recording remains available to workers. Commands call lifecycle-aware Entity/Scene seams, with physics creation completed after the mutation batch. Tests cover stale handles, wrong scenes, concurrent recording, native script teardown, body removal/replacement and scene destruction. |
| OPT23-R9 | P2 / EJ-10 | Repaired parent links left stale/duplicate child lists, permitting overlapping subtree writers. Transform traversal silently stopped at depth 128, recompute disagreed with scale inheritance, and physics children used stale parent caches during readback. Edited/deserialized data did not always publish caches before consumers read them. | Parent links are authoritative; rebuild inverse child links before disjoint jobs, evaluate subtrees iteratively, and apply the same inheritance rules in targeted ancestor-path recompute. Reparenting does not schedule the entire forest for each edit; the 3,500-node integration fixture exercises batch construction. Physics readback runs root-to-leaf and honors scale inheritance against the current parent pose. Complete transforms at load, edit update and post-step boundaries. Tests include malformed inverse links, deep trees, moving parent/child bodies with both inheritance modes, serial/parallel equivalence and concurrent read-only queries. |
| OPT23-R10 | P1 / AF-03 | The CMake target `Axle::SceneData` was only an alias for `AxleScene`; generic data still included concrete components and linked GPU/physics/audio/toolkits. The advertised build boundary did not exist. | Create a real `Engine/SceneData` library depending only on Core. Move basic components, entities, commands, transforms and generic copy codecs there. Runtime codecs and system hooks remain in Scene. A CPU structural document codec preserves uninterpreted concrete/plugin sections. A separate executable links only SceneData and exercises document load/copy, hierarchy, passive sections, lifetime and parallel APIs. Architecture rules enforce the new boundary. |
| OPT23-R11 | P2 / AF-02, AF-04, EJ-11..14 | Several new suites used `assert` with assertions disabled in Release; `AxleAllTests` omitted five delivered test targets. Product parity tests instantiated two drivers without exercising either product adapter. | Keep those suite assertions active in Release, include every delivered target in the aggregate, and add native product probes plus permanent review regressions and real script DLL fixtures. |
| OPT23-R12 | P1 / AF-02, EJ-04 | Jolt's default CMake configuration publicly propagated `_HAS_EXCEPTIONS=0` to Scene while standalone CPU libraries and the native script fixture used the normal exception ABI. The real post-commit bind-failure fixture hung instead of reaching recovery. | Enable C++ exceptions in the vendored Jolt build configuration without editing vendor sources. Include STL exception mode, iterator debug level and CRT linkage in the script build identity. Real DLL fixtures reject incompatible build identity and missing exports before destroying the active generation. |

## Current-source verification

All required gates completed on Windows with the final implementation above.
Every command exited **0**. Generated logs/reports live under ignored `build/`.

| Gate | Debug | Release | Evidence under `build/` |
| --- | --- | --- | --- |
| Configure and build `AxleAllTests`, `AxleEditor`, `AxleRuntime` | PASS | PASS | `opt0203-review-configure.log`, `opt0203-review-build-debug.log`, `opt0203-review-build-release.log` |
| Full CTest registration | **42/42** | **42/42** | `opt0203-review-ctest-debug.log`, `opt0203-review-ctest-release.log` |
| Full default `AxleTests` integration, including native GPU paths | PASS | PASS | `opt0203-review-integration-debug.log`, `opt0203-review-integration-release.log` |
| Typed jobs/ECS harness | **16 PASS, 0 defects, 3 verified probes, 0 unexpected failures** | **16 PASS, 0 defects, 3 verified probes, 0 unexpected failures** | `opt0203-review-debug/harness.json`, `opt0203-review-release/harness.json` |
| Real Editor and Runtime simulation | PASS, both product processes exit 0 | PASS, both product processes exit 0 | `opt0203-review-native-debug.json`, `opt0203-review-native-release.json` and their product logs |
| Repeated constrained physics pool scenario (256 reuses/run) | **100/100** | **1,000/1,000** | `opt0203-review-physics-stress-debug/summary.json`, `opt0203-review-physics-stress-release/summary.json` |
| Architecture boundary checks | PASS: **374 files, 18 CMake files, 0 violations** | Shared source check | `opt0203-review-architecture.json` |
| Baseline tooling regressions | **18/18** | Shared tooling check | `opt0203-review-tools.log` |
| Staged whitespace validation | PASS | PASS | `git diff --cached --check` before implementation commits |

Both native products execute their actual `OnUpdate` path at **30, 60 and 144 Hz**.
Each one-second trace executes **60 fixed ticks** and observes **60 held-input ticks**.
All six poses per configuration agree (`x = 1.998332`, comparison tolerance `1e-4`).
The native checks also assert pause, zero-wall-time single step, zero speed,
resume identity, callback counts and Editor world restoration. They initialize
real OpenGL resources on the host NVIDIA GeForce RTX 3070. The full integration
suite additionally exercises native D3D12 and Vulkan paths.

Permanent regressions include `Opt0203ReviewRegressions`, `SceneDataBoundaryTests`
and `ScriptLifecycleReviewTests` (all included in the 42 CTest cases). Script
fixtures are real generation A/B DLLs and incompatible ABI/build/export/metadata
or throwing-bind DLLs; the SceneData executable links only SceneData and Core.
The repeated pressure test invokes the permanent `ej05-physics-lifetime` scenario.

Commands used:

```powershell
cmake -S . -B build
cmake --build build --config Debug --parallel 2 --target AxleAllTests AxleEditor AxleRuntime
cmake --build build --config Release --parallel 3 --target AxleAllTests AxleEditor AxleRuntime
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
build/Tests/Debug/AxleTests.exe
build/Tests/Release/AxleTests.exe
python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt0203-review-debug --report build/opt0203-review-debug/harness.json
python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt0203-review-release --report build/opt0203-review-release/harness.json
python scripts/run_simulation_product_probe.py build/Editor/Debug/AxleEditor.exe build/Runtime/Debug/AxleRuntime.exe --report build/opt0203-review-native-debug.json
python scripts/run_simulation_product_probe.py build/Editor/Release/AxleEditor.exe build/Runtime/Release/AxleRuntime.exe --report build/opt0203-review-native-release.json
python scripts/check_architecture.py --root . --closed-package AF-04 --report build/opt0203-review-architecture.json
python scripts/test_baseline_tools.py
```

The additional stress runs used the local, ignored runner
`python build/opt0203-review-run-physics-stress.py Debug 100` and
`python build/opt0203-review-run-physics-stress.py Release 1000`. Each invocation
of the maintained scenario is `AxleJobsECSTests.exe ej05-physics-lifetime`.

Earlier unsuccessful attempts are not counted as acceptance evidence. These
included concurrent CMake regeneration, stale objects after header layout changes,
the exception-mode mismatch (R12), and a rare pool-slot pressure race (R2) caught
by final Debug/Release gates. The final source uses publication-safe recovery,
including a retry when a wrapper returns its slot between allocation and the live
count query; the 1,000 Release runs completed after this correction. Full integration
also caught old test callers of pinned completion pumping and the migrated camera's
`OnUpdate`; those now exercise the explicit pump and `OnLateUpdate`, with an added
check that the camera does not execute twice. Scene construction no longer performs
a complete forest evaluation per reparent, verified by the 3,500-node integration.

The integration executable's historical banner says "68 steps"; this report uses
the exit code and final full-suite PASS marker rather than repeating that stale count.

## Scope and practical limits

- Acceptance here concerns OPT-02/03 foundations and their current product
  integration. It does not release or accept OPT-04+, a plugin loader, or later
  renderer work.
- Raw EnTT/enki access remains an advanced caller-owned interface. Callers must
  join their batches and respect scene ownership; the reserved physics priority
  lane must not contain arbitrary Scene/UI work.
- General plugin module leases and untracked user-created native threads are not
  implemented by this script-module correction.
- Script ABI is **4**. Existing project script DLLs must be rebuilt; incompatible modules are rejected before altering the active generation.
- The separate OPT-00 manual/full-product acceptance gap remains open.
- No Vulkan software ICD was available; integration used the physical Vulkan device.
- Native product probes exercise real update/render/resource paths. They are
  distinct from a manual visual inspection or an exhaustive interactive UI audit.
