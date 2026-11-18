# M15-U5-G Delivery & Self-Verification Report (S5.7: LTC Rectangular and Disk Area Lights)

> Implemented and self-verified by Gemini on base `2c7e5cab`; pending independent Codex review and consolidated post-U5 interactive Editor visual pass.

- **Milestone / Package:** `M15-U5-G` / `PLAN.md` batch row `U5-H` (`S5.7`: `M15-U5-40` through `M15-U5-45.3`)
- **Naming Reconciliation:** `U5-F` (`21a3e25e` + Codex repair/acceptance `2c7e5cab`, `docs/reviews/M15_U5_F_CODEX_REVIEW.md`) completed and accepted all `S5.6` leaf items (`32.1..39.6`, covering both table rows `U5-F` and `U5-G`). This checkpoint implements the next unimplemented U5 unit: `S5.7` (`40..45.3`).
- **Base Commit:** `2c7e5cab` (note: `5c0ff0f4` added feature-completeness review rules in `docs/agents/WORKING_PROTOCOL.md`)
- **Interactive Editor Visual Status:** **NOT RUN / DEFERRED** for `M15-U5-43`, `M15-U5-44.4`, and `M15-U5-45.1` (deferred per user direction to the consolidated post-U5 visual verification pass).

---

## 1. Architecture & Design Summary

1. **Validated Reproducible LTC Tables & Shared CPU/GPU Evaluators (`M15-U5-40`):**
   - Added [`AreaLightLTCContract`](Engine/Renderer/include/Axle/RenderContracts.hpp) ([`RenderContracts.cpp`](Engine/Renderer/src/RenderContracts.cpp)) and matching HLSL routines in [`PBRCommon.hlsli`](Engine/Resources/Shaders/PBRCommon.hlsli).
   - Evaluates a validated 8x8 `(roughness, sqrt(1 - NdotV))` grid (`EvaluateLTC1GridPoint`, `EvaluateLTC2GridPoint`) with bilinear interpolation (`SampleLTC1`, `SampleLTC2`): `LTC1` stores the projective inverse transform `M^-1 = (m00, m02, m20, m22)` matching GGX reflection-lobe half-widths and peak zenith tilt (converging identically to `diag(1, 1)` at `roughness = 1.0`), and `LTC2` stores the split-sum GGX directional albedo (`ggxNorm`, `fresnelTerm`, `sphereFormFactor`).
   - Placing the table evaluators in [`PBRCommon.hlsli`](Engine/Resources/Shaders/PBRCommon.hlsli) adds zero new texture/descriptor slots in `space2`/`space4`, preserving existing `MaterialGraphRuntime`/`MaterialGraphResources` descriptor layouts.

2. **Two-Stage Horizon-Clipped Rectangle & Disk Emitters (`M15-U5-41.1..42.2`):**
   - Clips emitter vertices in shading tangent space `(T1, T2, N)` against physical horizon `L.z >= 0`, applies `M^-1`, clips again against clamped-cosine support `w.z >= 0` (`LTC_ClipPolygonZ0` / `ClipPolygonToHorizon`), projects onto the unit sphere, and sums `LTC_IntegrateEdgeVec` (already normalized by `1 / (2 * PI)`).
   - **Rectangle:** 4 corners ordered around `(lightRight, lightUp)`.
   - **Disk / Elliptical Disk:** Area-preserving regular octagon (`kDiskOctagonRadiusScale = 1.05392891`, preserving exact ellipse area `pi * a * b`) with ellipsoid closest-point range attenuation.
   - **Sidedness, Units & Range:** Back-facing one-sided emitters (`dirDot <= 0`) return `0`; two-sided emitters (`SignedIntensity < 0`) evaluate both hemispheres symmetrically; collinear/non-unit `Direction`/`Up` vectors are safely orthonormalized; finite `Range` applies smooth windowing over closest emitter distance without duplicating inverse-square decay.
   - Both [`ForwardPBR.hlsl`](Engine/Resources/Shaders/ForwardPBR.hlsl) (`ApplyClearcoat` + subsurface preserved) and [`MaterialSurface.hlsl`](Engine/Resources/Shaders/MaterialSurface.hlsl) invoke `EvaluateLTCAreaLightTerms`.

