# M15 U4–U6 independent follow-up

Review started at `75a63622`. The assigned scope covers the U4 GTAO/contact follow-up, U5 SSR and shadowed planar capture, outstanding Editor evidence, and the U6-E correction recheck. The repairs are `a71fc05c` (U4), `28bec5d3` (U5), `05b1e453` (U6-E), `2467a9aa` (capture completion) and `5fd995f3` (capture file diagnostics). Gemini's capture and windowed reports remain the implementer's evidence. This record covers the corrected source through `5fd995f3`.

**Disposition:** The scoped U4–U6 follow-up passed. U6-E's four remaining leaves are accepted after the independent recheck and the native and Editor matrix.

The user's close-up later exposed a near-mirror artifact in the planar-off capture. The [U5-R2 recheck](#u5-r2-vertical-ssr-streaks-under-the-reflected-spheres) records the correction and the remaining faint pattern.

## Findings

| Finding | Severity | Correction and verification |
| --- | --- | --- |
| U4-R1 | P2 | GTAO identified perspective using the projection's depth offset and used perspective view vectors and pixel scale with orthographic cameras. The compute shader now derives the projection type from clip W and uses a consistent view direction for reconstructed normals and AO integration. The native shallow-floor test reads resolved contact visibility with each projection. |
| U5-R1 | P2 | Thin SSR rays could invert the continuity clamp bounds. The C++ reference and HLSL both cap the depth spread by the same positive thickness limit. The minimum-thickness CPU case traces a continuous plane and the native suite verifies the contour and CPU/GPU parity cases. |
| U5 planar CSM | Verified | `93234b4e` retains the physical camera projection for CSM frustum fitting. The reflected view still clips raster geometry with the oblique projection. A directional-light native case requires a committed shadowed planar generation. The Editor shows the same camera before and after changing planar resolve. |
| U6E-R7 | P1 | Bilinear projected depth could average a foreground blocker with farther geometry while the matching color sample retained the blocker. Forward PBR now checks the nearest depth in the contributing color footprint for green and both dispersion rays. The fogged glass fixture changes only foreground emission and checks the first visible glass texel beside the blocker. |
| Capture-R1 | P2 | The presenter could consume a request after a failed setup or submit, and the Editor plan could continue without receiving the readback. Setup, submission and readback failures now fail the frame. The plan advances after the PNG has been written and fails if the window closes before completion. File write errors surface with the output path. Tests cover RGBA readback, failed submission and a deliberate output collision. |

A separate-context reviewer rechecked the U6-E correction `232298f5`, its current consumers and the new repair. The same reviewer rechecked the scoped U4, SSR/CSM and capture changes. This was a source review. The GPU and Editor results below come from the primary review session.

## Requirement disposition

| Requirements | Production paths | Independent evidence | Status |
| --- | --- | --- | --- |
| U4-01 through U4-06.5 | `GTAOPass`, `GTAO.hlsl`, `Occlusion.hlsli`, Forward PBR and generated surfaces | Pre-lighting visibility isolation, exact sampling, odd extents and reconstruction. Unoccluded and emissive native checks, floor/sphere and shallow-floor readbacks, and matching Editor mirror viewport pixels with GTAO on and off. | PASS |
| U4-07.1 and U4-07.2 | Normal prepass, mapped and deformed geometry, resolved AO/contact | Native current-depth/normal and deformation fixture. Mirrored and positively scaled normal-mapped meshes are visible in the Editor capture with AO/contact enabled and disabled. | PASS |
| U5-03 and U5-07.1 | Hi-Z graph resources, views and debug display | Integer depth levels and odd-edge reduction in the native suite. Editor viewport comparison at levels 0 and 3. | PASS |
| U5-08 through U5-31.5 | Dynamic reflection capture and prefilter, generation publication, multi-probe selection and per-surface resolve | Native face orientation, dynamic invalidation and retention, parallax, blending and fallback cases. Editor box/sphere probe scene, SSR toggle, matte reference and Play/Eject captures. | PASS |
| U5-32 through U5-39.6 and the contour regression | `SSR.hlsl`, the CPU reference and reflection resolve | Thin-ray CPU check, native contour and odd/even Hi-Z ray parity cases, and an Editor roughness sweep with SSR enabled and disabled. | PASS |
| U5-43, U5-44.4 and U5-45.1 | LTC shading, area light scene component and Inspector | Native area-light policy and roughness cases. Editor Inspector displays the rectangle settings and the unshadowed policy. The earlier independent Assembly edit, save and preview evidence remains in the [U5-G review](M15_U5_G_CODEX_REVIEW.md). | PASS |
| U5-46.5 and U5-46.8 | `RHICascadedShadows`, mirrored planar camera, capture/resolve and Inspector | Native successful shadowed generation and energy/fallback oracles. Same-camera Editor images with planar on and off and Render FX visible. | PASS |
| U6-02.2, U6-07.1 and U6-07.2 | Froxel injection, shadow atlas and authored Editor scene | Native blocked directional/local lighting and slit tests. Editor Play images show the shaft through the slit with authored fog, and the dark reference with fog disabled. | PASS |
| U6-05.1 | Fog apply, refraction copy and transparent built-in/graph/particle consumers | Native linear-HDR analytic comparison, foreground-scatter isolation and fogged glass silhouette regression. | PASS |
| U6-05.2 | Fog policy, extraction, Inspector and Runtime | Native no-light zero scattering/positive extinction, point-only scattering and Runtime scene-component pixel response. | PASS |
| U6-07.5 and U6-07.6 | View target allocation, completion-token GPU timings and fallback policy | Live four-volume allocation and six measured samples per tier and extent. Runtime and service exercise D3D12/Vulkan and OpenGL fallback. | PASS |

