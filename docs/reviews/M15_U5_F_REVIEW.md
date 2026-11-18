# M15-U5-F Delivery & Self-Verification Report (S5.6: Material-Aware SSR and Reflection Resolve)

> Historical Gemini self-verification. Current independent findings, repairs, reruns and acceptance limits are recorded in [the Codex U5-F review](M15_U5_F_CODEX_REVIEW.md). Original values and binary-refinement descriptions below describe the delivery before Codex repairs.

- **Milestone / Package:** `M15-U5-F` (`S5.6`, leaf items `M15-U5-32.1` through `M15-U5-39.6`)
- **Base Commit:** `c11e6edb` (accepted M15-U5-E baseline)
- **Implementer:** Gemini (self-verification completed across Debug & Release; pending independent Codex review)
- **Interactive Editor Visual Status:** **NOT RUN / DEFERRED** for `M15-U5-34.1`, `35.1`, `35.3`, `37.2`, `38.1`, and `39.4` (deferred per user direction to consolidated post-U5 visual verification pass).

---

## 1. Architecture & Implementation Summary

### 1.1 Frozen Ray/Depth Projection & Hierarchical Hi-Z DDA Traversal (`32.1`–`33.3`)
- Added `Axle::SSRRaymarchContract` in [`RenderContracts.hpp`](Engine/Renderer/include/Axle/RenderContracts.hpp) and [`RenderContracts.cpp`](Engine/Renderer/src/RenderContracts.cpp) freezing RH `[0, 1]` UVZ depth projection (`ProjectViewToUVZ`, `ReconstructViewPos`), slab clipping (`ClipScreenRay`), initial normal-biased ray construction (`BuildInitialSSRRay`), Hi-Z pyramid traversal (`TraceHiZDepthBuffer`), roughness cone radius sizing (`ComputeRoughnessConePixelRadius`), roughness taper (`EvaluateRoughnessFade`), and Fresnel F0 math (`ComputeSpecularF0`).
- Upgraded `CSMain` in [`SSR.hlsl`](Engine/Resources/Shaders/SSR.hlsl) to mirror `SSRRaymarchContract` with hierarchical Hi-Z DDA over `u_HiZ0`..`u_HiZ7`:
  - Empty/sky cells (`rayZMax < cellMinZ || cellMinZ >= 0.9999`) advance and ascend `mip = min(mip + 1u, maxMip)`.
  - Potential coarse hits clamp `tCurr` to cell depth plane and descend (`--mip`) down to mip 0.
  - Mip-0 cells test view-space penetration thickness (`withinBehind`), execute 4-step binary crossing refinement, attenuate confidence near foreground disocclusions using 4 orthogonal neighbor depth checks plus screen-border/distance/camera-facing fades, and reset rejected mip-0 steps at `mip = 0u` so rays past foreground occluders re-enter open-space coarse skipping without mip ping-ponging.
  - Increased SSR trace budget in [`ScreenSpaceReflectionPass.cpp`](Engine/Scene/src/Passes/ScreenSpaceReflectionPass.cpp) (`SSRParams.y = 64.0f`, clamped `16..96` in `SSR.hlsl`) and added `RHISSRConstants::SSRExtra` (`maxUsefulHiZMip`, `MaxConeRadiusPixels`) in [`RHISceneRenderer.hpp`](Engine/Scene/include/Axle/RHISceneRenderer.hpp) while preserving `sizeof(RHISSRConstants) == 512`.
  - Encodes `u_ComputeOutput` normalized in `[0, 1]` (`hitUV.xy`, `saturate(hitDist / maxDist)`, `confidence`) for compatibility with both `RGBA16_FLOAT` and `RGBA8_UNORM` `ScreenStorage` targets.

