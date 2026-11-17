# M15 U4-D independent Codex review

Reviewed Gemini delivery `f6417e043d52b44208c5648ac0f4e90f252fe308`, based on
`020c32ed`, in response to `CG-20261117T030910598689Z-GEMINI-0c598d`.
Scope is M15-U4-06.1 through U4-06.5. Codex performed the corrections below
and independently executed the checks in this report. Gemini's original report
is historical self-verification, not evidence of independent acceptance.

## Verdict

**U4-06.1 through U4-06.4 accepted after corrections. Automated U4-06.5 passes;
the checkpoint's required interactive Editor visual gate remains open.**
No known code finding remains. The one consolidated Codex commit contains
production repairs, stronger tests, the reusable Editor fixture, and this review.
No subsequent U4 package is released by this verdict.

## Findings and corrections

### U4D-R1 — P1 — Unoccluded surfaces were nearly black under GTAO — FIXED

`Engine/Resources/Shaders/GTAO.hlsl` used a sine expression that evaluates to
zero for an open, face-on plane. It also maximized horizon angles rather than
their cosines, used a nonorthogonal slice basis, and reconstructed interpolated
depth at a different surface position. The defect predates this delivery but
directly violates the new unoccluded-sphere requirement.

Gemini's own DX12 and Vulkan PPMs show the sphere center changing from
`(241,147,99)` in `red.ppm` to `(8,3,2)` in `M15_U4D_sphere.ppm`.
Codex reproduced failure with a new native comparison before fixing the shader:
`Codex-U4D-before.log` exits **1**, with a 0.994844 relative loss over 1264 sphere
interior pixels. That initial comparison read FP16 storage containing the
renderer’s display-mapped shading; the final fixture below explicitly reads
linear radiance instead.

The corrected search maximizes projected horizon cosines, clips signed arcs to
the normal hemisphere, and integrates the cosine-weighted visible arc using
`F(theta,gamma) = (cos(gamma)-cos(2*theta-gamma)+2*theta*sin(gamma))/4`.
Loss is normalized by the same open-hemisphere quadrature, so an unobstructed
slice contributes zero occlusion independent of four-slice sampling bias.
The slice basis is orthonormal with screen Y inverted. Exact depth loads use
their corresponding pixel-center positions. Tangent-plane rejection prevents
convex self-occlusion; back-facing normals receive neutral AO. U4-A/B composition,
contact policy and the bounded post-composite fallback are preserved.

Direct dispatch of the production CS verifies open flat/tilted planes and sky
at AO **1**, while a real foreground depth step produces minimum AO **0.252197**
on the rear plane. This prevents a hardcoded-neutral implementation from passing.

### U4D-R2 — P1 — Sphere checks allowed a severe rendering regression — FIXED

`Tests/src/Test_EnvironmentRendering.cpp` checked only a nonzero red byte and
two adjacent red differences. Those checks passed the near-black image and
could not establish HDR finiteness, reflection preservation or smooth curvature.

`RunM15OcclusionSphereTests` now uses the actual renderer, a directional gradient
HDR environment and a metallic sphere. `EnableToneMapping=true` selects linear
forward shading; the fixture reads `GetIntermediateHDRTarget()` **before** tone
mapping and requires reference radiance above one. It checks finite RGBA for
all pixels, reflection preservation across 1264 interior pixels, AO-only,
contact-only, combined and disabled-again renders. Native normals are compared
with analytic sphere curvature across the interior. Display artifacts apply
ACES/gamma once to the measured linear image.

All four final runs have worst relative reflection loss **0.000944316**
(0.0945%), maximum absolute HDR difference **0.00195312**, and maximum analytic
normal error **0.00834931**. Contact-only and disabled-again differences are
exactly zero. Native built-in and canonical graph fixtures also exercise
disabled contact with partial/fully blocked CSM and a 3x3 emissive AO/contact
sweep; all RGBA channels are checked for finiteness.

### U4D-R3 — P2 — Contract verifiers rejected valid disabled AO and missed mixed emission defects — FIXED

`VerifyUnoccludedSpecularInvariance` expected an ambient difference even when
AO was disabled and could accept nonfinite mixed radiance because comparisons
with NaN are false. It now respects `EnableAO`, rejects nonfinite values,
retains legacy direct-specular inputs and uses a scale-aware tolerance.
`VerifyEmissiveInvariance` now compares mixed lighting with/without authored
emission across AO/contact pairs, instead of merely checking mixed finiteness.
CPU regressions cover disabled AO with nonzero ambient, invalid mixed radiance,
and disabled contact at multiple CSM visibilities.

