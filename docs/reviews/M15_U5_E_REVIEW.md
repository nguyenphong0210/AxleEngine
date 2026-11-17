# M15 U5 Batch E — Multi-Probe Selection and Blend Self-Verification Review (`M15-U5-27.1` -> `M15-U5-31.5` + `P5 exit`)

Self-verification delivery report for Milestone 15 Unit 5 Batch E (`U5-E`, SubUnit `S5.5`: `M15-U5-27.1` through `M15-U5-31.5` + `P5 exit`), implemented on top of accepted `M15-U5-D` commit `b77f70d9`. Independent Codex review and acceptance remain pending per [WORKING_PROTOCOL.md](../agents/WORKING_PROTOCOL.md). Interactive Editor visual checks (`M15-U5-27.2`, `28`, `29`, `30.1`, `31.5`) remain open (`NOT RUN`) and deferred per user direction to the post-U5 consolidated visual pass.

---

## 1. Requirement Coverage Summary

| Requirement | Status | Summary of Implementation & Verification |
| :--- | :--- | :--- |
| **M15-U5-27.1 — Compute box influence** | **Automated PASS (self-verification)** | Implemented normalized OBB local-space box influence in `ReflectionService::EvaluateProbeInfluence` (`Engine/Renderer/src/ReflectionService.cpp`) and `EvaluateProbeInfluenceHLSL` (`Engine/Resources/Shaders/ReflectionProbeCommon.hlsli`) with `BlendDistance` propagation (`RenderWorld.hpp`, `Components.hpp`, `SceneExtraction.cpp`, `SceneSerializer.cpp`) and C1 `smoothstep` inner/outer transition (`Coverage`, `BlendWeight`). Verified on CPU unit/contract tests (`Test_LightingServicesUnit.cpp`, `Test_M15_ReflectionCaptureContractUnit.cpp`) including rotated OBB and NaN safety, and GPU readback (`Test_M15_ReflectionCaptureRendering.cpp`). |
| **M15-U5-27.2 — Compute sphere influence** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Implemented radial C1 `smoothstep` sphere influence (`Shape == 1`) identically in CPU (`ReflectionService.cpp`) and HLSL (`ReflectionProbeCommon.hlsli`). Verified on CPU unit tests and native D3D12/Vulkan GPU readback (`[U5-E SphereOverlap] left=(0.959961,0.172974) mid=(0.869473,0.87334) right=(0.172974,0.959961)`), finite and seam-free. Interactive Editor visual check deferred. |
| **M15-U5-28 — Blend multiple valid probes without hard seams** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Implemented deterministic 4-probe multi-probe selection (`MaxActiveReflectionProbes = 4`), `512B` `RHIReflectionProbeBufferData` (`b6` / `b1`) + 4-slot cubemap bindings (`t2..t5 space4` in forward passes, `t5..t8 space1` in `SSRApply`), reverse-priority LRU stamping in `PrepareReflectionProbe`, and normalized blend in `EvaluateMultiProbeBlendHLSL`. Verified on D3D12/Vulkan (`[U5-E BoxOverlap]` monotonic Red -> balanced 50/50 -> Green ramp, plus 5-probe `ActiveProbeCount == 4` cap). Interactive Editor visual check deferred. |
| **M15-U5-29 — Apply parallax correction per probe before blending** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Implemented per-probe box and sphere parallax correction in `ApplyProbeParallax` (`ReflectionService.cpp`) and `ApplyProbeParallaxHLSL` (`ReflectionProbeCommon.hlsli`), evaluated for each contributing probe before cubemap sampling and blending. Verified on CPU unit checks (box + sphere) and native D3D12/Vulkan GPU (`[U5-E Parallax] raw=(0.073623,0.978027) corrected=(0.950684,0.0644263)`). Interactive Editor visual check deferred. |
| **M15-U5-30.1 — Blend probe with SSR confidence** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Decoupled `SSR.hlsl` `CSMain` (raw screen-space hit + confidence `[0..1]`) from `SSRApply` (`PSMain`), which reconstructs the exact multi-probe + sky fallback via `EvaluateMultiProbeBlendHLSL` and blends SSR via `(ssrColor - fallbackEnv) * (ssrWeight * F)`. Verified on CPU unit checks (`EvaluateEnvironmentBlend`) and D3D12/Vulkan GPU (`[U5-E SSR] hitL_no=(0.959961,0.172974) hitL_on=(0.403046,0.466727)`). Interactive Editor visual check deferred. |
| **M15-U5-30.2 — Blend probe with sky fallback** | **Automated PASS (self-verification)** | Implemented multiplicative sky complement `skyComplement *= (1 - Coverage)` and `lerp(skyFallback, ProbeColor, ProbeCoverage)` across `ForwardPBR.hlsl`, `MaterialSurface.hlsl`, and `SSR.hlsl` while decoupling sky ambient gating (`envIntensity`) from local probe intensity (`probe.Intensity`). Verified on CPU and D3D12/Vulkan (`[U5-E BoundarySky] inside=(0.959961,0.172974) edge=(0.90459,0.908594) outside=(0.172974,0.959961)`). |
| **M15-U5-30.3 — Enforce one energy policy** | **Automated PASS (self-verification)** | Enforced convex energy conservation (`SSRWeight + ProbeWeight + SkyWeight == 1.0f`) on CPU (`EvaluateEnvironmentBlend`) and GPU (`SSRApply` replaces `fallbackEnv` proportionally via `(ssrColor - fallbackEnv) * (ssrWeight * F)` rather than adding SSR radiance on top of IBL). Verified with unit radiance scene (`[U5-E Energy] base=(0.905273,0.905273,0.905273) withSSR=(0.876614,0.876614,0.876614)` on D3D12 & Vulkan). |
| **M15-U5-31.1 — Test overlap** | **Automated PASS (self-verification)** | Verified overlapping probe weight normalization, monotonic continuity across sample sweeps, and `MaxActiveReflectionProbes = 4` selection cap on CPU and D3D12/Vulkan GPU (`[U5-E BoxOverlap]`, `[U5-E SphereOverlap]`). |
| **M15-U5-31.2 — Test boundary movement** | **Automated PASS (self-verification)** | Verified smooth boundary exit from local Red probe to Blue sky fallback without black seams or discontinuities on CPU (`Test_LightingServicesUnit.cpp`) and D3D12/Vulkan GPU (`[U5-E BoundarySky]`). |
| **M15-U5-31.3 — Test missing probe** | **Automated PASS (self-verification)** | Verified missing/unready probes (`PrefilterReady == false`) are excluded (`MissingProbeCount == 1`), `PrefilterInfo.y == 0` skips unready GPU slots, and remaining active probe renormalizes without darkening (`[U5-E MissingProbe] ref=(0.959961,0.172974) withMissing=(0.959961,0.172974)` on D3D12 & Vulkan). |
| **M15-U5-31.4 — Test stale probe** | **Automated PASS (self-verification)** | Verified edited manual probes retain their valid prior cubemap generation (`StaleProbeCount == 1`, `CaptureStale == true`, `[U5-E StaleManual] staleCenter=(0.978516,0)`) and continue blending cleanly until explicit `RequestDynamicReflectionCapture` updates to Green on D3D12 & Vulkan. |
| **M15-U5-31.5 — Test SSR miss** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Verified SSR misses (`confidence = 0`) leave pixels untouched and seamlessly fall back to local probes inside coverage (`[U5-E SSR] missProbe=(0.959961,0.172974,0.172974)`) and sky IBL outside coverage (`missSky=(0.172974,0.959961)`) on D3D12 & Vulkan. Interactive Editor visual check deferred. |
| **P5 exit** | **Automated PASS (self-verification)** | With SSR disabled (`SSRCompute` and `SSRApply` skipped, `EnableSkybox = false`), dynamic capture records emissive Red object (`[P5 Exit Cube -Z] center=(4,0.159912,0.159912)`), chrome sphere samples the captured cubemap (`Fallback == None`, `State == DynamicReady`), and linear HDR roughness sweep before tone mapping reduces reflected Red peak monotonically across roughness `0.05 -> 0.40 -> 0.85` (`3.29863 -> 1.09219 -> 0.138735`) on both D3D12 and Vulkan (`Debug` & `Release`). |

