# M15 U6-E Codex review and corrections

The user assigned the independent review and repairs for the four released U6-E leaves. Review started at `60eae855` and covered the delivered changes from `e17212da` through `ba13d1da`. The correction commit is `232298f5`. The Gemini delivery in the exchange is historical, implementer-reported evidence. The test results below were executed by Codex.

The corrected source passed the scoped tests. Formal acceptance remains open. Three requests for the protocol's separate-context correction recheck stopped at agent authentication before source inspection. That recheck is **NOT RUN**.

## Findings and repairs

| Finding | Severity | Delivered behavior and correction |
| --- | --- | --- |
| U6E-R1 | P1 | Refraction tinted foreground in-scattering because the copied HDR background already contained it. Forward PBR now separates the near segment at the sampled ray, applies glass tint and absorption to transmission, and adds foreground in-scattering along the actual ray. Chromatic taps preserve the foreground depth rejection. |
| U6E-R2 | P1 | The fog pass moved ahead of the material-generation observation. The first frame after a material change could use stale fog history. Material observation now precedes fog recording, and that frame explicitly seeds current media with zero history weight. |
| U6E-R3 | P2 | Canonical Material Graph draws bypassed the froxel bindings. The generated surface shader, shader reflection and custom draw preparation now share the engine fog ABI. Alpha and additive surfaces select their corresponding composition. Arbitrary custom shaders without that ABI receive an explicit view diagnostic. |
| U6E-R4 | P2 | The particle renderer updated a shared fog CBV during recording and did not check the update result. Renderer-owned view uniforms now publish the active constants before graph compilation, and particle passes declare the same CBV they bind. Standalone particle draws use an initialized, immutable disabled CBV. |
| U6E-R5 | P2 | The Editor ignored authored fog until its viewport toggle was forced on, while Runtime followed the component by default. Both now follow the authored component by default. Editor Render FX can override that choice and return to scene control. |
| U6E-R6 | P2 | The scorecard used texture requirements from an allocation probe, read the previous frame's timings as the current frame's, and allowed incomplete samples. It now checks live allocations against the tier budget, reports shared copy storage, matches timing records to submission tokens, and requires all six post-warmup samples. |

## Requirement disposition

| Requirement | Production path | Evidence | Automated result |
| --- | --- | --- | --- |
| M15-U6-05.1 transparency and depth | `FroxelFogPass`, Forward PBR, generated `MaterialSurface`, `RHISceneRendererPasses`, `RHIParticleRenderer` | Linear HDR readbacks compare mesh and particle output to the analytic integral at the fragment depth. Refractive black glass isolates foreground scatter. The particle fixture covers AlphaBlend, Additive, Cutout and Multiply. Material mutation verifies the first frame ignores prior media. | PASS |
| M15-U6-05.2 no-light and authoring | `FroxelFogContract`, Scene extraction and codec, SceneSerializer, Inspector, Editor, Runtime, RenderService | The scene renders a visible sky without environment lighting. Unlit injection and integrated radiance are exactly zero with positive extinction. Point-only lighting gives positive scattering. Unit tests exercise scene serialization and Inspector. Runtime toggles a deserialized component in the real application loop and checks actual pixels. | PASS |
| M15-U6-07.5 integrated memory | `RHIViewTargets`, `RHITexture`, `FroxelFogContract` | The native scorecard checks the actual allocation requirements for scattering, integrated and both history textures. Shared opaque color and depth copies already exist without fog and appear separately. This is a resource allocation measurement for one view. | PASS |
| M15-U6-07.6 GPU pass costs and fallback | Native query collection, `FroxelFogPass`, backend policy and RenderService | Six warmed samples per tier and resolution. `FogCopy`, `FogInject`, `FogReproject`, `FogIntegrate` and `FogApply` are measured separately. MSAA and OpenGL verify their respective fallback diagnostics. | PASS |

The arbitrary-HLSL fallback leaves the material renderable and publishes `froxel_custom_transparency_unsupported` when its shader has no froxel ABI. The generated graph template keeps its existing material opacity and refraction policy. Multiply particles preserve the fixed-function blend contract and fade the multiplier toward one using foreground transmittance.

## Verification commands

The working logs and readbacks are under `build/agent-runs/M15-U6-E/codex`. Configuration and backend directories contain the process logs. The Debug froxel-only reruns are in `Debug/dx12-focus` and `Debug/vulkan-focus`. The completed matrix writes `native-results.txt` per configuration.