3. **Area-Light Policy, Data Flow, Serialization & Inspector (`M15-U5-43..44.4`):**
   - Added `AreaLightShape` (`Rectangle = 0`, `Disk = 1`) and `AreaLightShadowPolicy::UnshadowedAnalyticLTC` in [`LocalShadowSettings.hpp`](Engine/Renderer/include/Axle/LocalShadowSettings.hpp).
   - Propagated through [`RectLightComponent`](Engine/Scene/include/Axle/Components.hpp), [`RenderRectLight`](Engine/Renderer/include/Axle/RenderWorld.hpp), [`LightingRectFrame`](Engine/Renderer/include/Axle/LightingService.hpp), [`LightingService.cpp`](Engine/Renderer/src/LightingService.cpp), [`SceneExtraction.cpp`](Engine/Scene/src/SceneExtraction.cpp), [`RHISceneRenderer.cpp`](Engine/Scene/src/RHISceneRenderer.cpp) (`ComputeLightingStamp`), [`RHISceneRendererPasses.cpp`](Engine/Scene/src/RHISceneRendererPasses.cpp), and [`RHILight.hpp`](Engine/RHI/include/Axle/RHI/RHILight.hpp) / [`RHILight.cpp`](Engine/RHI/src/RHILight.cpp) (`Shape = Disk` encoded in negative `DirectionAndHeight.w`, keeping `sizeof(RHIGPURectLight) == 64` and `sizeof(RHIMultiLightBufferData) == 2304`).
   - Added `Shape` YAML persistence (`Shape: Rectangle` / `Disk`, omitted fields default to `Rectangle`) in [`SceneSerializer.cpp`](Engine/Scene/src/SceneSerializer.cpp) and complete `[RectLight]` assembly round-trips in [`AssemblySerializer.hpp`](Engine/Scene/include/Axle/AssemblySerializer.hpp) / [`AssemblySerializer.cpp`](Engine/Scene/src/AssemblySerializer.cpp).
   - Added `Area Light (LTC)` Inspector controls (`Enabled`, `Shape` combo, `Light Color`, `Intensity`, `Width`, `Height`, `Range`, `Two-Sided`, read-only shadow policy label) in [`EntityInspector.cpp`](Engine/UI/src/EntityInspector.cpp), light creation entries in [`SceneHierarchyPanel.cpp`](Engine/UI/src/SceneHierarchyPanel.cpp), and icon support in [`UIVectorIcons.hpp`](Engine/UI/include/Axle/UIVectorIcons.hpp).
   - No GTAO/Occlusion shaders (`GTAO.hlsl`, `GTAODenoise.hlsl`, `Occlusion.hlsli`) were modified.

---

## 2. Feature Completeness Audit (`S5.7`: `M15-U5-40` through `M15-U5-45.3`)

