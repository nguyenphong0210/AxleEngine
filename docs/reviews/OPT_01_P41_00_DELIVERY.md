# Gemini Delivery: OPT-01 / P41-00 Baseline and Always-Active Render Tests

Date: **2026-11-05**  
Implementer: **Gemini**  
Coordinator/Reviewer: **Codex**  
Scope: **P41-00 only** (per `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` and user authorization)  
Status: **Delivered (pending Codex independent review)**

---

## 1. Summary of Delivered Work

In strict accordance with the P41-00 requirements:
1. **Isolated Device-Free Render Regression Target (`AxleRenderUnitTests`)**:
   - Authored test suite under `Tests/rendering/` requiring no GPU adapter, display server, graphics context, window, or NRI device creation:
     - `RenderTestCommon.hpp`: Common assertion macros (`RENDER_CHECK`, `Check`) and floating-point / vector near-equality comparators (`Near`).
     - `Test_RenderContractsUnit.cpp`: Pure CPU contracts covering `RHIType` bidirectional string mapping and `GraphicsAPI` conversion; `RHIVertex3D` cache-line alignment (exactly 64 bytes) and member offsets; `RHISkinnedVertex3D` layout (exactly 96 bytes) and offsets; NRI vertex attribute and stream descriptors; MikkTSpace tangent generation and normal orthogonality; `RHIBarrier` access bitwise operators, `RHIAccess` to NRI bit conversions, `RHIPipelineStage` to NRI stage mappings, and default layout access deductions; `RHISampler` address mode, filter, and compare op mappings.
     - `Test_RenderBackendPolicyUnit.cpp`: CLI argument parsing via `RHISwitcher::ParseArgument` and `ParseCommandLine` (`--rhi=dx12`, `--rhi=vulkan`, `--rhi=opengl`, `--no-fallback`, `--strict-rhi`, `--rhi-validation`, `--adapter=N`, invalid backend fallback); device desc default policies.
     - `Test_RenderPrimitiveIdentityUnit.cpp`: Builtin primitive canonical UUIDs (`0xBA5E0001` - `0xBA5E0005`) and URIs (`builtin://cube`, `builtin://sphere`, `builtin://cylinder`, `builtin://cone`, `builtin://plane`); CPU procedural geometry generation contracts (`GenerateCube`, `GenerateSphere`, `GeneratePlane`, `GenerateCylinder`, `GenerateCone`), index integrity (multiples of 3, vertex index bounds), normal unit lengths, tangent orthogonality, and coordinate bounds.
     - `RenderUnitTestMain.cpp`: Entry point supporting intentional `--self-test-failure` probe.
2. **Always-Active CTest Harness Integration**:
   - Added `AxleRenderUnitTests` to `Tests/CMakeLists.txt` and `AXLE_TEST_TARGETS`.
   - Registered `RenderUnitTests` test in CTest (labels `render;cpu`).
   - Registered `RenderHarnessFailure` test running `AxleRenderUnitTests --self-test-failure` with `WILL_FAIL TRUE` (labels `render;cpu`), verifying that harness detection and failure propagation are always active.
   - Added opt-in physical GPU test option: `option(AXLE_ENABLE_RENDER_GPU_TESTS "Register physical D3D12/Vulkan render tests" OFF)`.
3. **Conversion of Side-Effecting Assertions in Touched Legacy Tests**:
   - Audited and updated touched RHI tests in `Tests/src/Test_Milestone8_Part2.cpp` (Step 51, Step 52, Step 53).
   - Extracted state-changing calls (e.g. `pipelineLayout.Init`, `cmdBuf.Init`, `cmdBuf.Begin`, `cmdBuf.End`, `cmdBuf.Submit`, `surface.Init`, `swapChain.Init`, `swapChain.BeginFrame`, `frameCmd.TransitionTexture`, `swapChain.PresentFrame`) into explicit boolean results prior to assertion, preventing side-effect stripping under non-assert build configurations.

---

## 2. Verification Evidence

### 2.1 Direct Test Execution
- **Debug Configuration**:
  - `.\build\Tests\Debug\AxleRenderUnitTests.exe` -> Exit Code: 0 (All suites PASS).
  - `.\build\Tests\Debug\AxleRenderUnitTests.exe --self-test-failure` -> Exit Code: 1 (Intentional failure correctly reported).
- **Release Configuration**:
  - `.\build\Tests\Release\AxleRenderUnitTests.exe` -> Exit Code: 0 (All suites PASS).
  - `.\build\Tests\Release\AxleRenderUnitTests.exe --self-test-failure` -> Exit Code: 1 (Intentional failure correctly reported).

### 2.2 CTest Regression Suites
- **Debug Configuration**:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: **18/18 passed (100%)**, 11.24s.
  - Render tests: `RenderUnitTests` Passed (0.01s), `RenderHarnessFailure` Passed (0.01s, `WILL_FAIL TRUE` verified).
- **Release Configuration**:
  - `ctest --test-dir build -C Release --output-on-failure`
  - Result: **18/18 passed (100%)**, 10.64s.
  - Render tests: `RenderUnitTests` Passed (0.01s), `RenderHarnessFailure` Passed (0.01s, `WILL_FAIL TRUE` verified).

### 2.3 Architecture & Tooling Regressions
- `python scripts/check_architecture.py --root .`:
  - Result: `AXLE_ARCH_CHECK PASS` (344 files scanned, 17 CMake files).
- `python scripts/test_baseline_tools.py`:
  - Result: 18/18 tests passed (`OK`).

### 2.4 Product Builds
- `AxleEditor` and `AxleRuntime` built cleanly in both Debug and Release configurations without errors or warnings.

---

## 3. Scope Exclusions & NOT RUN Boundaries

- **NOT RUN**: Physical GPU device verification (hardware D3D12/Vulkan adapter readback), interactive windowed present checks, and standalone player export execution.
- **NO SCOPE CREEP**: Did not implement P41-01..29, Phase 40 D1/D2/G2, PLG-00, AF-02+, or full renderer rewrites.
- **PRESERVED BASELINE**: Known baseline ECS/jobs defects preserved as visible per contract.