```powershell
cmake --build build --config <Config> --target AxleEnvironmentRenderingTests AxleRuntimeRenderTests AxleRHIResourceViewTests AxleRHIStateTests AxleRenderServiceTests AxleRenderUnitTests AxleVFXUnitTests AxleSceneDataUnitTests AxleSceneMutationUnitTests AxleEditorCutoverUnitTests AxleRuntimeProductParityUnitTests AxleEditor AxleRuntime -- /m:5 /v:minimal
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> <output-directory>
build/Tests/Debug/AxleEnvironmentRenderingTests.exe <backend> <output-directory> --m15-froxel-fog
build/Tests/<Config>/AxleRuntimeRenderTests.exe <backend>
build/Tests/<Config>/AxleRuntimeRenderTests.exe <backend> cooked
build/Tests/<Config>/AxleRHIStateTests.exe <backend>
build/Tests/<Config>/AxleRHIResourceViewTests.exe <backend> storage
build/Tests/<Config>/AxleRHIResourceViewTests.exe <backend> targets
build/Tests/<Config>/AxleRenderServiceTests.exe <backend>
build/Tests/<Config>/AxleRenderServiceTests.exe gl
build/Editor/<Config>/AxleEditor.exe --project=<copied-FroxelSlit.axleproj> --verify-startup --rhi=<backend> --no-fallback --rhi-validation
ctest --test-dir build -C <Config> --output-on-failure -R "^(SceneMutationUnitTests|SceneDataUnitTests|M15FroxelFogContract|M15HiZContract|M15OcclusionContract|M15ReflectionCaptureContract|RenderHarnessFailure)$"
```

Commands run from the repository root with the current Runtime and PluginSDK binary directories added to `PATH` for Runtime. The full environment suite also exercises the production GTAO, Forward PBR, generated surface and reflection callers that share the affected layouts. The named backends are `dx12` and `vulkan`. GPU timing and comparison targets are 1920 × 1080 and 2560 × 1440 on an RTX 3070. The scope is a hardware spot measurement with four warmup frames and six samples per row. Separate shared color/depth allocations already exist for other renderer consumers.

Debug and Release builds and all 17 matrix launches per configuration exited 0. Both additional Debug froxel runs exited 0. The six direct CPU binaries exited 0 in each configuration, and focused CTest passed 7/7 in each. Standalone DXC and `spirv-val` passed ten shader entry points for DXIL and Vulkan 1.3 SPIR-V. The four Editor startup runs opened the copied authored fog project and rendered five verified UI frames each. Every D3D12 and Vulkan Runtime fixture, loose and cooked, measured the center red channel at 213 with fog and 231 after disabling the component. The additional Debug OpenGL Runtime run passed without a fog-induced pixel change.

## Integrated allocation and timing snapshot

Release at 1920 × 1080 on the RTX 3070. Milliseconds sum the five fog scopes. The four device allocations count only the per-view 3D volumes. At this resolution the shared copies occupy another 17,694,720 color bytes and 8,847,360 depth bytes on each backend.

| Tier | D3D12 volume allocation (MiB) | Vulkan volume allocation (MiB) | D3D12 fog GPU ms | Vulkan fog GPU ms |
| --- | ---: | ---: | ---: | ---: |
| Low | 3.75 | 3.75 | 3.07 | 2.99 |
| Medium | 18.00 | 13.50 | 4.45 | 3.04 |
| High | 30.00 | 30.00 | 5.82 | 2.44 |
| Ultra | 112.50 | 95.63 | 6.26 | 4.10 |

The complete 1080p and 1440p scorecards are `M15_U6E_froxel_scorecard.txt` in the run directories.

## Outside the U6-E acceptance matrix

The standalone Debug VFX suite exits 1 on both native backends at the particle instance-growth fault injection in `Tests/src/Test_VFXRendering.cpp:597`. The earlier delivery recorded this same failure on the pre-U6-E baseline. Source comparison confirms the unchanged early return from emitter preparation when instance-buffer allocation fails. The fixture runs with fog disabled. Codex reproduced the current failure and did not rebuild the historical baseline. The preceding descriptor-isolation and submission-order checks pass on both backends. Logs are `Debug/standalone-vfx-D3D12.log` and `Debug/standalone-vfx-Vulkan.log`.

The separate `AxleEnvironmentRenderingTests.exe gl <output-directory>` process exits 1 at WGL context creation with GLFW error 65542 in both configurations. Its skybox and IBL assertions are NOT RUN. The U6-E OpenGL fallback is verified on a real OpenGL 4.5 context by `AxleRenderServiceTests.exe gl` in both configurations and by the Debug Runtime run.

The scoped review does not close the deferred interactive Editor visual checks or other milestone packages.
