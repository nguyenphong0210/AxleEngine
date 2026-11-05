# OPT-01 Baseline Delivery: AF-00 & EJ-00

Date: **2026-11-05**
Author/Implementer: **Gemini**
Coordinator/Reviewer: **Codex**
Scope: **AF-00** (Shared baseline and architecture checks) and **EJ-00** (Permanent baseline and meaningful jobs/ECS harness)
Starting baseline commit: `8ad5d75`
Preparation baseline: `842cf7d`

---

## 1. Scope and Authority

Per user instructions and handoff (`docs/agents/handoffs/OPT_01_AF00_EJ00_GEMINI.md`):
- Work is strictly bounded to **AF-00 + EJ-00 only**, in that order. Packages AF-01, P41-00, PLG-00, and OPT-02+ remain deferred.
- **Engine cleanliness prioritized:** No old data formats, legacy Vehicle payloads, compatibility readers, or migration code are supported or retained.
- **Honest defect tracking:** Known engine defects are reproduced and asserted against their expected correct behaviors. They fail honestly and are recorded as `REPRODUCED_DEFECT`, not hidden, waived, or converted into false `PASS` results.
- **No production threading/ECS/Jolt modifications:** Production code in threading, ECS, compiler reload, and Jolt remains untouched in this baseline round.

---

## 2. AF-00: Shared Baseline & Architecture Checks

### 2.1 Boundary Rules & Inventory Tooling
- Implemented `scripts/check_architecture.py` and `scripts/architecture_rules.json`.
- Validates both CMake target linkage (`target_link_libraries`) and C++ source `#include` dependencies across all engine subsystems, plugins, tools, and applications.
- Enforces strict hierarchical layering:
  - **Level 0 (Foundation):** `Core`
  - **Level 1 (Hardware / Services):** `Platform`, `RHI`, `Audio`, `Physics`
  - **Level 2 (Resources / Visuals):** `Asset`, `Renderer`, `Animation`, `VFX`, `Project`, `UI2D`
  - **Level 3 (Scene / Simulation):** `Scene`
  - **Level 4 (Plugins):** `Spline`, `Terrain`
  - **Level 5 (Applications & Tooling):** `UI` (Dear ImGui editor tooling), `Editor`, `Runtime`

### 2.2 Narrow Expiring Baseline Allowances
Existing baseline architectural violations are cataloged into 10 explicit, narrow allowances with owning packages and expiration conditions:
1. `ALLOW-001` (`AF-02`): `Application.cpp` includes `<Axle/Window.hpp>` and `<Axle/Input.hpp>`.
2. `ALLOW-002` (`AF-02`): `Application.cpp` directly invokes `PhysicsEngine::Init()` / `Shutdown()`.
3. `ALLOW-003` (`AF-02`): `Application.cpp` directly calls `RenderCommand::Init()`.
4. `ALLOW-004` (`AF-04` / `P41-11`): `AssetManager` references `Texture2D` and links `Axle::Renderer`.
5. `ALLOW-005` (`AF-04` / `P41-11`): `AssetSerializer` includes `Axle/RHI/RHIDevice.hpp` and links `Axle::RHI`.
6. `ALLOW-006` (`P41-25`): `PhysicsDebugRenderer` draws through `RenderCommand` and links `Axle::Renderer`.
7. `ALLOW-007` (`P41-25`): `RHIPhysicsDebugRenderer` records RHI command buffers and links `Axle::RHI`.
8. `ALLOW-008` (`AF-01`): `PhysicsMaterial` includes `Axle/AssetDSL.hpp` and links `Axle::Asset`.
9. `ALLOW-009` (`PLG-19`): `Spline` plugin embeds ImGui editor tooling and links `Axle::UI`.
10. `ALLOW-010` (`PLG-19`): `Terrain` plugin embeds ImGui editor tooling and links `Axle::UI`.

### 2.3 Intentional Architecture Violation Fixture
- Created `Tests/Fixtures/ArchitectureViolationFixture/`.
- Introduces forbidden upward includes (`Axle/Scene.hpp` and `Axle/ImGuiLayer.hpp` inside `Engine/Core`) and unallowed CMake target links (`Axle::Scene` and `Axle::UI` in `AxleCore`).
- Verified: Running `scripts/check_architecture.py` against this fixture detects all 4 violations and exits with returncode `1`. Registered in CTest as `ArchitectureForbiddenEdgeFixture` with `WILL_FAIL TRUE`.

---

## 3. EJ-00: Permanent Meaningful Jobs/ECS Fault Harness

### 3.1 Test Target & Process Watchdog Runner
- Created test target `AxleJobsECSTests` in `Tests/CMakeLists.txt` (`EXCLUDE_FROM_ALL`, CPU-only, links `Axle::Core`, `Axle::Physics`, `Axle::Scene`; no Editor/Runtime dependencies).
- Created Python runner `scripts/run_jobs_ecs_harness.py`.
- Isolates every scenario into its own subprocess with an explicit watchdog timeout, logs stderr/stdout, and records structured results in JSON.

### 3.2 Matrix of Fault Probes & Controls
1. **ECSJ-01 (`ecsj-01-compiler-thread`):**
   - Calls `ScriptCompiler::Get().CompileAsync(...)` against an isolated CMake fixture.
   - Assert: Callback runs on the owning/main thread.
   - Result: `REPRODUCED_DEFECT` (exit 1). Callback ran on `m_WorkerThread` (`callbackOnMain=0`).