### 1.2 HDR Color Pyramid & Roughness Cone Resolve (`34.1`–`38.3`)
- Added `RHIViewTarget::SSRColor0`..`SSRColor3` (`GetSSRColor(uint32_t level)`) in [`RHIViewTargets.hpp`](Engine/RHI/include/Axle/RHI/RHIViewTargets.hpp) and [`RHIViewTargets.cpp`](Engine/RHI/src/RHIViewTargets.cpp) as a four-level half-resolution chain (`W>>1,H>>1`..`W>>4,H>>4`, `RGBA16_FLOAT` storage textures) allocated whenever `EnableReflectionTerms` is enabled.
- Added `DownsampleColorCS` in [`SSR.hlsl`](Engine/Resources/Shaders/SSR.hlsl) and wired `SSRColor0`..`SSRColor3` compute passes, descriptor sets (`m_SSRDownsampleTextureSets[4]`, `m_SSRDownsampleStorageSets[4]`), pipeline layout (`m_SSRDownsamplePipelineLayout`), and RenderGraph barriers into [`RHISceneRenderer.cpp`](Engine/Scene/src/RHISceneRenderer.cpp), [`RHISceneRendererPasses.cpp`](Engine/Scene/src/RHISceneRendererPasses.cpp), [`ScreenSpaceReflectionPass.hpp`](Engine/Scene/src/Passes/ScreenSpaceReflectionPass.hpp), and [`ScreenSpaceReflectionPass.cpp`](Engine/Scene/src/Passes/ScreenSpaceReflectionPass.cpp).
- Added `MakeMaterialReflectionSurfaceOutput(color, environment, response, roughness, reflectionEnabled)` in [`ReflectionProbeCommon.hlsli`](Engine/Resources/Shaders/ReflectionProbeCommon.hlsli) packing signed roughness (`>0` enabled, `<0` disabled) into `ReflectionEnvironment.a`, preserving `ReflectionResponse.a` for the U5-E companion-only regression contract.
- Updated [`ForwardPBR.hlsl`](Engine/Resources/Shaders/ForwardPBR.hlsl) and [`MaterialSurface.hlsl`](Engine/Resources/Shaders/MaterialSurface.hlsl) to emit signed roughness and reflection enablement cleanly, fixed `MaterialSurface.hlsl` to keep indirect `reflectionResponse` physically calibrated without `* 0.02` attenuation when `u_LightCounts.w == 0` with no probe, and initialized `OpaqueAndSky` MRT clear values in [`RHISceneRendererPasses.cpp`](Engine/Scene/src/RHISceneRendererPasses.cpp) (`{0,0,0,-1}` for `ReflectionEnvironment`, `{0,0,0,1}` for `ReflectionResponse`).
- Updated `PSMain` (`SSRApply`) in [`SSR.hlsl`](Engine/Resources/Shaders/SSR.hlsl) and `m_SSRApplyPipelineLayout` (`t0..t8`, `s0`) in [`RHISceneRenderer.cpp`](Engine/Scene/src/RHISceneRenderer.cpp) to reject disabled/zero-response materials (`signedRoughness <= 0.0`), project GGX roughness cones (`roughness^2 * 0.75 * hitDistMeters`) into fractional pyramid LODs across `u_SceneColor` + `u_SSRColor0..3`, integrate 9-tap depth-aware concentric samples, and replace the exported `envSample.rgb` contribution in linear HDR while preserving `RenderGraph` pass order checks in [`Test_EnvironmentRendering.cpp`](Tests/src/Test_EnvironmentRendering.cpp).

---

## 2. Modified & Added Files

- [`Engine/Renderer/include/Axle/RenderContracts.hpp`](Engine/Renderer/include/Axle/RenderContracts.hpp)
- [`Engine/Renderer/src/RenderContracts.cpp`](Engine/Renderer/src/RenderContracts.cpp)
- [`Engine/RHI/include/Axle/RHI/RHIViewTargets.hpp`](Engine/RHI/include/Axle/RHI/RHIViewTargets.hpp)
- [`Engine/RHI/src/RHIViewTargets.cpp`](Engine/RHI/src/RHIViewTargets.cpp)
- [`Engine/Resources/Shaders/ReflectionProbeCommon.hlsli`](Engine/Resources/Shaders/ReflectionProbeCommon.hlsli)
- [`Engine/Resources/Shaders/ForwardPBR.hlsl`](Engine/Resources/Shaders/ForwardPBR.hlsl)
- [`Engine/Resources/Shaders/MaterialSurface.hlsl`](Engine/Resources/Shaders/MaterialSurface.hlsl)
- [`Engine/Resources/Shaders/SSR.hlsl`](Engine/Resources/Shaders/SSR.hlsl)
- [`Engine/Scene/include/Axle/RHISceneRenderer.hpp`](Engine/Scene/include/Axle/RHISceneRenderer.hpp)
- [`Engine/Scene/src/RHISceneRenderer.cpp`](Engine/Scene/src/RHISceneRenderer.cpp)
- [`Engine/Scene/src/RHISceneRendererPasses.cpp`](Engine/Scene/src/RHISceneRendererPasses.cpp)
- [`Engine/Scene/src/Passes/ScreenSpaceReflectionPass.hpp`](Engine/Scene/src/Passes/ScreenSpaceReflectionPass.hpp)
- [`Engine/Scene/src/Passes/ScreenSpaceReflectionPass.cpp`](Engine/Scene/src/Passes/ScreenSpaceReflectionPass.cpp)
- [`Tests/rendering/Test_M15_HiZContractUnit.cpp`](Tests/rendering/Test_M15_HiZContractUnit.cpp)
- [`Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp`](Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp)
- [`Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp`](Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp)
- [`Tests/src/Test_EnvironmentRendering.cpp`](Tests/src/Test_EnvironmentRendering.cpp)
- [`docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md`](docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md)
- [`docs/reviews/M15_U5_F_REVIEW.md`](docs/reviews/M15_U5_F_REVIEW.md)
- [`docs/agents/CODEX_GEMINI_EXCHANGE.json`](docs/agents/CODEX_GEMINI_EXCHANGE.json)

---

## 3. Verification Matrix & Quantitative Results

All checks below ran cleanly after the final code/shader/test edits with exit code `0`:

