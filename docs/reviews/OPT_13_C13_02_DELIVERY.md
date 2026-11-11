# C13-02 authored HDR environment and IBL

User release: 2026-11-11, baseline `2ad65a1`. Codex implemented this package
directly under the user assignment. C13-03 and later packages remain unreleased.

## Checkpoints and behavior

- `90036bf`: deterministic CPU HDR bake, cosine diffuse E/pi, GGX prefilter
  roughness mips, split-sum DFG LUT and analytic/invalid-input regressions.
- `b9a8319`: native authored source/cache, transactional RGBA32F upload, HDR sky,
  built-in PBR IBL receivers, exposure, diagnosed diffuse-only fallback and GPU
  regressions. Self-verification: Debug native 2/2, Release environment/CSM 5/5.
- Final checkpoint: extended native orientation/roughness/lifetime acceptance,
  Unicode descriptor resolution and empty-source diagnostics. Verification and
  independent review are recorded below when complete.

Sources use existing root-relative references, explicit CubemapHDR descriptors
and VFS reads. No generated asset metadata or vendor changes are introduced.
Radiance remains linear and above one through CPU convolution and GPU upload.
The existing display transform runs after linear HDR shading. The sky and IBL
use sky intensity, sky exposure and descriptor exposure consistently. Exposure
changes do not rebuild the immutable maps. Environment and BRDF sampling use
a separate clamp sampler; reflection LOD follows the actual prefilter mip count.

Four native generations are cached per renderer using source-root generation,
authored request, resolved source identity/content revision, decode options and
bake settings. Same-source corruption or failed bake/upload retains the most
recent valid generation with a diagnostic. Switching a descriptor to a different
missing source uses authored diffuse ambient, with no synthetic metal reflection.
Malformed descriptors and empty references are diagnosed. Allocation/upload
failure never publishes a partially initialized generation and is retryable.

Every cached material descriptor set and the sky set refresh their exact native
resource dependencies when selection changes. Native leases cover submission;
LRU eviction and shutdown release owners, and stale device generations cannot
be reused after device recreation. Bake settings validate bounded work before
allocation. Defaults are 256 radiance, 32 irradiance, 128 prefilter with 5 mips,
128 BRDF LUT and 256 samples; tests use small real fixtures through production
APIs rather than mock texture data.

## Verification

The physical GPU is an NVIDIA GeForce RTX 3070. Tests explicitly require the
requested D3D12/Vulkan backend with no software adapter or fallback. NRI and
available native API validation are enabled; successful native runs report zero
validation errors. Fixtures and readbacks live under
`build/Tests/Environment/<config>/<backend>/`.

Executed native checks include:

- Every face and mip preserves the HDR constant `(2, .25, .125)` in radiance,
  irradiance and specular maps; native DFG data is finite and energy bounded.
- Axis-coded equirectangular HDR is sampled in the correct direction at all
  six sky views, including both poles, within two 8-bit display levels of the
  analytic expected result.
- A narrow HDR lobe produces a metallic center of 255 at roughness .05 and 150
  at roughness .9 in both Debug backends; sharp/rough images were inspected.
- Sky and metallic/diffuse receivers respond to source, intensity and exposure.
  Cache hits and exposure edits cause no additional bake/upload. Source reload,
  corrupt source, missing source, descriptor redirect, LDR rejection, allocation
  fault/retry, sky/IBL disable, tone mapping, MSAA and FXAA are exercised.
- Five source switches keep the LRU at four generations. Evicted generation
  owners and their native handles expire after completion, including unused
  cached draw descriptors. Device recreation invalidates and rebuilds native
  resources. Unicode descriptor names are exercised through the VFS.

Final command results:

| Gate | Result |
| --- | --- |
| Debug build: `AxleEditor AxleRuntime AxleAllTests --parallel 6` | Exit 0; log `build/c13_02_products_debug.log` |
| Debug CTest: all registered tests | **112/112**, exit 0; log `build/c13_02_ctest_debug.log` |
| Final Debug Editor relink after descriptor repair | Exit 0; `build/c13_02_editor_final_debug.log` |
| Release Editor/Runtime and focused targets; CTest | Exit 0, **24/24**; final native Release GPU test 2/2 exit 0; `build/c13_02_products_release.log`, `build/c13_02_ctest_release.log` |
| Real OpenGL explicit/fallback source/cache regression | **4/4** command outcomes, exit 0, Debug/Release `AxleImageSourceTests gl` and `fallback`; logs `build/c13_02_opengl_<config>_<mode>.log` |
| Architecture checker | PASS, 568 source and 25 CMake files |
| Separate-context source review | PASS; `/root/c13_environment_review_retry`; read-only review, no rerun; parent evidence independently checked against source |

Debug and Release D3D12/Vulkan comparisons of `red.ppm`, `blue.ppm`, `sharp.ppm` and
`rough.ppm` match exactly across all RGB pixels (maximum and mean error 0).
The checks use generated Radiance fixtures at 64x32, small bake settings and
128x128 output views. This is fixture parity, not a universal image-quality or
performance guarantee.

## Scope and limits

The native integration covers the built-in PBR and sky pipelines. C12 custom
material layouts remain separate; this package does not add environment texture
bindings to authored custom graph shaders. The existing OpenGL compatibility
renderer is retained. No manual Editor interaction or general-purpose GPU
performance claim is made. Cold CPU baking is synchronous on the render owner
and uses the existing JobSystem when its scheduler context is valid; subsequent
unchanged views reuse GPU resources while the existing source loader still
fingerprints source content for reload detection. C13-07 owns broader lighting budget and
quality measurements, and is not activated by this delivery.

## Independent review

A separate-context reviewer inspected the complete C13-02 diff after the final
repairs and returned no release-blocking findings. It specifically checked source
and content identity isolation, same-source last-good versus different-source
fallback, all-four-texture transactional publication, native descriptor leases,
device epoch invalidation, sky/PBR register spaces and sampler s3, and preservation
of CSM, OpenGL, custom material and existing image-source paths. The reviewer
confirmed that `ReplaceDescriptorRange` rejects in-use descriptor writes and the
per-frame `m_BoundIBL` reset retries safely; it recorded the ignored setter return
value as a minor observability issue only. The reviewer did not rerun GPU tests;
its evidence was source inspection against the executed logs and readbacks above.

Acceptance: PASS for C13-02. C13-03 remains unreleased.
