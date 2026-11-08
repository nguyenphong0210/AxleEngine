# OPT-07 / OPT-08 plugin foundation review

Date: **2026-11-08**. Reviewer and repair owner: **Codex**.
Gemini implementation: `4827738..c3a56c8`, delivery head `9a390e5`.
Integrated review baseline: `b36167a` (preserves the later Create Plugin UI).
Corrected source: **`bd22550`**.

## Decision and user scope

**ACCEPTED for plugin foundation and operational lifecycle only.** The user
explicitly narrowed this review to a solid plugin foundation, with detailed
Vehicle/Spline/Terrain capabilities to be completed later. This is not full
feature acceptance for PLG-14..21 or a packaged selected player acceptance.
No remaining concrete P1/P2 was found in the corrected, bounded foundation scope.

Vehicle, Spline and Terrain now discover and build through the actual matched SDK,
activate six Runtime/Editor DLLs in dependency order, own their component data,
draw native Inspectors and preserve data through save/reopen, copy and repeated
Play/Stop. DLL code unloads after owned scenes/contributions are released.
Runtime is independent of optional domain libraries and stages a UI-free shipping
script module. Unsupported selected player composition fails explicitly.

The original candidate did not establish these claims: its successful prototype
tests were not proof that the selected DLL graph could build or run.

## Findings and repairs

| ID | Foundation finding | Final status |
| --- | --- | --- |
| F1 / P1 | Terrain manifest lacked the canonical schema/kind/source roots and used unsupported dependency syntax. Real resolution rejected `axle.terrain`. | **FIXED**: canonical schema and explicit plugin/module dependencies; real Terrain-to-Spline resolution/build/load pass. |
| F2 / P1 | Spline/Terrain CMake built static Engine-dependent libraries, not selected SDK modules. Actual Spline configure failed because Axle::Core was unavailable in the standalone SDK. | **FIXED**: explicit selected SDK branches produce matching native modules; development prototype libraries are excluded from normal builds and are not the DLL graph. |
| F3 / P1 | Native Spline/Terrain callbacks attempted to cast diagnostic service addresses to PhysicsScene and used private Engine/vendor objects across the SDK boundary. | **FIXED**: DLL-owned standard C++ authored data, bounded codecs/deep copies, supported lifecycle/debug operations and narrow Runtime exports for Inspectors. No private Engine/Jolt/ImGui state enters these DLLs. |
| F4 / P2 | Runtime still included, serialized and rebuilt Spline/Terrain directly and linked their libraries unconditionally. | **FIXED**: removed those product bindings; architecture rules now reject direct domain imports/links from Editor/Runtime. Required extension readiness and actual runtime-start result are checked. |
| F5 / P2 | Vehicle sample used raw type headers/Payload text rather than canonical extension envelopes; text-search tests did not detect that the real codec could not consume it. | **FIXED**: schema/required/reference/hex payload envelopes; real sample deserialize/readiness/copy and codec parsing tests pass. |
| F6 / P2 | Runtime staged the Editor AxleScripts DLL with UI linkage; a source preprocessor guard alone did not isolate shipping scripts. | **FIXED**: distinct AxleRuntimeScripts target, AXLE_SHIPPING, empty generated baseline, SceneData/Platform links only; actual DLL ABI/build identity/count are tested. |
| F7 / P2 | A selected-plugin project with an empty scene could start without selected modules and report success. | **FIXED**: early explicit rejection until selected static composition is delivered; empty selection remains supported. |
| F8 / P2 | Actual Runtime smoke lost Vietnamese characters in narrow Windows CLI arguments. | **FIXED**: wide entry point, stable UTF-8 argv storage, PathFromUtf8 project parsing and UTF-8 path logs/titles; native Unicode project tests pass. |

All repairs are in `bd22550`. There were **zero Engine source changes**, no vendor
upgrades and no edits to `Samples/`. The Vehicle plugin's own example was corrected
within the released plugin scope. Existing Create Plugin UI and unrelated work are
preserved. No push or OPT-09 release occurred.

## What is actually delivered

The native Vehicle DLL has owned Chassis/Wheel data, codecs, reference remapping,
Inspectors, telemetry callbacks and debug contributions. Its Jolt driving system
and audio/VFX adapters remain separate development prototypes, not connected to
the DLL Start/Update path. This review does not claim a drivable native vehicle.

