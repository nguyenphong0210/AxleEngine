# OPT-01: Independent AF-00 + EJ-00 Baseline Review

> **Follow-up, 2026-11-05:** Codex corrected O1-R1..R4 and verified the changes
> in `af5721b` plus the completed baseline inventory. See the
> [correction report](OPT_01_BASELINE_CORRECTIONS.md). The findings below remain
> the historical independent review of `95a6c9d`; the follow-up explicitly records
> author-run verification rather than a second independent agent review.

Date: **2026-11-05**. Reviewer: **Codex**.
Implementation: **`95a6c9d`**, delivered on **`0f23b86`**, compared with `8ad5d75`.

**Decision: CHANGES_REQUIRED. AF-00 and EJ-00 remain unaccepted.**

The package adds real production-API probes and keeps production code unchanged.
Newly rebuilt Debug/Release jobs/ECS binaries reproduce the four documented Engine
faults, the explicit bridge-drain control and current-format lifecycle pass, and
CTest passes 11/11 in both configurations. Those existing Engine faults are
intentional baseline observations, not reasons to implement EJ-01+ here.

Acceptance fails because the baseline tooling can misclassify unrelated errors as
specific defects, the architecture checker accepts new forbidden dependencies,
and the main-thread test rejects correctly queued delivery. The required shared
baseline inventory is also incomplete. These are findings in this delivery.

No old-format support, Vehicle preservation, conversion or production fixes are
required or implemented by this review. OPT-00 has not been independently accepted.

Evidence: [structured review results](OPT_01_BASELINE_REVIEW_RESULTS.json),
[maintained review reproductions](../../Tests/Review/ArchitectureBaseline/README.md).
Gemini's [delivery report](OPT_01_BASELINE_DELIVERY.md) remains implementer evidence.

## O1-R1 - P1: infrastructure errors become reproduced Engine defects

**Sources:** [runner](../../scripts/run_jobs_ecs_harness.py), lines 32-70 and
184-202; [native compiler fixture](../../Tests/src/JobsECSTestMain.cpp), lines 44-76.

**Trigger:** a known-defect child cannot launch, exits for an unrelated reason,
or times out before reaching its intended assertion. The runner uses only
`exit_code != 0 or timed_out` for `REPRODUCED_DEFECT`. A launch exception sets
exit -1 and follows that same success path. Aggregate unexpected-failure counting
does not identify those outcomes as infrastructure failures.

**Expected:** report the specific observed defect only after the required fixture
setup and scenario checkpoint/behavior evidence. Launch/setup failures, unexpected
crashes and timeouts in the wrong phase must remain distinct and make collection
fail. A baseline collector may exit zero after recording genuine known defects;
that does not justify accepting every nonzero child exit as the intended defect.

**New reproduction:** direct calls to production `run_scenario` with a missing
executable, an unrelated Python exit 73, and a 0.2-second timeout all return
`status=REPRODUCED_DEFECT`. None executes an Engine API. The review script reports
all three as failed contracts and exits nonzero.

The native compiler probe separately ignores CMake configure status and never
requires `started` or `compilationSucceeded`. With PATH intentionally restricted
to Windows System32, the rebuilt Release executable prints
`started=1 callbackRan=1 compilationSucceeded=0 callbackOnMain=0` and still labels
its exit 1 as the same reproduced scenario. The wrong-thread callback observation
is real, but the intended successful-build prerequisite did not run successfully.

**Required correction:** establish explicit scenario result/exit protocols and
validate setup, behavior evidence and the bridge timeout checkpoint. Distinguish
infrastructure failure from known defect and intentional harness validation.
Require successful compiler-fixture setup/build for its successful-build case;
retain failure-completion coverage as a separately identified scenario if useful.
Add negative controls for launch/setup failure, unrelated crash/exit and early hang.
Do not fix Engine production code to make these baseline checks pass.

## O1-R2 - P1: new forbidden dependencies pass the architecture checker

**Sources:** [checker](../../scripts/check_architecture.py), lines 91-125 and
181-198. [Rules/allowances](../../scripts/architecture_rules.json).

**Expected:** new forbidden source/link edges fail from the beginning, including
new edges introduced in modules that already have a narrow baseline allowance.

**New isolated mutation results:**

| Mutation | Expected | Actual |
| --- | --- | --- |
| Direct `AxleCore -> Axle::UI` in module CMakeLists (control) | Reject, exit 1 | Exit 1 |
| Same edge in `Engine/Core/nested/CMakeLists.txt`, reached by `add_subdirectory(nested)` | Reject | **PASS, exit 0** |
| `set(NEW_DEP Axle::UI)` then `AxleAsset -> ${NEW_DEP}` | Reject | **PASS, exit 0** |
| New Core source includes `../../Scene/include/Axle/Scene.hpp` | Reject | **PASS, exit 0** |

Each mutation lives in its own review workspace; production source is not changed.
The relative include resolves to a real header in the fixture.

**Cause:** only one top-level CMakeLists per configured module is scanned. Link
variables are not resolved; any unrecognized link gets `target_mod is None`, which
lets an unrelated existing allowance for that CMake file authorize it. Source
include matching uses header spellings rather than resolving relative paths and
silently skips unknown matches. Thus ALLOW-004/005 for Asset's Renderer/RHI coupling
can swallow the new variable-based UI edge.

**Required correction:** inspect the actual configured target graph or implement an
explicitly supported, fail-closed source representation covering reachable CMake
files and target variables. Resolve project includes to module ownership. Match
allowances to exact dependency kind/source/target without a wildcard for unknown
targets, and report unresolved coverage instead of declaring a full PASS. Keep
controls for direct, nested, variable-based and relative-include violations. New
violations must fail even when the module has another baseline allowance.

