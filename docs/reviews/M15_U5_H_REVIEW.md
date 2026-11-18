# M15-U5-H (S5.8) Planar Reflection — Consolidated Delivery & Verification Report

> Historical Gemini delivery/self-verification. Superseded for independent review, correction commits, final 34/34 results and remaining visual gates by the [consolidated Codex review](M15_U5_H_CODEX_REVIEW.md).

Date: 2026-11-18
Milestone / Scope: **M15 U5 / S5.8 — Planar Reflection (`M15-U5-46.1` .. `M15-U5-46.8`)**
Commits:
- **Commit 1 (`M15-U5-H1`):** `bb20102b` (Base: `1fa57e80`) — Planar surface/oblique/confidence contract, settings/backend policy, `PlanarReflectionComponent` extraction/serialization/assembly, `RHIViewTargets` planar slots, isolated `m_PlanarViewContext`, automatic reflected winding inversion, non-recursive `CapturePlanarReflection`. See [M15_U5_H1_CHECKPOINT.md](M15_U5_H1_CHECKPOINT.md).
- **Commit 2 (`M15-U5-H2`):** Consolidated on parent `6daa53aa` (H1 code `bb20102b` + Codex U10-doc `6daa53aa`; awaiting independent Codex review).

---

## 1. Summary of Changes in H2 (`M15-U5-46.4`, `46.5`, `46.7`, `46.8`)

1. **`PlanarReflectionPass` & RenderGraph DAG (`M15-U5-46.4`)**:
   - Added [PlanarReflectionPass.hpp](../../Engine/Scene/src/Passes/PlanarReflectionPass.hpp) & [PlanarReflectionPass.cpp](../../Engine/Scene/src/Passes/PlanarReflectionPass.cpp), registered in [Engine/Scene/CMakeLists.txt](../../Engine/Scene/CMakeLists.txt).
   - Declares explicit DAG edges:
     - Optional `PlanarCopy` (runs only when SSR did not already copy `SceneColor -> OpaqueColor`, and copies `SceneDepth -> OpaqueDepth` when neither SSR nor Hi-Z visualization did).
     - `PlanarResolve` (`Depth`, `SceneNormal`, `OpaqueColor`, `ReflectionEnvironment`, `ReflectionResponse`, `PlanarColor` [imported `defined=true`], `SceneColor` with `LoadOp::LOAD` + `Access::Write`).
     - `PlanarRelease` ordered explicitly after `"PlanarResolve"`.
   - Unified `useReflectionMRT = useSSR || usePlanarResolve` across built-in forward PBR, skybox, custom material shaders (with graceful `HasReflectionTerms` fallback disablement), and `OpaqueAndSky` MRT attachments in [RHISceneRendererPasses.cpp](../../Engine/Scene/src/RHISceneRendererPasses.cpp).

2. **Unified Convex Resolve & Fallback Shader (`M15-U5-46.5`)**:
   - Added [PlanarReflection.hlsl](../../Engine/Resources/Shaders/PlanarReflection.hlsl) (`VSMain`/`PSMain`, 512B `RHIPlanarReflectionConstants`) + `RHIBlendAttachmentDesc::AlphaBlend()` pipeline in [RHISceneRenderer.cpp](../../Engine/Scene/src/RHISceneRenderer.cpp).
   - Evaluates single-sided plane normal alignment, distance-to-plane tolerance, quad extent bounds + soft edge fade, `MaxRoughness` smooth rolloff, reflected camera projection border fade, roughness-scaled narrow multi-tap smoothing, and HDR NaN/Inf sanitization.
   - Extracts `baseLit = max(OpaqueColor - Environment, 0.0)` and blends `src = (baseLit + planarRadiance * response, wPlanar)` via hardware `AlphaBlend` over `SceneColor` (`HDR` intermediate), guaranteeing exact normalized weight sum (`1.0`) across `Planar -> SSR -> LocalProbe -> SkyIBL` both with and without SSR.
   - Unified `EnableReflectionTerms`, `EnableNormal`, `EnableScreenStorage`, `HDR`, and auto-linear HDR tone-map elevation across [RHISceneRenderer.cpp](../../Engine/Scene/src/RHISceneRenderer.cpp), [RenderService.cpp](../../Engine/Scene/src/RenderService.cpp), and [RHIViewportDescriptor.cpp](../../Engine/UI/src/RHIViewportDescriptor.cpp).

