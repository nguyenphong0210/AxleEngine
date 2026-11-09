# OPT-03 Execution Report: Separate Core, Scene Data and Assets; Unify Simulation

Evolving execution log and verification record for the sequential implementation of **OPT-03 (AF-02, EJ-08..EJ-10, AF-03, AF-04, EJ-11..EJ-14)**, authored by Gemini under the historical OPT-03 scope recorded in the exchange.

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

---

## Package EJ-11: Shared Runtime Phases and Script API

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `b418b9e`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) / [ENTT_ENKITS_REFACTOR_PLAN.md](../design/ENTT_ENKITS_REFACTOR_PLAN.md)

### Implementation Summary
1. **Discrete Simulation Phase Architecture (`Engine/Scene/include/Axle/ScenePhase.hpp`):**
   - Formalized canonical simulation phases: `FrameScripts`, `FixedControls`, `Physics`, `PostStepSync`, `AnimationVFX`, and `LatePresentation`.
   - Added phase telemetry trace (`EnablePhaseTracing`, `ClearPhaseTrace`, `GetPhaseTrace`, `RecordPhase`) for sequence verification and regression profiling.
   - Decomposed monolithic `Scene::OnUpdateRuntime` into discrete phase invokers: `ExecuteFrameScriptPhase`, `ExecuteFixedControlsPhase`, `ExecutePhysicsPhase`, `ExecutePostStepSyncPhase`, `ExecuteAnimationVFXPhase`, and `ExecuteLatePresentationPhase`.
   - Added `ExecuteFixedTick(fixedDt, physicsScene)` and `StepSimulation(dt, physicsScene)` executing full end-to-end discrete phases with command buffer flushing at phase boundaries.
2. **Extended Script Lifecycle Callbacks (`Engine/Scene/include/Axle/ScriptableEntity.hpp`, `SceneScriptBridge.hpp`, `SceneScriptBridge.cpp`):**
   - Added `virtual void OnFixedUpdate(Timestep ts) {}` for fixed simulation ticks, vehicle/character input polling, and rigid body impulses before physics integration.
   - Added `virtual void OnLateUpdate(Timestep ts) {}` for post-physics camera follow, HUD telemetry, and presentation updates after physics transforms have synchronized.
   - Preserved `virtual void OnUpdate(Timestep ts) {}` for frame-rate variable updates.
   - Updated `SceneScriptBridge` to provide distinct iteration passes: `OnUpdate`, `OnFixedUpdate`, and `OnLateUpdate`.
3. **Script Module ABI Handshake & Versioning (`Engine/Scene/include/Axle/ScriptModule.hpp`, `Editor/src/Scripts/ScriptsModule.cpp`, `Engine/Scene/src/ScriptHotReloadManager.cpp`):**
   - Defined `constexpr uint32_t AXLE_SCRIPT_ABI_VERSION = 2;`.
   - Exported `AXLE_SCRIPT_API uint32_t Axle_GetScriptABIVersion()` from `ScriptsModule.cpp`.
   - Enforced ABI handshake in `ScriptHotReloadManager::Reload`: candidate script DLLs missing `Axle_GetScriptABIVersion` or exporting mismatched ABI versions are gracefully rejected before touching active vtables or unmapping active DLLs.
4. **Decoupled Character and Animation/VFX Bridges (`Engine/Scene/include/Axle/SceneCharacterBridge.hpp`, `SceneCharacterBridge.cpp`):**
   - Decomposed character update into discrete phases: `OnFixedUpdate` (kinematic controller physics updates), `OnPostStepSync` (post-physics state synchronization to `AnimatorComponent` for speed, vertical velocity, grounded state, and `CharacterVFXComponent`), and `UpdateAnimationAndVFX` (updating standalone animators and particle emitters).
5. **Script Fixture Migration (`Tests/Fixtures/Assets/scripts/`):**
   - Migrated `ThirdPersonController.hpp` and `CarController.hpp` to implement `OnFixedUpdate` for responsive same-tick physics input.
   - Migrated `ThirdPersonCamera.hpp`, `VehicleChaseCamera.hpp`, and `RacingHUD.hpp` to implement `OnLateUpdate` for zero-jitter camera tracking after physics transform synchronization.