| Check / Target | Debug Status | Release Status | Notes |
| --- | --- | --- | --- |
| Build targets (`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`, `AxleEnvironmentBakeTests`, `AxleRHIResourceViewTests`, `AxleEditor`, 6 `AxleMaterialGraph*` targets) | PASS (exit 0) | PASS (exit 0) | Clean zero-error builds |
| `AxleRenderUnitTests.exe` (`--lighting-services`, `--m15-hiz-contract`, `--m15-reflection-capture-contract`, full suite) | PASS (exit 0) | PASS (exit 0) | Includes new S5.6 CPU & RenderGraph checks |
| `ctest --test-dir build -C <Config> -R "M15HiZContract\|M15ReflectionCaptureContract\|M15EnvironmentContract\|MaterialGraph" --output-on-failure` | PASS (`9/9`, exit 0) | PASS (`9/9`, exit 0) | 100% passed, 0 failed |
| `AxleEnvironmentBakeTests.exe` | PASS (exit 0) | PASS (exit 0) | Env/GGX/DFG/sky CPU checks |
| `AxleRHIResourceViewTests.exe {dx12,vulkan} {storage,targets}` | PASS (`4/4`, exit 0) | PASS (`4/4`, exit 0) | `RHIResourceView: PASS` on both backends |
| `AxleEnvironmentRenderingTests.exe dx12` | PASS (exit 0) | PASS (exit 0) | 0 RHI validation errors; U4 + U5-A..U5-F |
| `AxleEnvironmentRenderingTests.exe vulkan` | PASS (exit 0) | PASS (exit 0) | 0 RHI validation errors; U4 + U5-A..U5-F |
| `AxleEditor.exe --verify-empty-startup --rhi={dx12,vulkan} --no-fallback` | PASS (`2/2`, exit 0) | PASS (`2/2`, exit 0) | 0 entities, 0 assets, 5 UI frames |
| `AxleEditor.exe --verify-startup --project Tests/Fixtures/M15/U4E/U4E.axleproj --rhi={dx12,vulkan} --no-fallback` | PASS (`2/2`, exit 0) | PASS (`2/2`, exit 0) | 3 entities loaded cleanly, 5 UI frames |
| `python scripts/check_architecture.py` | PASS (exit 0) | PASS (exit 0) | `AXLE_ARCH_CHECK PASS` (613 files, 25 CMake files) |

### Key Native D3D12 & Vulkan GPU Measurements (`RunM15MaterialSSRTests`)
Observed identically on both Direct3D 12 and Vulkan 1.3 (Debug and Release) in linear HDR before tone mapping:
- **Hit (`32.7 / 39.1`):** `storage=(0.501855, 0.406592, 0.0703271, conf=0.810352)`, `hdr=(4.05078, 0.161987, 0.161987)`
- **Thickness Pass-Through (`33.1`):** `st=(0.501855, 0.406592, 0.0703271, conf=0.484912)`, `hdr=(2.42344, 0.096936, 0.096936)`
- **Miss to Local Probe / Sky Fallback (`38.2 / 38.3 / 39.2`):**
  - Probe fallback: `conf=0`, `hdr=(0.0312439, 0.0312439, 1.99961)`
  - Sky fallback: `hdr=(0.0390503, 1.79648, 0.0390503)` (DX12) / `(0.0390564, 1.79668, 0.0390564)` (Vulkan)
- **Roughness Cone Pyramid Sweep (`34.1 / 34.2 / 39.4`):** `G(0.05)=11.3359 -> G(0.30)=8.93125 -> G(0.65)=2.65836` with populated `SSRColor0..3` pyramid
- **Gold / Chrome / Blue Dielectric (`35.2 / 36.1 / 36.2 / 39.5`):**
  - Chrome: `(4.86016, 4.86016, 4.86016)` (achromatic conductor)
  - Gold: `(4.86016, 3.73008, 2.20039)` (metallic tinted conductor matching grazing Schlick Fresnel)
  - Blue Dielectric: `(1.28965, 1.28965, 1.28965)` (achromatic dielectric `F0=0.04` specular without blue albedo tint)
- **Reflection Flag Disabled (`35.3`):** `conf=0.810352` retained in `ScreenStorage`, `hdr=(0, 0, 0)` cleanly suppressed in resolve
- **Moving Object & Camera Motion (`39.3 / 39.6`):** `f0=(4.83656, 0) -> f1=(0, 4.86016)` with zero stale ghosting across frames and zero validation errors across camera motion frames.

---

## 4. Open Boundaries & Deferred Scope

- **Interactive Visual Inspection (`EDITOR VISUAL`):** Items `M15-U5-34.1`, `35.1`, `35.3`, `37.2`, `38.1`, and `39.4` remain unchecked `[ ]` in `PLAN.md` because live interactive Editor captures are deferred to the consolidated post-U5 visual verification pass.
- **Unmodified Deferred Work:** GTAO half-resolution halo investigation remains deferred until post-U5 visual fixes per user confirmation. Existing unrelated warnings in engine logs (e.g. Vulkan unused vertex attributes / GTAO typed-storage warnings / LNK4098) are unchanged and outside S5.6 scope.
- **Independent Acceptance:** Awaiting independent review by Codex.
