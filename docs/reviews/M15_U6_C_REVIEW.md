# M15 U6-C independent review

The review covers the U6-C delivery at `75464e5d` against baseline `bbf33b6e`, with repairs in `de364044c01d171dfeb77774e0fe92950968c53c`. Gemini's self-verification is recorded in the delivery exchange. This report records the independent review and execution.

## Findings and repairs

| ID | Severity | Finding and repair |
| --- | --- | --- |
| U6C-R1 | High | The delivered injection switched the medium and all lights from center evaluation to four depth samples whenever an atlas was active. Toggling shadows could change extinction and the emission from unrelated lights. The repair keeps center extinction and unoccluded lighting fixed and estimates visibility separately for the sun and each mapped local light. |
| U6C-R2 | High | Injection required the comparison sampler for the entire fog feature. The disabled depth-map path could also bind a color placeholder to a shader with depth-comparison instructions. The repair compiles an unshadowed injection layout with no atlas reads or shadow roots when depth sampling or the fog comparison sampler is unavailable. Fog, authored lights, and extinction remain active with a diagnostic. |
| U6C-R3 | Medium | Local shadow subsamples at a cone or range edge could fall outside the light and contribute neutral atlas visibility to an occluded froxel. The repair averages only in-range and in-cone taps, using the center when all four miss. Point shadows check that all six atlas faces belong to the published range. |
| U6C-R4 | Medium | The authored spot cutoff and near-light epsilon differed between the CPU evaluator and the shader. They now agree. The disk and rectangle calculation uses a stable equivalent solid-angle factor, preserving low radiance at distance. Native tests cover the narrow blocked cone, packed directional slots, back-facing two-sided disk, and the complete slit row. |

A separate-context review traced the CBV and atlas lifetimes through the shadow preparation, graph declaration and injection recording steps. The follow-up confirmed the cone-edge geometry, packed light indexing, disk case, and full-row bound.

## Requirements and evidence

| Requirement | Implementation and callers | Independent evidence | Status |
| --- | --- | --- | --- |
| M15-U6-02.2 directional and local injection | `CSInjectMedia`, `FroxelFogPass`, `RHISceneRendererPasses`, `LightingService`, cascade and local shadow publications | D3D12 and Vulkan readbacks cover directional CSM, spot and point shadowing, additional directional emission, rectangle and two-sided disk emission. A comparison-sampler OOM keeps the surface shadow atlas active and restores the unshadowed fog reference with a diagnostic. | PASS |
| M15-U6-02.2 Editor visual | `EditorLayer`, `RenderService`, native D3D12 viewport | Captured and inspected the same slit scene in Debug and Release. Fog off gives a dark viewport. Fog on shows a bright shaft through the opening and darker fog behind the finite blocker walls. Both captures show Console `Error 0`. | PASS |
| M15-U6-07.1 blockers | CSM tiles, spot projection, point face lookup and RGBA16F volume | Readbacks show both foreground and blocked depth slices. Shadow toggles preserve the full alpha slices exactly in stratified fog. The blocked spot edge reaches zero even where the cone intersects the depth segment. | PASS |
| M15-U6-07.2 bounded and no-sun shafts | Slit fixture and light publication | The bright columns stay within the 1.6 m opening plus the filter margin across the complete probe row. Removing the directional sun yields zero scattered light in the measured slice and zero HDR RGB in the fixture. | PASS |

The visible Render FX toggle goes through scene extraction and the same renderer used by the native fixtures. Custom materials and depth casters retain the existing shadow-atlas policy. The Runtime application builds with the shared shader and RenderService. The enabled-fog visual coverage here is the Editor viewport and the native fixture.

## Editor capture record

The reproducible project is [FroxelSlit.axleproj](../../Tests/Fixtures/M15/U6C/FroxelSlit.axleproj). The Debug and Release capture runs used identical scene data in temporary project copies under `build/agent-runs/M15-U6-C/codex/visual/<Config>`. The working directory was each Editor executable's directory.

```powershell
AxleEditor.exe --rhi=dx12 --no-fallback --rhi-validation --project <absolute-path-to-FroxelSlit.axleproj>
```

Play selected the fixture's game camera. Render FX isolated High-quality Volumetric Fog from SSR, planar reflections, GTAO, contact shadows, and bloom. The fog-off and fog-on screenshots are in the review conversation for both configurations. The native Editor logs are `visual/<Config>/editor-capture.log` under the same run directory. Both instances closed cleanly.

## Native readbacks

