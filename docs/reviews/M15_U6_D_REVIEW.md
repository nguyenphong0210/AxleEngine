# Milestone 15 U6-D Independent Review

**Verdict:** PASS (accepted without P1/P2 defects)  
**Reviewer:** Gemini  
**Implementer:** Codex (`ad6c1ddfe8beae84ecfb34278ed8e9a1bcc0fc4a`, verification `d2335aca`)  
**Baseline:** `a56a78fa` (U6-C accepted)  
**Scope:** `M15-U6-04.1`, `M15-U6-04.2`, `M15-U6-04.3`, `M15-U6-07.3`, `M15-U6-07.4`

## 1. Requirement Completeness Matrix

| Requirement | Implementation & Caller Paths | Reviewer Independent Verification | Status |
| --- | --- | --- | --- |
| `M15-U6-04.1` Temporal reprojection | `VolumetricFog.hlsl` (`CSReproject`, 512B `VolumetricFogConstants`), `FroxelFogPass.hpp/.cpp`, `RHISceneRenderer.cpp` (`m_FogReproject*`), `RHISceneRendererPasses.cpp` (ping-pong `history0`/`history1`, resolved volume routed into `FogIntegrate` `t2` & `FogApply` `t3`), `SceneRenderSettings::FogTemporalWeight` | DXC SPIR-V + DXIL + `spirv-val` exit `0`; Debug & Release `AxleEnvironmentRenderingTests` D3D12/Vulkan verify slot 0 seeding, slot 1 reuse (`blended=126`, `world_reprojected=126`), linear HDR delta vs `FogTemporalWeight=0`, and `FogReprojectLayout` OOM static fallback | PASS |
| `M15-U6-04.2` Temporal rejection | `VolumetricFog.hlsl:482-526` (`u_TemporalParams.x <= 0`, `current.a <= 0`, `!any(current.rgb > 0)`, frustum clip/`w`, `prevDepth` vs `[Near, Max]`, half-texel bounds, `isfinite`, density tolerance, 6-neighbor AABB intersected with `[0.75 * current.rgb, 1.25 * current.rgb]`, `current.a` preserved) | Debug & Release D3D12/Vulkan verify `rejected_frustum=18`, out-of-range near-slice rejection, neighborhood & `[0.75, 1.25]` bounds, exact extinction `moved[i].a == raw[i].a`, and `extinguished_light_trail=0` | PASS |
| `M15-U6-04.3` Invalidation | `ViewContext.hpp/.cpp` (`FroxelHistoryFrame`, `SceneInstanceID`, `Observe`, `ObserveMaterialGeneration`, `RequestReset`, `EndFrameAndSubmit`, `AbandonFrame`, `HashViewFeaturePolicy`), `ViewRequest::CameraCut`, `EditorLayer`/`RuntimeLayer` (`m_LastViewCamera`), `RenderService`, `RHISceneRendererPasses.cpp` (`HashFroxelMedium`, pre-fog material observation, post-submit `CommitFroxelHistory`) | `AxleRenderUnitTests` (`ViewContext`) & Debug/Release `AxleEnvironmentRenderingTests` verify frame 0, `CameraCut`, `SceneRevision`, `SceneInstanceID`, authored medium & feature toggles, disable/re-enable, device epoch, material generation, and failed `EndCommandBuffer` rollback | PASS |
| `M15-U6-07.3` Camera motion | `VolumetricFog.hlsl` (`CSReproject`), `RHISceneRendererPasses.cpp`, `Tests/rendering/Test_M15_FroxelFogRendering.cpp` | Controlled camera translation matches world-space `seed[i + shift]` (`126/126` blended matching cells vs distinct from same-index `seed[i]`); yaw rotation after extinguishing point light produces `0` trail across slice | PASS |
| `M15-U6-07.4` Resize & tier changes | `RHIViewTargets.cpp`, `ViewContext.cpp`, `Tests/rendering/Test_M15_FroxelFogRendering.cpp` | In-place `RHIViewTargets::Configure` resize `256x144 -> 320x180` updates generation, resets `FrameIndex=0`, seeds new volume before alternating; `Low -> Medium` tier switch re-seeds cleanly in Debug & Release D3D12/Vulkan | PASS |

## 2. Architecture & Design Audit Observations (Non-Blocking)

