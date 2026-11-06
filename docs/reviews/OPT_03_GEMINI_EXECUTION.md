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

---

## Package EJ-08: Parallel EnTT View Correctness

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `374933a`
- **Defect Resolved:** `ECSJ-04` (`ParallelECS::ForEach` visited tombstones in EnTT stable/in_place_delete pools).

### Implementation Summary
1. **Parallel View Iteration Correctness (`ParallelECS::ForEach`):**
   - Fixed entity partitioning: rather than indexing raw contiguous memory `view.handle()->data()` which contains uncompacted `entt::tombstone` slots for pools with `in_place_delete = true`, `ForEach` iterates `for (auto entity : view)`.
   - View iteration automatically skips tombstones and evaluates multi-component membership across all participating component pools.
   - For parallel execution above `minBatchSize`, valid entities are collected and distributed via `JobSystem::ParallelForRange`.
   - Introduced C++20 `GetViewSizeHint` to handle `size()` on standard storage views, `size_hint()` on in-place delete storage views, and `basic_view`.
2. **Target Deduplication in `ParallelECS::ForEachEntity`:**
   - Added filtering and deduplication via `std::unordered_set<entt::entity> seen`.
   - Checks `e != entt::null && e != entt::tombstone && registry.valid(e) && view.contains(e)` to guarantee disjoint, unique write targets across worker threads.
3. **Defect Conversion and Comprehensive Probe Expansion:**
   - Converted `ecsj-04-entt-tombstone` in `scripts/run_jobs_ecs_harness.py` from `REPRODUCED_DEFECT` to expected `PASS`.
   - Expanded `RunEnTTTombstoneProbe()` in `Tests/src/JobsECSTestMain.cpp` to verify:
     - Single-component stable storage tombstone skipping (original ECSJ-04 repro case).
     - High-volume (2000 entities) multi-component view with interleaved tombstones, asserting exact entity set equivalence between serial EnTT view and parallel execution, plus component value integrity across threads.
     - `ForEachEntity` deduplication and tombstone/null filtering with duplicate inputs.

### Verification Evidence
- `cmake --build build --config Debug --target AxleJobsECSTests`: exit 0.
- `cmake --build build --config Release --target AxleJobsECSTests`: exit 0.
- `build/Tests/Debug/AxleJobsECSTests.exe ecsj-04-entt-tombstone`: exit 0, PASS (sequential=1 parallel=1 multiCount=666 dedupCount=66 invalidCallbacks=0).
- `build/Tests/Release/AxleJobsECSTests.exe ecsj-04-entt-tombstone`: exit 0, PASS.
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Release/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `ctest --test-dir build -C Debug --output-on-failure`: 32/32 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 32/32 tests passed (100%).
- `python scripts/check_architecture.py --root . --closed-package AF-02`: exit 0, PASS.
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Package EJ-09: Scene Command and Mutation Boundaries

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `4286425`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md)

### Implementation Summary
1. **Scene Command Buffer (`Axle::SceneCommandBuffer`):**
   - Introduced dedicated lifecycle-aware `SceneCommandBuffer` in `Engine/Scene/include/Axle/SceneCommandBuffer.hpp` and `Engine/Scene/src/SceneCommandBuffer.cpp`.
   - Supports thread-safe recording of `DestroyEntity`, `SetParent`, `CreateEntity`, `CreateEntityWithUUID`, `EmplaceOrReplace`, `Remove`, and `Custom` operations.
   - Enforces scene generation, entity generation, and conflict resolution rules during execution.
2. **Conflict & Boundary Resolution:**
   - **Destroy-then-edit rejection:** Tracks all entities destroyed during execution (including recursive hierarchy descendants); subsequent mutation commands targeting destroyed entities are safely rejected.
   - **Wrong-scene boundary enforcement:** Rejects any command whose entity target belongs to a different scene instance.
   - **Post-join merge & execution:** Commands are merged (`bufferA.Merge(std::move(bufferB))`) and executed on the calling thread only after parallel worker batches join.
3. **Hierarchy & Lifecycle Teardown Hardening:**
   - Hardened `Scene::DestroyEntity`:
     - Value-copies `parentEntity` and `childrenCopy` before recursive destruction, preventing reference invalidation when component pools reallocate during child destruction.
     - Performs clean Native Script `OnDestroy` and instance teardown.
     - Automatically cleans up physics bodies in `PhysicsScene` (using explicit or `m_RuntimePhysicsScene`).
     - Cleans up `AudioSourceComponent`, `CharacterControllerComponent`, and `CharacterVFXComponent`.
   - Hardened `Entity::SetParent`: rejects cross-scene parenting, self-parenting, and cycle creation; safely copies old parent handle before pool modifications.
   - Added `Entity::Destroy()` convenience method delegating to `Scene::DestroyEntity()`.
4. **Dedicated Test Fixture (`AxleSceneMutationUnitTests`):**
   - Created `Tests/src/Test_SceneCommandAndMutationUnit.cpp` registered in `Tests/CMakeLists.txt` and CTest.
   - Verifies:
     - `TestParentChildRecursiveDestroy`: full subtree destruction without dangling references.
     - `TestChildUnparentingAndDestruction`: single-child destruction with clean parent child list pruning.
     - `TestScriptAndPhysicsTeardownOnDestroy`: verified script `OnDestroy` execution and physics body removal from `PhysicsScene`.
     - `TestCommandBufferDestroyThenEditConflict`: verified destroy-then-edit conflict rejection.
     - `TestWrongSceneCollisionRejection`: verified cross-scene parenting and mutation rejection.
     - `TestMultiThreadedRecordingAndPostJoinCommit`: concurrent recording from 32 tasks across worker threads with deterministic post-join commit.
     - `TestCommandBufferMerge`: buffer move-merge and sequential execution.

### Verification Evidence
- `cmake --build build --config Debug --target AxleSceneMutationUnitTests AxleFoundationUnitTests AxleJobsECSTests AxleEditor AxleRuntime`: exit 0.
- `cmake --build build --config Release --target AxleSceneMutationUnitTests`: exit 0.
- `build/Tests/Debug/AxleSceneMutationUnitTests.exe`: exit 0, ALL SCENE MUTATION UNIT TESTS PASSED.
- `build/Tests/Release/AxleSceneMutationUnitTests.exe`: exit 0, ALL SCENE MUTATION UNIT TESTS PASSED.
- `ctest --test-dir build -C Debug --output-on-failure`: 33/33 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 33/33 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-02`: exit 0, PASS (344 files, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.