| Requirement | Implementation / Caller Paths | Verified Evidence | Status |
|---|---|---|---|
| `M15-U5-40` | `AreaLightLTCContract` (`RenderContracts.hpp`/`.cpp`), `PBRCommon.hlsli` | `ValidateLTCTables`, identity convergence (`roughness=1.0`), DX12/Vulkan CPU-GPU parity | Automated PASS |
| `M15-U5-41.1` | `EvaluateLTCAreaLightCPU`, `EvaluateLTCAreaLightTerms` | Horizon-straddling quad vs 32x32 quadrature, native DX12/Vulkan `gpuRect=0.0466199` vs `cpuRect=0.0472238` | Automated PASS |
| `M15-U5-41.2` | Octagon disk contour (`RenderContracts.cpp`, `PBRCommon.hlsli`) | `pi/4` ratio vs square, tangent-rotation invariance, native `gpuDisk=0.0371838` vs `cpuDisk=0.0376753` | Automated PASS |
| `M15-U5-41.3` | `SceneExtraction.cpp`, `LightingService.cpp`, `RHILight.cpp`, `PBRCommon.hlsli` | Collinear/non-unit basis & NaN/negative dimension tests in `Test_LightingServicesUnit.cpp` & ContractUnit | Automated PASS |
| `M15-U5-41.4` | `EvaluateLTCAreaLightCPU`, `EvaluateLTCAreaLightTerms` | One-sided back-face zeroing, `TwoSided` symmetry (`twoC.r` in `(0.03, 0.08)`), range cutoff `=0` on CPU/DX12/Vulkan | Automated PASS |
| `M15-U5-42.1` | `ForwardPBR.hlsl`, `MaterialSurface.hlsl`, `PBRCommon.hlsli` | Built-in vs MaterialSurface parity `0.0509644 == 0.0509644` on DX12/Vulkan (Debug & Release) | Automated PASS |
| `M15-U5-42.2` | `ForwardPBR.hlsl`, `MaterialSurface.hlsl`, `PBRCommon.hlsli` | Zero diffuse on `metallic=1`, tinted gold F0, metallic sweep on DX12/Vulkan | Automated PASS |
| `M15-U5-43` | `AreaLightShadowPolicy::UnshadowedAnalyticLTC`, `LightingService.cpp`, `EntityInspector.cpp` | Automated PASS (0 local atlas tiles claimed); interactive Inspector check deferred | **Automated PASS; EDITOR VISUAL: NOT RUN (deferred)** |
| `M15-U5-44.1` | `LocalShadowSettings.hpp`, `Components.hpp` | `RectLightComponent` defaults, constructors, `RuntimeComponentCodec` | Automated PASS |
| `M15-U5-44.2` | `SceneExtraction.cpp`, `RenderWorld.hpp`, `LightingService.cpp`, `RHISceneRenderer.cpp`, `RHISceneRendererPasses.cpp`, `RHILight.cpp` | `Test_RenderWorldExtractionUnit.cpp`, `Test_LightingServicesUnit.cpp`, 64B/2304B layout static asserts | Automated PASS |
| `M15-U5-44.3` | `SceneSerializer.cpp`, `AssemblySerializer.hpp`/`.cpp` | Scene YAML (`Shape: Disk` + legacy default `Rectangle`) and `.axle` `[RectLight]` round-trips | Automated PASS |
| `M15-U5-44.4` | `EntityInspector.cpp`, `SceneHierarchyPanel.cpp`, `UIVectorIcons.hpp` | Strict empty + U4E `AxleEditor.exe` startup PASS (DX12 & Vulkan, Debug & Release); interactive UI check deferred | **Automated PASS; EDITOR VISUAL: NOT RUN (deferred)** |
| `M15-U5-45.1` | `Test_M15_ReflectionCaptureContractUnit.cpp`, `RunM15AreaLightLTCTests` | CPU roughness/Fresnel + DX12/Vulkan peak `1.80074 -> 0.813867 -> 0.0759985`, wing `0.000547 -> 0.00695 -> 0.02808`; visual capture deferred | **Automated PASS; EDITOR VISUAL: NOT RUN (deferred)** |
| `M15-U5-45.2` | `Test_M15_ReflectionCaptureContractUnit.cpp`, `RunM15AreaLightLTCTests` | CPU tilt (`0 deg: 0.199934 -> 45 deg: 0.171469`) and DX12/Vulkan grazing tilt reduction | Automated PASS |
| `M15-U5-45.3` | `EvaluateAreaLightNumericalReferenceCPU` (`RenderContracts.cpp`) | 32x32 quadrature vs LTC agreement (`0.199934` vs `0.200018`; `< 0.015` across facing, tilted, and straddling) | Automated PASS |

---

## 3. Verification Matrix & Evidence

- **Builds (`Debug` & `Release`, exit `0`):**
  `cmake --build build --config <Config> --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor AxleMaterialGraphTests AxleMaterialGraphCompilerTests AxleMaterialGraphOpenGLPolicyTests AxleMaterialGraphSlangTests AxleMaterialGraphRuntimeTests AxleMaterialGraphResourceTests --parallel 8`
- **32-Command Verification Matrix (`32/32` exit `0`, summary at `build/Gemini-U5G-results.json`, individual logs `build/Gemini-U5G-*.log`):**
  - CPU suites (`Debug` & `Release`): `--m15-hiz-contract`, `--m15-reflection-capture-contract`, `--lighting-services`, full `AxleRenderUnitTests.exe`, `AxleEnvironmentBakeTests.exe`.
  - CTest (`Debug` & `Release`): `ctest --test-dir build -C <Config> --output-on-failure -R "M15HiZContract|M15ReflectionCaptureContract|M15EnvironmentContract|MaterialGraph"` (`9/9` PASS each).
  - Native RHI (`Debug` & `Release`, `dx12` & `vulkan`): `AxleRHIResourceViewTests.exe` `storage` & `targets` PASS.
  - Native Rendering (`Debug` & `Release`, `dx12` & `vulkan`): `AxleEnvironmentRenderingTests.exe` (`build/Tests/EnvironmentRendering/Gemini-U5G-<Config>-<Backend>`) PASS with `0` asserted RHI validation errors.
  - Strict Editor Startup (`Debug` & `Release`, `dx12` & `vulkan`): `AxleEditor.exe --verify-empty-startup --rhi=<B> --no-fallback` and `--verify-startup --project Tests/Fixtures/M15/U4E/U4E.axleproj --rhi=<B> --no-fallback` PASS.
- **Repository Hygiene:**
  - `python scripts/check_architecture.py`: `Files scanned: 613; CMake files: 25; AXLE_ARCH_CHECK PASS` (exit `0`).
  - `git diff --check`: exit `0`.
  - `python scripts/agent_exchange.py validate`: PASS after exchange update.
