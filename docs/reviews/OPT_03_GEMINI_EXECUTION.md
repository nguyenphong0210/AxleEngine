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

---

## Package EJ-10: Transform Evaluation Ownership

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `cc059af`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) / [ENTT_ENKITS_REFACTOR_PLAN.md](../design/ENTT_ENKITS_REFACTOR_PLAN.md)

### Implementation Summary
1. **Explicit Write/Recompute vs Pure Read-Only Queries:**
   - Eliminated const-query cache mutations (`const_cast<entt::registry&>` writing `LocalMatrix` and `WorldMatrix` on every read query).
   - `Scene::GetWorldTransform(entityID) const` and `Entity::GetWorldTransform() const` are now pure read-only queries returning `TransformComponent::WorldMatrix`.
   - Introduced explicit write/recompute APIs: `Scene::RecomputeWorldTransform(entityID)`, `Entity::RecomputeWorldTransform()`, and `Scene::UpdateWorldTransforms()`.
   - Published a monotonic transform revision counter (`GetTransformRevision()`) incremented on transform evaluation passes and `Scene::Clear()`.
2. **Parallel Disjoint Subtree Hierarchy Evaluation:**
   - In `ParallelECS::UpdateWorldTransforms`:
     - Added Floyd's Tortoise-and-Hare cycle detection and self-parenting repair, breaking illegal loops before root collection to prevent deadlock/stack overflow.
     - Root entities are partitioned across worker threads via `JobSystem::ParallelForRange`, evaluating disjoint subtrees in parallel with zero lock contention.
     - `UpdateEntitySubtree` enforces a recursion depth guard (`depth > 128`), safe child list value-copying, and strict scale inheritance logic (rigid parent transform without scale when `InheritScale == false`).
3. **Renderer & Runtime Integration:**
   - `RHISceneRenderer::RenderScene` and `RHIObjectPicker::RenderPickingPass` ensure world transforms are evaluated (`if (scene) scene->UpdateWorldTransforms();`) prior to geometry extraction or draw call dispatch.
   - `Scene::OnUpdateRuntime` now evaluates `UpdateWorldTransformsParallel(64)` during frame simulation before audio listener/3D spatial audio synchronization, aligning with `OnUpdateEditor`.
   - `EntityInspector` and `EditorLayer` gizmo manipulation explicitly trigger `RecomputeWorldTransform()`.
4. **Comprehensive Unit Tests (`AxleSceneMutationUnitTests`):**
   - Added 5 dedicated EJ-10 tests:
     - `TestTransformEvaluationSerialVsParallel`: 120 entities across 20 multi-level hierarchy trees; verified serial fallback and parallel evaluation yield identical `WorldMatrix` results.
     - `TestTransformScaleInheritance`: verified rigid parent transform when `InheritScale == false` vs full scale propagation when `InheritScale == true`.
     - `TestTransformCycleDetectionAndRepair`: verified detection and clean resolution of 2-node cycles (A <-> B) and self-parenting (C -> C) without hanging or crashing.
     - `TestTransformRevisionProgression`: verified monotonic revision increments across updates, recomputations, and scene clear.
     - `TestTransformReadOnlyQueryConcurrency`: 8 reader threads executing 50,000 concurrent `GetWorldTransform` queries on hierarchy nodes without data races or corruption.