## Editor captures

The Editor ran with `--no-fallback --rhi-validation --capture-plan=<plan>` on the RTX 3070. Plans and copied fixtures are under `build/agent-runs/codex-m15-u4-u6`. The U4D/U4E and FroxelSlit fixtures come from `Tests/Fixtures/M15`. The reflection audit scene was recreated locally with a shadow-enabled planar floor, two probes, colored reference walls, a matte reference and an LTC rectangle, since Gemini's generated scene was absent from this checkout.

| Capture directory | Evidence files |
| --- | --- |
| `u4d` | `gtao_on_editor.png`, `gtao_off_editor.png` and their viewport readbacks |
| `u4e` | `mapped_on_editor.png`, `mapped_off_editor.png` and their viewport readbacks |
| `u5rough` | `rough_ssr_on_editor.png`, `rough_ssr_off_editor.png` and their viewport readbacks |
| `u5reflect` | `planar_on_editor.png`, `planar_off_editor.png`, `hiz0_editor.png`, `hiz3_editor.png`, `rect_ltc_editor.png`, `probe_fallback_editor.png` and Play/Eject captures |
| `u6slit` | `fog_scene_editor.png`, `fog_off_editor.png` and their viewport readbacks |

The tool reads GPU-composited Editor and viewport textures before Windows desktop composition. The numerical comparisons use the full viewport PNGs. Separate native HDR readbacks cover numerical behavior before view presentation.

All 15 plans exited 0 across Debug D3D12, Release D3D12 and Release Vulkan. Each logged zero Editor errors and failed views. The 96 expected PNGs decoded successfully, and the viewport readbacks reported zero non-finite values. Vulkan reported unused vertex attribute warnings from the native validation layer. Representative images were inspected for each fixture and the viewport pairs were compared across all three configurations.

The 96 source PNGs, logs, capture plans and copied fixtures are packaged in `M15-U4-U6-Editor-Evidence.zip` under the run directory. After the final file-diagnostics edit, three plans produced 24 more verified PNGs. All ten rerun viewport PNGs matched their earlier captures byte for byte. The deliberate directory-as-output case exited 1 and identified the failed PNG path. See `final-capture-controls/results.json`.

| Viewport comparison | Changed pixels | Mean absolute RGB difference on the 0–255 scale |
| --- | --- | --- |
| Unoccluded mirror, GTAO on/off | 0% | 0 |
| Normal-mapped spheres, GTAO/contact on/off | 27.79–27.80% | 0.4874 |
| Gold roughness sweep, SSR on/off | 30.40% | 1.9764–1.9765 |
| Shadowed planar resolve on/off | 53.11% | 7.3192–7.3193 |
| Hi-Z visualization levels 0/3 | 7.09% | 1.2532 |
| Authored slit fog/override off | 100% | 162.4346 |

## Native and build evidence

Builds and reruns were launched from the repository root. The CMake build targets were `AxleEditor AxleEnvironmentRenderingTests AxleRenderUnitTests AxleRHIPresentationTests AxleRuntimeRenderTests AxleRenderServiceTests`. The MSBuild worker count was six for Release and three for Debug. Both Editor binaries were rebuilt after the diagnostics-only edit.

```powershell
cmake --build build --config <Config> --target <targets above> -- /m:<workers> /v:minimal /p:MultiProcessorCompilation=true /p:CL_MPCount=<4 or 2>
ctest --test-dir build -C <Config> --output-on-failure -R "^(M15(HiZ|Occlusion|ReflectionCapture|FroxelFog)Contract|RenderHarnessFailure)$"
build/Tests/<Config>/AxleRenderUnitTests.exe
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <dx12|vulkan|gl> <run-directory>
build/Tests/<Config>/AxleRenderServiceTests.exe <dx12|vulkan|gl>
build/Tests/<Config>/AxleRHIPresentationTests.exe <dx12|vulkan>
build/Tests/<Config>/AxleRuntimeRenderTests.exe <dx12|vulkan|gl> [cooked]
build/Editor/<Config>/AxleEditor.exe --project=<fixture> --rhi=<dx12|vulkan> --no-fallback --rhi-validation --capture-plan=<plan>
python scripts/check_architecture.py
```

