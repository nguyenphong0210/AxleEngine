# M15-U10 Pre-Refactor Baseline Verification & Parity Manifest (`U10-D1`)

## 1. Frozen Source & Binary Identity (`M15-U10-08.1..08.3`, `16.2`)

- **Frozen Engine / Editor / Runtime Source Commit:** `4abbc663` (zero changes to `Engine/`, `Editor/`, `Runtime/`, or `Engine/Resources/Shaders/` after `4abbc663`).
- **Test-Only Follow-up Commit:** `b10da533` (`Tests/CMakeLists.txt`, `Tests/src/MaterialGraphTestDraw.hpp`, `Tests/rendering/Test_M15_SurfaceAndMaterialContractUnit.cpp` — added missing Froxel 3D dummy `StorageTexture3D` in MaterialGraph GPU test helper and fixed `RenderUnitTests` CTest working directory + 120s timeout; Engine/Editor/Runtime binaries unchanged).
- **Hardware / OS:** Windows 11 (`Windows-11-10.0.26200-SP0`), `NVIDIA GeForce RTX 3070` (`8232 MB` VRAM, discrete, `D3D12` + `Vulkan 1.3` active, `Software: No`).
- **Machine Manifest:** `build/agent-runs/u10d1/binary_manifest.json` (`150` binaries at `b10da533`):
  - `build/Editor/Debug/AxleEditor.exe`: `37,356,032` bytes, SHA256 prefix `77096447b0b787ef`
  - `build/Editor/Release/AxleEditor.exe`: `14,220,800` bytes, SHA256 prefix `7ccbb6f5ee243cd6`
  - `build/Runtime/Debug/AxleRuntime.exe`: `30,787,072` bytes, SHA256 prefix `86e3cf12bdf0d245`
  - `build/Runtime/Release/AxleRuntime.exe`: `7,823,872` bytes, SHA256 prefix `16fe3e005a69d7e9`
  - `build/Tests/Debug/AxleEnvironmentRenderingTests.exe`: `32,593,408` bytes, SHA256 prefix `feb1f761dfe5f09f`
  - `build/Tests/Release/AxleEnvironmentRenderingTests.exe`: `8,553,984` bytes, SHA256 prefix `cd572c18e679b2df`
  - `build/Tests/Debug/AxleRenderUnitTests.exe`: `32,487,424` bytes, SHA256 prefix `cdb3881337cacb71`
  - `build/Tests/Release/AxleRenderUnitTests.exe`: `8,606,720` bytes, SHA256 prefix `929313a3887864fc`

## 2. Automated Matrix Summary (`M15-U10-09.1..10.2`)

- **Architecture & Policy:** `python scripts/check_architecture.py` -> **PASS** (`629` source files, `25` CMake files).
- **CTest Full Default Suite (`b10da533`):**
  - `ctest --test-dir build -C Debug --output-on-failure`: **107/107 PASSED** (`229.86s`, log: `build/agent-runs/u10d1/logs/ctest_Debug_final.log`).
  - `ctest --test-dir build -C Release --output-on-failure`: **107/107 PASSED** (`191.21s`, log: `build/agent-runs/u10d1/logs/ctest_Release_final.log`).
