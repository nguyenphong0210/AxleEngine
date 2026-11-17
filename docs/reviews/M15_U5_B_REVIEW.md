# M15 U5 Batch B — Dynamic Six-Face Reflection Capture Self-Verification Review (`M15-U5-08.1` → `M15-U5-15`)

Self-verification delivery report for Milestone 15 Unit 5 Batch B (`U5-B`, SubUnit `S5.2`: `M15-U5-08.1` through `M15-U5-15`), implemented against accepted `U5-A` baseline commit `966049eb` (delivered in a single coherent local commit). Independent Codex review and acceptance remain pending per [WORKING_PROTOCOL.md](../agents/WORKING_PROTOCOL.md). Interactive Editor visual capture (`M15-U5-08.1`, `08.2`, `10.1`–`10.4`, `11.2`, `13.1`, `13.2`, `14`) remains explicitly deferred per user direction to the post-U5 consolidated visual pass.

---

## 1. Requirement Coverage Summary

| Requirement | Status | Summary of Implementation & Verification |
| :--- | :--- | :--- |
| **M15-U5-08.1 — Allocate capture color targets** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `Passes::ReflectionCaptureTargets::Configure` allocates single-sample linear HDR `RGBA16_FLOAT` `FaceColor` and `OrientedColor` targets plus immutable 6-layer `RGBA16_FLOAT` candidate `Cubemap` transactionally via `RHITextureDesc`. `ReflectionCaptureContract::ValidateCapturePolicy` rejects `SampleCount != 1`, non-HDR color formats, non-power-of-two/out-of-range resolutions, and invalid clip ranges. |
| **M15-U5-08.2 — Allocate capture depth targets** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `Passes::ReflectionCaptureTargets::Configure` allocates single-sample `D32_FLOAT` `FaceDepth` attached to `RHIViewTargets`; `ValidateCapturePolicy` enforces `D32_SFLOAT`; native D3D12/Vulkan readbacks confirm finite non-negative linear HDR output (`+X` emissive intensity `3.0` preserved > `2.0`, not tone-mapped). |
| **M15-U5-09.1 — Render six 90-degree views** | **PASS (self-verification)** | `ReflectionCaptureContract::BuildCubeFaceCamera` configures `90.0f` FOV and `1.0f` square aspect across faces `0..5` (`+X, -X, +Y, -Y, +Z, -Z`); verified by CPU contract checks and native Debug/Release D3D12/Vulkan 6-face readbacks (`M15_U5B_faces_strip.ppm`). |
| **M15-U5-09.2 — Validate directions/up vectors** | **PASS (self-verification)** | `ReflectionCaptureContract::ValidateCubeFaceBasis` checks finiteness, unit length, mutual orthogonality, `Right == cross(Up, Forward)` matching `EnvironmentCubeDirection` (`+Y,+Y,-Z,+Z,+Y,+Y`), and `FlipHorizontalForCubemapCopy`. Native D3D12/Vulkan off-axis quadrant markers on `+X`, `+Y`, `+Z` verify exact horizontal & vertical orientation (`[U5-B +X] TR=(0.84,1.96,1.96) TL=(1.92,0,0) BR=(1.92,0,0)`). |
| **M15-U5-10.1 — Disable recursive SSR/probe sampling** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `SanitizeCaptureSettings` forces `EnableSSR = false`; `FilterCaptureWorld` clears `ReflectionProbes` when `!AllowProbeSampling`; `RHISceneRenderer::PrepareReflectionProbe` clears `m_ActiveReflectionProbePrefilter` during `m_CaptureSession.Active`. Verified on CPU and native D3D12/Vulkan. |
| **M15-U5-10.2 — Disable TAA history** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Capture uses an isolated `m_CaptureViewContext` (`ActiveViewContext()` routing in `RHISceneRenderer`, `RHISceneRendererPasses`, and `PostProcessPass`), preserves `m_ImplicitViewTargets`, and restores all main-view state cleanly (`pendingMainCompletion` copy prevents clearing `m_LastCompletion`). Native D3D12/Vulkan checks verify main-view `FrameIndex` and `m_LastCompletion` are invariant across capture and advance cleanly (`frame + 1`) on subsequent render. |
| **M15-U5-10.3 — Disable Editor/UI** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `SanitizeCaptureSettings` disables `Wireframe` and `ShowPhysicsDebug`; `FilterCaptureWorld` strips `DebugLines`; `RHISceneRendererPasses` skips overlay/debug passes during isolated capture (`IncludeEditorOverlays = false`), verified with crossing debug lines on CPU and native D3D12/Vulkan. |
| **M15-U5-10.4 — Disable capture feedback** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `FilterCaptureWorld` strips `ExcludeEntityID` and `AdditionalExcludedEntityIDs`; candidate cubemap is never bound as SRV during capture passes. Native D3D12/Vulkan test verifies bright emissive self-sphere at `(0,0,0)` (`EntityID = 777 == probe.EntityID`) does not pollute captured faces. |
| **M15-U5-11.1 — Render opaque/cutout scene** | **PASS (self-verification)** | `FilterCaptureWorld` and `RHISceneRendererPasses` render `Opaque` and `Cutout` meshes + skybox (`AtmosphericSkybox` linear HDR mode `-1.0f`) while skipping `Transparent`/`Additive` meshes and `Particles`; native D3D12/Vulkan verify orange `Cutout` marker in `-Z` bottom-left while excluding `Transparent` blue blocker in front of `+X`. |
| **M15-U5-11.2 — Render selected shadows** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `FilterCaptureWorld` and `SanitizeCaptureSettings` honor `CaptureShadows` policy (`EnableShadows`, directional `CastsShadows`, local `CastShadows`). Native Debug/Release D3D12/Vulkan readbacks confirm shadow delta on `+X` receiver wall (`[U5-B Shadows] unlit=(1.94751,1.94751,1.94751) shadowed=(0,0,0)`). |
| **M15-U5-12.1 — Copy faces with correct layer offsets** | **PASS (self-verification)** | `CubemapCapture.hlsl` (`OrientFaceCS`) horizontally mirrors RH face X (`size - 1u - dstPixel.x`) and sanitizes non-finite/negative values; `ReflectionCapturePass::RecordOrientAndCopy` executes `CopyTexture` into `mipOffset = 0`, `layerOffset = faceIndex` (`0..5`). Verified via native 6-layer `RGBA16_FLOAT` readbacks on D3D12 and Vulkan. |
| **M15-U5-12.2 — Validate copy transitions** | **PASS (self-verification)** | CPU `RenderGraph` test compiles `OpaqueAndSky -> CaptureOrient -> CaptureCopy -> Export` barriers and rejects read-before-write; native Debug/Release D3D12/Vulkan execute all transitions (`ColorAttachment -> ShaderResource -> ShaderResourceStorage -> CopySource -> CopyDest -> ShaderResource`) with `0` RHI validation errors. |
| **M15-U5-13.1 — Retain capture resources through completion** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `RenderGraph` `KeepAlive` retains face/oriented/depth/candidate cubemap leases through `RHICompletionToken`; inter-face and pre-reuse synchronization waits via `m_Device->WaitForCompletion` before mutating shared host-upload buffers or pools. |
| **M15-U5-13.2 — Publish capture generation transactionally** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Candidate cubemap is staged across faces `0..5` and committed into `m_DynamicProbeCaptures[probeKey]` only when all 6 faces succeed; `ReflectionService::Prepare` / `ResolvePublication` tracks `DynamicPending -> DynamicReady` only upon valid live handle publication (`Test_LightingServicesUnit.cpp`). |
| **M15-U5-14 — Prove red reference object in cubemap** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Native Debug/Release D3D12/Vulkan fixtures prove `+X` red HDR reference wall is captured cleanly in face `0` (`cPosX.r > 2.0`) and drives mirror-sphere reflection via SSR probe fallback (`[U5-B SSR Fallback] withProbe=(43,0,0) withoutProbe=(0,0,0)`, `ReflectionState::DynamicReady`, strip artifact `M15_U5B_faces_strip.ppm`). |
| **M15-U5-15 — Retain last-good generation on failure** | **PASS (self-verification)** | Mid-capture `ScopedRHIFault` on 3rd `BeginCommandBuffer` aborts Face `2` (`Success = false, CapturedFaces = 2, PreservedPreviousGeneration = true`), keeping prior valid green generation handle & texels intact; invalid `CaptureResolution = 127` emits `DYNAMIC_PROBE_SETTINGS` and preserves prior generation; subsequent clean capture recovers cleanly on Debug/Release D3D12 and Vulkan. |

