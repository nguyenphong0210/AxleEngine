# M15-U5-G independent Codex review

Reviewed 2026-11-18. Scope: S5.7, **M15-U5-40 through M15-U5-45.3** (15 requirements).
The live package is called U5-G; the historical PLAN batch row calls this U5-H.
S5.6/SSR was already accepted in U5-F. This review does not release S5.8.
Gemini delivery: `01af2f43`, recorded by `501f246c`, on `2c7e5cab`.
Reply to `CG-20261118T030554773663Z-GEMINI-50149c`.
The [Gemini report](M15_U5_G_REVIEW.md) records historical self-verification.
This document records Codex findings, repairs and independently executed checks.

**Verdict: automated U5-G/S5.7 scope accepted after repairs.** Interactive Editor checks 43, 44.4 and
45.1 remain user-deferred. Automated acceptance must not be read as full visual acceptance.

## Findings and repairs

| ID | Severity | Original behavior and correction |
| --- | --- | --- |
| U5G-R1 | P1 | The purported validated 8x8 GGX LTC table was an analytic lobe-width heuristic, with an unjustified diffuse-identity condition at roughness 1. Tests compared only diffuse form factors with quadrature. A new independent specular regression failed: roughness .8, normal view, LTC .064612 vs integrated GGX .0442639 (about 46% error). Replaced it with the authors' pinned 64x64 fitted GGX matrix/amplitude data. CPU and HLSL decode the same six half-floats per cell, bilinearly sampled by roughness and sqrt(1-NdotV). Added 18 dense independent GGX comparisons plus real GPU specular comparisons for both emitter shapes. |
| U5G-R2 | P2 | The disk was an area-preserving octagon. Sharp specular at its edge changed from .392956 to .558847 simply by rotating the tangent of a circular emitter. It now uses an area-preserving 32-segment contour and 36-vertex clipping capacity. A new rotation sweep bounds the remaining polygon approximation. |
| U5G-R3 | P2 | Disk range used radial projection onto the ellipse, which is not its Euclidean closest point for unequal axes. CPU/HLSL now solve a bracketed ellipse projection. An analytic eccentric off-axis test expects sqrt(13); a native eccentric-light fixture checks CPU/HLSL range agreement. |
| U5G-R4 | P2 | The new isotropic area path dropped existing builtin anisotropy/tangent rotation and GGX multiple-scatter compensation. Canonical area clearcoat was processed with the sun's half vector and had no area coat lobe. Restored builtin compensation and authored anisotropy through an explicitly bounded representative-point approximation, blended smoothly near zero. Both material paths now integrate clearcoat with each emitter's LTC lobe, excluding graph area lighting from the later sun-only coat transform. Native regressions require anisotropy strength/rotation response, near-zero continuity, and true graph/builtin coat parity with no sun or silent material fallback. |
| U5G-R5 | P2 | Assembly serialization retained the new light, but Assembly Editor never instantiated it in its private preview or captured its changes/removal. Both paths now carry RectLightComponent. A real panel test opens a disk-light assembly, extracts the light from preview, saves an edited width, then removes and saves the component. |

