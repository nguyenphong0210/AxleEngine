# Current Architecture Assessment and Improvement Plan

Reviewed: **2026-11-01**. Scope: first-party C++/CMake architecture and its integration paths. This is a source-based engineering assessment, not a performance benchmark or a claim of reproduced runtime failures.

## 1. Overall assessment

AxleEngine has a useful subsystem decomposition and can be improved incrementally. Individual libraries, include/src separation, EnTT storage, common scheduling, serializer callbacks and separate application entry points provide practical foundations.

Its main constraint is dependency isolation. Module directories and static-library targets exist, but Core includes upper-level services, Scene integrates many concrete systems, and runtime plugins pull editor tooling. The current design is best described as **a modular subsystem engine with incomplete layering**.

The recommended approach is to preserve working subsystem behavior while extracting clear build, data and lifecycle boundaries. A rewrite is not required to begin improving modularity.

## 2. Strengths worth preserving

| Strength | Architectural value | Evidence |
|---|---|---|
| Subsystem CMake targets and aliases | Clear starting points for independently checked boundaries | [Root build composition](../../CMakeLists.txt), [module map](DEPENDENCY_MAP.md) |
| EnTT registry with Entity facade | Keeps component storage distinct from scripting ergonomics | [Scene](../../Engine/Scene/include/Axle/Scene.hpp), [Entity](../../Engine/Scene/include/Axle/Entity.hpp) |
| Common enkiTS scheduler/Jolt adapter | Shared scheduling rather than an independent physics worker pool | [JobSystem](../../Engine/Core/include/Axle/JobSystem.hpp), [adapter](../../Engine/Physics/include/Axle/JobSystemEnkiTS.hpp) |
| Serializer callbacks | Allows caller-installed plugin serialization without making Scene link every plugin | [SceneSerializerContext](../../Engine/Scene/include/Axle/SceneSerializer.hpp) |
| Separate edit/runtime scenes | Supports restoring authoring state after play | [Scene::Copy](../../Engine/Scene/src/Scene.cpp), [Editor play/stop](../../Editor/src/EditorLayer.cpp) |
| Separate UI2D/editor UI targets | Existing seam for runtime/tooling isolation | [UI targets](../../Engine/UI/CMakeLists.txt) |
| Behavioral test sources | Provides regression material for staged refactoring | [Test groups](../../Editor/src/Tests), [runner](../../Editor/src/Test.cpp) |

## 3. Limitations, impact and priority

Priorities rank migration attention, not confirmed production incident severity. **High** items affect dependency foundations, identity or ownership/thread contracts. **Medium** items increase duplication, build cost or maintenance burden.

| ID | Priority | Current limitation | Practical impact | Improvement direction |
|---|---|---|---|---|
| A1 | High | Core/Application calls Platform, Renderer and Physics; upper include paths are exported | Core is not independently reusable/headless; target-only checks miss reverse source dependencies | Extract Application/bootstrap; remove upper includes from Core |
| A2 | High | Runtime → Spline/Terrain → AxleUI; scripts and resource staging are defined around Editor | Runtime-only builds and optional toolkit use are coupled to tools | Split runtime/editor plugin targets; configure scripts and resource staging independently |
| A3 | High | `.meta` is ignored while missing metadata creates random UUIDs | Fresh checkouts can change identity used by serialized references | Version stable identity or introduce a committed equivalent registry; validate clean-checkout references |
| A4 | High | ScriptCompiler completion executes reload/editor state mutation on its worker | Scene/script/UI ownership is not explicitly confined to a controlling thread | Marshal state mutation to the owning thread; define reload synchronization and failure behavior |
| A5 | High | Editor and Runtime use different scene/physics ordering and time scaling | Play/step/standalone semantics can diverge | Share a documented simulation scheduler and pause/step/scaled-time contract |
| A6 | Medium | Scene handles ECS, vehicles, audio, characters, VFX, scripts and rendering | Optional systems and component extension require broad coordinated edits | Extract SceneData and subsystem bridges; keep domain components outside common headers |
| A7 | Medium | OpenGL and RHI resources/passes coexist across Scene, Physics and VFX | Backend changes require parallel integration and lifetime fixes | Define Renderer/RHI responsibilities; isolate ECS extraction and debug graphics |
| A8 | Medium | Asset registry, GPU caches, conversion and cooking share a target | Identity-only/headless tools depend on graphics-oriented code | Split AssetRegistry/Format/Resources and import/cooking tools |
| A9 | Medium | Test executable depends on Editor; no first-party CTest registration | Fast isolated checks and CI selection are harder | Independent subsystem test targets and resource staging; register meaningful test groups |
| A10 | Medium | Static services span Application, assets, project, device, scripts and audio | Multiple contexts, shutdown ordering and tests are difficult to isolate | Introduce explicit composition/lifetime owners and migrate services selectively |
| A11 | Medium | Canvas2D collects CPU geometry but EndCanvas has no GPU submission | Geometry generation and actual runtime presentation are separate readiness levels | Define a UI render backend, upload/submission and lifecycle contract |
| A12 | Medium | Shared component/serializer/editor integration files concentrate many responsibilities | Changes have wide rebuild/review scope and inconsistent extension handling | Split by responsibility and use focused extension contracts where justified |

### Source evidence

