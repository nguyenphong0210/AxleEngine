# M15 U7 independent DDGI review

Gemini delivered `39372690..1f0b293c` and reported Editor recovery in `CG-20261121T023647247766Z-GEMINI-198b3b`. Codex reviewed the integration from `7f261884`, landed the corrections in `75a93b66`, and replayed the native and Editor fixtures on a GeForce RTX 3070 with driver `610.88`. A separate source reviewer verified the final corrections after identifying the live material-generation gap.

## Findings and fixes

| Finding | Trigger and correction |
|---|---|
| U7-R1 · P1 · Probe batches stall | View extraction creates fresh mesh wrappers for the same GPU buffers. Their addresses and continuously changing transforms and light values entered the history hash, which repeatedly cleared the atlas and restarted the cursor. DDGI now uses stable geometry generations, preserves the cursor under motion, and checks state once for both planning stages. |
| U7-R2 · P1 · Authored grid ignored in Runtime | Runtime never enabled the renderer flag. An enabled scene component now opts in on Runtime and Editor. Editor Render FX can override the viewport and restore the scene setting. Requests without a valid grid skip atlas allocation. Extremely large dimensions are rejected before multiplication. |
| U7-R3 · P1 · Invalid probe visibility | Backfaces had zero radiance and were missing from the integration denominator. A probe becoming trapped retained its old valid irradiance and blended its rejection weight slowly. Recapture now updates geometric validity immediately, retains the full integration denominator, and runs alpha discard on both faces before depth writes. The pre-update history hash uses the canonical native material generation, so a live cutoff edit reseeds immediately. The two-sided cutout assertion failed before the shader fix. |
| U7-R4 · P2 · Capture shadow lookup and gain | Probe faces selected cascades from their own view depth while sampling an atlas built from the main view. They now select cascades using the main-view depth plane. Capture feedback uses unit gain and the authored energy multiplier applies at the receiver. |

## Requirement verification

| M15 U7 requirement | Implementation and independent evidence | Status |
|---|---|---|
| 01.1 and 01.2 · Acquisition and cost policy | `DDGIPass` rasterizes six world-space faces and integrates them in `DDGIUpdate`. The `--m15-ddgi` suite reads colored probe radiance while the main camera faces away. Policy specifies face size, cap and input coverage. | PASS |
| 02 · Grid, indexing and bounds | `ExtractDDGIProbeGrid`, `SceneSerializer`, `DDGIContract` and `RHIViewTargets`. CPU tests cover world translation, serialization, CPU/RHI packing agreement and a dimension product of `2^64 + 4`. | PASS |
| 03.1–03.3 · Irradiance, distance and moments | `DDGI.hlsl` readbacks verify independent atlas sizes, mirrored octahedral gutters, unseeded tiles, and the distance-moment invariant. | PASS |
| 04.1–04.3 · Radial visibility, Chebyshev and bias | Native fixtures verify radial hit distance, enclosed and newly trapped probes, and a live `AlphaCutoff` change at a fixed scene revision. CPU contracts cover the view and normal bias. The sealed-chamber image checks visibility weighting. | PASS |
| 05.1 and 05.2 · Hysteresis and staggering | Native tests measure the feedback difference for `0.0` versus `0.95` hysteresis. The cursor advances `0, 8, 16, 24, 0` under camera, geometry, light and sky changes with freshly wrapped mesh views. | PASS |
| 06 · Diffuse integration and Editor visual | Built-in ForwardPBR and canonical MaterialSurface receivers both acquire indirect RGB in the GPU fixture. Editor shows warm and cool bounce on the sphere, wall and floor. The component controls and zero Console errors are visible in the swapchain capture. Runtime pixel comparisons cover authored enablement and live disablement. | PASS |
| 07.1 · Invalidation | Native tests cover scene revision, authored grid origin, a replaced material, and a live alpha cutoff. `ViewContext` checks scene/target/material generations. Hysteresis changes apply on the next update. | PASS |
| 07.2 and 08.4 · Fallbacks | GPU fixtures compare disabled grid, disabled effect, all-enclosed probes, and 4× MSAA to ambient/IBL. CPU checks confirm no atlas without an active grid. OpenGL Runtime verifies pixel parity after toggling the authored component. | PASS |
| 07.3 · Memory and dispatch budgets | CPU caps and native 1080p scorecards cover 32 to 16,384 probes. The largest preset uses 40.5625 MiB of device-reported atlas and scratch memory, within 64 MiB. Timings are tied to real GPU submission tokens. | PASS |
| 08.1 · Indirect bounce | Native off-camera, oblique receiver, second-bounce and canonical MaterialSurface cases pass. Editor image pairs isolate the change with fixed camera and lights. | PASS |
| 08.2 · Wall leakage | Native sealed chamber remains at most 2 RGB units inside while the outside receiver exceeds 50 green units. | PASS |
| 08.3 · Grid settings | Native sweeps exercise origin, gain, update cap and hysteresis. CPU and Editor confirm authored spacing and bias controls. | PASS |