### Verification Evidence
- `cmake --build build --config Debug --target AxleSceneMutationUnitTests AxleFoundationUnitTests AxleJobsECSTests AxleEditor AxleRuntime AxleTests`: exit 0.
- `cmake --build build --config Release --target AxleSceneMutationUnitTests`: exit 0.
- `build/Tests/Debug/AxleSceneMutationUnitTests.exe`: exit 0, ALL 12 SCENE MUTATION & TRANSFORM UNIT TESTS PASSED.
- `build/Tests/Release/AxleSceneMutationUnitTests.exe`: exit 0, ALL 12 SCENE MUTATION & TRANSFORM UNIT TESTS PASSED.
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 33/33 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 33/33 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-02`: exit 0, PASS (344 files, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Package AF-03: Scene Data and System Bridge Boundaries

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `b434dd0`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md)

### Implementation Summary
1. **Generic SceneData Ownership (`Engine/Scene/include/Axle/SceneData.hpp`, `src/SceneData.cpp`):**
   - Extracted headless `SceneData` owning `entt::registry m_Registry`, `u32 m_SceneGeneration`, monotonic `uint64_t m_TransformRevision`, and `SceneCommandBuffer`.
   - Manages headless entity lifecycle (`CreateEntity`, `CreateEntityWithUUID`, `DestroyEntity`, `DuplicateEntity`, `Clear`, `GetEntityByUUID`).
   - Owns hierarchy traversal and world transform evaluation (`GetWorldTransform`, `RecomputeWorldTransform`, `UpdateWorldTransforms`, `UpdateWorldTransformsParallel`).
   - Deep copying (`SceneData::Copy`): clones all entities, duplicates components via extensible component codecs, and remaps hierarchy relationships without requiring GPU, physics, or script context.
2. **Component Codec Registry (`Engine/Scene/include/Axle/ComponentCodec.hpp`, `src/ComponentCodec.cpp`):**
   - Introduced `IComponentCodec` and `ComponentCodecRegistry` singleton.
   - Built-in codecs for 20+ components (`Transform`, `MeshInstance3D`, `Material`, `DirectionalLight`, `PointLight`, `SpotLight`, `Camera`, `Skybox`, `Assembly`, `CollisionShape3D`, `RigidBody3D`, `NativeScript`, `AudioListener`, `AudioSource`, `SkeletalMesh`, `Animator`, `CharacterController`, `ParticleEmitter`, `CharacterVFX`, `UICanvas`, `PassiveExtensionComponent`).
   - Provides an extensible registration seam for plugin components to register copy/remap codecs cleanly.
3. **Passive Extension Storage:**
   - Introduced `PassiveExtensionComponent` storing unknown or plugin YAML sections (`StorePassiveSection`, `GetPassiveSections`, `HasPassiveSections`).
   - In `SceneSerializer`: unhandled custom component sections are captured into `PassiveExtensionComponent` and re-emitted on serialization.
   - Preserves unknown data roundtrip through load, copy, and save without concrete plugin dependencies.
4. **Specialized System Bridges:**
   - `ScenePhysicsBridge`: Jolt transform synchronization (`SynchronizeTransforms`), zero-drift body reset (`RestoreBodies`), body teardown (`TeardownEntityPhysics`), body ID lookup (`FindEntityByBodyID`), and body creation (`CreateBodyForEntity`).
   - `SceneAudioBridge`: audio source playback on awake (`OnRuntimeStart`), 3D listener/source spatial sync (`OnUpdate`), audio stoppage (`OnRuntimeStop`), and entity audio teardown (`TeardownEntityAudio`).
   - `SceneScriptBridge`: native script instantiation and `OnCreate` (`OnRuntimeStart`), tick execution (`OnUpdate`), teardown (`OnRuntimeStop`), hot reload state capture/restore (`OnPreScriptReload`, `OnPostScriptReload`), and entity script teardown (`TeardownEntityScript`).
   - `SceneCharacterBridge`: kinematic character controller runtime initialization, animator synchronization, and character VFX step updates.
   - `SceneRenderBridge`: geometry pass with lights/shadows, shadow pass, skeletal mesh pass, and particle rendering (OpenGL & RHI).
5. **EnTT Decoupled from Low-Level RHI:**
   - Moved `RHIECSStreamer` from `Engine/RHI/` to `Engine/Renderer/`.
   - Purged EnTT dependency and includes from `AxleRHI`.
6. **Dedicated Test Fixture (`AxleSceneDataUnitTests`):**
   - Added `Tests/src/Test_SceneDataUnit.cpp` registered in CTest as `SceneDataUnitTests`.
   - 6 test cases verifying entity lifecycle, hierarchy cycle rejection & unparenting, transform evaluation & revision progression, component codecs & deep copying, passive extension storage roundtrip through serialization & copying, and `Scene` facade parity.

### Verification Evidence
- `cmake --build build --config Debug --target AxleSceneDataUnitTests AxleFoundationUnitTests AxleJobsECSTests AxleEditor AxleRuntime AxleTests`: exit 0.
- `cmake --build build --config Release --target AxleSceneDataUnitTests`: exit 0.
- `build/Tests/Debug/AxleSceneDataUnitTests.exe`: exit 0, ALL 6 SCENE DATA UNIT TESTS PASSED (100%).
- `build/Tests/Release/AxleSceneDataUnitTests.exe`: exit 0, ALL 6 SCENE DATA UNIT TESTS PASSED (100%).
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 34/34 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 34/34 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-02`: exit 0, PASS (360 files, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Package AF-04: Asset Registry/Format versus Resources and Tools

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `ca1fb42`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md)

