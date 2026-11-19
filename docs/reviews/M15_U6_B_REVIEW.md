# M15 U6-B independent review

The review covers the static 3D froxel fog delivery at `ac9ee556` against baseline `74f64f7c`. The repair source is `081d5355ea5596bb1f5a37b9c152b5a3c79448af`. The delivery exchange records Gemini's self-verification. The evidence below records the independent Codex runs.

## Findings and repairs

| ID | Severity | Finding and repair |
| --- | --- | --- |
| U6B-R1 | High | FogApply interpolated cumulative scattering and transmittance in logarithmic slice coordinates. Dense media produced incorrect transmittance inside a slice, and distant opaque fragments near depth 1 used the sky fog distance. It now weights adjacent stored endpoints by the Beer–Lambert optical depth of the physical segment, preserving continuity between slices, and reserves the clear-depth case for depth 1. |
| U6B-R2 | High | A positive-density fog pass in the legacy SDR path composed already tone-mapped color with linear in-scattering. Scene, RenderService, and viewport allocation now request linear color for positive-density fog and apply one final output conversion. The zero-density or zero-intensity path preserves the existing output mode and copied RGBA values. |
| U6B-R3 | High | The view target gate checked the global far distance before the enabled fog component could supply its own distance. Scene extraction, Editor and owned view preparation now share the same component selection and range policy. An invalid component range can use a valid view range, with a diagnostic. |
| U6B-R4 | Medium | Orthographic columns used a perspective ray model. They now use distinct world-space origins and parallel directions. The shader and reference use segment lengths in meters. Medium albedo is bounded to its physical domain and FP16 storage and positive output are bounded to the representable range. |
| U6B-R5 | Medium | Fog still requested the legacy screen-sized storage texture, and a descriptor pool allocation failure could abort the view. The 3D paths use their own volume descriptors and one resolved medium. A replacement descriptor pool is published after all its sets are ready, and missing fog GPU resources are reported while the scene continues. The per-pass uniform uses 256 bytes. |
| U6B-R6 | High | The delivered Vulkan compute shaders declared `Rgba32f` storage images for `RGBA16_FLOAT` view targets. The two pipeline entry points and the native probe now declare the actual `Rgba16f` image format in SPIR-V. [DXC SPIR-V image-format reference](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#vulkan-specific-image-formats) |

A separate-context reviewer rechecked depth, component and HDR policy, world rays, and descriptor lifetime. The follow-up recheck confirmed that the filtered endpoints are continuous, the FP16 limit follows the identity bypass, and the new stratified fixture covers depth boundaries and orthographic origins.

## Integration boundary

The current `VolumetricFogComponent` fields travel through scene extraction and the Runtime component codec into the snapshot. Editor uses the same extraction for viewport options, and RenderService uses snapshot media for its owned main targets. The native graph performs `FogCopy → FogInject → FogIntegrate → FogApply` in one frame. Fog shadow visibility, history reprojection, transparency composition, and integrated performance checks belong to U6-C through U6-E.

## Requirements and independent evidence

| Requirement | Implementation and callers | Independent evidence | Status |
| --- | --- | --- | --- |
| M15-U6-02.1 media injection | `ResolveMediaState`, `SceneExtraction`, `RHISceneRendererPasses`, `CSInjectMedia` | Readback covers the native 3D scattering volume, component overrides when the default range is invalid, column origins under height falloff, and the four graph passes. Both compute entry points declare `Rgba16f` in their SPIR-V disassemblies. | PASS |
| M15-U6-03.1 integration and apply | `CSIntegrate`, `PSMain`, `FroxelFogPass` | The native fixtures compare integrated slices and composited HDR against the homogeneous reference. Dense partial slices, stratified boundaries, oblique and orthographic rays, and finite depths near the far clip have dedicated readbacks. | PASS |
| M15-U6-03.2 physical units | `FroxelFogContract` and native fog shader | CPU verifies phase normalization, optical path length and fine-step double-precision quadrature. Native readbacks cover homogeneous extinction, height falloff and forward/backward Henyey–Greenstein response. The graph consumes current-frame volumes. | PASS |
| M15-U6-05.3 identity | `ResolveMediaState`, `CSInjectMedia`, `PSMain`, and RenderService | A zero-density or zero-intensity medium writes exact scattering zero and integrated identity. Every decoded HDR pixel and alpha equals the fog-off fixture. The RenderService fixture also compares the SDR bytes. | PASS |
| M15-U6-06 sunlight and diagnostics | `ResolveMediaState`, `RenderBackendPolicy`, `RHISceneRendererPasses` | Missing authored lighting produces extinction with zero added scattering and emits a diagnostic. A bad component range uses the valid default range with a diagnostic. An injected fog descriptor-pool OOM leaves the scene renderable and the next frame restores fog. | PASS |

Representative D3D12 Release red-channel readbacks were 0.865723 at center and off axis versus 0.866029 analytically for the dense orthographic partial slice. Across the stratified boundary they were 1.16715 and 1.1652. At a far-plane-adjacent surface they were 1.0791 versus 1.0792 analytically. Full physics records are under `build/agent-runs/M15-U6-B/codex/<Config>/<backend>/M15_U6B_froxel_physics.txt`.

## Build and execution record

Both configurations built and linked with exit 0. After the final Vulkan storage-format edit, the environment fixture, Editor and Runtime were rebuilt in both configurations with exit 0. The packaged Editor and Runtime fog shader hashes match the corrected source.

```powershell
cmake --build build --config <Config> --target AxleRenderUnitTests AxleRHIResourceViewTests AxleRHIStateTests AxleRenderServiceTests AxleEnvironmentRenderingTests AxleEditor AxleRuntime -- /m:4 /v:minimal
cmake --build build --config <Config> --target AxleEnvironmentRenderingTests AxleEditor AxleRuntime -- /m:4 /v:minimal
```

Build logs are `build/agent-runs/M15-U6-B/codex/build-<Config>.log` and `build-format-<Config>.log` in the same directory. The MSVC linker emitted LNK4098 during the builds. The exact test command lines, working directory and exit codes are in the `<Config>/*.log` subdirectories.

| Command from repository root | Debug | Release |
| --- | --- | --- |
| `build/Tests/<Config>/AxleRenderUnitTests.exe` | 0 | 0 |
| `ctest --test-dir build -C <Config> --output-on-failure -R "^(M15FroxelFogContract\|M15HiZContract\|M15OcclusionContract\|M15ReflectionCaptureContract\|RenderHarnessFailure)$"` | 0, 5/5 | 0, 5/5 |
| `AxleRHIStateTests.exe policy` | 0 | 0 |
| `AxleRHIStateTests.exe <backend>` | 0, both | 0, both |
| `AxleRHIResourceViewTests.exe <backend> storage` | 0, both | 0, both |
| `AxleRenderServiceTests.exe <backend>` | 0, both | 0, both |
| `AxleEnvironmentRenderingTests.exe <backend> build/agent-runs/M15-U6-B/codex/<Config>/<backend>` | 0, both | 0, both |
| `AxleRHIResourceViewTests.exe <backend> targets` | 0, both | 0, both |
| `AxleEditor.exe --verify-empty-startup --rhi=<backend> --no-fallback --rhi-validation` | 0, both | 0, both |

The backend arguments were `dx12` and `vulkan`. Test binaries are under `build/Tests/<Config>` and Editor is under `build/Editor/<Config>`. The previously open U6-A D3D12 windowed viewport and Editor startup checks passed during this review. Both windowed Editor checks exercised five UI frames per configuration.

Local `dxc -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -T cs_6_0 -E <Entry> -D __spirv__=1 -Fo <Output> <Source>` followed by `spirv-dis <Output>` exited 0 for `CSInjectMedia` and `CSIntegrate` against both source revisions. The disassemblies under `build/agent-runs/M15-U6-B/codex/spirv` show the change from `Rgba32f` to `Rgba16f` for the storage image. The Vulkan environment suite continues to emit storage-image format warnings from the existing GTAO fixtures. The final logs show zero validation errors and zero storage-format diagnostics for the froxel descriptors.

`python scripts/check_architecture.py --root .`, `python scripts/agent_exchange.py validate` and `git diff --check` exited 0.

All five U6-B requirements pass their automated gates. Editor startup and viewport checks passed on both native backends in Debug and Release.
