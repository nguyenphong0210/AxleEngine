# C13-01 native cascade delivery

Date: 2026-11-11. Baseline: `a0b51d7` (CPU checkpoint).
Status: C13-01.2 and C13-01.3 complete within the supported built-in PBR scope.
C13-01 is accepted; C13-02 onward remain unreleased.

## Implementation

`RHICascadedShadows` owns a D32 depth atlas, depth-only shaders/pipelines,
per-cascade camera buffers, cached caster material buffers and descriptors.
It consumes the verified CPU contract with three/four camera-space cascades,
fixed-world texel snapping and right-handed [0, 1] light depth. The CPU fit now
explicitly includes the preceding split's final 10%, matching receiver blending.

The modern RenderGraph records `CascadedShadows` before `OpaqueAndSky` and
transitions stored depth to shader-read for opaque and transparent receivers.
Forward PBR samples a 5x5 comparison kernel, clamps taps inside each atlas tile,
blends split boundaries and fades at max distance. Only primary-sun direct
radiance is shadowed; ambient/IBL and other lights retain their existing paths.
Base/slope bias uses normalized shadow depth, normal offset uses world units,
and PCF spacing is clamped to 0..2 texels to stay within the CPU guard footprint.

Modern capability reporting now advertises CSM and RenderService reports actual
format support and frame diagnostics. Authored light data and serialization are
preserved. `RenderDirectionalLight` bias defaults now match the scene component.
`SceneRenderSettings::ShadowCascadeCount` selects three or four for modern RHI.

Opaque and alpha-cutout static meshes cast shadows, honoring mesh flags,
material culling, UVs, alpha cutoff, procedural/vertex alpha and albedo alpha.
Transparent/additive meshes receive built-in PBR sun shadows but do not cast
solid depth. Arbitrary custom HLSL has no CSM interface and is skipped as a
caster with `CSM_CUSTOM_SHADER`; a custom shader that falls back to built-in PBR
can receive the built-in effect. This diagnostic also covers non-casting custom
receivers. Skinned meshes remain outside the modern renderer's existing support.

Settings require valid finite values and positive camera near depth. Max shadow
distance is capped to camera far clip. Non-positive near clips, including the
editor's default orthographic range, render without CSM and report
`CSM_CAMERA_RANGE`. Unsupported D32 attachment/sampling reports
`CSM_UNSUPPORTED_FORMAT`. Atlas creation failure rejects the view, retains the
prior allocation and permits retry. The quality tiers use 1024/2048/4096 pixels
per tile in a 2x2 atlas (2048/4096/8192 overall).

The OpenGL compatibility renderer, its shaders and its `ShadowMap` are unchanged.
C12 graph/custom layout behavior and C11-07 rollout deferrals are preserved.
The embedded fallback forward shader now uses canonical ForwardPBR source,
including inlined PBRCommon, so missing/stale on-disk shader resources cannot
silently select the older shader without the cascade receiver interface.

## Ownership and correction review

Prepare runs after the owning view frame has waited for prior submission.
Actual command bindings pin atlas, pipeline/layout, mesh buffers, material
uniforms and descriptor dependencies. Abandon does not publish partial work.
Stable frames reuse native caster resources. Atlas replacement is transactional.
A tiny depth fallback is initialized once; disabled views sample it and record
no shadow pass, including after Ultra quality.

Separate-context read-only Codex reviewer `c13_contract_review` found and
rechecked these corrections; final source recheck PASS, no outstanding findings:

- **C13-GPU-R1 (P1):** skybox changed bindings before transparent draws. Restore
  built-in layout, pool, scene/light and cascade CBVs before the transparent pass.
- **C13-GPU-R2 (P2):** disabled views cleared retained full-size depth. Use the
  initialized 2x2 fallback and skip shadow recording.
- **C13-GPU-R3 (P2):** unused caster cache entries retained removed meshes and
  textures. Release unused owners and replace their descriptor texture
  dependencies. Refresh all cached native material sets on atlas generation
  changes so unused sets cannot retain retired Ultra atlases.
- Move unsupported-custom-receiver diagnosis ahead of the caster flag filter.
- **C13-GPU-R4 (parent audit):** the push-constant union defaults to a legacy
  sun vector whose W could be interpreted as Cutout. Set an explicit Opaque
  surface before optional native-material overrides. The strengthened native
  regression uses solid mode, zero color alpha and no native material, ensuring
  the old accidental Cutout path would write no depth. Separate-context code
  recheck PASS; final native runs include this case.

Reviewer reran CPU Debug CTest and diff whitespace checks. Native evidence below
was executed by the implementing parent, not claimed as reviewer/Gemini runs.

## Verification

Executed against final corrected source on the physical NVIDIA GeForce RTX
3070, with NRI and available API validation enabled and no software/API fallback:

