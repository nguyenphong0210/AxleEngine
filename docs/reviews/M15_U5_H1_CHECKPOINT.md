# M15-U5-H1 Checkpoint — Planar Reflection Foundation, Contract, Targets & Mirrored Capture

> Historical Gemini delivery/self-verification. Superseded for independent review, correction commits, final 34/34 results and remaining visual gates by the [consolidated Codex review](M15_U5_H_CODEX_REVIEW.md).

Date: 2026-11-18
Base commit: `1fa57e80` (`docs(m15): record U5-G independent review and feature coverage`)
Scope covered: **S5.8 Part 1 (`M15-U5-46.1`, `46.2`, `46.3`, capture/oblique/winding portion of `46.6`)**
Remaining in S5.8 (Commit 2 / `M15-U5-H2`): `PlanarReflectionPass` (`46.4`), unified convex `Planar -> SSR -> Probe -> SkyIBL` resolve/fallback shader (`46.5`), scheduled capture cache + invalidation/failure retention (`46.7`), and Inspector/AssemblyEditor/EditorLayer UI + `PLAN.md` closure (`46.8`).

## Delivered Components & Architecture

1. **Planar Surface Contract (`M15-U5-46.1`)**
   - `Engine/Renderer/include/Axle/RenderContracts.hpp` & `src/RenderContracts.cpp` (`PlanarReflectionContract`):
     - `PlanarReflectionSurface`, `PlanarReflectionPolicy`, `MirroredCameraResult`, `PlanarReflectionDomainStamps`, `PlanarReflectionInvalidation`.
     - Orthonormal surface extraction (`BuildSurfaceFromReflector`), exact Householder reflection (`BuildReflectionMatrix`, `det = -1`, `R^2 = I`), `ReflectPoint`, `ReflectDirection`, `SignedDistanceToPlane`.
     - **Zero-to-One (`[0, 1]`) Oblique Near-Plane Clipping** (`BuildObliqueClipProjectionZO`): maps the world mirror plane into mirrored view space, solves far-corner scale `(row3 . q) / (viewPlane . q)` preserving `z_ndc in [0, 1]` with `z_ndc = 0` on the mirror plane and `< 0` clipped behind it, while reproducing standard `glm::perspectiveRH_ZO` at `C_view = (0, 0, -1, -near)`.
     - Confidence rolloff (`EvaluatePlanarReflectionConfidence`) combining surface normal alignment, plane distance tolerance, extent bounds fade, and roughness cutoff.
   - `Engine/Renderer/include/Axle/PerspectiveCamera.hpp` & `src/PerspectiveCamera.cpp`:
     - Stores `m_UnclippedProjectionMatrixZO` alongside `m_ProjectionMatrix` / `m_ProjectionMatrixZO` and exposes `SetCustomProjection(...)` so skybox far-plane ray unprojection uses the unclipped projection without dividing by skewed oblique far `w`.

2. **Component, Extraction, Serialization, Assembly & Settings Policy**
   - `Engine/Core/include/Axle/Types.hpp`: `PlanarReflectionCaptureMode` (`Automatic`, `Manual`) + string helpers.
   - `Engine/Renderer/include/Axle/SceneRenderSettings.hpp`: `EnablePlanarReflections`, `PlanarReflectionResolutionScale`, `PlanarReflectionStrength`, `PlanarReflectionMaxRoughness`.
   - `Engine/Renderer/src/RenderBackendPolicy.cpp`: disables `EnablePlanarReflections` with explicit `"planar_reflections_disabled:compatibility_backend"` on Compatibility tier; native D3D12/Vulkan Standard tiers enable it natively.
   - `Engine/Renderer/src/ViewContext.cpp`: includes planar feature settings in `HashViewFeaturePolicy`.
   - `Engine/Renderer/include/Axle/RenderWorld.hpp` (`RenderPlanarReflector`), `Engine/Scene/include/Axle/Components.hpp` (`PlanarReflectionComponent`), `RuntimeComponentCodec.cpp`, `SceneExtraction.cpp`, `SceneSerializer.cpp`, `AssemblySerializer.hpp`/`.cpp`.

3. **RHI View Target Generation & Allocation (`M15-U5-46.3`)**
   - `Engine/RHI/include/Axle/RHI/RHIViewTargets.hpp` & `src/RHIViewTargets.cpp`:
     - Added `RHIViewTarget::PlanarColor` (`RGBA16_FLOAT`) and `RHIViewTarget::PlanarDepth` (`DepthFormat` / `D32_FLOAT`), `RHIViewTargetDesc::EnablePlanarReflection`, `PlanarResolutionScale` (clamped `[0.25, 1.0]`), and `ComputePlanarTargetDimension`.
     - Validated atomic target generation creation, resize scaling, disabled zero-allocation, and OOM rollback.
   - Wired through `Engine/Scene/src/RenderService.cpp`, `Engine/UI/include/Axle/RHIViewportDescriptor.hpp`/`.cpp`, and `Engine/Scene/src/RHISceneRenderer.cpp`.