The runtime harness requires `build/Runtime/<Config>` and `build/PluginSDK/<Config>/bin` on its process PATH. Initial Runtime launches returned Windows loader code `0xC0000135` while the runner pointed at the parent SDK directory. Reruns with the corrected PATH tested the same binaries. The original outcomes remain in `results-native.jsonl` and the final Runtime outcomes in `results-runtime.jsonl`.

| Verification | Result |
| --- | --- |
| Six build targets in Debug and Release, final Editor rebuilds in both | Exit 0 |
| Full RenderUnit executable and focused CTest | Full suite exit 0 and 5/5 CTest in each configuration |
| Environment suite | 6/6 D3D12, Vulkan and OpenGL executions across both configurations, exit 0 |
| RenderService, Runtime and shared presentation | 20/20 commands across both configurations, exit 0. Native Runtime center red was 213 with fog and 231 without. |
| Forward PBR, GTAO and SSR shader entries | Compiled to DXIL and Vulkan 1.3 SPIR-V. `spirv-val` passed all three modules. |
| Architecture and exchange validation | Exit 0 |

Exact native arguments, exit codes and log paths are in `verified-native-matrix.json`. The Vulkan log includes expected missing-resource fault fixtures and unused vertex attribute warnings. Native validation and teardown assertions passed.

The D3D12 negative control used the current test binary and a scratch shader tree containing the old Forward PBR shader from `75a63622`. With `--m15-froxel-fog`, it exited 1 at the foreground-edge assertion. The current shader passed the same assertion in the native matrix. See `negative-legacy-refraction/legacy-refraction.log`.

The four U6-E scorecards each record two extents, four quality tiers and six completed samples per row. At 1080p, D3D12 measured 3.75, 18.00, 30.00 and 112.50 MiB for Low through Ultra. Vulkan measured 3.75, 13.50, 30.00 and 95.625 MiB. Shared color and depth copies appear separately in each card. The timing rows are spot measurements on the RTX 3070. See each configuration/backend's `native/M15_U6E_froxel_scorecard.txt`.

The earlier U6-E report records the separate standalone VFX instance-buffer fault case with fog disabled. That standalone case was outside this follow-up's native matrix. [Original U6-E review](M15_U6_E_CODEX_REVIEW.md)

## U5-R2. Vertical SSR streaks under the reflected spheres

The user identified visible streaks in the planar-off Editor capture on source `e083a9b7`. The original screenshot inspection missed them. Fixed-camera captures at 1472×891 reproduced the streaks with SSR alone. They disappeared with SSR disabled while GTAO and contact shadows stayed enabled. Direct planar reflection also rendered a smooth contact zone in that view.

The mirror floor has roughness 0.055. The old spatial resolve ramp from 0.045 to 0.10 gave the filtered neighborhood about 9% of the blend at that roughness, so individual screen-space hits dominated near contact. In `b5e6960c`, the ramp reaches full resolve at 0.055 and retains the single-tap path at minimum roughness. The native zoomed sphere-floor test exercises both 0.055 and 0.12.

The same camera and feature flags were captured again on Release D3D12 and Vulkan, with planar reflection and TAA disabled. Both backends produced finite viewport images with zero Editor errors and zero failed views. A second camera pose was also captured. Reflections remain visible and the streaks are substantially reduced. Some faint structure remains visible when inspecting the contact region at enlarged scale.

The following values are mean absolute horizontal second differences of 8-bit luma, divided by two. The two rectangles use normalized viewport coordinates of x=[0.28, 0.46), y=[0.59, 0.655) and x=[0.54, 0.73), y=[0.62, 0.69).

| Capture | Left contact | Right contact |
| --- | ---: | ---: |
| Before | 1.5116 | 1.9584 |
| After, D3D12 | 0.4491 | 0.4480 |
| After, Vulkan | 0.4491 | 0.4476 |
| SSR off reference | 0.2950 | 0.1022 |

The original and corrected viewport images, fixed-camera plan, per-backend logs and same-scale crop `ssr-before-after.png` are under `build/agent-runs/codex-m15-vertical-streaks`.

`cmake --build build --config Release --target AxleEnvironmentRenderingTests -- /m:2 /v:minimal /p:BuildProjectReferences=false` exited 0. The rebuilt binary ran with `dx12` and `vulkan` and separate output directories. Both full runs exited 0, including the two roughness cases. At 0.055, each backend reported full SSR hit coverage and a relative horizontal difference of 0.0023 in the separate native floor fixture. The Release CTest Hi-Z and reflection capture contracts passed 2/2, and the architecture check passed.