2. **ECSJ-02 (`ecsj-02-thread-identity`):**
   - Spawns external `std::thread`, queries `JobSystem::GetCurrentThreadIndex()` and `IsMainThread()`, calls `DispatchToMainThread`.
   - Assert: External thread index != 0, `IsMainThread() == false`, callback does not run before pump.
   - Result: `REPRODUCED_DEFECT` (exit 1). External thread recognized as thread 0 (`wrapper index=0`, `incorrectlyMain=1`), callback executed inline before pump (`calledBeforePump=1`).
3. **ECSJ-03 Hang (`ecsj-03-bridge-hang`):**
   - `JobSystem::Init(1)`, creates Jolt job on barrier, waits for barrier, destroys bridge without draining enki.
   - Assert: Bridge destructor completes forward progress without hang.
   - Result: `REPRODUCED_DEFECT` (timed out after 4.0s; watchdog killed process). Spins yielding in `~JobSystemEnkiTS()` on active tasks.
4. **ECSJ-03 Control (`ecsj-03-bridge-control`):**
   - Identical setup, but explicitly executes `JobSystem::WaitForAll()` before bridge destruction.
   - Result: `PASS` (exit 0). Bridge destroyed cleanly.
5. **ECSJ-04 (`ecsj-04-entt-tombstone`):**
   - Creates entities with `in_place_delete` stable component, removes one (creating tombstone), runs `ParallelECS::ForEach` with `minBatchSize=1`.
   - Assert: Parallel iteration skips tombstones and matches sequential view count.
   - Result: `REPRODUCED_DEFECT` (exit 1). Raw packed array visited tombstone (`sequential=1 parallel=2 invalidCallbacks=1`).
6. **Current-Format Scene & Physics Lifecycle (`current-format-lifecycle`):**
   - Tests current-format Scene Copy, DuplicateEntity with PhysicsScene tracking, SceneSerializer round trip, AssemblySerializer save/load, and ordinary-body Play/Stop physics simulation.
   - Result: `PASS` (exit 0). Body fell under gravity (`y=10.0 -> 9.78828`), body count strictly 1, clean shutdown.
7. **Intentional Failure Probe (`deliberate-failure`):**
   - Returns exit code 1 to prove runner detects nonzero exit code.
   - Result: `PROBE_VERIFIED` (exit 1).
8. **Watchdog Hang Probe (`watchdog-hang`):**
   - Spins in an infinite loop to verify watchdog process termination.
   - Result: `WATCHDOG_TERMINATED` (killed after 2.5s).

---

## 4. Verification Evidence

### 4.1 CTest Results (Both Debug and Release)
Commands executed:
- `ctest --test-dir build -C Debug --output-on-failure` : **11/11 tests passed (100%)** in 3.08s.
- `ctest --test-dir build -C Release --output-on-failure` : **11/11 tests passed (100%)** in 4.73s.

Passing registered tests:
1. `MaterialUnitTests` (PASS)
2. `VFXUnitTests` (PASS)
3. `AnimationUnitTests` (PASS)
4. `AnimationHarnessFailure` (PASS, WILL_FAIL)
5. `ProjectStartupTests` (PASS)
6. `JobsECS_Control` (PASS)
7. `JobsECS_HarnessFailure` (PASS, WILL_FAIL)
8. `ArchitectureBoundaryChecks` (PASS)
9. `ArchitectureForbiddenEdgeFixture` (PASS, WILL_FAIL)
10. `BuildWorkflowFixtureConfigure` (PASS)
11. `BuildWorkflowTests` (PASS)

### 4.2 Permanent Harness Runner Results
Commands executed:
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe --report docs/reviews/OPT_01_BASELINE_RESULTS_DEBUG.json` : exit 0, all 10 scenarios behaved as expected.
- `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe --report docs/reviews/OPT_01_BASELINE_RESULTS_RELEASE.json` : exit 0, all 10 scenarios behaved as expected.
- Saved unified default report: `docs/reviews/OPT_01_BASELINE_RESULTS.json`.

### 4.3 Full Integration Runner (`AxleTests.exe`)
Command executed:
- `build/Tests/Debug/AxleTests.exe` : exit 0.
- All 68 test steps (including VFX Step 47, RHI/NRI physical D3D12/Vulkan on RTX 3070, UI2D, and PBR shaders) passed 100%.

### 4.4 Product Builds & Native Executables
- `AxleEditor` and `AxleRuntime` successfully rebuilt in both Debug and Release configurations (exit 0).
- `AxleAllTests` aggregate target builds successfully in both configurations (exit 0).

---

## 5. Limitations & Boundaries

1. **Defects Unfixed by Design:** Threading, script compiler completion, external thread wrapper index, Jolt/enki completion tracking, and EnTT tombstone iteration remain unfixed in this baseline phase. They are scheduled for repair in `OPT-02` (`EJ-01..07`) and `OPT-03` (`EJ-08`).
2. **ECSJ-05 Scheduling Parity:** Preserved as a source-confirmed discrepancy between EditorLayer and RuntimeLayer call order. No speculative numerical gameplay harness was fabricated.
3. **NOT RUN:** Native interactive editor mouse manipulations (e.g. manual Inspector dragging) and standalone game deployment packaging were not run manually.
4. **No push:** Work is committed locally and awaits Codex independent review before any subsequent packages are started.