No P1 or P2 functional, lifetime, validation, or parity issues were found across `a56a78fa..ad6c1ddf`:
1. **Binary history rejection vs soft clamping (`VolumetricFog.hlsl:519-522`):** Out-of-range history performs hard binary rejection back to `current.rgb` rather than clamping inside `[low, high]`. This strictly enforces bounded trails (`max_scatter_delta=0.0449219`, `0` unlit trail) and satisfies `M15-U6-04.2`; soft history clamping could be tuned orthogonally later if fast moving shadow edges show sub-froxel shimmer.
2. **Pass-order & legacy entrypoints (`RHISceneRendererPasses.cpp:1572`, `RHISceneRenderer.cpp:6249`):** Moving `ObserveMaterialGeneration` into the `FroxelFogPass` record callback cleanly seeds `HistoryWrite` on frame 0 when live materials change (since `HistoryRead` checks `viewCtx.GetFrameIndex() != 0`), relying on `FroxelFogPass` staying present in the schedule even when `useFog == false`. `RHISceneRenderer::RenderScene(...)` keeps default `cameraCut = false` while all Editor/Runtime frame submissions flow through `RenderService::RenderView` with `ViewRequest::CameraCut` and `m_LastViewCamera`.
3. **Scope boundaries:** `FogTemporalWeight` UI Inspector exposure and `M15-U6-05.*` / `07.5` / `07.6` remain cleanly deferred to U6-E. None of the U6-D leaves carry an `EDITOR VISUAL` gate.

## 3. Reviewer Independent Verification Record

All checks below were executed independently against checked-out commit `d2335aca`:

- **Builds (`cmake --build build --config Debug|Release --target AxleRenderUnitTests AxleRHIResourceViewTests AxleRHIStateTests AxleRenderServiceTests AxleEnvironmentRenderingTests AxleEditor AxleRuntime --parallel 8`):** Exit `0` for both Debug and Release.
- **Shader compilation & SPIR-V validation:**
  - `ThirdParty\dxc\bin\x64\dxc.exe -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -HV 2021 -T cs_6_0 -E CSReproject -D __spirv__=1 Engine/Resources/Shaders/VolumetricFog.hlsl` -> Exit `0`.
  - `spirv-val --target-env vulkan1.3` on compiled `CSReproject` SPIR-V -> Exit `0`.
  - `dxc.exe -HV 2021 -T cs_6_0 -E CSReproject` DXIL -> Exit `0`.
- **CPU & static suites (Debug & Release):**
  - `AxleRenderUnitTests.exe` (run from repo root) -> Exit `0` (Debug & Release).
  - `ctest --test-dir build -C <Config> --output-on-failure -R "^(M15FroxelFogContract|M15HiZContract|M15OcclusionContract|M15ReflectionCaptureContract|RenderHarnessFailure|ViewContext)$"` -> `5/5 PASS` (Debug & Release; note `ViewContext` tests run inside `AxleRenderUnitTests.exe`).
  - `python scripts/check_architecture.py` -> `Files scanned: 615; CMake files: 25; AXLE_ARCH_CHECK PASS`.
  - `python scripts/agent_exchange.py validate` -> `PASS`; `git diff --check a56a78fa..d2335aca` -> clean.
- **Native GPU & Editor checks (Debug & Release):**
  - `AxleEnvironmentRenderingTests.exe dx12|vulkan build/agent-runs/M15-U6-D/gemini/<Config>/<backend>` -> Exit `0` across all 4 `(Debug|Release) x (dx12|vulkan)` runs with zero froxel D3D12/Vulkan validation errors. All 4 readback files `M15_U6D_froxel_temporal.txt` report identically:
    `blended=126 world_reprojected=126 max_scatter_delta=0.0449219 rejected_frustum=18 extinguished_light_trail=0 resize_and_reset=PASS`.
  - `AxleRHIStateTests.exe` (`policy`, `dx12`, `vulkan`, Debug & Release) -> Exit `0` (6/6).
  - `AxleRHIResourceViewTests.exe` (`dx12|vulkan` `storage` & `targets`, Debug & Release) -> Exit `0` (8/8).
  - `AxleRenderServiceTests.exe` (`dx12`, `vulkan`, Debug & Release) -> Exit `0` (4/4).
  - `AxleEditor.exe --verify-empty-startup --rhi=vulkan|dx12 --no-fallback --rhi-validation` (Debug & Release) -> Exit `0` (4/4).