4. **Independent Mirrored Capture `ViewContext`, Reflected Winding & Non-Recursive Execution (`M15-U5-46.2`, `46.6`)**
   - `Engine/Scene/include/Axle/RHISceneRenderer.hpp`, `src/RHISceneRenderer.cpp`, `src/RHISceneRendererPasses.cpp`, `CustomMaterialProgram.hpp`/`.cpp`:
     - Dedicated `m_PlanarViewContext` (`ActiveViewContext()` dispatches to `m_PlanarViewContext` when `m_PlanarSession.Active` is true, leaving main `m_ViewContext` frame index & TAA temporal history completely unmutated).
     - Automatic winding inversion (`ReverseWinding = (det(mirroredView) < 0)`) plumbed across built-in `GetOrCreatePipeline`, `GetOrCreateSurfaceNormalPipeline`, and `CreateCustomMaterialPipeline` / `GetOrCreateCustomPipeline` (`FrontCounterClockwise = !reverseWinding`) plus fast-path guards.
     - `CapturePlanarReflection` / `CapturePlanarReflectionToTargets` synchronizes prior completions, sanitizes settings (`EnablePlanarReflections = false`, `EnableSSR = false`, `EnableTAA = false`, `MSAASamples = 1`, linear HDR output), excludes self-reflector entity & nested reflectors via `FilterCaptureWorld`, blocks direct recursion, and guards state with `CaptureViewStateGuard`.

## Verification Evidence (`34/34` Runtime Commands + Static Checks)

All 13 Debug & Release targets built cleanly with exit code 0:
`AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor AxleRuntime AxleMaterialGraphTests AxleMaterialGraphCompilerTests AxleMaterialGraphOpenGLPolicyTests AxleMaterialGraphSlangTests AxleMaterialGraphRuntimeTests AxleMaterialGraphResourceTests AxlePluginSessionTests`

Runner ledger: `build/Gemini-U5H1-results.json` (`34/34` PASS, exit code `0`):
- **CPU Focused & Full Suites (Debug & Release):**
  - `AxleRenderUnitTests.exe --m15-hiz-contract`: PASS (`exit 0`)
  - `AxleRenderUnitTests.exe --m15-reflection-capture-contract`: PASS (`exit 0`, including planar surface math, ZO oblique `[0,1]` clip plane & tilted plane tests, identity limit check, camera-behind-plane rejection, edge/roughness confidence, extent scaling, feature hash & per-domain invalidation)
  - `AxleRenderUnitTests.exe --lighting-services`: PASS (`exit 0`)
  - `AxleRenderUnitTests.exe` (full CPU suite, including `PlanarReflectionComponent` extraction, YAML round-trip, `RuntimeComponentCodec` clone, `AssemblySerializer` round-trip, OpenGL fallback policy): PASS (`exit 0`)
- **CTest & Bake (Debug & Release):**
  - `ctest --test-dir build -C {Debug,Release} --output-on-failure -R "M15HiZContract|M15ReflectionCaptureContract|M15EnvironmentContract|MaterialGraph"`: `9/9` PASS each
  - `ctest --test-dir build -C {Debug,Release} --output-on-failure -R "^PluginSessionTests$"`: PASS each
  - `AxleEnvironmentBakeTests.exe`: PASS Debug & Release (`exit 0`)
- **RHI Target & Storage Views (`AxleRHIResourceViewTests.exe` Debug & Release x DX12 & Vulkan):**
  - `dx12 storage`, `dx12 targets`, `vulkan storage`, `vulkan targets`: all 8 runs PASS (`exit 0`, zero RHI validation errors; verifies `PlanarColor`/`PlanarDepth` allocation, resize, invalid scale rejection, and OOM generation retention).
- **Native GPU Capture (`AxleEnvironmentRenderingTests.exe` Debug & Release x DX12 & Vulkan):**
  - All prior U4/U5/U5-B..U5-G suites plus `M15PlanarGPU` & planar device-reset checks: 4/4 PASS (`exit 0`, zero RHI validation errors).
  - Readback center of `PlanarColor` (`RGBA16_FLOAT`) observing single-sided `CullMode::Back` green cube `(0, 8, 0)` while excluding red self-reflector mesh and clipping blue behind-mirror blocker cube:
    - Debug DX12: `(0, 8, 0)`
    - Debug Vulkan: `(0, 8, 0)`
    - Release DX12: `(0, 8, 0)`
    - Release Vulkan: `(0, 8, 0)`
- **Strict Editor Startup (`AxleEditor.exe --verify-empty-startup` & `--verify-startup Tests/Fixtures/M15/U4E/U4E.axleproj` with `--rhi={dx12,vulkan} --no-fallback`):**
  - All 8 Debug & Release runs PASS (`exit 0`, `fallback=none`).
- **Architecture & Repo Hygiene:**
  - `python scripts/check_architecture.py`: `Files scanned: 613; CMake files: 25`, `AXLE_ARCH_CHECK PASS`
  - `python scripts/agent_exchange.py validate`: PASS
  - `git diff --check`: PASS (`exit 0`)