| Command | Result |
| --- | --- |
| `cmake -S . -B build -DAXLE_ENABLE_CASCADE_GPU_TESTS=ON` | Exit 0; existing Slang/material GPU and OPT-11 lifetime options retained |
| `cmake --build build --config Debug --target AxleAllTests AxleEditor AxleRuntime --parallel 6` | Exit 0 |
| Release focused target build (listed below) | Exit 0 |
| `ctest --test-dir build -C Debug --output-on-failure` | Exit 0; **109/109**, final run 247.42 seconds |
| Release focused CTest selection (listed below) | Exit 0; **18/18**, final run 25.94 seconds |
| `build/Tests/Debug/AxleTests.exe 73` | Exit 0; existing OpenGL CSM contract regression |
| `build/Tests/Debug/AxleRenderServiceTests.exe gl` | Exit 0; actual OpenGL compatibility service |
| `python scripts/verify_cascade_images.py build/Tests/CascadedShadows/Debug` | Exit 0; PASS |
| `python scripts/verify_cascade_images.py build/Tests/CascadedShadows/Release` | Exit 0; PASS |
| `python scripts/check_architecture.py` | Exit 0; PASS, 564 source / 25 CMake files |
| `python scripts/agent_exchange.py validate` | Exit 0; PASS |
| `git diff --check` | Exit 0 |

Exact Release commands:

```powershell
cmake --build build --config Release --target AxleCascadedShadowRenderingTests AxleCascadedShadowMapTests AxleRenderUnitTests AxleMaterialGraphCompilerTests AxleMaterialGraphRuntimeTests AxleMaterialGraphResourceTests AxleMaterialGraphSlangTests AxleMaterialGraphOpenGLPolicyTests --parallel 6
ctest --test-dir build -C Release -R '^(CascadedShadow|RenderUnitTests|MaterialGraph(Compiler|OpenGL|Runtime|Resource|Slang|GPU))' --output-on-failure
```

Native CSM tests in each configuration pass with zero API/NRI validation errors.
Each backend writes depth in all four test cascade tiles. The test fixture
observes **54 visibly darkened pixels** in its 128x128 on/off image comparison.
Textured cutout reduces written depth pixels from **48,401 to 38,617**. These are
fixture measurements, not scene-wide quality/performance estimates.

| Image pair (D3D12 versus Vulkan) | Debug max / mean channel error | Release max / mean channel error |
| --- | --- | --- |
| Shadows off | 1 / 0.00004069 | 1 / 0.00004069 |
| Shadows on | 1 / 0.00004069 | 1 / 0.00004069 |
| Textured cutout | 1 / 0.00004069 | 1 / 0.00004069 |
| Sky plus transparent receiver | 0 / 0 | 0 / 0 |

Bounds are max <=2 and mean <=0.1 in 8-bit channel units. Final images were
visually inspected, including the cast shadow, cutout holes and sky/transparent
case. PPMs are direct native readbacks; PNGs used for viewing are only converted
and enlarged copies.

Logs: `build/c13_final_build_debug.log`, `build/c13_final_build_release.log`,
`build/c13_final_native_fixture_{debug,release}.log`,
`build/c13_final_ctest_{debug,release}.log`,
`build/c13_final_ctest_{debug,release}_console.log`,
`build/c13_final_gl_csm_debug.log`, `build/c13_final_gl_service_debug.log`,
`build/c13_final_parity_{debug,release}.log`.
Existing MSVC delay-load/runtime-library and Vulkan unused-attribute warnings
remain; neither native test reports validation errors.

Physical fixtures exercise depth writes in every cascade, image changes on
shadow enable/disable, light and caster flags, no-sun frames, three/four cascades,
constant and textured cutouts, bias application, sky plus transparent receiver,
HDR/FXAA/MSAA, every atlas quality tier, color/depth resize, real allocation
fault/retry, invalid cascade settings, camera policy, custom diagnostics,
removed owner/descriptor lifetime and the actual RenderService entry point.
`verify_cascade_images.py` checks four returned image pairs with explicit bounds
(max error <=2, mean <=0.1) and requires visible shadow darkening in both APIs.

Artifacts are generated under `build/Tests/CascadedShadows/{Debug,Release}/`:
`dx12/` and `vulkan/` contain RGB PPM readbacks; `parity.json` records dimensions,
channel differences and image hashes. All GPU tests require real hardware and
return failure for missing prerequisites; CTest registration is opt-in through
`AXLE_ENABLE_CASCADE_GPU_TESTS`.

## Limits

No full OPT-13 lighting-stack performance claim, ray tracing, point/spot shadows,
modern skinning, arbitrary custom-material shadow interface or C13-02+ work is
included. GPU timestamp/performance budgeting belongs to C13-07. Full Release CTest and Release product builds were NOT RUN; Release verification
uses the focused targets above. Interactive manual Editor manipulation is not
inferred from executable builds or fixtures.