6. **Dedicated Test Fixture (`AxleSharedRuntimePhasesUnitTests`):**
   - Added `Tests/src/Test_SharedRuntimePhasesUnit.cpp` registered in CTest as `SharedRuntimePhasesUnitTests`.
   - 5 comprehensive headless test cases:
     - `TestPhaseTraceOrder`: validates exact execution order across `ExecuteFixedTick` and full simulation frames.
     - `TestCallbackInvocationCounts`: validates exact invocation counts for `OnUpdate`, `OnFixedUpdate`, and `OnLateUpdate` across multiple ticks.
     - `TestSameTickControlsAndPostStepPoses`: validates SIM-01 same-tick contract: controls applied in `FixedControls` are integrated in `Physics`, synchronized in `PostStepSync`, and observed by cameras in `LatePresentation` within the exact same simulation tick.
     - `TestScriptABIVersionValidationAndRejection`: validates ABI 2 handshake and graceful rejection of legacy or corrupted candidate DLLs.
     - `TestCharacterBridgePhaseIsolation`: validates kinematic character movement in fixed tick and animator telemetry sync post-step.

### Verification Evidence
- `cmake --build build --config Debug --target AxleSharedRuntimePhasesUnitTests AxleAssetFormatUnitTests AxleAssetUnitTests AxleSceneDataUnitTests AxleTests AxleEditor AxleRuntime`: exit 0.
- `cmake --build build --config Release --target AxleSharedRuntimePhasesUnitTests AxleAssetFormatUnitTests AxleAssetUnitTests AxleSceneDataUnitTests AxleTests AxleEditor AxleRuntime`: exit 0.
- `build/Tests/Debug/AxleSharedRuntimePhasesUnitTests.exe`: exit 0, ALL RUNTIME PHASE TESTS PASSED (100%).
- `build/Tests/Release/AxleSharedRuntimePhasesUnitTests.exe`: exit 0, ALL RUNTIME PHASE TESTS PASSED (100%).
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 36/36 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 36/36 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-04`: exit 0, PASS (363 files scanned, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Package EJ-12: Fixed Clock, Inputs and Pause/Step

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `ae22b35`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) / [ENTT_ENKITS_REFACTOR_PLAN.md](../design/ENTT_ENKITS_REFACTOR_PLAN.md)

### Implementation Summary
1. **Simulation Clock & Fixed Accumulator (`Engine/Scene/include/Axle/SimulationClock.hpp`, `src/SimulationClock.cpp`):**
   - Implemented `SimulationClock` managing double-precision accumulator `m_Accumulator`, configurable fixed timestep (default 1/60s), configurable speed factor (0.0x, 0.5x, 1.0x, 2.0x, 4.0x), and spiral-of-death overload protection.
   - Enforced delta time validation: non-finite (NaN, inf) and negative delta times are rejected and counted via `InvalidDtDrops`.
   - Enforced overload cap: accumulator exceeding `m_MaxAccumulator` (default 0.2s = 12 ticks at 60Hz) is clamped (or dropped based on `ClockOverloadPolicy`), incrementing `OverloadDrops`.
   - Fixed accumulator tick division incorporates a `1e-6` floating-point epsilon to eliminate discrete tick loss at high refresh rates (such as 144 Hz).
   - Pause/Resume semantics: pausing clears the accumulator to prevent catch-up bursts on resume.
   - Paused single-step: `RequestSingleStep()` produces exactly 1 fixed tick on the next frame while maintaining paused state.
   - Independent instantiability: supports private preview clocks (e.g. for VFX and asset previews) that run independently without touching the main simulation clock.
2. **Simulation Input Adapter (`Engine/Scene/include/Axle/SimulationInput.hpp`, `src/SimulationInput.cpp`):**
   - Implemented `SimulationInputAdapter` and `SimulationInputSnapshot` capturing continuous key/mouse states and discrete single-tick edge triggers (`KeysJustPressed`, `KeysJustReleased`, `MouseButtonsJustPressed`, `MouseButtonsJustReleased`).
   - Consistent multi-tick consumption (SIM-05): edge triggers latched during 0-tick frames are preserved; in multi-tick frames (e.g. 30 Hz running 2 ticks), edge triggers are consumed exclusively on tick 0, while subsequent catch-up ticks observe held state without replaying edges.
   - Paused UI isolation (SIM-05): `Flush()` clears all pending game input buffers on pause/resume, ensuring paused UI events never bleed into gameplay.
   - Non-intrusive `InputContext` bridging: activates a scoped tick context during fixed updates so that scripts calling `Axle::Input::IsKeyPressed` or `Axle::Input::IsMouseButtonPressed` read transparently from the tick snapshot.
3. **Unified Simulation Driver (`Engine/Scene/include/Axle/SimulationDriver.hpp`, `src/SimulationDriver.cpp`):**
   - Implemented `SimulationDriver` combining `SimulationClock`, `SimulationInputAdapter`, and phase execution into one cohesive driver for both Editor and Runtime.
   - `AdvanceFrame(rawDt, scene, physicsScene)` orchestrates: input polling, paused check & flush, clock advance, frame script execution (rawDt), per-tick input context activation & `ExecuteFixedTick` (fixedDt), and late presentation phase (rawDt).
4. **Dedicated Test Fixture (`AxleSimulationClockUnitTests`):**
   - Added `Tests/src/Test_SimulationClockAndInputUnit.cpp` registered in CTest as `SimulationClockUnitTests`.
   - 9 comprehensive headless unit tests covering:
     - `TestClockDtValidation`: rejects negative, NaN, and inf delta times; counts `InvalidDtDrops`.
     - `TestClockOverloadCap`: verifies 2.0s spike clamps to 12 ticks; tests drop policy.
     - `TestClockFramePartitions`: verifies SIM-02 (30 Hz, 60 Hz, and 144 Hz frame rates produce identical 60 fixed ticks across 1.0 second of simulation).
     - `TestClockSpeedScaling`: validates exact linear scaling across 0.0x, 0.5x, 1.0x, 2.0x, and 4.0x speeds.
     - `TestClockPauseResumeAndSingleStep`: validates zero ticks when paused, exact single tick execution on single-step request, and clean resume without backlog spike (SIM-03).
     - `TestInputSnapshotZeroOneMultipleTicks`: validates edge latching across 0-tick frames and single consumption across multi-tick frames (SIM-05).
     - `TestPausedInputIsolation`: validates UI input flushing when paused/resumed (SIM-05).
     - `TestPrivatePreviewClockIndependence`: validates that private preview clocks advance independently while main game clock is paused.
     - `TestSimulationDriverEndToEnd`: validates headless scene simulation, physics falling under gravity, and telemetry tracking through `SimulationDriver::AdvanceFrame`.

### Verification Evidence
- `cmake --build build --config Debug --target AxleSimulationClockUnitTests AxleSharedRuntimePhasesUnitTests AxleAssetFormatUnitTests AxleAssetUnitTests AxleSceneDataUnitTests AxleTests AxleEditor AxleRuntime`: exit 0.
- `cmake --build build --config Release --target AxleSimulationClockUnitTests AxleSharedRuntimePhasesUnitTests AxleAssetFormatUnitTests AxleAssetUnitTests AxleSceneDataUnitTests AxleTests AxleEditor AxleRuntime`: exit 0.
- `build/Tests/Debug/AxleSimulationClockUnitTests.exe`: exit 0, ALL 9 CLOCK & INPUT TESTS PASSED (100%).
- `build/Tests/Release/AxleSimulationClockUnitTests.exe`: exit 0, ALL 9 CLOCK & INPUT TESTS PASSED (100%).
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 37/37 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 37/37 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-04`: exit 0, PASS (369 files scanned, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Package EJ-13: Editor Cutover

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `026bd91`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) / [ENTT_ENKITS_REFACTOR_PLAN.md](../design/ENTT_ENKITS_REFACTOR_PLAN.md)

