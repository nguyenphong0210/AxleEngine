# M15 U9 independent advanced materials review

Gemini delivered `5c140f37..7568cd5e` and reported additional Editor captures in `CG-20261121T023647247766Z-GEMINI-198b3b`. Codex reviewed the integration from `cf4376da` and corrected it in `6d6ace9b`. The native executions and Editor replays below are independent evidence.

## Findings and fixes

| Finding | Correction |
|---|---|
| The built-in SSS lobe stopped abruptly at the light terminator. The preview also cropped the material sphere and gave refraction no opaque reference. | The forward and graph shaders use the same wrapped diffuse function independently of the conventional BRDF cutoff. Native Material Preview frames the full sphere and provides an opaque striped reference for refractive assets. |
| SSS could cross coplanar objects with equal depth, normal and scattering profile. | Each participating draw exports a key split across two exact FP16 alpha components. Both blur passes compare the key and the authored profile, depth and normal. The same-profile seam fixture reaches the lit edge while the neighboring draw stays dark. |
| SDR output quantized or clipped specular and emissive terms before the subsurface composite. Non-RGBA8 SDR views could also receive a pipeline compiled for the wrong target format. | A dedicated FP16 target keeps sharp radiance linear until after diffusion. The composite uses the view's actual render format and preserves alpha and non-scattering pixels. The BGRA8 fixture requires the composite to execute and checks high emissive radiance against the forward reference. |
| Material Graph SSS depended on enabling SSS on the fallback material. A cooked generated shader could also fall back when no loose HLSL was available. | Scene, Preview and renderer target setup consider an authored opaque/cutout shader. Generated companions bind the draw key through the reflected engine buffer. Custom shader cache reads and fingerprints packaged source through the VFS. A native fixture compares explicit, implicit and package-only graph output against the same reference, with the base material's SSS disabled. |
| One vertical projection scale and a shared UV radius cap warped the blur footprint on rectangular and orthographic views. | Each axis uses its projection coefficient. The cap uses the short side in pixels, and depth scales the radius only for perspective. |
| Custom shader compatibility depends on the companion outputs. | The main, normal and reflection entry points of earlier generated shaders remain usable. Regeneration upgrades their SSS output layout. Legacy helpers retain their earlier signatures so authored shaders can still compile against the shared include. |

## Requirement-to-evidence matrix

| M15 U9 requirement | Independent evidence |
|---|---|
| 01 and 08.1 · Charlie sheen | Native fixture checks grazing response, roughness sweep, direct/point/LTC area response and a white furnace. CPU contract checks the shared layered energy implementation. |
| 02, 03 and 08.2 · Absorption and dispersion | Native readbacks measure Beer-Lambert transmittance and Cauchy RGB fringe, with zero leakage into opaque output. The path and thickness model are documented in the [refraction policy](../milestones/MILESTONE_15_LIGHTING_MATERIALS/REFRACTION_ABSORPTION_POLICY.md). |
| 04.1–04.3 and 08.3 · SSS diffusion and boundaries | Native fixture measures terminator lift and checks the depth silhouette, material seam and same-profile coplanar seam. The native side-lit sphere checks continuity across the terminator before diffusion. Editor replays show the wax silhouette and adjacent slate. |
| 05 · Preserve the specular and emissive lobes | Native fixtures compare HDR specular peaks and exercise the native BGRA8 composite with over-range emissive input. |
| 06.1–06.3 · Asset, component and GPU | Material and U9 contracts cover defaults, bounds, native asset and component round trips, and buffer packing. A serialized scene is reopened, extracted and rendered on both backends. |
| 06.4–06.5 · Inspector and Asset Preview | Editor replays select the saved wax and glass assets and show the corresponding controls and live spheres. The glass preview uses the production refraction path against opaque reference stripes. Native Preview surface tests and the reopened-scene shader fixtures cover matching render paths. |
| 06.6 · Runtime | Reopened-scene GPU fixture, VFS-only generated shader fixture, and cooked Runtime product smoke on both backends. |
| 07.1–07.2, 08.4 · Graph lowering, diagnostics and parity | DXIL/SPIR-V graph contracts, native built-in/graph readback parity and implicit SSS scheduling with the base material disabled. Authored optics on opaque Material Graph surfaces continue to emit the documented unsupported diagnostics. |

## Reproduce and inspect

The [replay fixture](../../Tests/Fixtures/M15/U9_Materials/) includes the scene, two native materials and the UTF-8 capture plan. Copy its directory before starting the Editor. Run at the repository root with `AxleEditor.exe --project=<copied project> --rhi=<dx12|vulkan> --no-fallback --rhi-validation --capture-plan=<copied plan>`. The [capture guide](../agents/EDITOR_VISUAL_CAPTURE.md) describes the raw viewport and full Editor GPU readbacks.

Evidence root is build/agent-runs/codex-m15-u9. Both builds completed with exit 0. The [capture plan](../../Tests/Fixtures/M15/U9_Materials/capture-plan.txt) fixes the camera and exposure and runs a second viewport capture with SSR enabled.

| Run | Independent result |
|---|---|
| `ctest --test-dir build -C <config> -R '^(M15U9AdvancedMaterialContract\|M15U8PostTemporalContract\|M15DDGIContract\|MaterialGraphRuntimeTests\|MaterialUnitTests)$' --output-on-failure` | 5/5 in Debug and Release. Dedicated surface-material contract also passed from the repo root. |
| `AxleEnvironmentRenderingTests.exe <backend> <output> --m15-u9` | 4/4. The side-lit sphere reads 0.191895 and 0.196899 across adjacent terminator pixels before diffusion. Silhouette, original seam and same-profile seam leakage are zero. The specular peak is 590.309 with scattering off and 589.597 with it on. The sampled patch differs by at most 0.002873 relative between the graph and built-in SSS paths. The package-only graph matches the loose shader pixel for pixel. |
| `AxlePreviewRenderTests.exe <backend> surfaces` and `AxleRuntimeRenderTests.exe <backend> cooked` | 4/4 Preview and 4/4 cooked Runtime. |
| `AxleEditor.exe ... --capture-plan=<plan>` | 4/4. Every replay logs 60 successful views, 0 failed views, 0 Console errors and 0 nonfinite viewport pixels. Vulkan emits 10 unused vertex interface warnings. Raw viewport captures are 1472×891 and full Editor captures are 2560×1369. |
| `python scripts/check_architecture.py` and `git diff --check` | PASS. The architecture gate scanned 625 source files and 25 CMake files. |

Open `materials_viewport.png` for the plum sheen, wax edge and glass over reference stripes. The paired `sss_with_ssr.png` verifies that reflection and SSS attachments can run together. The two `preview_*_editor.png` captures show a fully framed wax sphere with a smooth transition across the light terminator and refractive glass bending the opaque preview bars. Each configuration stores its original PNGs under `<config>_<backend>/`. The exit ledger is `verification-final.txt`.

The code review, fixes and tests are complete. The final separate-context source recheck of the terminator and Preview adjustments remains pending because the available reviewer processes reported a revoked sign-in token. The earlier separate-context recheck covered the render targets, key packing, projection cap, package cache and legacy shader changes.

## Coverage

The advanced material fixture exercises D3D12 and Vulkan on one GeForce RTX 3070. Refraction and spectral offsets sample the opaque screen-color copy under the existing thickness policy. The SSS pass uses single-sample opaque/cutout views. Opaque custom shaders request targets conservatively because a shader may supply its own profile. Raw custom shaders without the matching companion can cause the existing view fallback. Generated self-contained HLSL works from a cooked package. SDK shader includes still use the existing DXC include loader.