| Probe | Unshadowed scatter magnitude | Shadowed scatter magnitude |
| --- | ---: | ---: |
| CSM foreground | 0.0198843 | 0.0198843 |
| CSM behind wall | 0.0205676 | 0 |
| Spot behind wall | 0.178196 | 0 |
| Spot at cone edge | 0.531581 | 0 |
| Point behind wall | 0.877088 | 0 |

The slit row had 10 bright columns within the geometry and filter margin. Scatter was 0.293235 at the center and zero at both occluded side probes. Removing the sun returned zero for the maximum measured scatter and HDR RGB. The full native measurements are in `<Config>/<backend>/M15_U6C_froxel_shadows.txt` under the run directory.

## Build and execution record

The build commands ran for Debug and Release. The full build and both incremental runs exited 0 in each configuration. Logs are `build-<Config>.log`, `build-final-<Config>.log`, and `build-review-<Config>.log` under `build/agent-runs/M15-U6-C/codex`. MSVC reported warning LNK4098 during linking.

```powershell
cmake --build build --config <Config> --target AxleRenderUnitTests AxleRHIResourceViewTests AxleRHIStateTests AxleRenderServiceTests AxleEnvironmentRenderingTests AxleEditor AxleRuntime -- /m:4 /v:minimal
cmake --build build --config <Config> --target AxleEnvironmentRenderingTests AxleEditor AxleRuntime -- /m:4 /v:minimal
```

The final `CSInjectMedia` shader compiled to DXIL and SPIR-V with exit 0 using DXC. The second compile used `-D AXLE_FOG_SHADOWS_DISABLED=1`. The SPIR-V disassemblies `shadowed.spvasm` and `unshadowed.spvasm` in the run directory both declare `Rgba16f` storage. Only the shadow variant contains depth-comparison sampling and atlas descriptors.

```powershell
dxc -spirv "-fspv-target-env=vulkan1.3" -fvk-use-dx-layout -D __spirv__=1 -T cs_6_0 -E CSInjectMedia -Fo <output.spv> Engine/Resources/Shaders/VolumetricFog.hlsl
dxc -T cs_6_0 -E CSInjectMedia -Fo <output.dxil> Engine/Resources/Shaders/VolumetricFog.hlsl
spirv-dis <output.spv> -o <output.spvasm>
```

The test working directory was the repository root. Tests used the binaries under `build/Tests/<Config>` and Editor under `build/Editor/<Config>`. The full commands and exit codes are in `<Config>/*.log` under the run directory.

| Command | Debug | Release |
| --- | --- | --- |
| `AxleRenderUnitTests.exe` | 0 | 0 |
| `ctest --test-dir build -C <Config> --output-on-failure -R "^(M15FroxelFogContract\|M15HiZContract\|M15OcclusionContract\|M15ReflectionCaptureContract\|RenderHarnessFailure)$"` | 0, 5/5 | 0, 5/5 |
| `AxleRHIStateTests.exe policy` | 0 | 0 |
| `AxleRHIStateTests.exe <dx12 or vulkan>` | 0, both | 0, both |
| `AxleRHIResourceViewTests.exe <dx12 or vulkan> storage` | 0, both | 0, both |
| `AxleRenderServiceTests.exe <dx12 or vulkan>` | 0, both | 0, both |
| `AxleEnvironmentRenderingTests.exe <backend> build/agent-runs/M15-U6-C/codex/<Config>/<backend>` | 0, both | 0, both |
| `AxleRHIResourceViewTests.exe vulkan targets` | 0 | 0 |
| `AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback --rhi-validation` | 0 | 0 |
| `AxleRHIResourceViewTests.exe dx12 targets` | 1, blocked at swap-chain initialization | 1, blocked at swap-chain initialization |
| `AxleEditor.exe --verify-empty-startup --rhi=dx12 --no-fallback --rhi-validation` | 1, blocked at swap-chain initialization | 1, blocked at swap-chain initialization |

All four native shadow readbacks match byte for byte. The environment test exits include the validation error count. The matched corrected fog shader is packaged with Editor and Runtime in both configurations. `python scripts/check_architecture.py --root .`, `python scripts/agent_exchange.py validate` and `git diff --check` exited 0.

The later D3D12 windowed smoke processes enumerated three adapters and stopped before rendering because the NRI `SwapChainInterface` was unavailable. A launch retry for each configuration returned exit 1 with the same capability error. See `<Config>/retry-dx12-*.log` and `windowed-retry.json`. The earlier D3D12 scene captures enumerated four adapters and initialized the swap chain successfully.

The U6-C native D3D12 and Vulkan checks and the required Debug and Release Editor scene captures passed. The D3D12 empty-startup and standalone viewport smoke checks remain blocked on the later display state.
