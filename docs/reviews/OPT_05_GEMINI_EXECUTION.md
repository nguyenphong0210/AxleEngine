# OPT-05 Gemini Execution Record: Unify Rendering Across Editor, Previews and Runtime

Live execution log and verification record for the implementation of **Phase OPT-05 (P41-12 through P41-20, P41-25)**.

Date: 2026-11-07
Baseline commit: `2baf3de7223b5d3c8c77395048cb6faad8e515d9`
Workflow: Default workflow under `docs/agents/WORKING_PROTOCOL.md` (Gemini implements and self-verifies; Codex consolidated review and repairs at checkpoint).

Scope:
- [x] **P41-12:** RenderWorld and shared scene extraction
- [x] **P41-13:** RenderService and minimal modern render graph
- [x] **P41-14:** OpenGL compatibility behind the same service
- [x] **P41-15:** Asset and material previews
- [x] **P41-16:** VFX preview
- [x] **P41-17:** Assembly preview
- [ ] **P41-18:** Thumbnail render jobs
- [ ] **P41-19:** Standalone Runtime render integration
- [ ] **P41-20:** Material/shader/color/capability contract
- [ ] **P41-25:** Physics debug and overlays

---

## Package P41-12: RenderWorld and Shared Scene Extraction

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 241-250

### Implementation Summary
1. **RenderWorld (`Engine/Renderer/include/Axle/RenderWorld.hpp`):**
   - Pure, self-contained data structures representing an immutable snapshot of extracted scene data.
   - Zero mutable references to `Scene`, `entt::registry`, or GPU device pointers.
   - Structures: `RenderCamera`, `RenderMaterialData`, `RenderMeshInstance`, `RenderDirectionalLight`, `RenderPointLight`, `RenderSpotLight`, `RenderSkyboxData`.
   - Built-in lookup: `EntityToStableUUID` mapping for stable entity picking correlation.
2. **Scene Extraction (`Engine/Scene/include/Axle/SceneExtraction.hpp`, `Engine/Scene/src/SceneExtraction.cpp`):**
   - `ExtractRenderWorld(Scene& scene, const SceneExtractionOptions& options)` updates world transforms and snapshots all meshes, lights, skybox, camera, and selection flags.
   - `ExtractRenderWorld(const Scene& scene, const SceneExtractionOptions& options)` provides pure read-only extraction without mutating scene state.
   - Correctly propagates hierarchical parent transforms for both meshes and lights.
   - Resource resolution via `ResolveMeshResources` ensures canonical builtin mesh identities and asset paths are bound.
   - Zero GPU allocations or command submissions occur during extraction.