3. **Transactional Capture Cache, Multi-Domain Invalidation & Last-Good Retention (`M15-U5-46.7`)**:
   - Added `PlanarCaptureLifecycleState`, `PlanarReflectionDomainStamps`, `PlanarReflectionInvalidation`, `PlanarReflectionCaptureStatus`, and `PlanarReflectionCacheStats` in [ReflectionService.hpp](../../Engine/Renderer/include/Axle/ReflectionService.hpp) and exposed control/diagnostic APIs on `RHISceneRenderer` & `RenderService`.
   - Implemented `ComputePlanarCaptureDomainStamps`, `SynchronizePlanarCaptureCacheState`, `PublishPlanarScratchCopy`, and `SchedulePlanarReflectionCaptures` (invoked before `viewCtx.BeginFrame()`).
   - Renders into isolated scratch `m_PlanarPublishScratchColor`/`m_PlanarPublishScratchDepth` and commits into `viewTargets->GetPlanarColor()` via GPU copy only upon full success, preserving prior published reflection pixels verbatim across allocation/render/copy failures (`PreservedPreviousGeneration = true`) or cleanly skipping resolve when no valid capture exists yet.
   - Supports `Automatic` and `Manual` (`RequestPlanarReflectionCapture`, `InvalidatePlanarReflection`) capture lifecycles.

4. **Editor UI, Hierarchy, Assembly Panel & Automated Tests (`M15-U5-46.8`)**:
   - Wired Inspector property drawer + live capture status/diagnostics + `Capture Now` / `Invalidate` actions + Add Component menu in [EntityInspector.cpp](../../Engine/UI/src/EntityInspector.cpp).
   - Added `Planar Reflector` creation/child menu entries in [SceneHierarchyPanel.cpp](../../Engine/UI/src/SceneHierarchyPanel.cpp), vector icon in [UIVectorIcons.hpp](../../Engine/UI/include/Axle/UIVectorIcons.hpp), preview/instantiate/save support in [AssemblyEditorPanel.cpp](../../Engine/UI/src/AssemblyEditorPanel.cpp), and `m_EnablePlanarReflections` in `RenderFXPopup` + `View -> Rendering Features` in [EditorLayer.hpp](../../Editor/src/EditorLayer.hpp) / [EditorLayer.cpp](../../Editor/src/EditorLayer.cpp).
   - Extended [Test_M15_ReflectionCaptureContractUnit.cpp](../../Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp), [Test_M15_ReflectionCaptureRendering.cpp](../../Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp), and [Test_PluginSession.cpp](../../Tests/src/Test_PluginSession.cpp).

---

## 2. Verification Evidence (Debug & Release)

### Build Targets (`13/13` Debug + `13/13` Release — Exit `0`)
Built cleanly in both `Debug` and `Release`:
`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`, `AxleEnvironmentBakeTests`, `AxleRHIResourceViewTests`, `AxleEditor`, `AxleRuntime`, `AxleMaterialGraphTests`, `AxleMaterialGraphCompilerTests`, `AxleMaterialGraphOpenGLPolicyTests`, `AxleMaterialGraphSlangTests`, `AxleMaterialGraphRuntimeTests`, `AxleMaterialGraphResourceTests`, `AxlePluginSessionTests`.

### Automated Test Matrix (`build/Gemini-U5H2-results.json`: `28/34` PASS, `6` DX12 SwapChain Environment-Blocked)
- **PASS (`exit 0`) — All CPU Contract & Unit Suites (Debug & Release):**
  - `AxleRenderUnitTests.exe --m15-hiz-contract` (Debug & Release): PASS
  - `AxleRenderUnitTests.exe --m15-reflection-capture-contract` (Debug & Release): PASS (includes H2 `PlanarReflection.hlsl`, `PlanarReflectionPass.cpp`, `RHISceneRendererPasses.cpp`, and `RenderService.cpp` contract checks)
  - `AxleRenderUnitTests.exe --lighting-services` (Debug & Release): PASS
  - `AxleRenderUnitTests.exe` full CPU suite (Debug & Release): PASS
  - `ctest --test-dir build -C {Debug,Release} -R "M15HiZContract|M15ReflectionCaptureContract|M15EnvironmentContract|MaterialGraph"`: `9/9` PASS (Debug & Release)
  - `ctest --test-dir build -C {Debug,Release} -R "^PluginSessionTests$"`: PASS (Debug & Release; includes `PlanarReflectionComponent` Assembly Editor preview/edit/save/remove round-trip)
  - `AxleEnvironmentBakeTests.exe` (Debug & Release): PASS