### Implementation Summary
1. **Extraction of GPU Material & Texture Caching into Renderer (`Engine/Renderer/include/Axle/MaterialCache.hpp`, `src/MaterialCache.cpp`):**
   - Introduced `MaterialCache` in `AxleRenderer` owning `Texture2D`, `Material`, and `RHI::RHIMaterial` caching, normalized path keys, embedded native UUID lookup via `FileSystem::ReadFileIdentity`, in-place live reloading (`ReloadMaterial`), and texture attachment.
   - Introduced dependency inversion hooks (`UUIDPathResolver` and `PathUUIDResolver`) on `MaterialCache` so that `MaterialCache` can query asset paths and validate handles against `AssetManager` without introducing upward dependencies from `Renderer` to `Asset`.
2. **Purged GPU Dependencies from Asset Module (`Engine/Asset/include/Axle/AssetManager.hpp`, `src/AssetManager.cpp`, `AssetSerializer.hpp`, `AssetSerializer.cpp`):**
   - Removed `Texture2D.hpp`, `Material.hpp`, `RHIMaterial.hpp`, and GPU cache maps from `AssetManager`. `AssetManager` is now 100% pure CPU Asset Registry and Identity index.
   - Removed `VertexArray.hpp`, `Buffer.hpp`, and `RHI/RHIDevice.hpp` from `AssetSerializer`.
   - `AxleLoadedMesh` now encapsulates pure CPU mesh data (`AxleMeshSource SourceData; bool LoadedFromCache; bool Success;`), loading and caching `.axle` and `.axlebin` files without touching GPU vertex arrays.
   - Removed `Axle::Renderer` and `Axle::RHI` links from `Engine/Asset/CMakeLists.txt`. `AxleAsset` links purely `Axle::Core`.
3. **Decoupled PhysicsMaterial from AssetDSL (`Engine/Physics/include/Axle/PhysicsMaterial.hpp`, `CMakeLists.txt`):**
   - Removed `#include <Axle/AssetDSL.hpp>`.
   - Implemented self-contained string parser in `PhysicsMaterial::Deserialize` supporting declarative `.axleasset` format and legacy YAML format without any dependency on `AssetDSL` or `Asset`.
   - Removed `Axle::Asset` link from `Engine/Physics/CMakeLists.txt`.
4. **Decoupled VAO Instantiation in Scene (`Engine/Scene/include/Axle/SceneRenderBridge.hpp`, `src/SceneRenderBridge.cpp`):**
   - Introduced `CreateVertexArrayFromMeshSource(const AxleMeshSource& source)` in `SceneRenderBridge`, bridging CPU mesh sources to OpenGL `VertexArray` inside `AxleScene`.
   - Updated all VAO instantiation sites across Editor and UI panels (`AssemblySerializer.cpp`, `SceneSerializer.cpp`, `EditorLayer.cpp`, `AssemblyEditorPanel.cpp`, `AssetPreviewPanel.cpp`, `AssetThumbnailManager.cpp`, `EntityInspector.cpp`, `SceneHierarchyPanel.cpp`).
5. **Architectural Allowance Retirement:**
   - Retired `ALLOW-004` (Asset -> Renderer).
   - Retired `ALLOW-005` (Asset -> RHI).
   - Retired `ALLOW-008` (Physics -> Asset).
   - Verified `check_architecture.py --root . --closed-package AF-04` passes with zero violations across all 362 scanned files.
6. **Dedicated Headless Asset Unit Test Target (`AxleAssetFormatUnitTests`):**
   - Added `Tests/src/Test_AssetUnit.cpp` registered in CTest as `AssetFormatUnitTests`.
   - 5 comprehensive headless test cases verifying:
     - Pure headless `AssetManager` registry and identity mapping.
     - `AssetDSL` and `AssetDocument` parser/emitter roundtrip.
     - `AssetSerializer` pure CPU mesh loading and binary baking.
     - Decoupled `PhysicsMaterial` parsing and serialization.
     - `MaterialCache` deduplication, path resolution, and in-place reload.

### Verification Evidence
- `cmake --build build --config Debug --target AxleAssetFormatUnitTests AxleAssetUnitTests AxleSceneDataUnitTests AxleTests AxleEditor AxleRuntime`: exit 0.
- `cmake --build build --config Release --target AxleAssetFormatUnitTests AxleAssetUnitTests AxleSceneDataUnitTests AxleTests AxleEditor AxleRuntime`: exit 0.
- `build/Tests/Debug/AxleAssetFormatUnitTests.exe`: exit 0, ALL ASSET UNIT TESTS PASSED (100%).
- `build/Tests/Release/AxleAssetFormatUnitTests.exe`: exit 0, ALL ASSET UNIT TESTS PASSED (100%).
- `build/Tests/Debug/AxleAssetUnitTests.exe`: exit 0, PASS 14/14 asset suites (100%).
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 35/35 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 35/35 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-04`: exit 0, PASS (362 files scanned, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.