- **Standalone Multi-Backend & GPU Matrix (Debug & Release, `build/agent-runs/u10d1/run_d1_gpu_matrix.py` + `b10da533` recheck):**
  - **D3D12 Native (`09.1`) — PASS:** `LocalShadowGPU_dx12`, `CascadedShadowGPU_dx12`, `RenderGraphGPU_dx12`, `EnvironmentGPU_dx12` (`U0..U9`), `RenderService_dx12`, `PreviewRender_dx12_{policy,default,surfaces}`, `RuntimeRender_dx12_{m15parity,loose,cooked}`, `RuntimeDirect_dx12` (`AxleRuntime.exe --rhi=dx12 --no-fallback --rhi-validation --frames=10`), `MGCompiler_dx12`, `MGRuntime_dx12` (`b10da533`), `MGResource_dx12` (`b10da533`), `RHIFoundation_dx12`, `RHI{Error,Identity,Resource,Retirement,State,Frame}_dx12`, `RHIResourceView_dx12_{default,targets,storage,leases,epoch,errors}` (all Debug & Release exit `0`, zero validation errors).
  - **Vulkan 1.3 Native (`09.2`) — PASS:** Identical matrix on `--rhi=vulkan --no-fallback --rhi-validation` (`LocalShadowGPU_vulkan`, `CascadedShadowGPU_vulkan`, `RenderGraphGPU_vulkan`, `EnvironmentGPU_vulkan`, `RenderService_vulkan`, `PreviewRender_vulkan_{policy,default,surfaces}`, `RuntimeRender_vulkan_{m15parity,loose,cooked}`, `RuntimeDirect_vulkan`, `MGCompiler_vulkan`, `MGRuntime_vulkan` at `b10da533`, `MGResource_vulkan` at `b10da533`, `RHIFoundation_vulkan`, `RHIFoundation_vulkan-window`, `RHI{Error,Identity,Resource,Retirement,State,Frame}_vulkan`, `RHIResourceView_vulkan_{default,targets,storage,leases,epoch,errors}` — all Debug & Release exit `0`).
  - **OpenGL Compatibility (`10.1`) — PASS:** `EnvironmentGPU_gl`, `RenderService_gl`, `PreviewRender_gl_policy`, `RuntimeRender_gl_{m15parity,loose}`, `RHIFoundation_gl`, `MaterialGraphOpenGLPolicyTests` (all Debug & Release exit `0`).
  - **Unsupported Capability & Recovery (`10.2`) — PASS:** Verified in `AxleRenderUnitTests` (`Test_RenderBackendPolicyUnit`, `Test_U10_C_CapabilityFallbackAndPresetDiagnostics`), `AxleRenderServiceTests` (GL Ultra/custom-shader fallback preserving authored settings, invalid external `RHIViewTargets` rejection, OOM target rollback keeping last valid generation), and `MGSlang_selection` / `AxleRHIErrorTests`.
  - **Optional Out-of-Default Failures Logged Transparently (Not Promoted to PASS):**
    - `MGSlang_{policy,dx12,vulkan}` (`AXLE_ENABLE_SLANG=OFF`): unavailable in this build configuration (`MG_COMPILER_UNAVAILABLE`); remains **NOT RUN / UNAVAILABLE**.
    - `VFXGPU_{dx12,vulkan}` (`AXLE_ENABLE_VFX_GPU_TESTS=OFF`, Debug & Release): fails pre-existing particle growth OOM assertion (`Test_VFXRendering.cpp:597`: `View pass failed: ParticlesAndResolve: recording failed`), tracked outside M15 lighting/materials.

## 3. `EDITOR VISUAL` Inspections (`M15-U10-11.1..11.3`)

All Editor visual runs launched with `--no-fallback --rhi-validation` on **Debug DX12** and **Release Vulkan** (`okViews > 0`, `failedViews = 0`, Console `Error 0`, `nonFinite = 0` under `build/agent-runs/u10d1/visual_out/{dx12,vk}/`).

1. **`M15-U10-11.1` Smooth Sphere (`Tests/Fixtures/M15/U4D/U4D.axleproj` — `UnoccludedSphere.axlescene`):**
   - Toggled GTAO + Contact Shadows **ON** vs **OFF** (`u4d_ao_on.png`, `u4d_ao_on_editor.png`, `u4d_ao_off.png`).
   - Visual observation: smooth continuous gradient reflection across sphere with zero triangle faceting, zero edge blackening, and finite continuous shading across Debug DX12 and Release Vulkan (`PSNR = 83.45 dB`, `SSIM = 1.00000`, `maxChErr = 1.00/255`).
2. **`M15-U10-11.2` Roughness Sweep (`Tests/Fixtures/M15/M15_RoughnessSweep.axleproj`):**
   - Five metallic gold spheres (`roughness = 0.04, 0.12, 0.25, 0.50, 0.80`) captured under **SSR ON** (`roughness_ssr_on.png`), **SSR OFF / IBL ON** (`roughness_ssr_off.png`), and **IBL OFF** (`roughness_ibl_off.png`).
   - Visual observation: monotonic specular lobe softening from crisp mirror highlight (`0.04`) to broad diffuse metallic response (`0.80`); `ssr_on` adds grounded floor/horizon SSR tint, `ibl_off` isolates direct sun contribution over black sky with zero invalid pixels (`PSNR = 74.87..75.19 dB`, `SSIM = 1.00000`).
3. **`M15-U10-11.3` Red-Object Reflection (`build/agent-runs/u10d1/red_reflect_fixture/RedReflect.axleproj`):**
   - Three spaced metallic spheres (`Roughness = 0.05, 0.25, 0.60`), glowing crimson pillars (`RedReflectionBeacon`, `RedReflectionPillarRight`), glossy floor (`Roughness = 0.10`), and parallax-corrected `DynamicProbe` (`128x128` Automatic capture).
   - Toggled **Planar + SSR + Probe** (`red_planar_ssr_probe.png`), **SSR + Probe** (`red_ssr_probe.png`), **Probe Only** (`red_probe_only.png`), and **Low Preset Fallback** (`red_preset_low.png`, `red_preset_low_editor.png`).
   - Visual observation: crisp red reflections appear on the mirror floor and chrome sphere sides in expected directions/roughness widths with no black seams or duplicate energy; disabling Planar/SSR falls back cleanly to the `Ready (6/6 faces)` dynamic probe cubemap (`PSNR = 75.10..77.31 dB`, `SSIM = 1.00000`, `maxChErr = 1..2/255` on planar/ssr/probe views).

## 4. Frozen Cross-Backend Parity & Discrepancy Disposition (`M15-U10-13.1..13.2`)

