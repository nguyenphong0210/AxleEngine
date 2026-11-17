# M15-U5-C independent Codex review

Reviewed 2026-11-17. Scope: S5.3, M15-U5-16.1 through M15-U5-20.
Gemini delivery: `f7b5f5141247495a922885ab340262ae93043eb3`, based on
`c7153144`; exchange delivery `CG-20261117T104854217071Z-GEMINI-c036e9`.
This report records Codex inspection, repairs and reruns, separately from
[Gemini's historical self-verification](M15_U5_C_REVIEW.md).

**Verdict: automated U5-C scope accepted after repairs.** No unresolved source
finding remains. Interactive Editor visuals for U5-17.1, U5-18.2 and U5-19.2
remain NOT RUN/user-deferred, with their plan checkboxes open. DX12 presentation
initialization is unavailable on this adapter, as detailed below; headless native
DX12 filtering and storage-view validation did run successfully. This review
does not release U5-D or claim complete visual/presentation acceptance.

## Findings and corrections

| ID | Severity | Trigger and original behavior | Correction and regression |
| --- | --- | --- | --- |
| U5C-R1 | P1 | Per-mip UAV creation also applies to 3D storage textures. NRI's default `sliceNum = REMAINING` resolves against base depth: an 8-deep texture's mip 1 incorrectly describes 8 W slices instead of 4. | `RHITexture.cpp` explicitly supplies base depth for mip 0 and `max(1, depth >> mip)` thereafter. Native validation fixture allocates an 8x8x8 storage volume with 4 mip UAVs down to depth 1. |
| U5C-R2 | P2 | CPU `SampleCubemapBilinear3D` clamped its footprint to one face, producing a discontinuous reference at shared edges. A first cross-face correction also required symmetric treatment at three-face corners. | Reproject out-of-face bilinear taps onto adjacent faces and average the three meeting texels for a corner tap. Tests cover a discontinuous face-color edge, all 8 corners approached from each of 3 major faces, constant HDR conservation and pole isotropy. Nonfinite coordinates/roughness/norm lengths receive an explicit neutral fallback. |
| U5C-R3 | P2 | The sphere sweep read an FP16 target with `EnableToneMapping=false`; the forward shader's display mapping still ran. FP16 storage alone did not establish scene-linear HDR, despite the delivery's HDR evidence claim. | Enable the HDR post-processing route, read `GetIntermediateHDRTarget()` before tone mapping, validate finite nonnegative RGB and finite alpha, and require the sharp highlight to exceed 1. Keep the monotonic peak and skirt assertions. |
| U5C-R4 | P2 evidence gap | Prefilter failure injection covered `BeginCommandBuffer` only, before any mip work was recorded. | Exercise both begin and end of command buffer 7, after six captured faces. The end fault occurs after all prefilter dispatches are recorded. Both failures must preserve generation ID, texture handle and old mip-2 radiance; a subsequent recapture must succeed. |

`RHIResourceViewMain.cpp` now shares the volume/cubemap allocation, retained
generation and partial-UAV-failure regressions between `targets` and a `storage`
headless mode. The latter keeps native validation enabled and strict backend
selection; it avoids requiring presentation for texture-view validation.

## Architecture and recheck

Reviewed texture-view creation/destruction and generation leases, compute
descriptor/constant-buffer lifetime, per-mip barriers, raw/filtered provenance,
atomic generation publication and ForwardPBR roughness sampling. The pure 3D
downsample/GGX pipeline and existing architecture are retained. Raw mip 0 is
captured first; each downsampled source mip becomes readable before use; filtered
destinations become readable before publication. Both texture generations and
recording resources remain pinned through submission. Failed candidates leave
the previous published generation intact.

A separate-context reviewer found U5C-R1, rechecked the repairs and affected
callers, caught the initial corner asymmetry, and accepted the final source
corrections with no remaining blocker. That recheck was source-only; the
execution results below were run by the primary Codex reviewer.

## Verification against final source

Commands ran from the repository root. In this table, `C` is each of `Debug` and
`Release`, and `B` is each of `dx12` and `vulkan`; every listed combination ran.

| Check | Command | Exit / observed result |
| --- | --- | --- |
| Build | `cmake --build build --config C --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor --parallel 8` | 0 / 0; existing LNK4098 warning remains |
| Focused CPU | `build/Tests/C/AxleRenderUnitTests.exe --m15-reflection-capture-contract` | 0 / 0 |
| Full CPU | `build/Tests/C/AxleRenderUnitTests.exe` | 0 / 0 |
| Bake | `build/Tests/C/AxleEnvironmentBakeTests.exe` | 0 / 0 |
| Registered contracts | `ctest --test-dir build -C C --output-on-failure -R "M15ReflectionCaptureContract\|M15EnvironmentContract"` | 0 / 0; 2/2 each |
| Native storage views | `build/Tests/C/AxleRHIResourceViewTests.exe B storage` | 0 in all 4 combinations |
| Native rendering | `build/Tests/C/AxleEnvironmentRenderingTests.exe B build/Tests/EnvironmentRendering/Codex-U5C-C-B` | 0 in all 4 combinations; full environment/U4/U5 rendering regressions, including both prefilter fault boundaries |
| Vulkan view/presentation | `build/Tests/C/AxleRHIResourceViewTests.exe vulkan targets` | 0 / 0 |
| DX12 view/presentation | `build/Tests/C/AxleRHIResourceViewTests.exe dx12 targets` | 1 / 1 at device initialization; `SwapChainInterface` unavailable, target fixture NOT RUN |
| Strict Vulkan Editor | `build/Editor/C/AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback` | 0 / 0; Vulkan active, 5 UI frames |
| Strict DX12 Editor | `build/Editor/C/AxleEditor.exe --verify-empty-startup --rhi=dx12 --no-fallback` | 1 / 1; `SwapChainInterface` unavailable, startup gate NOT RUN |
| DX12-requested fallback | `build/Editor/C/AxleEditor.exe --verify-empty-startup --rhi=dx12` | 0 / 0; actual backend Vulkan, not DX12 presentation evidence |
| Architecture | `python scripts/check_architecture.py` | 0; 613 source files / 25 CMake files |
| Exchange / whitespace | `python scripts/agent_exchange.py validate`; `git diff --check` | 0 / 0 |

Successful native fixtures assert zero RHI validation errors. DX12 presentation
fails before the U5-C fixture: pinned NRI reports the adapter's swapchain feature
unavailable (`DeviceD3D12::FillDesc` uses `HasOutput()`). No fallback result is
counted as DX12 execution. Vendor code and presentation policy are unchanged.

Logs: `build/Codex-U5C-build-{Debug,Release}.log`,
`build/Codex-U5C-{C}-{focused,CPU,bake,ctest}.log`,
`build/Codex-U5C-{C}-{storage,RHI,GPU}-{B}.log`,
`build/Codex-U5C-{C}-editor-{vulkan,dx12-strict,dx12-fallback}.log`,
`build/Codex-U5C-architecture.log`, `build/Codex-U5C-exchange.log`.
`build/Codex-U5C-results.json` records the exact executable, arguments, exit and
log for each final execution; artifacts remain under
`build/Tests/EnvironmentRendering/Codex-U5C-{C}-{B}`.

Representative scene-linear sphere measurements (DX12, both configurations):

| Roughness | Peak luminance | Ring / peak |
| --- | --- | --- |
| 0.05 | 6.4925 | 0.0758104 |
| 0.35 | 3.76098 | 0.118724 |
| 0.70 | 1.038 | 0.39823 |
| 0.95 | 0.729727 | 0.53212 |

Vulkan reproduces the same peaks; its roughness-0.05 ring ratio is 0.0758092
(about 0.0000012 below DX12), with the remaining displayed ratios unchanged.

Controlled +Z reference errors remain 0.0190894 at mip 1 and 0.1366 at mip 2
(existing 0.25 tolerance). This is a bounded fixture comparison: the CPU GGX
reference samples the base mip, while the GPU uses solid-angle source LOD.
It is not an exact numerical equivalence proof for arbitrary environments.
All six faces/all five output mips pass finite/nonnegative readback; native
seam and +Y pole fixtures also pass. Interactive visual judgment remains open.
