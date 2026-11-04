# OPT-00: Independent Vehicle Retirement Review

Date: **2026-11-04**. Reviewer: **Codex**.
Reviewed production baseline: **`6f9f488`**. Retirement: **`fa74981`**, compared
with **`503715e`**. Later `bdf2cb2` VFX restoration and `cc184cf` sample cleanup
are distinguished from the original retirement delivery.

**Decision: CHANGES_REQUIRED. Review complete; OPT-00 remains open.**

The built-in module and production dependencies are removed, and normal startup,
basic legacy round-trips, scene copying and a vehicle-free physics lifecycle work.
Acceptance fails on two P1 preservation/physics gaps and one P2 payload-fidelity
gap. The existing full integration runner also stops on a separate VFX assertion.
No production fixes or subsequent optimization packages were implemented here.

Machine-readable evidence: [results](OPT_00_VEHICLE_RETIREMENT_RESULTS.json).
Maintained reproduction: [review fixture](../../Tests/Review/VehicleRetirement/README.md).
Implementer's claims remain in the [original delivery](../archive/design/reports/VEHICLE_RETIREMENT_DELIVERY.md).

## Verified findings

### VR-R1 - P1: Assembly Editor save silently discards all legacy Vehicle payloads

**Trigger:** open an old `.axle` prefab through Assembly Editor and save it.
The real UI calls
[LoadAssemblyData](../../Engine/UI/src/AssemblyEditorPanel.cpp#L125) and then
[SaveAssemblyData](../../Engine/UI/src/AssemblyEditorPanel.cpp#L141) on the same asset.

**Expected:** retain legacy Vehicle, wheel, audio and VFX blocks while editing
supported properties; unsupported authored data must remain intact.

**Observed:** the entity-based `Serialize`/`Deserialize` path retains the holder,
but [AssemblyNode](../../Engine/Scene/include/Axle/AssemblySerializer.hpp#L18)
has no legacy storage. The [raw-to-node mapping](../../Engine/Scene/src/AssemblySerializer.cpp#L1220)
drops `raw.LegacyComponents`; the node writer/save path cannot emit it. Save
returns success and atomically replaces the file with the payloads missing.

**Native reproduction:** `assembly-editor` and `assembly-editor-authored` both
return **1**. The latter copies the existing
[SportsCar prefab](../../Tests/Fixtures/Assets/prefabs/SportsCar_Assembly.axle)
into the writable review workspace, loads/saves through these production APIs,
and reports `authored_editor_kept_legacy=0`. Source assets are not modified.
The entity-based `assembly-basic` control passes.

**Required correction:** preserve opaque legacy data through every AssemblyData
node, child, load/save and editor path. Add an authored-prefab editor round-trip
regression comparing all payload values, not just the existence of one tag.

**Attribution:** AssemblyNode's limited model predates retirement. This is an
unclosed requirement of the new preservation guarantee, not a claim that the
old Assembly Editor previously preserved every component.

### VR-R2 - P1: Assembly instantiation and duplication create ghost physics bodies

**Trigger:** instantiate a legacy prefab, or duplicate a legacy entity while a
PhysicsScene is supplied, with authored `RigidBody3D` and `CollisionShape3D`.

**Expected:** unsupported legacy chassis remain passive; ordinary rigid bodies
continue to work. The legacy-preservation path must not synthesize a replacement
static chassis obstacle.

**Observed:** [SceneSerializer's guard](../../Engine/Scene/src/SceneSerializer.cpp#L1714)
checks `HasLegacyVehicleData()` before body creation. Equivalent protection is
missing from [assembly physics integration](../../Engine/Scene/src/AssemblySerializer.cpp#L1050)
and [DuplicateEntity](../../Engine/Scene/src/Scene.cpp#L184). Both create a valid
static Jolt body from the same unsupported chassis data.

**Native reproduction:** `scene-basic` loads the fixture with **0** bodies and
passes. `assembly-physics` reports `assembly_bodies=1`; `duplicate-physics`
reports `duplicate_bodies=1`; each returns **1**. Payload retention itself still
works in those two cases. This isolates the missing body-readiness protection.

**Required correction:** enforce the passive legacy readiness policy at all body
creation entry points, including assembly/duplicate paths; retain ordinary-body
controls. Check actual Jolt body counts and runtime IDs after each operation.

The existing Step 25 fixture lacks rigid-body/collision components on its legacy
entities, so its zero-body assertions do not exercise this trigger.

### VR-R3 - P2: Legacy capture changes payload structure and component identity

**Trigger:** round-trip a legacy section with a nested map/comment, or the
recognized `VehicleWheel3D` alias.

**Expected:** opaque preservation retains nested data and the original component
identifier until an explicit conversion is requested.

**Observed:** scene [capture](../../Engine/Scene/src/SceneSerializer.cpp#L1253)
and assembly [capture](../../Engine/Scene/src/AssemblySerializer.cpp#L746) store
`trimmed`, while both writers add one fixed indentation level. Comments/blank
lines are skipped before legacy capture. Scene and assembly parsers also replace
`VehicleWheel3D` with `WheelComponent` instead of retaining its authored tag.

For example, `Tuning` loses its child and `TorqueCurve` becomes a sibling:

```yaml
# Input
    VehicleComponent:
      Tuning:
        TorqueCurve: [900, 7000]
# Saved
    VehicleComponent:
      Tuning:
      TorqueCurve: [900, 7000]
```

**Native reproduction:** `scene-nested`, `assembly-nested` and `scene-alias`
return **1**. The scene output loses the comment/nesting; the alias output
contains `WheelComponent` instead of `VehicleWheel3D`. Flat scalar controls pass.
Nested fixtures test the promised opaque/verbatim contract; they are not a claim
that every historical vehicle used a nested tuning map.

**Required correction:** capture section headers and raw relative-indented content
before generic trimming/comment handling can modify it. Preserve authored tags
and section boundaries. Regressions should compare nested values and identities
through both scene and assembly paths, including copy and editor save.

## Separate integration-suite observation

**VR-OBS1:** rebuilt `AxleTests.exe` aborts at
[Step 47.1](../../Tests/src/Test_Milestone8_Part1.cpp#L1212):
`panel.GetPreviewSystem() != nullptr`. Exit code: **3221226505**.

The [VFX constructor](../../Engine/UI/src/VFXEditorPanel.cpp#L51) starts with a
neutral config and an empty document; the test expects an already-created preview
system. The constructor and failing initial test case match the `503715e`
baseline. `bdf2cb2` restored the prior VFX implementation after `fa74981` changed
initialization. This is a current suite/contract mismatch with pre-retirement
source provenance, not evidence that Vehicle removal newly broke particle physics.
The historical baseline was not rebuilt/executed independently.

Reconcile the intended VFX initialization contract and its meaningful test setup
separately. Do not restore an automatic automotive sample or remove a useful
assertion merely to obtain a green runner. A PASS reported against `fa74981`
does not establish a PASS on the later reviewed tree.

## Newly executed verification

All commands below used the actual Windows/MSVC build and production libraries.
The old build cache was absent; the review configured and rebuilt a fresh tree.

| Check | Result | Evidence under `build/opt00-review/` |
| --- | --- | --- |
| `cmake -S . -B build` | Exit 0 | `configure.log` |
| `cmake --build build --config Release --target AxleEditor AxleRuntime AxleAllTests --parallel 6` | Exit 0 | `build-release.log` |
| `ctest --test-dir build -C Release --output-on-failure` | Exit 0; **7/7** | `ctest.log`, `regression-results.json` |
| `build/Tests/Release/AxleTests.exe` | Exit **3221226505**, Step 47.1 assertion | `integration.log` |
| Opt-in review target configure/build | Both exit 0 | `configure-probe.log`, `build-probe.log` |
| `python Tests/Review/VehicleRetirement/run_review.py build/opt00-review/bin/Release/AxleVehicleRetirementReview.exe build/opt00-review/probes` | Exit **1**; **4/11 pass**, **7/11 fail** | `probes/results.json` and per-case logs |
| Editor `--verify-empty-startup --rhi=dx12` | Exit 0; five UI frames | `editor-empty-dx12.log` |
| Editor `--verify-empty-startup --rhi=vulkan` | Exit 0; five UI frames | `editor-empty-vulkan.log` |
| Editor `--verify-startup --project <build-local LegacyReview.axleproj> --rhi=dx12` | Exit 0; **16 entities**, five UI frames | `editor-legacy-dx12.log`, `editor-legacy-results.json` |
| `cmake -S . -B build -U CMAKE_PROJECT_INCLUDE` | Exit 0; local probe injection removed | `configure-restore.log` |

The editor smoke runs used `build/Editor/Release` as their working directory and
logged actual **Direct3D 12 / Vulkan 1.3** backends on **NVIDIA GeForce RTX 3070**.
Vulkan's underlying NRI log reports Vulkan v1.4 support. The legacy project uses
a writable copy of the test fixtures and `CarTest.axlescene`, not authored samples.

The production reference scan found no old Vehicle target/include/query/collection
references in root CMake, Engine, Editor, Runtime or Plugins; `Engine/Vehicle` is
absent. This is a source/build-boundary check, not a measured binary-size result.

The existing integration run passed the retirement Step 25, audio Step 31,
CharacterController Steps 39/40 and general VFX Step 46 before stopping at 47.
It did not execute later steps in that invocation. TestCommon explicitly keeps
assertions active in Release; the failure is not an artifact of disabled asserts.

### Focused fixture outcomes

| Scenario | Result |
| --- | --- |
| `scene-basic` | PASS: flat payloads retained; unsupported chassis creates 0 bodies |
| `scene-copy` | PASS: scene copy/duplicate retain independent payload storage |
| `assembly-basic` | PASS: entity-based flat assembly round-trip |
| `lifecycle` | PASS: ordinary body falls from y=10 to y=9.78828; 1 body; Stop clears runtime owner; legacy data retained |
| `assembly-editor`, `assembly-editor-authored` | FAIL: VR-R1, including the existing SportsCar fixture |
| `assembly-physics`, `duplicate-physics` | FAIL: VR-R2, 1 unwanted body each |
| `scene-nested`, `assembly-nested`, `scene-alias` | FAIL: VR-R3 |

Checks remain active in Release. Each focused case ran in its own process with
a 60-second watchdog; none timed out. Known failures return nonzero and are not
registered as passing/expected-failure CTest cases.

## Limits and disposition

- No manual Inspector or Editor Play/Pause/Stop interaction; the five-frame native
  smoke and headless production lifecycle checks are separate evidence.
- Runtime was rebuilt; packaged-player startup/export was not run.
- The full runner stops at Step 47; later integration steps remain NOT RUN here.
- No sanitizer, historical-baseline build or performance benchmark was run.
- Build logs include LNK4098 (`LIBCMT` conflict); the D3D12 smoke logs an NRI
  `CheckFeatureSupport(options22)` warning. These are recorded, not repaired or
  promoted into unrelated NRI/CRT work by this review.
- The later empty-sample cleanup is separate from retirement; this review does
  not restore deleted sample assets or claim they remain shipped/operational.
- An unrelated root `ROADMAP.md` formatting edit appeared during the review and
  is preserved outside this delivery. Historical exchange messages are retained.

Keep OPT-00 unchecked. Correct VR-R1..R3, rerun this fixture and the affected
product checks, and resolve/document the full-runner discrepancy before recording
acceptance. The reviewer has supplied findings and reproducible tests; no fix
assignment or OPT-01/Phase 40 implementation is released by this report.
