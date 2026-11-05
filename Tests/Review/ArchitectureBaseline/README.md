# AF-00 / EJ-00 acceptance reproductions

The [original Codex review](../../../docs/reviews/OPT_01_BASELINE_REVIEW.md)
records failures against `95a6c9d`. The current checks validate corrections without
fixing production Engine defects. Exit 1 is an acceptance failure, not a permanently
expected-failure CTest result. Each run owns a fresh ignored workspace and reports
actual statuses. Original authored fixtures and historical delivery results remain intact.

Rebuild the changed native executable, then run the independent negative controls:

```powershell
cmake -S . -B build
cmake --build build --config Debug --target AxleJobsECSTests --parallel 6
python Tests/Review/ArchitectureBaseline/run_review.py --binary build/Tests/Debug/AxleJobsECSTests.exe
cmake --build build --config Release --target AxleJobsECSTests --parallel 6
python Tests/Review/ArchitectureBaseline/run_review.py --binary build/Tests/Release/AxleJobsECSTests.exe
python scripts/test_baseline_tools.py
```

These check launch failure, unrelated exit, timeout before a scenario checkpoint,
direct/nested/variable CMake dependencies, relative source includes, actual queued
owner-thread delivery and native compiler prerequisite failure. They must all pass
as acceptance controls. The production external-thread/compiler/Jolt/EnTT faults
remain separate, explicitly identified baseline observations:

```powershell
python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --workspace build/opt01-fix/debug --report build/opt01-fix/harness-debug.json
python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --workspace build/opt01-fix/release --report build/opt01-fix/harness-release.json
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

The permanent `queued-delivery-control` replaces the review-only source-copy/CMake
injection used to demonstrate the old flag bug. It exercises the existing valid
registered-enki-worker production queue path. No cache injection is needed.
Checks remain active in Release. CTest runs passing controls through owned watchdogs;
known defects are collected separately and never registered as passing Engine tests.
The runner's default output is ignored build data, not a committed historical report.
No old-format or Vehicle preservation is required.