### Implementation Summary
1. **Editor Simulation Driver Integration (`Editor/src/EditorLayer.hpp`, `Editor/src/EditorLayer.cpp`):**
   - Added `Axle::SimulationDriver m_SimulationDriver;` member with const and non-const `GetSimulationDriver()` accessors.
   - Wired `OnScenePlay`: resets driver, sets configured `m_SimulationSpeed`, and resumes driver.
   - Wired `OnScenePause`: pauses driver, clears accumulator, and enables free-fly viewport inspection.
   - Wired `OnSceneStop`: resets simulation driver and tears down runtime physics state.
   - Wired `ResetSimulation`, `NewScene`, and `OpenScene`: cleanly resets driver state.
2. **Unified Frame Stepping & Removal of Standalone Physics Steps:**
   - Cut over `EditorLayer::OnUpdate(ts)`: replaced separate `m_Scene->OnUpdateRuntime(ts)` and separate standalone `m_PhysicsScene->Step(simTs)` with `m_SimulationDriver.AdvanceFrame(ts.GetSeconds(), *m_Scene, m_PhysicsScene.get())`.
   - Single-step requests (`m_StepSingleFrame = true`) now trigger `m_SimulationDriver.RequestSingleStep()`, advancing exactly 1 fixed tick through all phases without standalone physics bypass.
   - Completely eliminated the previous disparity where Pause single-step stepped physics without running scripts or animation/VFX updates.