3. **Dedicated Unit Verification (`Tests/rendering/Test_RenderWorldExtractionUnit.cpp`):**
   - `RenderWorld_EmptyScene`: Empty scene extracts cleanly with 0 meshes, 0 lights.
   - `RenderWorld_MeshAndMaterial`: Verifies entity ID, UUID, builtin URI, material colors/tile-scale, world transform translation, and selection flag.
   - `RenderWorld_NestedTransforms`: Proves parent translation propagates correctly to child mesh in extracted world.
   - `RenderWorld_LightsAndSkybox`: Extracts directional, point, and spot lights with correct world positions/radii/ranges, and HDR skybox properties.
   - `RenderWorld_ImmutabilityAndIsolation`: Proves that subsequent scene mutations (deleting an entity, moving another) have zero effect on an already extracted snapshot, while re-extraction reflects the changes.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all tests passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all tests passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (400 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS

---

## Package P41-13: RenderService and Minimal Modern Render Graph

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 251-260

### Implementation Summary
1. **ViewRequest & RenderResult Contracts (`Engine/Renderer/include/Axle/ViewRequest.hpp`):**
   - `RenderViewKind`: `Main`, `AssetPreview`, `VFXPreview`, `AssemblyPreview`, `Thumbnail`.
   - `ViewRequest`: Viewport dimensions, `PerspectiveCamera`, `SceneRenderSettings`, leased modern targets (`ColorTarget`, `DepthTarget`, `ViewTargets`), legacy OpenGL target (`GLTargetFBO`), optional `PhysicsScene`, and view diagnostic name.
   - `RenderResult`: Structured execution record reporting `Success`, `RHICompletionToken`, `DrawCallCount`, `RenderedEntityCount`, and `ErrorMessage`.
2. **RenderService Coordinator (`Engine/Scene/include/Axle/RenderService.hpp`, `Engine/Scene/src/RenderService.cpp`):**
   - Central coordinator orchestrating views across modern RHI and legacy GL rendering backends.
   - Entry points: `RenderView(const RenderWorld& world, const ViewRequest& request)` and `RenderView(Scene& scene, const ViewRequest& request)`.
   - Automatic target validation: gracefully rejects view requests with missing targets or unavailable backends without crashing.
   - External view/UI textures are imported and leased rather than destroyed by render graphs.
3. **Modern Scene Renderer RenderWorld Execution (`Engine/Scene/src/RHISceneRenderer.cpp`):**
   - Implemented `RenderWorldView` overloads taking `const RenderWorld& world` and camera/targets.
   - Consistently handles lighting buffer updates, skybox pass, opaque geometry queue, refraction, transparent queue, and post-process steps (MSAA resolve, tone mapping, FXAA).
4. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_ViewRequestDefaults`: Verifies default parameters and configurations.
   - `Test_ViewRequestCustomization`: Verifies preview-specific settings (MSAA, ToneMapping, dimensions).
   - `Test_RenderResultContract`: Verifies stats, draw call tracking, and completion token propagation.
   - `Test_RenderServiceTargetValidation`: Verifies safe rejection of invalid/unconfigured targets.
   - `Test_RenderServiceMultipleViewKinds`: Validates across all 5 view kinds without state leakage.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (403 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS


---

## Package P41-14: OpenGL Compatibility Behind the Same Service

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 261-270

### Implementation Summary
1. **RenderCapabilities Contract (`Engine/Renderer/include/Axle/RenderCapabilities.hpp`, `Engine/Renderer/src/RenderCapabilities.cpp`):**
   - Cleanly separated physical capabilities from implemented renderer features and active per-view enabled features.
   - Enums & Structs: `RendererBackendType`, `RenderCapabilities`, `QueryRendererCapabilities()`, `LogRenderCapabilities()`.
   - Distinguishes RHI backend capabilities (Modern Bindless/DX12/Vulkan) from OpenGL compatibility mode capabilities.
2. **OpenGL RenderWorld Execution (`Engine/Scene/include/Axle/SceneRenderer.hpp`, `Engine/Scene/src/SceneRenderer.cpp`):**
   - Implemented `RenderWorldView` in legacy `SceneRenderer`.
   - Renders extracted `RenderWorld` meshes, lighting (directional, point, spot with shadow maps), skybox, and physics debug visualization in OpenGL without depending on `Scene` or `entt::registry`.
3. **Honest Compatibility Diagnostics in RenderService (`Engine/Scene/src/RenderService.cpp`):**
   - `RenderService::GetActiveCapabilities()` queries the active backend's capabilities accurately.
   - When running on OpenGL, `RenderService::RenderView` detects if advanced features (HDR floating-point targets, FXAA post-processing, screen-space refraction copies, custom HLSL shaders) are requested, and issues explicit diagnostic warnings without mutating the authored scene settings.
4. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_RenderCapabilitiesContract`: Verifies querying capabilities across Direct3D 12, Vulkan, and OpenGL backends.
   - `Test_GLCompatibilityReporting`: Validates that requesting advanced post-processing / HDR under GL compatibility triggers diagnostics and records appropriate status.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (405 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS


---

## Package P41-15: Asset and Material Previews

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 271-278

### Implementation Summary
1. **Migrated `AssetPreviewPanel` to RenderService (`Engine/UI/src/AssetPreviewPanel.cpp`, `Engine/UI/include/Axle/AssetPreviewPanel.hpp`):**
   - Removed private renderer instance (`std::unique_ptr<RHISceneRenderer> m_RHISceneRenderer`), duplicate vertex/fragment shaders (`s_PreviewVertSrc`, `s_PreviewFragSrc`), and raw OpenGL submission (`RenderPreview3D`).
   - Unified rendering behind `RenderService::Get().RenderView(*m_PreviewScene, request)` across both Modern RHI (Direct3D 12, Vulkan) and OpenGL compatibility backends.
   - Preserved all interactive functionality: material editing, two preview lights (key & fill), camera turntable and gestures, checkerboard grid, custom mesh drag-and-drop, and icon overlays.
   - Leased output textures via `RHIViewportDescriptor` for RHI and managed `Framebuffer` for OpenGL without any idle waiting or private queue submission.
2. **Multi-Light and Material Scene Extraction (`Engine/Renderer/include/Axle/RenderWorld.hpp`, `Engine/Scene/src/SceneExtraction.cpp`, `Engine/Scene/src/RHISceneRenderer.cpp`):**
   - Added `AdditionalDirectionalLights` to `RenderWorld` to support the Godot-style two-light studio rig (primary key light + secondary fill light) through extracted snapshots.
   - Extracted `MaterialComponent` instance properties (`AlbedoColor`, `Roughness`, `Metallic`, `EmissionIntensity`) directly into `RenderMeshInstance::Material`, ensuring complete isolation between main scene materials and preview materials.
   - Handled additional directional lights in `RHISceneRenderer::RenderWorldView` consistent with standard scene rendering.
3. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_PreviewTwoLightsExtraction`: Verifies key and fill lights extract into primary and additional directional light slots with accurate intensities.
   - `Test_PreviewMaterialIsolationAndEdits`: Verifies that editing preview material parameters has zero crosstalk or state bleed into main scene materials.
   - `Test_PreviewViewRequestResizeIsolation`: Verifies that same-frame and rapid view request resizes preserve request parameters cleanly.

### Verification Evidence
- `cmake --build build --config Debug --target AxleUI AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleUI AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (405 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS

---

## Package P41-17: Assembly Preview

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 287-295

### Implementation Summary
1. **Migrated `AssemblyEditorPanel` to RenderService (`Engine/UI/include/Axle/AssemblyEditorPanel.hpp`, `Engine/UI/src/AssemblyEditorPanel.cpp`):**
   - Replaced GL-only viewport, raw OpenGL calls (`glPolygonMode`, `glDrawElements`), and duplicate shaders (`s_AsmVertSrc`, `s_AsmFragSrc`).
   - Unified preview rendering via `RenderService::Get().RenderView(world, request)` with `RenderViewKind::AssemblyPreview`.
   - Private preview scene `m_PreviewScene` dynamically built from the authored assembly hierarchy (`m_AssemblyData.RootNode` and nested children).
   - Studio lighting rig (primary key light + secondary fill light) and pedestal grid (`builtin://plane` with 3D caro pattern).
   - Parent-child transform propagation preserved: child parts inherit parent transforms accurately via `Entity::SetParent`.
   - Leased viewport targets via `RHIViewportDescriptor` on modern RHI and managed `Framebuffer` on OpenGL compatibility mode.
   - Interactive features preserved: hierarchy navigation, adding child parts, transform editing (translation/rotation/scale), mesh paths / drag-drop from content browser, collision shapes, rigid bodies, turntable auto-orbit, LMB orbit / RMB-MMB pan / zoom, save (`SaveAssembly`), and instantiate into active scene (`InstantiateIntoScene`).
2. **Wireframe Mode & Selection Highlighting (`Engine/Renderer/include/Axle/RenderCommand.hpp`, `Engine/Renderer/src/RenderCommand.cpp`, `Engine/Scene/src/SceneRenderer.cpp`, `Engine/Scene/src/RHISceneRenderer.cpp`, `Engine/Scene/src/RenderService.cpp`):**
   - Added `RenderCommand::SetWireframeMode(bool enabled)` OpenGL abstraction without exposing raw GL symbols.
   - OpenGL renderer executes wireframe rasterization when `request.Settings.Wireframe` is enabled.
   - Modern RHI reports honest capability diagnostics in `RenderResult::ErrorMessage` when wireframe is requested and renders solid geometry.
   - Selection highlighting: extracted `RenderMeshInstance::Selected` tints primary color with selection gold on both modern RHI and OpenGL backends.
3. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_AssemblyPreviewExtractionAndIsolation`: Builds a multi-part assembly with parent-child hierarchy, verifies world transform propagation (parent 1.0 + child 2.5 = 3.5), extracts `RenderWorld` with child selected, proves `Selected` flag propagation, validates `ViewRequest` parameters (AssemblyPreview, Wireframe), and verifies zero state leakage into the main scene.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRenderer AxleUI AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRenderer AxleUI AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (405 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS


---

## Package P41-16: VFX Preview

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 279-286

### Implementation Summary
1. **Migrated `VFXEditorPanel` to RenderService (`Engine/UI/include/Axle/VFXEditorPanel.hpp`, `Engine/UI/src/VFXEditorPanel.cpp`):**
   - Removed private `RHISceneRenderer` instance (`std::unique_ptr<RHISceneRenderer> m_RHISceneRenderer`) and raw GL drawing (`RenderPreview3D`).
   - Unified preview rendering through `RenderPreview(float width, float height)` using `RenderService::Get().RenderView(*m_PreviewScene, request)`.
   - Leveraged leased viewport targets via `RHIViewportDescriptor` on modern RHI and managed `Framebuffer` on OpenGL compatibility mode.
   - Preserved private deterministic simulation: `m_PreviewScene->OnUpdate(dt)` runs safely within the editor panel's preview scene with scrub/step/pause/play controls.
   - Preserved neutral empty-effect workflow: handles blank/unassigned effect safely without rendering artifacts or crashes.
   - Preserved interactive LMB orbit / RMB-MMB pan capture, multi-emitter stacks, and unsaved document behavior.
2. **RenderService Particle Stack Support (`Engine/Scene/src/RenderService.cpp`):**
   - Updated `RenderService::RenderView(Scene& scene, const ViewRequest& request)` to delegate directly to `RHISceneRenderer::RenderScene` and `SceneRenderer::RenderScene` respectively.
   - Automatically renders all meshes, materials, and GPU-instanced particle emitter stacks cleanly under the unified pipeline without duplicating render code.
3. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_VFXPreviewIsolationAndMultiEmitter`: Sets up a dedicated preview scene with multi-stack particle emitters, extracts and executes view requests, and verifies that particle stack configurations and preview scene mutations remain completely isolated from main scene state.

### Verification Evidence
- `cmake --build build --config Debug --target AxleUI AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleUI AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (405 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS

---

## Package P41-17: Assembly Preview

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 287-295

### Implementation Summary
1. **Migrated `AssemblyEditorPanel` to RenderService (`Engine/UI/include/Axle/AssemblyEditorPanel.hpp`, `Engine/UI/src/AssemblyEditorPanel.cpp`):**
   - Replaced GL-only viewport, raw OpenGL calls (`glPolygonMode`, `glDrawElements`), and duplicate shaders (`s_AsmVertSrc`, `s_AsmFragSrc`).
   - Unified preview rendering via `RenderService::Get().RenderView(world, request)` with `RenderViewKind::AssemblyPreview`.
   - Private preview scene `m_PreviewScene` dynamically built from the authored assembly hierarchy (`m_AssemblyData.RootNode` and nested children).
   - Studio lighting rig (primary key light + secondary fill light) and pedestal grid (`builtin://plane` with 3D caro pattern).
   - Parent-child transform propagation preserved: child parts inherit parent transforms accurately via `Entity::SetParent`.
   - Leased viewport targets via `RHIViewportDescriptor` on modern RHI and managed `Framebuffer` on OpenGL compatibility mode.
   - Interactive features preserved: hierarchy navigation, adding child parts, transform editing (translation/rotation/scale), mesh paths / drag-drop from content browser, collision shapes, rigid bodies, turntable auto-orbit, LMB orbit / RMB-MMB pan / zoom, save (`SaveAssembly`), and instantiate into active scene (`InstantiateIntoScene`).
2. **Wireframe Mode & Selection Highlighting (`Engine/Renderer/include/Axle/RenderCommand.hpp`, `Engine/Renderer/src/RenderCommand.cpp`, `Engine/Scene/src/SceneRenderer.cpp`, `Engine/Scene/src/RHISceneRenderer.cpp`, `Engine/Scene/src/RenderService.cpp`):**
   - Added `RenderCommand::SetWireframeMode(bool enabled)` OpenGL abstraction without exposing raw GL symbols.
   - OpenGL renderer executes wireframe rasterization when `request.Settings.Wireframe` is enabled.
   - Modern RHI reports honest capability diagnostics in `RenderResult::ErrorMessage` when wireframe is requested and renders solid geometry.
   - Selection highlighting: extracted `RenderMeshInstance::Selected` tints primary color with selection gold on both modern RHI and OpenGL backends.
3. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_AssemblyPreviewExtractionAndIsolation`: Builds a multi-part assembly with parent-child hierarchy, verifies world transform propagation (parent 1.0 + child 2.5 = 3.5), extracts `RenderWorld` with child selected, proves `Selected` flag propagation, validates `ViewRequest` parameters (AssemblyPreview, Wireframe), and verifies zero state leakage into the main scene.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRenderer AxleUI AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRenderer AxleUI AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Debug -R RenderUnitTests`: 1/1 passed, exit 0
- `ctest --test-dir build -C Release -R RenderUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (405 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS
