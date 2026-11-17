# M15 U4-E independent Codex review

Reviewed Gemini `c93a60894434fcd00cd40209ac266aceca0d1db1`, based on
`80e9837a`, replying to `CG-20261117T044932470410Z-GEMINI-c0038b`.
The released scope is U4-07.1 and U4-07.2. Codex owns the repairs and executed
the verification below; Gemini's original report is historical self-verification.

## Verdict

**Automated U4-07.1/07.2 behavior accepted after corrections.** No known code
blocker remains. Interactive Editor visual inspection/capture is still **NOT
RUN**, so the task card retains that open gate. The prior U4-D Editor visual
gate is unchanged. This does not accept all of U4 or release another package.

One consolidated local Codex commit includes source, tests, the dedicated Editor
fixture, this review, PLAN and exchange. No push.

## Findings and repairs

### U4E-R1 - P1 - Degenerate mapped tangents produced NaN ScreenNormal - FIXED

`ForwardPBR.hlsl` and `SurfaceNormal.hlsl` normalized the Gram-Schmidt tangent
without checking its length. A tangent parallel to the geometric normal gives
a zero vector, then NaNs in the normal basis. The canonical graph already had
a safe orthogonal fallback, so the production paths disagreed.

The new real-renderer regression failed on the original shaders:
`build/Codex-U4E-before-tangent-fix.log`, exit **1**, reports
`finite mapped ScreenNormal`. Both shaders now choose the same orthogonal
fallback as the canonical graph before normalization. Native degenerate-tangent
frames are finite on D3D12 and Vulkan in both configurations.

### U4E-R2 - P1 - Native fixtures did not exercise their claimed behavior - FIXED

The added `normal-map-*` draws reused a material with no enabled normal texture;
the canonical material returned `(0,0,1)`. Renaming the draws did not test normal
mapping. The deformed triangle used a fixed depth guide of 0.5 while its depth
changed, permitting the neutral missing-guide fallback. No actual prepass,
moving depth edge, reconstruction or disocclusion was measured.

Those draws were removed. `Test_M15_OcclusionSurfaceRendering.cpp` now exercises
the actual scene renderer, normal prepass, GTAO dispatch and forward composition:

- Built-in materials bind uploaded linear RGBA8 normal textures. The canonical
  graph samples identical TGA values through its reflected texture/sampler
  providers. Diagnostics require a valid custom generation with no fallback.
- ScreenNormal is compared against analytically decoded values at strengths
  0, 1 and 3. A second extreme texture at supported strength 4 crosses the
  view hemisphere: **72 back-facing source samples per pipeline** verify AO
  exactly 1. All ScreenNormal, depth and HDR channels used by the tests are
  checked for finiteness, and AO/contact remain in [0,1].
- Actual forward HDR changes with the map: built-in center radiance is
  **0.819336 / 0.562012 / 0.319092** at strengths 0/1/3. Front-facing
  built-in/canonical full-image parity error is at most **0.0012207**.
  Back-facing color-orientation policy is not part of this parity claim.
- Native mapped direct-light AO toggles and mapped emissive AO/contact toggles
  have **zero HDR difference**, with nonzero direct and HDR emissive controls.
  Mirrored/nonuniform model transforms and degenerate tangents are also drawn.
- Published deformed vertex generations move across a genuine rear receiver.
  The current foreground depth reconstructs to its analytic plane within
  **0.001** world units; current normals match within **0.002**. The newly
  revealed receiver reconstructs at its own depth, rather than the old pose.
  Coverage is **572 / 646 / 572 pixels**, with **1218 disocclusion events**.
  Receiver minimum AO is **0.28418 / 0.288086 / 0.28418**, proving actual
  occlusion. Each reused view is compared with freshly allocated view targets:
  HDR difference is **zero**, and AO/contact agree within **0.001**.
  A generation-equality assertion ensures the moving pose genuinely retains
  the previous targets; `Configure` is called only when the descriptor changes.

`OcclusionTestSupport.hpp` shares the existing native upload/readback helpers,
adding explicit D32 depth-plane and scalar float readback. Test separation keeps
the established U4-A/B/C/D fixtures intact. The selected U4-07.2 branch is
**deformed geometry publication**, not execution of skeletal bone palettes.

### U4E-R3 - P2 - CPU verifiers could report success without validating inputs - FIXED

`VerifyDeformedMeshOcclusionStability` ignored its projection parameter entirely;
a NaN or zero projection passed. It now validates matrices, projection
invertibility and clip W, and performs a projection/unprojection round trip.
It also rejects nonfinite tangent handedness and invalid background depths.

`VerifyNormalMapOcclusionStability` accepted zero/NaN view directions and used a
fixed up axis that degenerates for vertical views. It now validates and normalizes
directions and chooses a nonparallel axis. CPU regressions require invalid
directions/projections to fail and a valid vertical view to succeed.

### U4E-R4 - P2 - Evidence overstated skinning and reused an unrelated Editor scene - CORRECTED

PLAN and the task card now distinguish CPU affine references, actual deformed
vertex publication, real normal mapping, native automated acceptance and visual
inspection. The new Editor project is
`Tests/Fixtures/M15/U4E/U4E.axleproj`: two genuinely normal-mapped spheres under
mirrored/nonuniform scale in an HDR environment. Debug and Release Vulkan
Editor load this **three-entity scene for five UI frames**, exit 0.

A current interactive attempt found the actual U4-E Editor window, but capture
failed with `IGraphicsCaptureItemInterop.CreateForMonitor (0x80070057)`;
accessibility state was also unavailable. Thus **interactive Editor visual
inspection/capture is NOT RUN**. Native images were inspected separately; they
do not replace this gate. The temporary Editor process was closed afterward.

### U4E-R5 - P1 - Detail normal prepass sampled BRDF/clearcoat textures - FIXED

`SurfaceNormal.hlsl` declared detail normal at t8 and detail mask at t9, while
the shared forward material descriptors bind them at **t11/t12**. t8 is the
BRDF LUT and t9 is clearcoat. Thus the prepass could write unrelated normals
even though ForwardPBR shaded the correctly bound detail normal, corrupting
occlusion dataflow for layered materials.

The actual detail-only fixture reproduced **1.13572** ScreenNormal error before
the fix (`build/Codex-U4E-before-detail-fix.log`, exit **1**). The shader now
uses t11/t12, matching the shared layout. Logical material-enable flag bits
8/9 remain unchanged. Native white-mask detail and black-mask neutral cases
compare ScreenNormal against analytic normals within **0.002**. The final
four-backend/configuration matrix below includes both cases.

## Independently executed verification

Run from `D:/WorkSpace/AxleEngine`. `<Config>` means each of Debug and Release;
`<backend>` means each of dx12 and vulkan. All final commands below exit **0**.
The four native runs are serial, on hardware RTX 3070 with validation enabled.
Changed executables were rebuilt before final runs.

```text
cmake --build build --config <Config> --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEditor --parallel 8
build/Tests/<Config>/AxleRenderUnitTests.exe --m15-occlusion-contract
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> -R ^M15OcclusionContract$ --output-on-failure
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/Tests/EnvironmentRendering/Codex-U4E-<Config>-<backend>
build/Editor/<Config>/AxleEditor.exe --project=D:/WorkSpace/AxleEngine/Tests/Fixtures/M15/U4E/U4E.axleproj --rhi=vulkan --verify-startup
python scripts/check_architecture.py
git diff --check
```

| Evidence | Path |
| --- | --- |
| Final target builds | `build/Codex-U4E-finalbuild-<Config>.log` |
| Focused/full CPU | `build/Codex-U4E-<Config>-{focused,full}.log` |
| Focused CTest | `build/Codex-U4E-ctest-<Config>.log` |
| Native full runs | `build/Codex-U4E-<Config>-<backend>.log` |
| U4-E numeric assertions | `build/Tests/EnvironmentRendering/Codex-U4E-<Config>-<backend>/M15_U4E_surfaces.txt` |
| Actual mapped images | same directory, `M15_U4E_{builtin,graph}-strength{0,1,3,4}.ppm` |
| Moving receiver images | same directory, `M15_U4E_deformed-{left,right}.ppm` |
| Applicable Editor startup | `build/Codex-U4E-Editor-<Config>-startup.log` |
| Interactive startup/capture attempt | `build/Codex-U4E-Editor-visual.{log,err}` plus tool error recorded above |

Both CPU configurations pass the focused and full suites and focused CTest.
All four native runs also preserve the earlier U4 reconstruction, odd/minimal
dispatch, horizon, HDR sphere, CSM, forward invariance and material parity gates,
with **zero native validation errors**. Architecture check passes **611 files /
25 CMake files**; whitespace checks pass. Existing LNK4098 library warnings are
unrelated to this correction.

The protocol-required separate-context reviewer rechecked the shader fixes,
shared readback, actual pipeline coverage, CPU projection/direction validation,
retained-generation disocclusion comparison, grazing/toggle additions and detail
normal/mask descriptor bindings. No
blocking finding remains. That recheck was source-only; runtime results above
were executed by Codex in this session.
