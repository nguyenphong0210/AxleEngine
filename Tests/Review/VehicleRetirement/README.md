# Vehicle retirement acceptance fixture

This opt-in review fixture exercises production Scene/Assembly serializers,
copy/duplicate and Jolt lifecycle APIs. Checks stay active in Release. It does not
change production targets or register expected failures as passing CTest tests.

After the normal Editor/Runtime/test build finishes, configure the additional
target in the same build tree to reuse the rebuilt libraries:

```powershell
cmake -S . -B build -DCMAKE_PROJECT_INCLUDE="$PWD/Tests/Review/VehicleRetirement/CMakeLists.txt"
cmake --build build --config Release --target AxleVehicleRetirementReview --parallel 6
./build/opt00-review/bin/Release/AxleVehicleRetirementReview.exe scene-basic build/opt00-review/work
```

Run each scenario in a separate subprocess with a timeout, collecting actual
exit codes, per-case logs and `results.json`:

```powershell
python Tests/Review/VehicleRetirement/run_review.py build/opt00-review/bin/Release/AxleVehicleRetirementReview.exe build/opt00-review/probes
```

Scenarios:
`scene-basic`, `scene-copy`, `scene-nested`, `scene-alias`, `assembly-basic`,
`assembly-editor`, `assembly-editor-authored`, `assembly-physics`,
`assembly-nested`, `duplicate-physics`, `lifecycle`.
Each returns zero only if its contract passes; a known defect still returns one.
The workspace holds generated inputs/outputs for inspection; use a build-local
directory, not authored project assets.

Remove the local injection after the review:

```powershell
cmake -S . -B build -U CMAKE_PROJECT_INCLUDE
```

The independent [OPT-00 review](../../../docs/reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md)
records tested revisions, results, findings and limitations. The fixture does not
claim native Editor UI manipulation or rendering/backend acceptance.
