# OPT-03 Execution Report: Separate Core, Scene Data and Assets; Unify Simulation

Evolving execution log and verification record for the sequential implementation of **OPT-03 (AF-02, EJ-08..EJ-10, AF-03, AF-04, EJ-11..EJ-14)**, authored by Gemini under the authorized autonomous architecture plan ([GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md](../agents/handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md)).

---

## Package AF-02: Foundation, Application and Explicit Service Ownership

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `98fe297`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md)

### Implementation Summary
1. **Extraction of Application & Bootstrap from Core into Platform Composition:**
   - Moved `Application.hpp`, `ApplicationUI.hpp`, `Layer.hpp`, `LayerStack.hpp` from `Engine/Core/include/Axle/` to `Engine/Platform/include/Axle/`.
   - Moved `Application.cpp`, `LayerStack.cpp` from `Engine/Core/src/` to `Engine/Platform/src/`.
   - Purged all upward includes (`Window.hpp`, `Input.hpp`, `RenderCommand.hpp`, `PhysicsEngine.hpp`, `<GLFW/glfw3.h>`) and link/include dependencies (`glfw`, `Engine/Platform/include`, `Engine/Renderer/include`, `Engine/Physics/include`) from `Engine/Core`.
   - `AxleCore` is now purely Level 0 Foundation (`Log`, `FileSystem`, `UUID`, `JobSystem`, `Package`), linking only `spdlog` and `enkiTS`.
2. **Explicit Service Ownership on Application:**
   - Introduced `IApplicationService` interface with `OnInit()`, `OnUpdate(ts)`, `OnShutdown()`.
   - Added service registration on `Application`: `RegisterService()`, `UnregisterService()`, `GetService()`, `GetService<T>()`.
   - Services are shut down in reverse registration order during `Application::~Application()`.
   - Added optional host callbacks on `ApplicationSpecification`: `RendererInitCallback`, `ViewportResizeCallback`, `RendererShutdownCallback`.
3. **Product & Host Integration:**
   - Updated `Editor/src/Main.cpp` and `Runtime/src/Main.cpp` to register `PhysicsApplicationService` and supply OpenGL renderer init/resize callbacks explicitly.
4. **Cancellation Race Hardening in JobSystem:**
   - Hardened `JobHandle::Cancel()` by tracking task start in `Detail::AsyncLambdaTask`: tasks that have already started execution cannot be cancelled.
   - Hardened `ej07-handles-shutdown` probe in `JobsECSTestMain.cpp` to ensure scheduler worker saturation before cancellation verification.
5. **Architectural Allowance Retirement:**
   - Retired `ALLOW-001`, `ALLOW-002`, `ALLOW-003` from Core: verified `check_architecture.py --root . --closed-package AF-02` passes with zero violations.
6. **Dedicated Foundation Unit Test Target:**
   - Added `AxleFoundationUnitTests` covering pure headless Core (Log, UUID, FileSystem, JobSystem) and Application service registration/lifecycle/reverse-shutdown.

### Verification Evidence
- `cmake --build build --config Debug --target AxleCore AxlePlatform AxleFoundationUnitTests AxleJobsECSTests AxleEditor AxleRuntime AxleTests`: exit 0.
- `cmake --build build --config Release --target AxleCore AxlePlatform AxleFoundationUnitTests AxleJobsECSTests`: exit 0.
- `build/Tests/Debug/AxleFoundationUnitTests.exe`: exit 0, PASS (100%).
- `build/Tests/Release/AxleFoundationUnitTests.exe`: exit 0, PASS (100%).
- `python scripts/check_architecture.py --root . --closed-package AF-02`: exit 0, PASS (342 files scanned, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (14 PASS, 1 reproduced defect ECSJ-04 preserved, 3 verified probes, 0 unexpected failures).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe`: exit 0 (14 PASS, 1 reproduced defect ECSJ-04 preserved, 3 verified probes, 0 unexpected failures).
- `ctest --test-dir build -C Debug --output-on-failure`: 32/32 passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 32/32 passed (100%).
- `git diff --check`: exit 0.