Native Spline/Terrain own versioned spline/heightfield data and provide Inspectors
and transformed line/grid previews during Play. Their advanced curve/track mesh,
gizmos, sculpting, chunked LOD, carving and Jolt collider algorithms remain preserved
development prototypes with their existing regression tests. Their private Engine
objects are not exposed through the supported DLL SDK. Future feature integration
must use supported generic host operations instead of diagnostic address casts.

Project scripts retain the existing matched script ABI. The empty shipping baseline,
UI/domain exclusion and script lifecycle are verified; a new native plugin-aware
scripting API or full project script dependency feature is not claimed.
Selected static player composition and exact packaged content remain PLG-22/23.
The current player rejects nonempty selections, including an empty start scene,
rather than silently running without them.

## Executed evidence

Generated logs are in **`build/review-opt0708/`**. These are actual Codex runs,
separate from Gemini's initial reported results.

- Before repair: real Vehicle SDK build passed; Spline standalone configure failed;
  Terrain selection failed discovery. `candidate-axle.{vehicle,spline,terrain}.log`
  and their emitted build-log paths retain that evidence.
- Debug and Release: build actual foundation, existing domain regressions, script
  boundary/lifecycle, Editor, Runtime and restart helper targets: **exit 0**.
  `build-focused-debug.log`, `build-final-debug.log`, `build-final-release.log`,
  `build-last-{debug,release}.log`, `build-runtime-unicode-{debug,release}.log`.
- Focused CTest **11/11 PASS per configuration**, exit 0:
  `tests-final-debug.log`, `tests-final-release.log`.
- After the final Unicode product repair, script boundary and architecture checks
  pass **2/2 per configuration**, exit 0: `last-runtime-{debug,release}-tests.log`.
  The affected Runtime render test target also rebuilds successfully against the
  shipping script target; its broader rendering feature suite was not run here.
- `OptionalPluginFoundationTests` compiles and activates the six actual DLLs from
  a Unicode-path disposable project. It checks effective dependency selection,
  late rehydration, all loaded module handles, incompatible schema and malformed
  count rejection, per-entity authored payload roundtrip, sample readiness/copy,
  real host ImGui Inspector rendering, three Play/Stop cycles, retained extracted
  contributions, unchanged authored data, complete unload and marker cleanup.
- Native matrix **6/6 expected outcomes per configuration**, including explicit
  rejection exit 1: `native-{debug,release}.json` and corresponding per-case logs.
  Real Editor startup on D3D12 and Vulkan binds the exact six-DLL receipt graph,
  acknowledges five completed frames and tears down cleanly. Real empty Runtime
  presents five frames per backend with the shipping script DLL. Selected Runtime
  fails before GPU initialization with a usable diagnostic. Both use Unicode
  project paths. `fixture-{debug,release}.json` identifies disposable projects.
- Architecture checks cover custom SDK module declarations and their implicit host
  links, split header roots and Vehicle ownership; they retain forbidden-edge
  detection. Baseline tooling regressions pass. No boundary suppression was added.

Focused CTest expression (both Debug and Release):

```powershell
ctest --test-dir build -C Debug -R '^(OptionalPluginFoundationTests|VehiclePluginDataTests|VehiclePhysicsTests|VehicleEditorTests|VehicleAudioVFXTests|SplinePluginTests|TerrainPluginTests|ProjectScriptBoundaryTests|ScriptLifecycleReviewTests|ArchitectureBoundaryChecks|BaselineToolRegressions)$' --output-on-failure
```

The explicit native smoke is reproducible with:

```powershell
build/Tests/Debug/AxleOptionalPluginFoundationTests.exe "C:/Program Files/CMake/bin/cmake.exe" D:/WorkSpace/AxleEngine/Plugins
# Use its emitted Project and Fingerprint in the acknowledged native startup test:
build/Tests/Debug/AxlePluginRestartTests.exe --product build/Editor/Debug/AxleEditor.exe <project> <fingerprint> dx12
build/Tests/Debug/AxlePluginRestartTests.exe --product build/Editor/Debug/AxleEditor.exe <project> <fingerprint> vulkan
```

The separate-context reviewer identified F6/F7 and rechecked their repairs plus
the native ownership/codecs/dependencies, final Unicode argument conversion,
restart backend validation and architecture tightening. It reported no remaining
concrete P1/P2 in that scope. Its recheck was **source-only**; no independent build,
test execution or native UI/GPU run is attributed to it.

No full CTest rerun, detailed feature quality, domain performance, native driving,
complete terrain/track rendering, or selected packaged-player acceptance is claimed.
The existing domain tests passing are regression evidence for preserved prototypes,
not proof that those advanced features are integrated into the native DLLs.