---

## 2. Modified and Added Files

- **Scene & Renderer Contracts:**
  - `Engine/Scene/include/Axle/Components.hpp`
  - `Engine/Renderer/include/Axle/RenderWorld.hpp`
  - `Engine/Renderer/include/Axle/ReflectionService.hpp`, `Engine/Renderer/src/ReflectionService.cpp`
  - `Engine/Renderer/include/Axle/RenderContracts.hpp`, `Engine/Renderer/src/RenderContracts.cpp`
- **Shader, Passes & Scene Pipeline:**
  - `Engine/Resources/Shaders/CubemapCapture.hlsl`
  - `Engine/Scene/CMakeLists.txt`
  - `Engine/Scene/include/Axle/RHISceneRenderer.hpp`, `Engine/Scene/src/RHISceneRenderer.cpp`, `Engine/Scene/src/RHISceneRendererPasses.cpp`
  - `Engine/Scene/src/Passes/ReflectionCapturePass.hpp`, `Engine/Scene/src/Passes/ReflectionCapturePass.cpp`, `Engine/Scene/src/Passes/PostProcessPass.cpp`
  - `Engine/Scene/src/SceneExtraction.cpp`, `Engine/Scene/src/SceneSerializer.cpp`
- **Automated Tests:**
  - `Tests/CMakeLists.txt`, `Tests/rendering/RenderUnitTestMain.cpp`
  - `Tests/rendering/Test_LightingServicesUnit.cpp`
  - `Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp`
  - `Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp`
  - `Tests/src/Test_EnvironmentRendering.cpp`