---

## 2. Modified & Added Files

- **Renderer Contracts, Multi-Probe Policy & Shared HLSL Inclusion:**
  - [RenderWorld.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/RenderWorld.hpp)
  - [ReflectionService.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/ReflectionService.hpp)
  - [ReflectionService.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/src/ReflectionService.cpp)
  - [MaterialSurfaceShader.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/src/MaterialSurfaceShader.cpp)
  - [Engine/Renderer/CMakeLists.txt](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/CMakeLists.txt)
- **Scene Component, Extraction, Serialization, Pipeline & Passes:**
  - [Components.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/Components.hpp)
  - [SceneExtraction.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/SceneExtraction.cpp)
  - [SceneSerializer.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/SceneSerializer.cpp)
  - [RHISceneRenderer.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/RHISceneRenderer.hpp)
  - [RHISceneRenderer.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/RHISceneRenderer.cpp)
  - [RHISceneRendererPasses.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/RHISceneRendererPasses.cpp)
  - [ScreenSpaceReflectionPass.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/Passes/ScreenSpaceReflectionPass.cpp)
  - [Engine/Scene/CMakeLists.txt](file:///d:/WorkSpace/AxleEngine/Engine/Scene/CMakeLists.txt)
- **Shaders:**
  - [ReflectionProbeCommon.hlsli](file:///d:/WorkSpace/AxleEngine/Engine/Resources/Shaders/ReflectionProbeCommon.hlsli) *(new)*
  - [ForwardPBR.hlsl](file:///d:/WorkSpace/AxleEngine/Engine/Resources/Shaders/ForwardPBR.hlsl)
  - [MaterialSurface.hlsl](file:///d:/WorkSpace/AxleEngine/Engine/Resources/Shaders/MaterialSurface.hlsl)
  - [SSR.hlsl](file:///d:/WorkSpace/AxleEngine/Engine/Resources/Shaders/SSR.hlsl)
- **Automated Tests:**
  - [Tests/CMakeLists.txt](file:///d:/WorkSpace/AxleEngine/Tests/CMakeLists.txt)
  - [Test_LightingServicesUnit.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_LightingServicesUnit.cpp)
  - [Test_M15_ReflectionCaptureContractUnit.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp)
  - [Test_M15_ReflectionCaptureRendering.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp)
  - [Test_M15_OcclusionRendering.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_M15_OcclusionRendering.cpp)
- **Documentation & Coordination:**
  - [PLAN.md](file:///d:/WorkSpace/AxleEngine/docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md)
  - [M15_U5_E_REVIEW.md](file:///d:/WorkSpace/AxleEngine/docs/reviews/M15_U5_E_REVIEW.md)
  - [CODEX_GEMINI_EXCHANGE.json](file:///d:/WorkSpace/AxleEngine/docs/agents/CODEX_GEMINI_EXCHANGE.json)

---

## 3. Verification Matrix (Debug & Release after Final Code Edit)

All builds and checks below were executed after the final code/test edit (`exit 0`, zero asserted RHI validation errors on NVIDIA GeForce RTX 3070):

| Check | Debug Result | Release Result | Notes |
| :--- | :--- | :--- | :--- |
| `cmake --build build --config {Debug,Release} --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor AxleMaterialGraph*` | PASS (`exit 0`) | PASS (`exit 0`) | Clean compilation of engine libs, shaders, test binaries, and editor |
| `AxleRenderUnitTests.exe --lighting-services` | PASS (`exit 0`) | PASS (`exit 0`) | Box/sphere influence, parallax, overlap, energy policy, missing/stale checks |
| `AxleRenderUnitTests.exe --m15-reflection-capture-contract` | PASS (`exit 0`) | PASS (`exit 0`) | `BlendDistance` extraction/YAML round-trip, `512B` GPU CBV, shader tokens |
| `AxleRenderUnitTests.exe` (full suite) | PASS (`exit 0`) | PASS (`exit 0`) | All CPU render unit suites pass |
| `ctest --test-dir build -C {Debug,Release} -R "M15ReflectionCaptureContract\|M15EnvironmentContract\|MaterialGraph" --output-on-failure` | 8/8 PASS (`exit 0`) | 8/8 PASS (`exit 0`) | Includes all 6 registered `MaterialGraph*` CPU/policy/runtime/resource tests + M15 contracts |
| `AxleEnvironmentBakeTests.exe` | PASS (`exit 0`) | PASS (`exit 0`) | Bake orientation & transactional validation |
| `AxleRHIResourceViewTests.exe dx12 {storage,targets}` | PASS (`exit 0`) | PASS (`exit 0`) | `RHIResourceView: PASS` on D3D12 storage and swapchain targets |
| `AxleRHIResourceViewTests.exe vulkan {storage,targets}` | PASS (`exit 0`) | PASS (`exit 0`) | `RHIResourceView: PASS` on Vulkan storage and swapchain targets |
| `AxleEnvironmentRenderingTests.exe dx12 <out>` | PASS (`exit 0`) | PASS (`exit 0`) | Full U3/U4/U5-A/B/C/D/E + device reset on D3D12 (`0` validation errors) |
| `AxleEnvironmentRenderingTests.exe vulkan <out>` | PASS (`exit 0`) | PASS (`exit 0`) | Full U3/U4/U5-A/B/C/D/E + device reset on Vulkan (`0` validation errors) |
| `AxleEditor.exe --verify-empty-startup --rhi=dx12 --no-fallback` | PASS (`exit 0`) | PASS (`exit 0`) | Clean D3D12 editor startup (`0 entities, 0 assets, 5 UI frames`, `fallback=none`) |
| `AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback` | PASS (`exit 0`) | PASS (`exit 0`) | Clean Vulkan editor startup (`0 entities, 0 assets, 5 UI frames`, `fallback=none`) |
| `python scripts/check_architecture.py` | PASS (`exit 0`) | — | `Files scanned: 613; CMake files: 25` (`AXLE_ARCH_CHECK PASS`) |

### Observed GPU Readback Highlights (Debug & Release, D3D12 & Vulkan)
- `[U5-E BoxOverlap]`: `left=(0.959961,0.172974) mid=(0.869629,0.873438) right=(0.172974,0.959961)`
- `[U5-E SphereOverlap]`: `left=(0.959961,0.172974) mid=(0.869473,0.87334) right=(0.172974,0.959961)`
- `[U5-E BoundarySky]`: `inside=(0.959961,0.172974) edge=(0.90459,0.908594) outside=(0.172974,0.959961)`
- `[U5-E Parallax]`: `raw=(0.073623,0.978027) corrected=(0.950684,0.0644263)`
- `[U5-E MissingProbe]`: `ref=(0.959961,0.172974) withMissing=(0.959961,0.172974)`
- `[U5-E StaleManual]`: `staleCenter=(0.978516,0) staleCount=1`
- `[U5-E Energy]`: `base=(0.905273,0.905273,0.905273) withSSR=(0.876614,0.876614,0.876614)`
- `[U5-E SSR]`: `hitL_no=(0.959961,0.172974) hitL_on=(0.403046,0.466727) missProbe=(0.959961,0.172974,0.172974) missSky=(0.172974,0.959961)`
- `[P5 Exit Cube -Z]`: `center=(4,0.159912,0.159912)`
- `[P5 Exit Roughness SSR=OFF]`: `R(0.05)=3.29863 R(0.40)=1.09219 R(0.85)=0.138735`

### Explicit Scope & Verification Limits
- Interactive `EDITOR VISUAL` items (`M15-U5-27.2`, `28`, `29`, `30.1`, `31.5`) remain `NOT RUN` and unchecked (`[ ]`) in [PLAN.md](file:///d:/WorkSpace/AxleEngine/docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md), deferred per user direction to the consolidated post-U5 visual pass.
