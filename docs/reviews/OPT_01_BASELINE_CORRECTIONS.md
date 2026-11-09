# AF-00 / EJ-00 corrections and verification

Date: **2026-11-05**. Author and verifier: **Codex**.
Original Gemini implementation: `95a6c9d`; original independent review: `ba35221`.
Correction implementation: **`af5721b`**. Baseline inventory captured from that commit.
User explicitly requested Codex fix O1-R1..R4 and provide the next English prompt.

**Disposition: O1-R1..R4 FIXED and reverified; AF-00/EJ-00 baseline entry checks
are satisfied for proceeding to AF-01.** The corrections were verified by their
Codex author; no separate agent independently reviewed the post-fix implementation.
The original review and Gemini evidence remain historical and unchanged apart from
a dated pointer to this follow-up. OPT-01 as a whole and OPT-00 are not closed.

## Finding resolutions

| Finding | Resolution and meaningful verification |
| --- | --- |
| O1-R1 / P1 | Runner requires exact scenario events and exit protocol: 0/PASS, 1/DEFECT or intentional failure, 2/ERROR. Known bridge timeout requires its destructor-ready marker and no destructor-completed/terminal marker. Launch, setup, unrelated exit, missing proof and wrong-stage timeout are unexpected failures and propagate exit 1. C++ compiler fixture checks configure/start/callback/build-success prerequisites. The full CLI with CMake unavailable exits 1 with one infrastructure failure and zero reproduced defects. |
| O1-R2 / P1 | Checker inspects root/module/nested CMake, follows supported composition, resolves set/list-based dependency values and relative project includes, and matches allowances to 29 exact edges. Unknown targets cannot borrow another allowance. Unresolved/dynamic dependency construction fails coverage. Original direct/nested/variable/relative mutation controls all reject with exit 1; coverage/setup errors cannot validate an expected forbidden fixture. |
| O1-R3 / P2 | Probe snapshots callback count before pump, then verifies exactly one post-pump callback on the owning thread. A permanent registered-enki-producer queued control exercises the existing correct production path and passes in Debug/Release. The real external-thread defect still reproduces separately. |
| O1-R4 / P2 | Added reproducible source/vendor and actual configured CMake graph capture, the shared source-backed asset identity/persistence inventory, explicit build/evidence limits, exact allowance ledger and owner/expiration rules. [Inventory](../architecture/OPT_01_BASELINE_INVENTORY.md) and [JSON](../architecture/OPT_01_BASELINE_INVENTORY.json) distinguish actual configured prerequisites/includes from conservative source link/include policy. |

Ten allowance records retain narrowly enumerated edges and reject new edges in the
same files/modules. Stale owner labels were corrected: physical material's CPU
asset-format coupling belongs to AF-04, Terrain tooling belongs to PLG-20. Closing
an owner through `--closed-package ID` disables its allowances. No allowance was
expanded to conceal a new production dependency.

CTest uses the runner's owned watchdog for bridge, queued-delivery, lifecycle,
intentional-failure and watchdog controls. Deliberate failure requires the expected
marker plus exact exit 1; generic WILL_FAIL cannot convert a setup error to PASS.
Python is a required baseline toolchain component. Tests remain EXCLUDE_FROM_ALL,
and neither AxleJobsECSTests nor AxleAllTests acquires Editor/Runtime prerequisites.
The runner defaults to ignored build output and owns a unique workspace per run.

## Executed evidence

Artifacts are under ignored `build/opt01-fix/`; committed original delivery/review
snapshots were not overwritten. [Structured correction results](OPT_01_BASELINE_CORRECTIONS_RESULTS.json)
record the results and paths. [Review reproduction instructions](../../Tests/Review/ArchitectureBaseline/README.md)
now run the permanent passing/negative controls without temporary CMake injection.

| Commands/checks | Result |
| --- | --- |
| `cmake -S . -B build` | Exit 0; configured actual File API data |
| `cmake --build build --config Debug --target AxleJobsECSTests --parallel 6` | Exit 0; final changed native executable rebuilt |
| Same build command with Release | Exit 0; existing LNK4098 warning recorded |
| `python scripts/test_baseline_tools.py` | Exit 0; **18 tests** including launch/setup errors, scenario identity, timeout phase, unknown/variable/relative/nested dependencies, exact/expired allowances and included helpers |
| `ctest --test-dir build -C Debug --output-on-failure` | Exit 0; **15/15** |
| Same CTest command with Release | Exit 0; **15/15** |
| `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt01-fix/debug --report build/opt01-fix/harness-debug.json` | Exit 0; **4 ordinary PASS, 4 genuine recorded defects, 3 intentional validations, 0 unexpected failures** |
| Same runner with Release binary/workspace/report | Same classifications; exit 0 |
| `python Tests/Review/ArchitectureBaseline/run_review.py --binary build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt01-fix/adversarial-debug` | Exit 0; **9/9 acceptance controls** |
| Same review with Release binary/workspace | Exit 0; **9/9** |
| Full runner CLI restricted to compiler case with CMake intentionally unavailable in PATH | Expected exit **1**; JSON has 1 unexpected infrastructure failure, 0 defects; `negative-cli.json` |
| `python scripts/check_architecture.py --report build/opt01-fix/source-graph.json` | Exit 0; 342 unique source files, 17 CMake files, 104 source link edges, 694 cross-module includes |
| `python scripts/capture_architecture_baseline.py --report docs/architecture/OPT_01_BASELINE_INVENTORY.json` | Exit 0; all eight submodules match clean pinned revisions; actual Debug/Release graph has 29 Axle targets each and no test-to-product build prerequisite |

The four actual production defects remain the compiler background callback,
external-thread identity/inline delivery, single-thread bridge destructor hang,
and EnTT tombstone iteration. Those observations are recorded with their own
scenario proof, not relabeled Engine correctness PASS. The baseline tooling and
acceptance controls are fixed; their production repairs remain later EJ packages.

## Limits and next scope

No Engine/Editor/Runtime/Plugins production source or vendor was modified. No old
format reader, Vehicle holder, migration or compatibility fallback was introduced.
Editor/Runtime rebuild/full AxleTests/GPU/manual UI/player/export/sanitizer/performance
checks were not rerun for these tooling corrections. The current-format lifecycle
passes; OPT-00's remaining full product verification is still separately recorded.

The user-requested next package was **AF-01 only**. Its later delivery and
combined review are recorded in [OPT-01 AF-01/P41/PLG review](OPT_01_AF01_P41_PLG_REVIEW.md).
All changes were local; no push.