### U4D-R4 — P2 — PLAN claimed a nonexistent Editor capture — DOCUMENTATION FIXED; VISUAL GATE OPEN

The original PLAN marked U4-06.5 complete and said an interactive Editor capture
was recorded; the delivery report explicitly deferred that capture. Codex has
removed that claim and left the checkbox open.

The committed fixture is `Tests/Fixtures/M15/U4D/U4D.axleproj`, containing one
metallic sphere, a deterministic HDR gradient, and no sun. Debug and Release
Editor build and load the actual two-entity scene for five UI frames using
explicit Vulkan startup verification. A requested DX12 Editor startup cannot
obtain `SwapChainInterface` on this session and selects Vulkan; its exit zero
is not counted as DX12 Editor visual evidence.

Interactive capture was attempted, not silently skipped: the computer-use
helper reports an unavailable native pipe; the alternate native app tool finds
the Editor but capture fails with
`IGraphicsCaptureItemInterop.CreateForMonitor (0x80070057)`, including a retry
after restoring the window. Thus **Editor screenshot/visual inspection is
NOT RUN**. Native PPM inspection showed the reflected gradient and does not
replace this gate. The disposable Editor process was closed afterward.

## Independently executed verification

Commands run from `D:/WorkSpace/AxleEngine`. Every final command below exits **0**.
`<Config>` means each of `Debug`, `Release`; `<backend>` means each of `dx12`,
`vulkan`. Native GPU runs were serial and used hardware RTX 3070 with validation.

```text
cmake --build build --config <Config> --target AxleEnvironmentRenderingTests AxleRenderUnitTests AxleEditor --parallel 8
cmake --build build --config <Config> --target AxleEnvironmentRenderingTests --parallel 8
build/Tests/<Config>/AxleRenderUnitTests.exe --m15-occlusion-contract
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> -R ^M15OcclusionContract$ --output-on-failure
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/Tests/EnvironmentRendering/Codex-U4D-<Config>-<backend>
build/Editor/<Config>/AxleEditor.exe --project=D:/WorkSpace/AxleEngine/Tests/Fixtures/M15/U4D/U4D.axleproj --rhi=vulkan --verify-startup
python scripts/check_architecture.py
git diff --check
```

The second build includes the final linear-HDR fixture correction. CPU focused,
full suites and CTest pass in both configurations. Native runs include the
existing 14 reconstruction cases, real odd/minimal dispatch including
1919x1079, built-in/canonical graph forward invariance, HDR sphere regression,
CSM and material parity (zero center difference), with zero validation errors.
Architecture check passes: **611 files / 25 CMake files**. Existing LNK4098
runtime-library warnings remain unrelated to this repair.

Evidence root: `build/Tests/EnvironmentRendering/` (generated, ignored).

| Evidence | Path under evidence root |
| --- | --- |
| Final full target builds | `Codex-U4D-finalbuild-<Config>.log` |
| Final fixture rebuilds | `Codex-U4D-HDR-build-<Config>.log` |
| CPU focused/full | `Codex-U4D-<Config>-{focused,full}.log` |
| Focused CTest | `Codex-U4D-ctest-<Config>.log` |
| Native full run | `Codex-U4D-<Config>-<backend>.log` |
| Forward isolation values/errors | `Codex-U4D-<Config>-<backend>/M15_U4AB_forward.txt` |
| Compute open/blocker results | `Codex-U4D-<Config>-<backend>/M15_U4D_horizons.txt` |
| HDR sphere loss/normal errors | `Codex-U4D-<Config>-<backend>/M15_U4D_sphere.txt` |
| Native display captures | `Codex-U4D-<Config>-<backend>/M15_U4D_sphere-{off,ao,contact,both,disabled-again}.ppm` |
| Editor scene startup | `Codex-U4D-Editor-<Config>-vulkan-startup.log` |

The protocol-required separate-context reviewer rechecked the significant
shader/math corrections, affected fixtures and final pre-tone-map readback.
It found no blocking issue. That review was source-only; runtime results above
were executed by Codex in this working session.