- A1: [Core CMake](../../Engine/Core/CMakeLists.txt), [Application implementation](../../Engine/Core/src/Application.cpp).
- A2: [Runtime CMake](../../Runtime/CMakeLists.txt), [Spline CMake](../../Plugins/Spline/CMakeLists.txt), [Terrain CMake](../../Plugins/Terrain/CMakeLists.txt).
- A3: [Git ignore policy](../../.gitignore), [AssetManager metadata creation](../../Engine/Asset/src/AssetManager.cpp), [scene material handles](../../Engine/Scene/src/SceneSerializer.cpp).
- A4: [CompileAsync](../../Engine/Scene/src/ScriptCompiler.cpp), [Editor compile callback](../../Editor/src/EditorLayer.cpp), [DLL reload](../../Engine/Scene/src/ScriptHotReloadManager.cpp).
- A5: [Editor update](../../Editor/src/EditorLayer.cpp), [Runtime update](../../Runtime/src/RuntimeLayer.cpp), [sequence comparison](RUNTIME_FLOWS.md#4-different-editor-and-runtime-update-order).
- A6/A12: [Scene API](../../Engine/Scene/include/Axle/Scene.hpp), [common components](../../Engine/Scene/include/Axle/Components.hpp), [Editor composition](../../Editor/src/EditorLayer.hpp).
- A7: [Renderer target](../../Engine/Renderer/CMakeLists.txt), [RHI target](../../Engine/RHI/CMakeLists.txt), [RHI scene renderer](../../Engine/Scene/include/Axle/RHISceneRenderer.hpp), [VFX OpenGL renderer](../../Engine/VFX/src/ParticleRenderer.cpp).
- A8: [Asset target](../../Engine/Asset/CMakeLists.txt), [AssetManager](../../Engine/Asset/include/Axle/AssetManager.hpp).
- A9: [Editor/test target definitions](../../Editor/CMakeLists.txt).
- A10: [ownership design](SDD.md), [class diagrams](CLASS_RELATIONSHIPS.md).
- A11: [Canvas2D implementation](../../Engine/UI/src/UI2D/Canvas2D.cpp), [Runtime UI path](../../Runtime/src/RuntimeLayer.cpp).

## 4. Important qualifications

Transitive static-library dependencies do not prove that every editor object file appears in the final Runtime binary. A linker map or binary inspection is needed to measure that impact.

Shared settings/types do not prove visual parity. Editor supports backend selection through RHISwitcher; Runtime currently follows its OpenGL bootstrap. NRI native types remain public, so the RHI is not completely vendor-neutral.

UUID loss, reload races, output mismatches and shutdown failures are risks inferred from the source paths above, not failures reproduced by this documentation review. Prioritize behavioral verification before changing their semantics.

## 5. Improvement stages and exit criteria

| Stage | Work | Exit criterion |
|---|---|---|
| 1. Protect identity and lifecycle | Preserve asset identity; define owner-thread reload and teardown | A clean checkout resolves existing references; reload failure and scene lifetime checks are repeatable |
| 2. Repair foundation boundaries | Move Application out of Core and eliminate reverse include paths | Core builds/tests without window, graphics or physics initialization |
| 3. Separate products from tools | Split plugin runtime/editor targets and scripts/resource staging | Runtime configures/builds with Editor disabled |
| 4. Make tests independently useful | Separate CPU/headless/integration targets and resources | Selected core/ECS/registry checks run without building or launching the IDE |
| 5. Unify simulation semantics | Define pre/post-physics work, fixed step, pause and time scaling | Equivalent scripted inputs produce agreed Editor/Runtime behavior; paused step semantics are tested |
| 6. Extract data and resource boundaries | SceneData/bridges, asset identity/resources and CPU systems | Disabling Vehicle/Terrain leaves generic scene builds intact; data-only APIs do not require GPU contexts |
| 7. Consolidate rendering and UI presentation | Renderer/RHI contracts, debug adapters and canvas submission | Selected backends pass lifecycle, submission and representative visual checks |
| 8. Enforce boundaries | Target/include policy checks in CI | New reverse or cyclic dependencies fail checks before integration |

Stages may overlap when dependencies allow. Each change should be reviewable and preserve existing behavior except where a deliberate contract change is specified.

## 6. Progress indicators

Track evidence rather than a subjective architecture score:

- Number of forbidden target and include edges; target is zero against the agreed policy.
- Ability to build Core/headless tests and Runtime without Editor.
- Number of optional toolkits that can be disabled independently.
- Clean-checkout asset identity/reference stability.
- Explicit ownership/thread contracts for scene mutation, reload and GPU teardown.
- Agreement between Editor and Runtime simulation ordering.
- Repeatable backend/resource-lifetime checks and runtime UI submission checks.
- Documentation freshness when APIs/dependencies change.

No baseline timing, FPS, memory or binary-size benchmark was collected by this review. Establish reproducible workloads before using performance as a migration success criterion.

## 7. Next design decisions

Agree on [target layering](TARGET_LAYERED_ARCHITECTURE.md), the first extraction boundary, stable asset identity policy and simulation/reload ownership contracts before starting a broad refactor. Detailed feature authoring proposals live in the [editor tooling roadmap](../design/EDITOR_TOOLING_ROADMAP.md); feature expansion should not silently recreate dependencies removed during modularization.
