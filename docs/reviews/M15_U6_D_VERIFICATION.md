# M15 U6-D froxel history delivery

Codex implemented and verified U6-D in `ad6c1ddfe8beae84ecfb34278ed8e9a1bcc0fc4a` from the accepted U6-C baseline `a56a78fa`. The user assigned this package directly to Codex. This record describes implementation and first-hand verification.

## Implementation

The fog graph injects scene-linear scattering and extinction, reprojects scattering into a ping-pong 3D history, integrates the resolved volume, and composites fog using that same volume for partial slices. Each view commits its slot, jittered zero-to-one view-projection matrix and linear depth row after a successful submission. The first usable frame copies current samples. A disabled or unavailable temporal pass keeps the single-frame fog path active.

The filter projects each current froxel center into the prior frustum and logarithmic grid. It rejects out-of-volume samples, extinction mismatches and radiance outside the current six-neighbor range. A center-relative bound limits residual illumination at moving edges. Extinction always comes from the current frame. The default history weight is 0.8 and the maximum effective weight is 0.9.

ViewContext invalidates fog history at the first frame, camera cuts, extent or target generation changes, scene revision or instance changes, device epochs and feature policy changes. Effective medium and projection changes seed a new volume. Editor and Runtime flag camera source switches through ViewRequest and RenderService. A failed or abandoned frame clears history eligibility.

## Requirements and evidence

| Requirement | Implementation | Verification | Status |
| --- | --- | --- | --- |
| M15-U6-04.1 temporal reprojection | `ViewContext`, `FogReproject`, `CSReproject`, `FogIntegrate`, `FogApply` | Slot 0 is seeded with raw media and the following frame writes slot 1. A controlled translation matches the projected history sample and differs from the same-index calculation. A linear HDR readback distinguishes reused history from the zero-weight path. Injected temporal layout failure preserves static fog and reports the fallback. | PASS |
| M15-U6-04.2 rejection | `CSReproject` | GPU reads confirm newly exposed frustum columns and lit froxels outside the prior fog depth range match raw media. Every probe satisfies its current 3D neighborhood bound and preserves extinction exactly. | PASS |
| M15-U6-04.3 invalidation | `ViewContext`, `ViewRequest`, `RenderService`, `RHISceneRendererPasses`, Editor and Runtime camera selection | Native fixtures verify the first frame, camera cut, scene revision, same-revision scene replacement, authored density edit, feature change, fog disable/re-enable and command finalization failure. The ViewContext unit test isolates device epoch and material generation changes. Native RenderService tests reopen the service across device epochs. | PASS |
| M15-U6-07.3 camera motion | `Test_M15_FroxelFogRendering.cpp` | A translated camera confirms positive reuse against CPU-predicted coordinates. Rotating the camera after extinguishing a previously lit volume leaves zero scattering in the measured slice. | PASS |
| M15-U6-07.4 resize and tier | `RHIViewTargets`, `ViewContext`, `Test_M15_FroxelFogRendering.cpp` | The same target wrapper grows from 256x144 to 320x180, publishes a new generation and seeds history before using the opposite slot. The Low-to-Medium transition seeds the newly sized 3D volume. | PASS |

## Reproduction

Commands ran from the repository root. The logs and native readbacks are under `build/agent-runs/M15-U6-D/codex`.

```powershell
cmake --build build --config <Config> --target AxleRenderUnitTests AxleRHIResourceViewTests AxleRHIStateTests AxleRenderServiceTests AxleEnvironmentRenderingTests AxleEditor AxleRuntime -- /m:4 /v:minimal
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/agent-runs/M15-U6-D/codex/<Config>/<backend>
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> --output-on-failure -R "^(M15FroxelFogContract|M15HiZContract|M15OcclusionContract|M15ReflectionCaptureContract|RenderHarnessFailure)$"
```

`<Config>` is either Debug or Release and `<backend>` is either `dx12` or `vulkan`. The native fixture writes `M15_U6D_froxel_temporal.txt` in the backend output directory. Required native gates include the RHI validation error count in their exit status.

DXC compiled `CSReproject` to DXIL and Vulkan 1.3 SPIR-V. `spirv-val --target-env vulkan1.3` exited 0. The shader packaged with both Editor and Runtime matches the repository source in both configurations.

## Results

| Check | Debug | Release |
| --- | --- | --- |
| Builds for Renderer tests, Editor and Runtime | Exit 0 | Exit 0 |
| Native environment rendering and U6-D readbacks | D3D12 and Vulkan exit 0 | D3D12 and Vulkan exit 0 |
| Full render unit binary and focused CTest | Exit 0, 5/5 CTest | Exit 0, 5/5 CTest |
| Native RHI state, storage, view targets and RenderService | D3D12 and Vulkan exit 0 | D3D12 and Vulkan exit 0 |
| Editor empty startup with RHI validation | D3D12 and Vulkan exit 0 | D3D12 and Vulkan exit 0 |

All four readbacks report 126 blended samples matching the world-space projection, 18 newly exposed frustum samples with no history, and a maximum change of 0.0449219 in per-meter scattering. The extinguished-light trail measured zero. The resize and reset sequence passed.

The build logs are `build-<Config>.log` and `build-final-<Config>.log`. MSVC emitted linker warning LNK4098. Each test log records the executable path, arguments, working directory and exit code. Architecture and exchange schema checks passed.