- **Documentation & Exchange:**
  - `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md`
  - `docs/reviews/M15_U5_B_REVIEW.md`
  - `docs/agents/CODEX_GEMINI_EXCHANGE.json`

---

## 3. Native Readback Evidence Metrics

All four native GPU configurations (`Debug-DX12`, `Debug-Vulkan`, `Release-DX12`, `Release-Vulkan`) passed every U5-B check with `0` RHI validation errors and produced matching cubemap strip PPM artifacts (`M15_U5B_faces_strip.ppm` under `build/agent-runs/M15-U5-B/{Debug,Release}/{dx12,vulkan}/`):

- `[U5-B +X] TR=(0.84,1.96,1.96) TL=(1.92,0,0) BR=(1.92,0,0)` (all 6 faces oriented cleanly without X/Y mirror flips)
- `[U5-B SSR Fallback] withProbe=(43,0,0) withoutProbe=(0,0,0)` (`ReflectionState::DynamicReady`)
- `[U5-B Shadows] unlit=(1.94751,1.94751,1.94751) shadowed=(0,0,0)` (`CaptureShadows=false` vs `CaptureShadows=true`)

---

## 4. Verification Matrix

| Gate / Configuration | Command | Exit Code | Result Summary |
| :--- | :--- | :--- | :--- |
| **Debug Build** | `cmake --build build --config Debug --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleEditor --parallel 8` | `0` | **PASS** |
| **Debug Focused CPU Unit** | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-reflection-capture-contract` | `0` | **PASS**: `M15-U5-08.1`..`U5-15` CPU contract & serializer round-trip passed |
| **Debug Full CPU Suite** | `build\Tests\Debug\AxleRenderUnitTests.exe` (repo root) | `0` | **PASS**: all CPU contract suites passed |
| **Debug Bake Suite** | `build\Tests\Debug\AxleEnvironmentBakeTests.exe` | `0` | **PASS** |
| **Debug Focused CTest** | `ctest --test-dir build -C Debug --output-on-failure -R "^(M15ReflectionCaptureContract\|EnvironmentBakeTests)$"` | `0` | **PASS**: `2/2` (`EnvironmentBakeTests`, `M15ReflectionCaptureContract`) |
| **Debug RHI Resource View** | `build\Tests\Debug\AxleRHIResourceViewTests.exe` | `0` | **PASS**: `RHIResourceView: PASS` |
| **Debug D3D12 Native GPU** | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe dx12 build\agent-runs\M15-U5-B\Debug\dx12` | `0` | **PASS**: `All U5-B native D3D12/Vulkan checks passed!`, `0` validation errors |
| **Debug Vulkan Native GPU** | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe vulkan build\agent-runs\M15-U5-B\Debug\vulkan` | `0` | **PASS**: `All U5-B native D3D12/Vulkan checks passed!`, `0` validation errors |
| **Debug Editor Startup (Vulkan / DX12)** | `AxleEditor.exe --verify-empty-startup --rhi=vulkan` & `--rhi=dx12` (in `build\Editor\Debug`) | `0` / `0` | **PASS**: `5 UI frames`, clean shutdown |
| **Release Build** | `cmake --build build --config Release --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleEditor --parallel 8` | `0` | **PASS** |
| **Release Focused CPU Unit** | `build\Tests\Release\AxleRenderUnitTests.exe --m15-reflection-capture-contract` | `0` | **PASS** |
| **Release Full CPU Suite** | `build\Tests\Release\AxleRenderUnitTests.exe` (repo root) | `0` | **PASS**: all CPU contract suites passed |
| **Release Focused CTest** | `ctest --test-dir build -C Release --output-on-failure -R "^(M15ReflectionCaptureContract\|EnvironmentBakeTests)$"` | `0` | **PASS**: `2/2` (`EnvironmentBakeTests`, `M15ReflectionCaptureContract`) |
| **Release RHI Resource View** | `build\Tests\Release\AxleRHIResourceViewTests.exe` | `0` | **PASS** |
| **Release D3D12 Native GPU** | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build\agent-runs\M15-U5-B\Release\dx12` | `0` | **PASS**: `All U5-B native D3D12/Vulkan checks passed!`, `0` validation errors |
| **Release Vulkan Native GPU** | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build\agent-runs\M15-U5-B\Release\vulkan` | `0` | **PASS**: `All U5-B native D3D12/Vulkan checks passed!`, `0` validation errors |
| **Release Editor Startup (Vulkan / DX12)** | `AxleEditor.exe --verify-empty-startup --rhi=vulkan` & `--rhi=dx12` (in `build\Editor\Release`) | `0` / `0` | **PASS**: `5 UI frames`, clean shutdown |
| **Architecture Gate** | `python scripts/check_architecture.py` | `0` | **PASS**: `Files scanned: 613; CMake files: 25`, `AXLE_ARCH_CHECK PASS` |
| **Exchange Schema Gate** | `python scripts/agent_exchange.py validate` | `0` | **PASS** |
| **Diff / Style Gate** | `git diff --check` | `0` | **PASS** |
| **Interactive Editor Capture** | — | — | **NOT RUN (deferred per user direction)** |

---

## 5. Open Gates

- **Deferred Editor Visual Captures (`M15-U5-08.1`, `08.2`, `10.1`–`10.4`, `11.2`, `13.1`, `13.2`, `14`):** Remain open (`[ ]` in `PLAN.md`) until the consolidated interactive Editor visual pass following automated U5 completion.
- **Codex Independent Review:** Pending review/acceptance of `U5-B`.

## Independent Codex review supersedes self-verification

Codex repaired whole-operation recursion isolation, automatic request invalidation/retry, valid clip-plane preservation, anonymous-key safety, retained-generation feedback suppression and main environment restoration. The mirror regression now captures the current world instead of depending on a stale cached scene. See [the independent findings, executed verification and acceptance limits](M15_U5_B_CODEX_REVIEW.md). Automated scope is accepted after corrections; interactive captures remain user-deferred. The original self-verification above is retained as reported historical evidence, including checks not independently reproduced in this environment.