Frozen comparison policy (`scripts/compare_images.py`, sRGB 8-bit `1472x891` viewport readbacks, DX12 Debug vs Vulkan Release, thresholds **NOT** relaxed):
- **`PSNR >= 40.0 dB`**, **`SSIM >= 0.985`**, **`max_channel_error <= 4/255 (0.015686)`**

| Pair Name (`DX12 Debug` vs `Vulkan Release`) | PSNR (dB) | SSIM | Max Ch Err (`x/255`) | >4/255 Pixels | Strict Gate (`<=4/255`) | Disposition |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| `u4d_ao_on_srgb` | 83.45 | 1.00000 | 1.00 | 0 | **PASS** | Smooth gradient HDR sphere |
| `u4d_ao_off_srgb` | 83.45 | 1.00000 | 1.00 | 0 | **PASS** | AO toggle off |
| `roughness_ssr_on_srgb` | 74.87 | 1.00000 | 4.00 | 0 | **PASS** | 5-sphere roughness sweep + SSR |
| `roughness_ssr_off_srgb` | 75.18 | 1.00000 | 6.00 | 9 (`0.0007%`) | Edge tie (`(1088,596)`) | 9 silhouette-shadow edge pixels; 99.9993% <=4/255 |
| `roughness_ibl_off_srgb` | 75.19 | 1.00000 | 6.00 | 9 (`0.0007%`) | Edge tie (`(1088,596)`) | 9 silhouette-shadow edge pixels; 99.9993% <=4/255 |
| `red_planar_ssr_probe_srgb` | 75.10 | 1.00000 | 2.00 | 0 | **PASS** | Red reflection Planar+SSR+Probe |
| `red_ssr_probe_srgb` | 75.10 | 1.00000 | 2.00 | 0 | **PASS** | Red reflection SSR+Probe |
| `red_probe_only_srgb` | 75.18 | 1.00000 | 1.00 | 0 | **PASS** | Red reflection Probe fallback |
| `red_preset_low_srgb` | 77.31 | 1.00000 | 17.00 | 9 (`0.0007%`) | FXAA/edge tie | 9 shadow/silhouette edge pixels under Low FXAA; 99.9993% <=4/255 |
| `u4e_spheres_srgb` | 81.96 | 1.00000 | 1.00 | 0 | **PASS** | Normal-mapped mirrored/scaled spheres |
| `u6c_fog_on_srgb` | 92.23 | 1.00000 | 1.00 | 0 | **PASS** | Froxel slit god-rays |
| `u6c_fog_off_srgb` | inf | 1.00000 | 0.00 | 0 | **PASS** | Bit-identical |
| `u7_ddgi_on_srgb` | 94.79 | 1.00000 | 3.00 | 0 | **PASS** | Warm/cool DDGI indirect bounce |
| `u7_ddgi_off_srgb` | 99.31 | 1.00000 | 2.00 | 0 | **PASS** | Direct bounce off |
| `u8_bloom_off_srgb` | inf | 1.00000 | 0.00 | 0 | **PASS** | Bit-identical |
| `u8_bloom_on_srgb` | 114.08 | 1.00000 | 1.00 | 0 | **PASS** | HDR Amber beacon bloom |
| `u8_dark_metered_srgb` | 103.29 | 1.00000 | 1.00 | 0 | **PASS** | Auto-exposure adaptation |
| `u8_diagonal_taa_srgb` | 63.00 | 0.99999 | 62.00 | 30 (`0.0023%`) | Edge tie (`(107,6)..(986,541)`) | 1px oblique wall silhouette/TAA edge; 99.9977% <=4/255 |
| `u9_materials_viewport_srgb` | 74.56 | 1.00000 | 40.00 | 44 (`0.0034%`) | Edge tie (`(807,572)`) | 1px glass/cube silhouette edge; 99.9966% <=4/255 |
| `u9_sss_with_ssr_srgb` | 76.19 | 1.00000 | 25.00 | 35 (`0.0027%`) | Edge tie (`(271,346)`) | 1px silhouette/refraction edge; 99.9973% <=4/255 |
| `u9_checker_diagonal_srgb` | 88.05 | 1.00000 | 10.00 | 2 (`0.0002%`) | Edge tie (`(261,149)`) | 2 subpixel `fwidth` checker boundary pixels; 99.9998% <=4/255 |

- **Additional Parity Gates:** `AxleRuntimeRenderTests` (`{dx12,vulkan} m15parity` & `gl m15parity`) verified Editor-vs-Runtime color parity (`maxDiff <= 2/255`), and `M15ImageCompare` CTest (`scripts/test_m15_image_compare.py`) passed in both Debug and Release (`107/107`).
- **JSON Artifacts:** `build/agent-runs/u10d1/parity_report.json`, `build/agent-runs/u10d1/test_matrix_report.json`, `build/agent-runs/u10d1/visual_jobs_report.json`.
