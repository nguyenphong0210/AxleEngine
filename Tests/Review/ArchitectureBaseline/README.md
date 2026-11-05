# AF-00 / EJ-00 independent acceptance reproductions

Reviewed implementation: `95a6c9d`. See the
[Codex review](../../../docs/reviews/OPT_01_BASELINE_REVIEW.md).
These review checks expose delivery gaps; they are not Engine production fixes
or permanently expected-failure CTest registrations. Exit 1 means an acceptance
contract is violated. Original authored fixtures are not modified; each run creates
a new owned directory under `build/opt01-review/adversarial` and writes its report.

## Python checker and runner negative controls

From the repository root:

```powershell
python Tests/Review/ArchitectureBaseline/run_review.py
```

This imports the delivered runner and checks missing child executable, unrelated
exit and early timeout classification, then runs the actual architecture checker
against isolated direct/nested/variable CMake edges and a relative source include.
On the reviewed tree there are six failed contracts and one passing direct-edge
control. The script returns 1 and prints the generated results path.

## Native queued-delivery passing-behavior control

The delivered thread-identity test sets a flag in its callback, then checks that
live flag after pumping. To demonstrate its false rejection independently of the
known external-thread bug, the control uses a registered enki worker as producer.
That existing production route queues to the main thread correctly. The callback
flags and final predicate are retained from the reviewed source, not replaced.
Only the copied fixture actor and an observation print change. No Engine code changes.

Ensure the existing cache has no unrelated CMAKE_PROJECT_INCLUDE hook; if it does,
use a separate review build/cache or preserve/restore that hook rather than unset it.
The review used an existing cache with no such hook.

```powershell
python Tests/Review/ArchitectureBaseline/run_review.py --prepare-queued-control build/opt01-review/queued-control
$queuedReviewHook = (Resolve-Path build/opt01-review/queued-control/inject.cmake).Path
cmake -S . -B build "-DCMAKE_PROJECT_INCLUDE=$queuedReviewHook"
cmake --build build --config Release --target AxleOPT01QueuedReview --parallel 6
python Tests/Review/ArchitectureBaseline/run_review.py --queued-binary build/opt01-review/queued-control/bin/Release/AxleOPT01QueuedReview.exe
cmake -S . -B build -U CMAKE_PROJECT_INCLUDE
```

The generator requires the reviewed source patterns to still exist. If the test
changes, inspect the new implementation before adapting this review control.
On the reviewed tree, the native control reports no callback before pumping and
main-thread delivery after pumping, but exits 1 with the original defect message.
Including this control yields seven failed contracts plus one passing control.
The CMake hook is opt-in; restore/remove it even when a reproduction returns 1.

## Newly rebuilt normal baseline probes

```powershell
cmake --build build --config Debug --target AxleJobsECSTests --parallel 6
python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt01-review/debug --report build/opt01-review/harness-debug.json
cmake --build build --config Release --target AxleJobsECSTests --parallel 6
python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt01-review/release --report build/opt01-review/harness-release.json
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

The separate ignored report paths preserve Gemini's committed historical evidence.
The baseline collector records real known Engine defects and exits zero; that is
separate from an Engine correctness PASS and from this independent acceptance review.
No legacy Vehicle or old-format support is required by these checks.