## Reproduction and evidence

Fixture and UTF-8 plan are under [`Tests/Fixtures/M15/U7_DDGI`](../../Tests/Fixtures/M15/U7_DDGI/). Copy them under `build/agent-runs/` before running Editor and use the copied project and plan. The plan holds the camera fixed and saves viewport and swapchain PNGs for authored enablement, override off and override on. See the [Editor capture guide](../agents/EDITOR_VISUAL_CAPTURE.md).

Evidence root is `build/agent-runs/codex-m15-u7`. `final/verification-exits.txt` enumerates the corrected builds' Editor, contract, focused GPU and Runtime run exits. `verification-exits.txt` also records the full Release environment suite on the corrected build and RenderService checks before the last material-history change. `cutout-before.log` records the D3D12 regression failure before the shader fix.

| Command | Configurations and evidence |
|---|---|
| `cmake --build build --config <config> --target AxleEditor AxleEnvironmentRenderingTests AxleRenderUnitTests AxleRuntimeRenderTests` | Debug and Release passed through separate target builds. See `source-build-*.log`, `cutout-build-*.log` and `runtime-final-build-*.log`. |
| `ctest --test-dir build -C <config> -R '^(M15DDGIContract\|M15FroxelFogContract\|M15ReflectionCaptureContract\|M15OcclusionContract\|M15HiZContract\|M15EnvironmentContractReference)$' --output-on-failure` | Six tests per configuration. Logs are `final/ctest-*.log`. |
| `AxleEnvironmentRenderingTests.exe <backend> <output> --m15-ddgi` | D3D12 and Vulkan in Debug and Release. Logs are `final/ddgi-*.log`. Each run writes `M15_U7_ddgi_scorecard.txt` under its output directory. |
| `AxleEnvironmentRenderingTests.exe <backend> <output>` | Full Release suite on D3D12, Vulkan and OpenGL WGL. Logs are `environment-Release-*.log`. |
| `AxleRuntimeRenderTests.exe <backend> [cooked]` | D3D12 and Vulkan, both loose and cooked, plus OpenGL WGL in Debug and Release. Logs are `final/runtime-*.log`. Native receiver blue is 99 with DDGI enabled and 0 after disabling. OpenGL stays at 0. |
| `AxleEditor.exe --project=<project> --rhi=<backend> --no-fallback --rhi-validation --capture-plan=<plan>` | Debug D3D12, Release D3D12 and Release Vulkan. Logs are `final/editor-*.log`. Each reports 115 successful views, zero failed views, zero Console errors and zero nonfinite pixels. Vulkan emitted unused vertex input warnings during pipeline initialization. |
| `python scripts/check_architecture.py` and `git diff --check` | Both passed. The architecture scan covered 625 source files and 25 CMake files. |

The three Editor runs showed the same fixed-camera change. The left and right sample points on the matte sphere were `(191,94,29)` and `(25,160,185)` with the authored grid enabled. Both read `(10,11,13)` with the viewport override off. Re-enabling the viewport restored the authored image pixel-for-pixel. The `final/<config>_<backend>/` directories contain `authored_on_viewport.png`, `viewport_override_off.png` and `authored_on_editor.png` with the selected probe grid and Inspector. The sample coordinates and whole-image comparison are in `final/image-metrics.txt`.

## Experimental coverage

The fixtures exercise one native NVIDIA adapter. Per-pass performance samples cover the fixed 1080p workload. Raster capture uses the main view's directional shadow coverage and resolved built-in shading as the proxy for custom-material capture. Canonical MaterialSurface is verified as a DDGI receiver. The test scene visibly tapers where the probe volume ends.