3. **Dedicated Test Fixture (`AxleEditorCutoverUnitTests`):**
   - Added `Tests/src/Test_EditorCutoverUnit.cpp` registered in CTest as `EditorCutoverUnitTests`.
   - 4 comprehensive headless unit tests covering:
     - `TestEditorSimulationDriverIntegration`: validates Play/Pause/SingleStep/Resume/Stop lifecycle through `SimulationDriver` with gravity simulation on dynamic bodies.
     - `TestEditorZeroDriftRestoration`: validates that running 60 frames under gravity moves runtime entities while stopping and restoring bodies leaves the pristine editor scene with zero transform drift (verified against `1e-4` scene tolerance and `1e-3` Jolt physics body tolerance).
     - `TestEditorSpeedScaling`: validates that editor simulation speed (0.5x, 1.0x, 2.0x) correctly scales fixed tick execution rate.
     - `TestEditorPrivatePreviewClockIsolation`: validates that private asset/VFX preview clocks run independently and are not affected by editor play/pause state.

### Verification Evidence
- `cmake --build build --config Debug --target AxleEditorCutoverUnitTests AxleEditor`: exit 0.
- `cmake --build build --config Release --target AxleEditorCutoverUnitTests AxleEditor`: exit 0.
- `build/Tests/Debug/AxleEditorCutoverUnitTests.exe`: exit 0, ALL 4 EDITOR CUTOVER TESTS PASSED (100%).
- `build/Tests/Release/AxleEditorCutoverUnitTests.exe`: exit 0, ALL 4 EDITOR CUTOVER TESTS PASSED (100%).
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 38/38 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 38/38 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-04`: exit 0, PASS (369 files scanned, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Package EJ-14: Runtime Cutover and Product Parity

- **Status:** Implemented and self-verified; local delivery committed.
- **Base Baseline:** `abf3300`
- **Scope & Contract:** [ARCHITECTURE_FOUNDATION_MASTER_PLAN.md](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) / [ENTT_ENKITS_REFACTOR_PLAN.md](../design/ENTT_ENKITS_REFACTOR_PLAN.md)

### Implementation Summary
1. **Runtime Simulation Driver Integration (`Runtime/src/RuntimeLayer.hpp`, `Runtime/src/RuntimeLayer.cpp`):**
   - Added `Axle::SimulationDriver m_SimulationDriver;` member with const and non-const `GetSimulationDriver()` accessors.
   - Cut over `LoadProjectAndStartScene()`: resets driver, resumes simulation, and calls `m_Scene->OnRuntimeStart(m_PhysicsScene.get())`.
   - Cut over `OnUpdate(ts)`: replaced separate reversed `m_PhysicsScene->Step(ts)` and `m_Scene->OnUpdateRuntime(ts)` with unified `m_SimulationDriver.AdvanceFrame(ts.GetSeconds(), *m_Scene, m_PhysicsScene.get())`.
   - Wired Pause/Resume via Escape key toggle and UI pause menu buttons directly to `m_SimulationDriver.SetPaused()` and `Resume()`.
   - Wired `OnDetach()`: cleanly resets `m_SimulationDriver`, tears down runtime scenes and physics resources.
2. **Defect ECSJ-05 Fully Resolved:**
   - Prior to this cutover, Editor updated scripts before stepping physics, while Runtime stepped physics before updating scripts. Controls and telemetry were inverted between products, causing a 1-tick phase disparity.
   - Now, both Editor and Runtime execute the identical discrete simulation phases (`FrameScripts` -> `FixedControls` -> `Physics` -> `PostStepSync` -> `AnimationVFX` -> `LatePresentation`).
   - Controls applied in `OnFixedUpdate` are integrated by Jolt physics during the fixed tick, transforms are synchronized in post-step sync, and camera/HUD scripts in `OnLateUpdate` observe the synchronized pose in that exact same tick with 0-tick latency in both products.
3. **Dedicated Test Fixture (`AxleRuntimeProductParityUnitTests`):**
   - Added `Tests/src/Test_RuntimeProductParityUnit.cpp` registered in CTest as `RuntimeProductParityUnitTests`.
   - 5 comprehensive headless unit tests covering:
     - `TestSimulationParitySameTickSequence`: verifies exact match in total fixed ticks, script callback invocation counts (`OnUpdate`, `OnFixedUpdate`, `OnLateUpdate`), entity positions (`< 1e-4` tolerance), and physics linear velocities across variable-rate frame streams (60Hz, 30Hz, 120Hz, 144Hz) between Editor and Runtime drivers.
     - `TestECSJ05ControlsToTelemetryParity`: verifies that an impulse applied in `OnFixedUpdate` during tick $T$ is integrated by physics and observed in `OnLateUpdate` during that exact tick $T$ with identical world positions in both Editor and Runtime.
     - `TestRuntimePauseResumeInputIsolation`: verifies that pausing Runtime halts fixed ticks and keeps transforms static across 30 frames, flushes input buffers, and resuming executes 1 tick per frame without backlog burst.
     - `TestRuntimeSpeedScalingParity`: verifies that 0.5x, 1.0x, and 2.0x speeds produce identical tick counts and matching physical states across both drivers.
     - `TestRuntimeCleanTeardownAndPhysicsDeregistration`: verifies that simulation teardown cleans up all dynamic physics bodies without leaks.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRuntimeProductParityUnitTests AxleRuntime AxleEditor`: exit 0.