## O1-R3 - P2: main-thread probe rejects correct deferred delivery

**Source:** [JobsECSTestMain.cpp](../../Tests/src/JobsECSTestMain.cpp), lines 99-125.

The callback unconditionally sets `calledBeforePump = true`. After pumping the
queue, the final predicate rejects that same live flag. Therefore the intended
correct behavior (callback runs on the main thread during the pump) is rejected
as an inline-before-pump defect too. This test cannot become green when the
production identity/queue contract is repaired normally.

**New native control:** an opt-in review executable copies the delivered probe,
changes only the producer to a registered enki worker, and retains the original
callback flags and final predicate. This exercises the existing correctly queued
production `JobSystem::DispatchToMainThread` path without fixing Engine code.
The rebuilt Release control reports:

```text
external enki index=1 wrapper index=1
incorrectlyMain=0 calledBeforePump=0 callbackOnMain=0
review_after_pump incorrectlyMain=0 calledBeforePump=1 callbackOnMain=1
DEFECT REPRODUCED [ECSJ-02]: External thread classified as main thread zero; callback executed inline before pump
```

It returns **1** despite correct owner-thread delivery. This control does not
claim the external-thread Engine bug is fixed; the unmodified probe still
reproduces that bug. It proves the probe's flag lifetime is wrong on a valid queue
path. The review-only target is opt-in and its CMake cache injection was removed.

**Required correction:** snapshot callback state after the producer finishes and
before pumping; independently assert the post-pump count and owner identity.
Keep a real queued passing control so the regression distinguishes early delivery,
missing delivery, wrong-thread delivery and exactly-once correct delivery.

## O1-R4 - P2: shared baseline inventory is incomplete

**Contract:** [AF-00](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md#7-additional-foundation-packages),
lines 206-221 and AF-00 items 1-4.
**Reviewed artifacts:** the complete delivery diff, delivery report, architecture
rules and the three committed scenario-result JSON snapshots.

AF-00 requires one reproducible inventory of the actual target/include graph,
source/vendor revisions, current asset identity/persistence policy, build workflows
and acceptance gaps. The delivery catalogs desired layer/link rules and ten
allowances and records probe results. It supplies no pinned vendor revision
inventory, source-backed asset identity/persistence baseline, or complete measured
current target/include graph distinct from the intended allowed graph. A list of
allowed targets is a policy, not evidence of the actual graph being refactored.

**Required correction:** add the missing current-state inventory with reproducible
commands, actual revisions/edges and concrete asset identity/persistence code
references. Keep unresolved gaps explicit and connect them to owning packages.
This is baseline documentation/tooling only: it does not authorize AF-01 asset
implementation, PLG-00 feature inventory or any production refactor.

## Newly executed verification

All paths below are under ignored `build/opt01-review/`. The original delivery
snapshots were not overwritten. Native runs used Windows/MSVC and current
production libraries. Review checks did not initialize GPU rendering.

| Command/check | Result | Evidence |
| --- | --- | --- |
| `cmake --build build --config Debug --target AxleJobsECSTests --parallel 6` | Exit 0 | `build-jobs-debug.log` |
| Same command with `--config Release` | Exit 0; LNK4098 recorded | `build-jobs-release.log` |
| `ctest --test-dir build -C Debug --output-on-failure` | Exit 0; 11/11 | `ctest-debug.log` |
| Same CTest command with Release | Exit 0; 11/11 | `ctest-release.log` |
| `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt01-review/debug --report build/opt01-review/harness-debug.json` | Collector exit 0; 3 PASS, 4 recorded defects, 3 intentional probes | `harness-debug.json`, `debug/*.log` |
| Same runner with Release binary and separate `release` workspace/report | Identical classifications; collector exit 0 | `harness-release.json`, `release/*.log` |
| `python Tests/Review/ArchitectureBaseline/run_review.py --queued-binary build/opt01-review/queued-control/bin/Release/AxleOPT01QueuedReview.exe` | Exit **1**; seven failed contracts, one passing direct-edge control | `adversarial.log`, `adversarial/review-ayj9v_o6/results.json` |
| Queued native control configure/build | Both exit 0 | `configure-queued-control.log`, `build-queued-control.log` |
| Queued native control execution | Exit **1** on correct queued main-thread callback | `queued-control.log`, `queued-control.json` |
| Release compiler probe with CMake unavailable | Exit **1**; `compilationSucceeded=0` | `compiler-no-cmake.log`, `compiler-no-cmake.json` |
| `cmake -S . -B build -U CMAKE_PROJECT_INCLUDE` | Exit 0; review injection removed | `configure-restore.log` |

The normal newly executed harness runs reproduce the actual worker-thread compiler
callback, external-thread misclassification and inline dispatch, single-thread
bridge-destruction timeout, and one EnTT tombstone callback in both configurations.
The explicit bridge drain and ordinary current-format scene/assembly/copy/duplicate/
physics lifecycle controls pass. This is useful baseline evidence despite the
acceptance gaps in its checker and harness.

## Limits and disposition

- Only the changed jobs/ECS executable and opt-in review control were rebuilt in
  this review. CTest also ran the existing delivered unchanged suite binaries.
- Editor/Runtime builds and full `AxleTests.exe`/GPU integration were **not rerun**.
  Gemini reports those results; this review does not relabel them as independent.
- No native interactive Inspector/Play/Pause/Stop, packaged-player/export,
  sanitizer, performance or concurrent DLL-unload verification was performed.
- No production Engine source or original Gemini delivery report/results changed.
- Keep AF-00/EJ-00 unchecked and record CHANGES_REQUIRED. Correct O1-R1..R4 and
  reverify this same package before acceptance. No AF-01/P41-00/PLG-00/EJ-01+
  implementation is released by this review.