- **PASS (`exit 0`) — Vulkan & Offscreen DX12 RHIResourceView:**
  - `AxleRHIResourceViewTests.exe dx12 storage` (Debug & Release): PASS
  - `AxleRHIResourceViewTests.exe vulkan storage` (Debug & Release): PASS
  - `AxleRHIResourceViewTests.exe vulkan targets` (Debug & Release): PASS (`RHIResourceView: PASS`, zero RHI validation errors)
- **PASS (`exit 0`) — Native GPU D3D12 & Vulkan Rendering (`AxleEnvironmentRenderingTests.exe`, Debug & Release):**
  - `Debug D3D12`, `Debug Vulkan`, `Release D3D12`, `Release Vulkan` (`4/4`): ALL PASS (`exit 0`, zero RHI validation errors).
  - Observed planar readbacks across all 4 runs:
    - `[M15PlanarGPU] Center RGBA16=(0, 8, 0)` (self-mirror excluded, behind-plane cube clipped, single-sided winding preserved)
    - `[M15PlanarResolve] Mirror ON RGBA8=(0, 0.996078, 0)` (Planar ON without SSR vs Planar OFF baseline, combined SSR+Planar, `MaxRoughness` fallback to SSR and to SkyIBL, cache hit reuse, material/revision invalidation, `Manual` Idle->Ready->Stale->Refreshed lifecycle, `ScopedRHIFault("PlanarPublishCopy")` last-good green retention and blue recovery).
- **PASS (`exit 0`) — Strict Vulkan Editor Startup (Debug & Release):**
  - `AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback` (Debug & Release): PASS (`fallback=none`)
  - `AxleEditor.exe --verify-startup Tests/Fixtures/M15/U4E/U4E.axleproj --rhi=vulkan --no-fallback` (Debug & Release): PASS (`fallback=none`)
- **ENVIRONMENT-BLOCKED (`exit 1`) — 6 DX12 SwapChain Checks during unattended 3-adapter display state:**
  - `AxleRHIResourceViewTests.exe dx12 targets` (Debug & Release) and `AxleEditor.exe --verify-{empty,startup} ... --rhi=dx12 --no-fallback` (Debug & Release): failed device creation with `[NRI Warning] (Creation.cpp:760) D3D12::NVIDIA GeForce RTX 3070 - nriGetInterface(): Interface 'SwapChainInterface' is not supported by the device!` because Windows enumerated only 3 adapters (optimus/display-sleep state where `DeviceD3D12::FillDesc` sets `features.swapChain = HasOutput()` to `false`). Note: earlier on the active 4-adapter display state at `15:29:36`, `AxleRHIResourceViewTests.exe dx12 targets` Debug had passed with `RHIResourceView: PASS`.
- **Static & Hygiene Checks (`exit 0`):**
  - `python scripts/check_architecture.py`: `Files scanned: 615; CMake files: 25`, `AXLE_ARCH_CHECK PASS`
  - `python scripts/agent_exchange.py validate`: PASS
  - `git diff --check`: PASS

### Open / Deferred Gates
- `M15-U5-46.5` & `M15-U5-46.8` interactive **EDITOR VISUAL** captures remain user-deferred to the consolidated post-U5 visual pass.
- Re-verification of the 6 DX12 SwapChain checks (`AxleRHIResourceViewTests dx12 targets` and `AxleEditor --rhi=dx12`) once the active Windows display output is attached to the discrete RTX 3070 adapter.
- Independent Codex review across consolidated S5.8 (`H1 + H2`).