The fitted table is from [the authors' reference implementation](https://github.com/selfshadow/ltc_code/tree/31e5e96b54f98f33098f8503003119ba2231a1c6),
for *Real-Time Polygonal-Light Shading with Linearly Transformed Cosines*,
Heitz, Dupuy, Hill and Neubelt, SIGGRAPH 2016. The importer pins both commit and
source SHA256, retains the [upstream license](../../ThirdParty/ltc_code/LICENSE),
and reproduces the checked-in include. No runtime or build-time download, new
texture slot, descriptor binding, or external executable is added.

A separate-context reviewer rechecked significant corrections and affected callers.
Intermediate Codex repair defects were caught and repaired before acceptance:
the old positive unused-LTC2-channel invariant and an importer BOM were removed;
two large static float tables returned zero amplitude in native DX12, so the final
representation packs the needed data into one bounded constant array. Native
tests also required neutral native-only material controls before selecting the
graph; the graph's own coat remains authored in its shader. These are correction
validation findings, not additional original-delivery findings.

## Requirement coverage

All 15 requirements are accounted for below; execution evidence follows. No missing feature or failing automated gate remains in the assigned scope.

| Requirement | Implementation / affected caller paths | Independent evidence and disposition |
| --- | --- | --- |
| 40: validated LTC data | LTCTables.inc, importer, RenderContracts.cpp, PBRCommon.hlsli, both CMake embedding paths | Pinned importer reproduction/checksum; finite positive-determinant table contract; 18 specular quadrature cases and native GGX comparisons. **PASS (automated).** |
| 41.1: rectangle | CPU/HLSL contour evaluators; ForwardPBR / MaterialSurface | Horizon-straddling diffuse quadrature; native rectangle and specular integration checks. **PASS (automated).** |
| 41.2: disk | 32-segment CPU/HLSL contour and ellipse range | Disk/square area ratio, sharp-edge tangent rotation, circle symmetry, native disk/specular checks. **PASS (automated).** |
| 41.3: units/orientation | component, extraction, LightingService, RHILight packing, basis helpers | Existing collinear/degenerate/non-unit and invalid dimension tests independently rerun. **PASS (automated).** |
| 41.4: sidedness/range | signed intensity, closest-emitter range and GPU packing | CPU/native one-sided, two-sided and range cutoff; analytic eccentric distance; native eccentric range. **PASS (automated).** |
| 42.1: diffuse | shared CPU/HLSL integration and both material shaders | Independent surface quadrature; native builtin/canonical parity. **PASS (automated).** |
| 42.2: specular | fitted matrix/amplitude, material compensation, aniso fallback, per-emitter coat | Independent CPU/GPU GGX reference, gold partition, roughness sweep, aniso strength/rotation/continuity, graph coat parity without fallback. **PASS (automated).** |
| 43: shadow policy | UnshadowedAnalyticLTC enum, lighting frames and Inspector | **PASS (automated)**: source audit and tests retain explicit unshadowed policy/no local atlas allocation. Interactive visualization NOT RUN/user-deferred. |
| 44.1: component | RectLightComponent, runtime codec and copy path | Component defaults/shape/parameters exercised by extraction, scene and real preview tests. **PASS (automated).** |
| 44.2: extraction | SceneExtraction -> RenderWorld -> LightingService -> RHILight -> both shaders; ComputeLightingStamp | Independent CPU/native checks; existing 64-byte / 2304-byte GPU layouts retained. **PASS (automated).** |
| 44.3: serialization/runtime | SceneSerializer, AssemblySerializer, AssemblyEditorPanel, runtime simple codec | Scene and assembly round-trips plus real panel preview/edit/save/removal. **PASS (automated).** |
| 44.4: Inspector | EntityInspector, hierarchy creation, icon | **PASS (source/build integration)**: source audit of all light controls; strict Editor startup exercises integration. Interactive editing/capture NOT RUN/user-deferred. |
| 45.1: roughness/view | CPU matrix and native roughness/material regressions | **PASS (automated)**: three roughnesses, three view cosines, both shapes; sharp disk edge and GPU peak/wing checks. Interactive capture NOT RUN/user-deferred. |
| 45.2: emitter angles | emitter basis, sidedness and contour evaluation | Existing CPU/native tilt checks and independent GGX angle matrix rerun. **PASS (automated).** |
| 45.3: numerical reference | dense surface quadrature, independent native reference assertions | Diffuse plus actual specular comparisons; raw-fit fixtures use neutral compensation LUT so the oracle remains single-scatter. **PASS (automated).** |

## Final verification

Final Debug and Release twelve-target builds completed with exit 0:
`AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor AxleMaterialGraphTests AxleMaterialGraphCompilerTests AxleMaterialGraphOpenGLPolicyTests AxleMaterialGraphSlangTests AxleMaterialGraphRuntimeTests AxleMaterialGraphResourceTests AxlePluginSessionTests`.

Build logs: `build/Codex-U5G-{Debug,Release}-build-final-all.log`.
The refined unit assertions were subsequently rebuilt in both configurations
(`*-BRDF-unit-build.log`). One source assertion had retained the delivery's count
of three anisotropic GGX call sites; restoring the fourth area call site made it
fail. It now requires four sites and at least five compensation uses. The focused
and full CPU checks were rerun against the rebuilt executables. This did not
weaken any numeric gate.

**Final verification matrix: 34/34 commands exit 0**, with exact executable,
arguments, exit code and log in `build/Codex-U5G-results.json`:

| Check | Independently executed result |
| --- | --- |
| Focused Hi-Z, reflection-capture and lighting-services CPU suites; full CPU suite | Debug/Release PASS |
| CTest `M15HiZContract|M15ReflectionCaptureContract|M15EnvironmentContract|MaterialGraph` | 9/9 each configuration |
| Environment bake | Debug/Release PASS |
| RHI storage and target views | Debug/Release x DX12/Vulkan PASS |
| Full native environment rendering, retaining prior U4/U5 suites plus U5-G | Four runs PASS; zero asserted RHI validation errors |
| Actual Assembly Editor preview/edit/save/removal via PluginSessionTests | Debug/Release PASS |
| Strict Editor empty and U4E fixture startup, no backend fallback | Eight runs PASS; startup is not interactive visual evidence |
| Architecture check | 613 source files / 25 CMake files PASS |
| Pinned importer reproduction and py_compile | PASS; generated include and normalized complete license match byte-for-byte |
| Exchange validation and git diff --check | PASS |

Native artifacts: `build/Tests/EnvironmentRendering/Codex-U5G-{Debug,Release}-{dx12,vulkan}`.
The earlier CPU assertion failures and the malformed leading-BOM diagnostic in the
local runner are retained as intermediate evidence;
the final CPU rerun matrix records the corrected checks. Source-only correction
rechecks are separate from these root-executed results.

Representative final measurements (both configurations/backends):

- Rectangle HDR peak .08/.35/.75: 1.89723 -> 1.17199 -> .104609;
  wings .000021739 -> .00728012 -> .0468298.
- Independent native GGX specular: rectangle .35 = 1.23043 vs 1.23489,
  rectangle .75 = .11002 vs .126068; disk .35 = 1.16023 vs 1.16562,
  disk .75 = .0885474 vs .10298. The fit oracle uses 20% relative + .002 absolute tolerance;
  all 18 CPU and four native cases per run pass.
- Disk edge tangent phases between contour vertices: .465245..466255 after repair,
  versus .392956..558847 with the original octagon.
- Canonical/builtin uncoated disk: .0593652 == .0593652; area clearcoat:
  .171001 == .171001, with graph-native policy controls neutral and no shader fallback.
- Anisotropy rotation image delta 21938.6 and strength delta 28946.5 against total
  red image energy 33819.3; near-zero estimator transition also passes.

Correction commits:

- `af1fade3`: validated LTC lighting and preserved area material response.
- `d9a11a7c`: preserved area lights in Assembly Editor previews.

## Boundaries and remaining checks

- Interactive Editor 43, 44.4 and 45.1 stay open in PLAN for the consolidated post-U5 pass.
- Area lights remain explicitly unshadowed; LTC does not provide blocker shadows.
- Disk contours remain a bounded 32-segment approximation, rather than an analytic ellipse integral.
- Authored builtin anisotropy retains a representative-point approximation. A new anisotropic LTC fit is not implemented or claimed. The isotropic GGX table is an approximation; tested error bounds are recorded with the actual measurements, not exact equality.
- Area-light capacity remains the existing two per view. No visible emitter mesh is automatically added.
- Packed data is 4096 cells of three uint32 words (48 KiB CPU storage; up to 64 KiB shader constant alignment). No extra per-view render targets or sampled textures are needed. Embedding this data increases compiled material-shader size and cold pipeline compilation work; a separate performance budget is not claimed by these correctness gates.
- Existing compiler/NRI warnings are separate from asserted validation errors. The user-canceled GTAO halo investigation remains outside scope.
- Local commits only; no push or later-package implementation.
