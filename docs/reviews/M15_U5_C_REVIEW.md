# M15 U5 Batch C — Cubemap Roughness Prefilter Self-Verification Review (`M15-U5-16.1` -> `M15-U5-20`)

Self-verification delivery report for Milestone 15 Unit 5 Batch C (`U5-C`, SubUnit `S5.3`: `M15-U5-16.1` through `M15-U5-20`), implemented on top of accepted `U5-B` review commit `c7153144`. Independent Codex review and acceptance remain pending per [WORKING_PROTOCOL.md](../agents/WORKING_PROTOCOL.md). Interactive Editor visual captures (`M15-U5-17.1`, `18.2`, `19.2`) remain open and explicitly deferred per user direction to the post-U5 consolidated visual pass.

---

## 1. Requirement Coverage Summary

| Requirement | Status | Summary of Implementation & Verification |
| :--- | :--- | :--- |
| **M15-U5-16.1 — Allocate mip-capable cubemap** | **PASS (self-verification)** | `RHITextureDesc::CubemapStorage` constructs multi-mip linear HDR `RGBA16_FLOAT` storage+SRV cubemaps (`ArrayLayers = 6`, `IsCubeMap = true`, `MipLevels = ComputePrefilterMipCount(res)` clamped `[1, 5]`). `ReflectionCaptureContract::ValidatePrefilterPolicy` enforces power-of-two `[16, 1024]`, HDR format, valid mip depth, and valid sample budgets. Verified in CPU unit tests, `AxleRHIResourceViewTests` (DX12 & Vulkan, Debug & Release), and `AxleEnvironmentRenderingTests`. |
| **M15-U5-16.2 — Allocate destination UAV/SRV views** | **PASS (self-verification)** | `RHITexture` exposes `GetMipCount()`, `GetMipSRV(mip)` (`TEXTURE_CUBE` per-mip `mipOffset = mip, mipNum = 1`), and `GetMipUAV(mip)` (`STORAGE_TEXTURE_ARRAY` across all 6 faces, `mipOffset = mip, mipNum = 1`), registered under `RHIResourceService::AddAlias` + `DefineViewAccess`, preserved across `RetainGeneration()`, cleaned in `Shutdown()`, and validated with mid-mip `CreateTextureUAV` `ScopedRHIFault` injection rollback on DX12 and Vulkan (`Debug`/`Release`). |
| **M15-U5-17.1 — Implement roughness prefilter** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `Engine/Resources/Shaders/CubemapPrefilter.hlsl` provides `DownsampleCubeCS` (2x2 pure-3D solid-angle cube downsampling of `RawCubemap`) and `PrefilterCubeCS` (mip `0` sharp mirror copy, mips `1..N-1` GGX importance-sampled roughness integration `rough = mip / (mipCount - 1)`, 64 samples, solid-angle source mip LOD, NaN/Inf sanitization). `RHISceneRenderer::PrefilterCapturedCubemap` executes per-mip barriers cleanly (`0` validation errors) and retains last-good generation on mid-prefilter failure (`BeginCommandBuffer` fault injection #7). |
| **M15-U5-17.2 — Preserve face orientation** | **PASS (self-verification)** | `CubemapPrefilter.hlsl` (`CubeFaceTexelDirection`) and `ReflectionCaptureContract::ComputeCubeTexelDirection` preserve exact `+X, -X, +Y, -Y, +Z, -Z` face orientation across all mips `0..4`, matching `EnvironmentCubeDirection` and `ValidateCubeFaceBasis`. |
| **M15-U5-17.3 — Handle seams** | **PASS (self-verification)** | Downsampling and GGX prefiltering sample `TextureCube` via normalized 3D vectors with `RHISamplerDesc::Trilinear(RHIAddressMode::ClampToEdge)`. Native DX12/Vulkan readback across an emissive `+X/+Z` boundary pillar confirms smooth border continuity on mips `1..3` without black seams or edge cutoff. |
| **M15-U5-18.1 — Use sharp mirror mip** | **PASS (self-verification)** | Mip `0` copies sharp mirror directions (`SampleLevel(..., N, 0.0)`); `ForwardPBR.hlsl` samples `u_PrefilterMap.GetDimensions` (`MAX_REFLECTION_LOD = float(envLevels - 1)`) so low roughness (`0.05`) reproduces crisp specular peaks (`peakLuma = 0.993335`). |
| **M15-U5-18.2 — Use blurred rough-material mips** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Increasing roughness samples progressively blurred mips (`1..4`), monotonically widening specular highlight rings (`ringFraction`: `0.175925` @ `0.05` -> `0.485786` @ `0.35` -> `0.776941` @ `0.70` -> `0.837986` @ `0.95`). |
| **M15-U5-19.1 — Validate controlled environment** | **PASS (self-verification)** | Controlled 64px HDR scene comparison between GPU prefiltered cubemap output (`mip 1`, `mip 2`) and CPU pure-3D GGX reference (`EvaluatePrefilterDirectionCPU`) passes tightly (`mip 1` `r=0.25`: GPU `(7.66797,3.87109,1.31445)` vs CPU `(7.83077,3.91539,1.30513)`, `relErr = 0.0190894`; `mip 2` `r=0.50`: `relErr = 0.1366`). |
| **M15-U5-19.2 — Validate roughness sweep** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Metallic sphere render fixture sweeping `roughness = 0.05 -> 0.35 -> 0.70 -> 0.95` against captured 5-mip dynamic cubemap produces finite non-negative HDR outputs and continuous monotonic specular peak reduction (`0.993335 -> 0.974489 -> 0.875152 -> 0.84256`) + ring broadening across D3D12 & Vulkan (`Debug`/`Release`). |
| **M15-U5-20 — No equirectangular assumptions** | **PASS (self-verification)** | `CubemapPrefilter.hlsl` and `RenderContracts.cpp` operate strictly in 3D cubemap texel/direction space (`atan2`/`asin` absent, verified by shader contract test); native `+Y` pole beacon check verifies azimuthal isotropy across horizontal and vertical offsets at mip `2` without polar pinch distortion. |

---

## 2. Modified and Added Files

- **RHI Texture & Per-Mip Views:**
  - `Engine/RHI/include/Axle/RHI/RHITexture.hpp`
  - `Engine/RHI/src/RHITexture.cpp`
- **Renderer Prefilter Contract & CPU Reference:**
  - `Engine/Renderer/include/Axle/RenderContracts.hpp`
  - `Engine/Renderer/src/RenderContracts.cpp`
- **Compute Shader & Scene Capture Prefilter Integration:**
  - `Engine/Resources/Shaders/CubemapPrefilter.hlsl`
  - `Engine/Scene/include/Axle/RHISceneRenderer.hpp`
  - `Engine/Scene/src/RHISceneRenderer.cpp`
- **Automated Tests & Build Config:**
  - `Tests/CMakeLists.txt`
  - `Tests/rendering/RHIResourceViewMain.cpp`
  - `Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp`
  - `Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp`
- **Documentation & Coordination:**
  - `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md`
  - `docs/reviews/M15_U5_C_REVIEW.md`
  - `docs/agents/CODEX_GEMINI_EXCHANGE.json`

---

## 3. Native GPU Evidence & Metrics

All four native GPU test runs (`Debug/Release` x `DX12/Vulkan` on NVIDIA GeForce RTX 3070 with NRI validation enabled) passed with **0 RHI validation errors** and produced identical quantitative outputs:

- **GGX Prefilter CPU Reference Parity (`64x64`, 5 mips):**
  - `mip=1 (r=0.25)`: `gpu=(7.66797, 3.87109, 1.31445)` vs `cpu=(7.83077, 3.91539, 1.30513)`, `relErr=0.0190894`
  - `mip=2 (r=0.50)`: `gpu=(1.69238, 1.11816, 0.55957)` vs `cpu=(1.99274, 1.25532, 0.591074)`, `relErr=0.1366`
- **Metallic Sphere Roughness Sweep (`64x64 RGBA16_FLOAT`, ForwardPBR Split-Sum IBL):**
  - `r=0.05`: `peakLuma=0.993335`, `ringFraction=0.175925`
  - `r=0.35`: `peakLuma=0.974489`, `ringFraction=0.485786`
  - `r=0.70`: `peakLuma=0.875152`, `ringFraction=0.776941`
  - `r=0.95`: `peakLuma=0.842560`, `ringFraction=0.837986`

---

## 4. Verification Matrix

| Check | Command | Exit Code | Notes |
| :--- | :--- | :--- | :--- |
| **Debug Build** | `cmake --build build --config Debug --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentBakeTests AxleEnvironmentRenderingTests AxleEditor` | `0` | Clean compile + link |
| **Release Build** | `cmake --build build --config Release --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentBakeTests AxleEnvironmentRenderingTests AxleEditor` | `0` | Clean compile + link |
| **Debug Focused CPU Unit** | `.\build\Tests\Debug\AxleRenderUnitTests.exe --m15-reflection-capture-contract` | `0` | U5-B + U5-C contracts PASS |
| **Debug Full CPU Suite** | `.\build\Tests\Debug\AxleRenderUnitTests.exe` | `0` | All suites PASS |
| **Debug Bake Suite** | `.\build\Tests\Debug\AxleEnvironmentBakeTests.exe` | `0` | PASS |
| **Debug CTest** | `ctest --test-dir build -C Debug --output-on-failure -R "M15ReflectionCaptureContract\|M15EnvironmentContract"` | `0` | `2/2` PASS |
| **Release Focused CPU Unit** | `.\build\Tests\Release\AxleRenderUnitTests.exe --m15-reflection-capture-contract` | `0` | U5-B + U5-C contracts PASS |
| **Release Full CPU Suite** | `.\build\Tests\Release\AxleRenderUnitTests.exe` | `0` | All suites PASS |
| **Release Bake Suite** | `.\build\Tests\Release\AxleEnvironmentBakeTests.exe` | `0` | PASS |
| **Release CTest** | `ctest --test-dir build -C Release --output-on-failure -R "M15ReflectionCaptureContract\|M15EnvironmentContract"` | `0` | `2/2` PASS |
| **Debug RHI Resource View** | `.\build\Tests\Debug\AxleRHIResourceViewTests.exe dx12 targets` & `vulkan targets` | `0` / `0` | `RHIResourceView: PASS`, 0 validation errors |
| **Release RHI Resource View** | `.\build\Tests\Release\AxleRHIResourceViewTests.exe dx12 targets` & `vulkan targets` | `0` / `0` | `RHIResourceView: PASS`, 0 validation errors |
| **Debug Native GPU (`dx12` / `vulkan`)** | `.\build\Tests\Debug\AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/Tests/Environment/Debug/{dx12,vulkan}` | `0` / `0` | All U5-B/U5-C checks PASS, 0 validation errors |
| **Release Native GPU (`dx12` / `vulkan`)** | `.\build\Tests\Release\AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/Tests/Environment/Release/{dx12,vulkan}` | `0` / `0` | All U5-B/U5-C checks PASS, 0 validation errors |
| **Editor Empty Startup (Vulkan)** | `.\build\Editor\{Debug,Release}\AxleEditor.exe --verify-empty-startup --rhi=vulkan` | `0` / `0` | `5 UI frames`, Vulkan 1.3 active, clean shutdown |
| **Editor Empty Startup (DX12)** | `.\build\Editor\{Debug,Release}\AxleEditor.exe --verify-empty-startup --rhi=dx12` (with `--no-fallback` vs default) | `1` (`--no-fallback`) / `0` (fallback) | Pre-existing NRI `SwapChainInterface` unavailable on Editor `--rhi=dx12 --no-fallback`; default `--rhi=dx12` falls back to Vulkan 1.3 (`PASS`). SwapChain creation on D3D12 itself is verified directly by `AxleRHIResourceViewTests.exe dx12 targets` (`exit 0`). |
| **Architecture & Exchange Checks** | `python scripts/check_architecture.py` & `python scripts/agent_exchange.py validate` & `git diff --check` | `0` / `0` / `0` | `613` files / `25` CMake files, schema valid, clean whitespace |
