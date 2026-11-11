# C13-02 runtime lighting ownership correction

User release: 2026-11-11. Baseline: `2d10db6`. This corrects the accepted
C13-02 defaults; it does not release C13-03 or later packages.

## Resulting behavior

An authored sky is a visible background. New `SkyboxComponent` instances have
`EnvironmentLighting=false`; rendering that background does not automatically
give meshes ambient or specular energy. The Inspector exposes the explicit
Environment Lighting opt-in, which is extracted into the immutable RenderWorld
and saved in scene files. Files without the field keep it disabled. This is an
explicit per-sky opt-in rather than a new standalone SkyLight component.

When enabled, C13-02 HDR irradiance/GGX/BRDF lighting and its diagnosed fallback
remain available. When disabled, sky colors and HDR descriptor exposure do not
alter an existing authored directional ambient fill. That existing directional
fill control remains supported; the correction does not remove it. Private
asset, thumbnail and assembly preview scenes keep their explicitly authored
lights. No preview light is injected into ordinary runtime snapshots, and this
slice does not add a new ordinary-viewport preview toggle.

An actual DirectionalLight owns procedural sun color/intensity. Native and GL
sky passes derive that light's direction when Sync With Sun is enabled. A raw
RenderWorld's default SunColor cannot manufacture a disk/halo without a light.
An unlit procedural gradient can remain visible as a background; the gradient
is not a physical scattering solution or a light source for receivers.

## Separate commits

- `8ea95c9`: data/lighting contract, extraction, persistence and Inspector opt-in.
- `4f79e51`: native receiver gate and independent HDR background scale.
- `77e924c`: legacy ECS OpenGL IBL gate found during review.
- Final correction checkpoint: the remaining snapshot OpenGL gate, raw snapshot
  sun ownership, ambient tint/exposure isolation and direct regression coverage.

## Review findings and corrections

The separate-context reviewer identified these leaks during acceptance:

| Finding | Trigger | Correction |
| --- | --- | --- |
| ENV-R1 | Legacy ECS bridge enabled HDR IBL from visible sky alone | Require EnvironmentLighting in SceneRenderBridge |
| ENV-R2 | Snapshot OpenGL renderer used sky visibility to enable IBL | Require resolved EnvironmentLighting in SceneRenderer |
| ENV-R3 | Background-only sky retinted a directional ambient fill | Set sky AmbientColor only inside the lighting opt-in |
| ENV-R4 | Background-only HDR import exposure scaled directional fill | Apply SourceExposure to receivers only inside that opt-in |

`/root/c13_semantics_recheck` rechecked all four corrected paths and the new
regressions, returning PASS with no further findings. Its recheck was source
inspection only; build/GPU execution is the implementer's evidence below.

## Verification

The earlier 112-test Debug and 8-test Release runs preceded the final review
repairs and are preliminary evidence only. The final rebuilt gates are recorded
below.

| Final gate | Result |
| --- | --- |
| Debug Editor, Runtime and AxleAllTests build | Exit 0; `build/c13_02_semantics_all_final_debug_build.log` |
| Debug all registered CTest | **113/113**, exit 0; `build/c13_02_semantics_all_final_debug_ctest.log` |
| Release products and affected tests | Build exit 0; focused CTest **9/9**, exit 0; `build/c13_02_semantics_verified_release_build.log` and `build/c13_02_semantics_verified_release_ctest.log` |
| Physical OpenGL source/cache explicit and fallback regression | **4/4** Debug/Release command outcomes, exit 0; `build/c13_02_semantics_verified_opengl_<config>_<mode>.log` |
| Architecture | PASS, 568 source / 25 CMake files |
| Separate-context correction recheck | PASS, source-only |

Direct tests now cover the reported behavior instead of relying only on image
cache regressions. EnvironmentGPU runs on physical D3D12, Vulkan and OpenGL:
native/GL pixels prove visible background plus dark receiver while opted out,
then a lit receiver after opt-in. Real GL u_UseIBL readback also checks the
separate ECS bridge, including on-to-off transitions. Native checks verify
procedural sky without a light, direct illumination, sun disk with a light and
its absence at zero intensity. CPU tests exercise new-component defaults,
extraction, scene round-trip, omitted-field defaults and directional-fill tint
isolation. A native pixel test changes sky colors and HDR descriptor exposure
while opted out and requires unchanged receiver RGB.

Full manual interaction in a running Editor is not claimed. Existing CPU bake,
CSM, custom-material and OpenGL compatibility gates remain part of regression
verification; no new overall rendering performance claim is made.

Final D3D12/Vulkan comparisons of red, blue, sharp and rough HDR fixture RGB
readbacks have maximum and mean channel error **0**, in Debug and Release;
`build/c13_02_semantics_verified_parity.json` records the measurements. Physical
backends used the RTX 3070; native tests enable NRI and available API validation,
and the GL test checks actual pixel and uniform data and GL error status.

Acceptance: **PASS** for this C13-02 correction. C13-03 remains unreleased.