- `cmake --build build --config Release --target AxleRuntimeProductParityUnitTests AxleRuntime AxleEditor`: exit 0.
- `build/Tests/Debug/AxleRuntimeProductParityUnitTests.exe`: exit 0, ALL 5 PRODUCT PARITY TESTS PASSED (100%).
- `build/Tests/Release/AxleRuntimeProductParityUnitTests.exe`: exit 0, ALL 5 PRODUCT PARITY TESTS PASSED (100%).
- `build/Tests/Debug/AxleTests.exe`: exit 0, ALL REQUESTED TESTS PASSED (100%, all 68 steps passed).
- `ctest --test-dir build -C Debug --output-on-failure`: 39/39 tests passed (100%).
- `ctest --test-dir build -C Release --output-on-failure`: 39/39 tests passed (100%).
- `python scripts/run_jobs_ecs_harness.py build/Tests/Debug/AxleJobsECSTests.exe`: exit 0 (16 PASS, 0 defects, 3 verified probes, 0 unexpected failures).
- `python scripts/check_architecture.py --root . --closed-package AF-04`: exit 0, PASS (369 files scanned, 17 CMake files, 0 violations).
- `python scripts/test_baseline_tools.py`: exit 0, 18/18 tests passed.
- `git diff --check`: exit 0.

---

## Phase OPT-03 Completion Summary

All 10 packages of Phase OPT-03 have been fully implemented, self-reviewed, and verified in exact sequential order:
1. `AF-02` (commit `374933a`): Foundation, Application and explicit service ownership. Removed upward dependencies, retired ALLOW-001..003.
2. `EJ-08` (commit `4286425`): Parallel EnTT view correctness. Fixed tombstone handling, resolved defect ECSJ-04.
3. `EJ-09` (commit `cc059af`): Scene command and mutation boundaries. Enforced generation checks, safe recursive teardown, dedicated unit tests.
4. `EJ-10` (commit `b434dd0`): Transform evaluation ownership. Pure const read-only queries, monotonic revision tracking, Floyd cycle detection.
5. `AF-03` (commit `ca1fb42`): Scene data and system bridge boundaries. Decoupled headless SceneData from system bridges, component codecs, passive extension storage.
6. `AF-04` (commit `b418b9e`): Asset registry/format versus resources and tools. Separated CPU AssetManager from GPU MaterialCache, retired ALLOW-004, 005, 008.
7. `EJ-11` (commit `ae22b35`): Shared runtime phases and script API. Discrete simulation phases, OnFixedUpdate/OnLateUpdate, ABI version 2 handshake.
8. `EJ-12` (commit `026bd91`): Fixed clock, inputs and pause/step. SimulationClock with double-precision accumulator, SimulationInputAdapter with tick edge latching and paused UI isolation, unified SimulationDriver.
9. `EJ-13` (commit `abf3300`): Editor cutover. Unified Play/Pause/Step/Stop/Speed to shared SimulationDriver, zero-drift edit scene restoration, private preview clock isolation.
10. `EJ-14` (commit pending): Runtime cutover and product parity. Cut over standalone player to shared SimulationDriver, fully resolved ECSJ-05, identical simulation behavior across Editor and Runtime.
