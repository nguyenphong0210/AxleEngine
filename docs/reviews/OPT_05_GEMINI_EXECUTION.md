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
- [x] **P41-18:** Thumbnail render jobs
- [x] **P41-19:** Standalone Runtime render integration
- [x] **P41-20:** Material/shader/color/capability contract
- [x] **P41-25:** Physics debug and overlays

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

---

## Package P41-18: Thumbnail Render Jobs

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 296-302

### Implementation Summary
1. **Migrated `AssetThumbnailManager` to RenderService (`Engine/UI/include/Axle/AssetThumbnailManager.hpp`, `Engine/UI/src/AssetThumbnailManager.cpp`):**
   - Removed duplicate embedded shaders (`s_ThumbnailVertSrc`, `s_ThumbnailFragSrc`), raw GL Framebuffer (`m_FBO`), and Renderbuffer (`m_RBO`).
   - Unified 3D asset thumbnail generation (`.axlemesh`, `.axle`, `.axlebin`, `.obj`, `.gltf`, `.glb`, `.fbx`) behind `RenderService::Get().RenderView(world, request)` with `RenderViewKind::Thumbnail`.
   - Maintained asset texture pipeline (`ImageTextureCache`) and CPU PBR material thumbnail rendering (`RenderMaterialThumbnail`).
   - Leased RHI texture render targets via `RHI::RHITexture` and registered via `GuiTextureLease` on modern RHI; allocated clean `Framebuffer` targets on OpenGL compatibility mode.
   - Retired textures safely via `GuiDeferredReleaser`, preventing descriptor or memory reuse before frame completion.
2. **Cache Lifecycle & Automatic Invalidation:**
   - Added asset write-time revision tracking (`m_AssetRevisions`) using `std::filesystem::last_write_time`.
   - On cache lookup (`GetThumbnailTex`), compares recorded write time with current disk state; automatically evicts stale thumbnails and schedules re-render when assets are modified on disk.
   - Cleans up and clears caches safely on asset deletion.
3. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_ThumbnailRenderJobAndCacheContracts`: Verifies thumbnail `ViewRequest` configuration (`RenderViewKind::Thumbnail`, 128x128 resolution, disabled shadows/skybox), clean `RenderService` snapshot execution, and asset write-time eviction logic without lingering stale cache references.

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

## Package P41-19: Standalone Runtime Render Integration

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 303-315

### Implementation Summary
1. **Migrated `RuntimeLayer` to RenderService (`Runtime/src/RuntimeLayer.hpp`, `Runtime/src/RuntimeLayer.cpp`):**
   - Removed private OpenGL `SceneRenderer` instance (`std::unique_ptr<SceneRenderer> m_SceneRenderer`).
   - Integrated `RenderService::Get().RenderView(*m_Scene, request)` with `RenderViewKind::Main` on every frame update (`OnUpdate`).
   - Strictly preserved the Standalone Runtime zero-ImGui boundary: no dependency on `Axle::UI`, no headers from `Engine/UI/` (e.g. `RHIViewportDescriptor.hpp`), and no Dear ImGui context or widget calls.
2. **Direct Swapchain Blit Presentation (`Engine/RHI/include/Axle/RHI/RHIPresentationContext.hpp`, `Engine/RHI/src/RHIPresentationContext.cpp`):**
   - Extended `RHIPresentationContext::RenderFrame` with an optional `nri::Texture* blitSource` parameter.
   - When running in standalone mode (no UI draw callback), directly copies the rendered scene target texture to the current swapchain backbuffer:
     - Transitions `blitSource` to `nri::TextureTransitionBarrierDesc` with `AccessBits::COPY_SOURCE` and `TextureLayout::COPY_SOURCE`.
     - Transitions swapchain backbuffer to `AccessBits::COPY_DEST` and `TextureLayout::COPY_DEST`.
     - Executes `nri::CmdCopyTexture`.
     - Transitions backbuffer to `TextureLayout::PRESENT` and executes swapchain queue submission and `PresentBuffers`.
3. **Default Presentation Targets in RenderService (`Engine/Scene/include/Axle/RenderService.hpp`, `Engine/Scene/src/RenderService.cpp`):**
   - `RenderService` automatically manages and resizes default presentation targets (`m_DefaultMainTargets`) for `RenderViewKind::Main` when no custom offscreen target descriptor is passed.
   - Exposes `GetDefaultMainColorTarget()` returning the allocated `nri::Texture*` for swapchain presentation blit.
4. **Runtime Backend Selection & Product Parity (`Runtime/src/Main.cpp`):**
   - Unified backend precedence matching the Editor: explicit CLI arguments (`--rhi=dx12`, `--rhi=vulkan`, `--rhi=opengl`) override project settings; if omitted, inspects project configuration's `RHIBackend` (`ProjectSerializer`), falling back gracefully to D3D12/OpenGL.
   - Installs `RuntimeRenderContext` as the global presentation context before initializing the RHI device and swapchain.
5. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - `Test_StandaloneRuntimeContracts`: Verifies CLI backend argument parsing (`--rhi=dx12`, `--rhi=vulkan`, `--rhi=opengl`), `RenderViewKind::Main` default target allocation and resize semantics in `RenderService`, and validates that Runtime layer contracts do not depend on UI headers.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRHI AxleScene Runtime AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRHI AxleScene Runtime AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Release -R RuntimeProductParityUnitTests`: 1/1 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (405 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS

---

## Package P41-20: Material/Shader/Color/Capability Contract

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 316-325

### Implementation Summary
1. **Consolidated Render Contracts (`Engine/Renderer/include/Axle/RenderContracts.hpp`, `Engine/Renderer/src/RenderContracts.cpp`):**
   - **Canonical Vertex Layout:** Consolidated static 64-byte PBR vertex stream (`RHIVertex3D`: Position, Normal, Tangent with handedness w, TexCoord, Color) and skinned 96-byte stream (`RHISkinnedVertex3D`). Added `GetCanonicalVertexInputDesc()` and `VerifyVertexLayout()` checking expected offsets, formats, and stride.
   - **Constant Buffer Layouts & Packing:** Consolidated frame constants (register b0, space0, 256B CBV aligned), object root constants (128B push constants), light constants (b2, space0), material constants (`RHIMaterialConstants`, b3, space0, 256B), and tone mapping constants (b0, space0, 256B). Verified byte packing matching HLSL layouts.
   - **Texture Slot Table & Color Spaces:** Consolidated table of 10 standard slots (Albedo, Normal, Roughness, Metallic, AO, Emissive, Clearcoat, AnisotropyFlow, DetailNormal, DetailMask) declaring exact semantic roles, formats, and color space contracts (`IsSRGBSlot`: true for Albedo and Emissive, false for Normal, Roughness, Metallic, AO, Clearcoat, Anisotropy, Detail).
2. **Color Space & Tone Mapping Ownership:**
   - Formalized engine color pipeline: Lighting and radiance accumulation evaluate strictly in Linear HDR space.
   - Tone mapping (filmic ACES/Reinhard) and sRGB transfer are exclusively owned by the post-processing tone mapping pass (`ToneMapping.hlsl` / `RenderToneMapping`), never ad-hoc within surface materials.
   - Provided canonical conversion utilities (`LinearToSRGB`, `SRGBToLinear` for floats, vec3, and vec4).
3. **Clip & Depth Conventions:**
   - Right-handed coordinate system ($+X$ right, $+Y$ up, $-Z$ into screen).
   - Depth range: $[0.0, 1.0]$ for Modern RHI (D3D12/Vulkan), $[-1.0, 1.0]$ for OpenGL.
   - Default depth test compare op: `LESS`. Counter-clockwise (CCW) front face winding with back-face culling by default.
4. **Surface Modes & Blending:**
   - Standardized pipeline state generation for all four surface models:
     - `SurfaceType::Opaque`: DepthWrite = true, Blend = false, Cull = Back.
     - `SurfaceType::Cutout`: DepthWrite = true, Blend = false, Alpha cutoff in shader.
     - `SurfaceType::Transparent`: DepthWrite = false, Blend = true (`SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA`), Cull = Back/None.
     - `SurfaceType::Additive`: DepthWrite = false, Blend = true (`SRC_ALPHA` / `ONE`), Cull = Back/None.
5. **Non-Destructive Capability & Fallback Reporting:**
   - Separated physical hardware limits (`nri::DeviceDesc`, ray tracing tiers) from software renderer capabilities (`RendererCapabilities`).
   - Added `ValidateMaterialCompatibility()`: When OpenGL compatibility backend encounters advanced features (custom HLSL shaders, refraction copies), it generates explicit diagnostics in `AdvancedFeatureDiagnostic`, but strictly preserves all authored `Material` properties without mutating or resetting them.
   - Preserved `CustomProgramCache` last-good generations (`CustomReadyGeneration`) on recompilation or syntax errors.
6. **Dedicated Unit Verification (`Tests/rendering/Test_RenderContractsUnit.cpp`):**
   - Added `Test_RenderContractsSpecifications()` verifying vertex input descriptions, constant buffer packing, all 10 texture slot color spaces, linear/sRGB conversions, depth/clip ranges, surface blend states, and non-destructive OpenGL fallback.

### Verification Evidence
- `cmake --build build --config Debug --target AxleRenderer AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleRenderer AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Release -R "RenderUnitTests|MaterialUnitTests"`: 2/2 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (407 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS

---

## Package P41-25: Physics Debug and Overlays

- **Status:** Implemented and self-verified in Debug and Release.
- **Scope & Contract:** `docs/design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md` lines 362-369

### Implementation Summary
1. **Supplied Active Edit/Play `PhysicsScene` to ViewRequest & Viewports (`Editor/src/EditorLayer.cpp`):**
   - Fixed modern RHI viewport scene submission in `EditorLayer::RenderMainViewport()`: replaced hardcoded `nullptr` with `m_PhysicsScene.get()`.
   - The active `m_PhysicsScene` is now consistently supplied across all lifecycle states (Edit mode, Play mode simulation clone, Pause inspection, and Stop restoration).
2. **Internal Fallback Physics Debug Renderer (`Engine/Scene/include/Axle/SceneRenderer.hpp`, `Engine/Scene/src/SceneRenderer.cpp`):**
   - Added `std::unique_ptr<PhysicsDebugRenderer> m_InternalDebugRenderer` to `SceneRenderer`.
   - In `SceneRenderer::Init()`, allocates `m_InternalDebugRenderer` when a window context is active.
   - When callers (such as `RenderService::RenderView`) pass `physicsDebugRenderer = nullptr`, `SceneRenderer` automatically falls back to `m_InternalDebugRenderer.get()`, ensuring physics wireframes render without requiring callers to construct or pass ad-hoc debug renderers.
3. **Preserved Collider / Vehicle Flags, Depth Bias, Alpha & View Isolation:**
   - Full support for `PhysicsDrawSettings` flags (`DrawShapes`, `DrawWireframe`, `DrawBoundingBoxes`, `DrawVelocity`, `DrawCenterOfMass`).
   - Line generation operates directly through JPH `DrawDebug` line batching without requiring GL or RHI mesh allocation or creation.
   - View isolation strictly enforced: preview requests (`RenderViewKind::AssetPreview`, `RenderViewKind::VFXPreview`, `RenderViewKind::AssemblyPreview`, `RenderViewKind::Thumbnail`) pass `request.PhysicsScene = nullptr` and `request.Settings.ShowPhysicsDebug = false`, guaranteeing zero line leakage or crosstalk into preview targets.
4. **Dedicated Unit Verification (`Tests/rendering/Test_RenderServiceUnit.cpp`):**
   - Added `Test_PhysicsDebugAndOverlayContracts()`:
     - Verifies `PhysicsScene` collider creation (Box, Sphere, Cylinder, Plane).
     - Verifies `PhysicsDebugRenderer` line batching and flag responsiveness (shape wireframes and bounding box expansions) with zero GL mesh creation.
     - Verifies `ViewRequest` configuration, active `PhysicsScene` propagation, depth bias, and alpha settings.
     - Verifies view isolation between main viewport and preview requests.
     - Verifies Edit / Play / Pause / Stop lifecycle transitions, proving zero physics drift and safe pointer lifetimes.

### Verification Evidence
- `cmake --build build --config Debug --target AxleScene AxleEditor AxleRenderUnitTests`: exit 0
- `cmake --build build --config Release --target AxleScene AxleEditor AxleRenderUnitTests`: exit 0
- `build/Tests/Debug/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `build/Tests/Release/AxleRenderUnitTests.exe`: exit 0 (all 5 suites passed)
- `ctest --test-dir build -C Release -R "RenderUnitTests|MaterialUnitTests"`: 2/2 passed, exit 0
- `python scripts/check_architecture.py --root .`: PASS (407 files / 18 CMake files)
- `python scripts/test_baseline_tools.py`: 18/18 PASS
- `git diff --check`: PASS

---

## Phase OPT-05 Final Verification & Delivery Summary

All 10 packages of Phase OPT-05 have been implemented, self-verified in Debug and Release, and committed:

| Package | Commit | Description | Verification Suites |
|---|---|---|---|
| **P41-12** | `48209c7` | RenderWorld & shared scene extraction | `RenderWorldExtractionUnit` |
| **P41-13** | `2ca327e` | RenderService & modern render graph | `RenderServiceUnit` (5 test cases) |
| **P41-14** | `a674041` | OpenGL compatibility behind RenderService | `RenderServiceUnit` (GL backend path) |
| **P41-15** | `9f89ff2` | Asset/Material preview migration & leased textures | Panel builds, texture leasing verified |
| **P41-16** | `d8c847f` | VFX preview panel migration to RenderService | Panel builds, particle view requests verified |
| **P41-17** | `4543a0a` | Assembly preview migration to RenderService | Panel builds, multi-entity isolation verified |
| **P41-18** | `7ae65a6` | Thumbnail render jobs via RenderService | `AssetThumbnailManager` async/leased jobs |
| **P41-19** | `22ffd61` | Standalone Runtime integration & presentation | Zero-ImGui runtime boundary, headless swapchain |
| **P41-20** | `3c2f22b` | Material/shader/color/capability contracts | `RenderContractsUnit` (layout, sRGB, tone mapping) |
| **P41-25** | `5ce3aa0` | Physics debug & overlays via ViewRequest | `RenderServiceUnit` (debug lines, zero GL mesh) |

### Global Verification Matrix
- **Full CTest Suite:** `ctest --test-dir build -C Release --output-on-failure`: **50/50 tests passed (100%)**.
- **Architecture Integrity:** `python scripts/check_architecture.py --root .`: **PASS** (407 source files, 18 CMake files).
- **Baseline Tools:** `python scripts/test_baseline_tools.py`: **18/18 PASS**.
- **Dedicated Rendering Unit Tests:** `AxleRenderUnitTests.exe` (Debug & Release): **5/5 suites passed** (`RenderContracts`, `RenderPolicy`, `RenderPrimitives`, `RenderWorld`, `RenderService`).
- **Vendor Integrity:** Pinned NRI commit `6998e3fbbaed1b132f386a9d49743a2481003487` completely pristine.
