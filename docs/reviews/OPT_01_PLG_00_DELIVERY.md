# Gemini Delivery: OPT-01 / PLG-00 Baseline and Retirement Inventory

Date: **2026-11-05**  
Implementer: **Gemini**  
Coordinator/Reviewer: **Codex**  
Scope: **PLG-00 only** (per `docs/design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md` and user authorization)  
Status: **Delivered (pending Codex independent review)**

---

## 1. Summary of Delivered Work

In strict accordance with the PLG-00 requirements:
1. **Authored Comprehensive Baseline & Retirement Inventory Report**:
   - Created [current inventory](../architecture/PLUGIN_BASELINE_INVENTORY.md) (original delivery path: `docs/design/PLUGIN_BASELINE_INVENTORY.md`).
   - Analyzed current build & module link topology: root `CMakeLists.txt`, monolithic static libraries `AxleSpline` and `AxleTerrain`, and static linkage into `AxleEditor` and `AxleRuntime`.
   - Documented Editor and Runtime startup sequencing (`EditorLayer::OnAttach`, `RuntimeLayer::OnAttach`) and `Scene::OnUpdateRuntime` lifecycle execution flow.
   - Audited physics step ordering (`PhysicsScene::Step`), confirming Jolt simulation boundaries and complete separation from domain-specific vehicle code.
   - Cataloged all consumers of Spline and Terrain includes and link libraries across `Editor/`, `Runtime/`, and `Tests/`.
   - Established the exhaustive Vehicle Monolith Retirement Ledger:
     - Documented retired components (`VehicleComponent`, `VehicleWheelComponent`, `VehicleAudioComponent`, `VehicleVFXComponent`) and module removal (`fa74981`, `be7b071`).
     - Separated general physics (retained in `Engine/Physics`), vehicle behavior (to be re-implemented cleanly in `axle.vehicle` under PLG-11..17), domain-specific demo scripts (`CarController`, `RacingHUD`, `VehicleChaseCamera`), and obsolete APIs (`GetRuntimeVehicle`).
     - Classified remaining authored scene and assembly assets (`SportsCar_Assembly.axle`, `CarTest.axlescene`, etc.) under the clean Engine contract: unknown legacy tags are ignored safely without crashing or polluting Engine core.
2. **Dedicated Device-Free Plugin Baseline Test Target (`AxlePluginUnitTests`)**:
   - Authored test suite under [`Tests/plugin/`](../../Tests/plugin/):
     - `PluginTestCommon.hpp`: Assertion macros (`PLUGIN_CHECK`, `Check`) and floating-point near-equality comparator (`Near`).
     - `Test_PluginBaselineUnit.cpp`: 6 isolated unit tests verifying:
       1. `SplinePlugin` reflection metadata and lifecycle re-entrance.
       2. `TerrainPlugin` reflection metadata and lifecycle re-entrance.
       3. Plugin data structure independence (`Curve3D` operates without `TerrainData`, `TerrainData` operates independently).
       4. General physics lifecycle without vehicle (dynamic box falls under gravity on static ground without any vehicle constraint).
       5. Clean Engine scene lifecycle without vehicle (standard entities update and stop cleanly without vehicle components).
       6. Generic `UIDataContext` telemetry bindings (evaluating, overriding, and querying bindings independently of concrete vehicle classes).
     - `PluginUnitTestMain.cpp`: Always-active harness entry point supporting intentional `--self-test-failure` probe.
3. **Always-Active CTest Harness Integration**:
   - Added `AxlePluginUnitTests` to `Tests/CMakeLists.txt` and `AXLE_TEST_TARGETS`.
   - Registered `PluginUnitTests` test in CTest (labels `plugin;cpu`).
   - Registered `PluginHarnessFailure` test running `AxlePluginUnitTests --self-test-failure` with `WILL_FAIL TRUE` (labels `plugin;cpu`), verifying that harness detection and failure propagation are always active.

---

## 2. Verification Evidence

### 2.1 Direct Test Execution
- **Debug Configuration**:
  - `.\build\Tests\Debug\AxlePluginUnitTests.exe` -> Exit Code: 0 (All suites PASS).
  - `.\build\Tests\Debug\AxlePluginUnitTests.exe --self-test-failure` -> Exit Code: 1 (Intentional failure correctly reported).
- **Release Configuration**:
  - `.\build\Tests\Release\AxlePluginUnitTests.exe` -> Exit Code: 0 (All suites PASS).
  - `.\build\Tests\Release\AxlePluginUnitTests.exe --self-test-failure` -> Exit Code: 1 (Intentional failure correctly reported).

### 2.2 CTest Regression Suites
- **Debug Configuration**:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: **20/20 passed (100%)**, 11.44s.
  - Plugin tests: `PluginUnitTests` Passed (0.03s), `PluginHarnessFailure` Passed (0.02s, `WILL_FAIL TRUE` verified).
- **Release Configuration**:
  - `ctest --test-dir build -C Release --output-on-failure`
  - Result: **20/20 passed (100%)**, 10.74s.
  - Plugin tests: `PluginUnitTests` Passed (0.02s), `PluginHarnessFailure` Passed (0.01s, `WILL_FAIL TRUE` verified).

### 2.3 Architecture & Tooling Regressions
- `python scripts/check_architecture.py --root .`:
  - Result: `AXLE_ARCH_CHECK PASS` (344 files scanned, 17 CMake files).
- `python scripts/test_baseline_tools.py`:
  - Result: 18/18 tests passed (`OK`).

### 2.4 Product Builds
- `AxleEditor` and `AxleRuntime` built cleanly in both Debug and Release configurations without errors or warnings.

---

## 3. Scope Exclusions & NOT RUN Boundaries

- **NOT RUN**: Physical GPU hardware execution, interactive windowed present checks, and standalone player export packaging.
- **NO SCOPE CREEP**: Did not implement PLG-01..25, full dynamic DLL loading, hot-reloading, or new vehicle plugin implementations.
- **PRESERVED BASELINE**: Known baseline ECS/jobs defects preserved as visible per contract; no premature deletion of authored asset fixtures.
