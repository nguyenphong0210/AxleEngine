# C13-02 HDR environment and IBL checkpoints

User release: 2026-11-11. Baseline: `2ad65a1`.
Each verified checkpoint receives its own local commit, per user instruction.
C13-03 and later packages are not released.

| Checkpoint | Scope | State |
| --- | --- | --- |
| C13-02.1 | Pure CPU linear HDR bake, cube convention, cosine diffuse, GGX roughness mips and split-sum DFG | Implemented/self-verified; Debug/Release focused CTest 1/1 each |
| C13-02.2 | Authored source resolution/cache, transactional native resources, HDR sky and PBR receivers | Implemented/self-verified in b9a8319; native acceptance extended in final checkpoint |
| C13-02.3 | Physical D3D12/Vulkan images, reload/fallback/ownership, product regression and separate-context acceptance | Accepted in final checkpoint; see delivery report |

## CPU contract

`EnvironmentBake` consumes unflipped, linear floating-point equirectangular
images, with the top row at north. Cube faces are +X,-X,+Y,-Y,+Z,-Z; rows start
at cube v=0. The builder preserves values above one and performs no display
conversion or exposure. Input values must be finite and in [0, 1e12].

Diffuse convolution stores E/pi, matching the renderer's Lambert receiver: a
constant environment remains constant. Specular prefiltering uses deterministic
GGX importance samples and source-mip footprint selection. Roughness-zero mip
samples the original radiance. The BRDF LUT stores Fresnel scale/bias in RG,
with NdotV along X and roughness along Y. Configurations validate dimensions,
mip count and bounded work before allocating. Failed bakes retain prior output.

Defaults: environment 256, irradiance 32, prefilter 128 with 5 mips, BRDF LUT
128 and 256 samples. The builder uses existing JobSystem workers only when
called from a valid scheduler context; otherwise it runs serially. This does
not create render threads or a private GPU submission path.

The CPU checkpoint tests constant HDR energy, analytic directional diffuse,
GGX roughness contrast, face orientation, analytic smooth-surface DFG,
determinism and malformed inputs. Commands: build AxleEnvironmentBakeTests in
Debug and Release, then `ctest --test-dir build -C <config> -R '^EnvironmentBakeTests$' --output-on-failure`;
all exited 0, one test per configuration. Native execution is not claimed here.

## Native and acceptance boundary

Use the existing authored root-relative paths/descriptors/VFS and native resource
leases. Bind real HDR radiance, diffuse and GGX maps plus a clamped BRDF LUT.
Keep sky and environment illumination in linear radiance until the existing
view display transform. Define same-source last-good versus different-source
fallback explicitly; switching worlds must not leak a previous environment.
Cache with actual source identity/revision and bounded lifetime. Preserve C13-01,
C12 custom layouts and OpenGL compatibility; unsupported source/format failures
must have diagnostics and an authored ambient fallback.

Native checkpoint: `RHIEnvironment` resolves linear Radiance sources and explicit
CubemapHDR descriptors through the existing VFS. Four native generations are
cached by authored request, resolved source identity/content revision, decode
options and bake settings. Intensity/exposure scale the sky and illumination
without rebaking. Same-source decode/bake/upload failures retain the last-good
generation with a diagnostic; different or invalid sources use authored diffuse
ambient, with no invented specular reflection. Source replacement is transactional.
Descriptors retain native leases through submission and refresh all cached draw
sets when the selected generation changes. The sky uses the existing HDR/display
path and IBL has a separate clamped sampler.

Checkpoint .2 commands (all exit 0): Debug build of
`AxleEnvironmentRenderingTests`, CTest `^EnvironmentGPU_` (2/2); Release build of
`AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleCascadedShadowRenderingTests`,
CTest `^(EnvironmentBakeTests|EnvironmentGPU_.*|CascadedShadowGPU_.*)$` (5/5).
Physical D3D12 and Vulkan used the RTX 3070, with no validation errors. Native
subresource readbacks preserve (2,.25,.125) in every cube face/mip; receiver and
sky images, exposure/cache hits, source reload, descriptor redirect, diffuse-only
fallback, upload failure/retry and tone-map/FXAA/MSAA execution pass.
Final product regression, nonconstant orientation/roughness images, lifetime,
backend parity and separate-context review remain in checkpoint .3.

## Runtime lighting semantics correction

The corrective C13-02 slice adopts UE-style runtime ownership with an explicit
Editor preview convenience path. A `SkyboxComponent` may render an atmosphere or
HDR background while `EnvironmentLighting` remains false. In that state the sky
does not contribute ambient, diffuse hemisphere light, IBL or specular reflection
to scene geometry. A directional light owns the procedural sun direction/color
and sun disk; a sky environment contributes to receivers only after the explicit
lighting opt-in. Legacy scene files without the new field load conservatively with
lighting disabled; the Editor exposes the opt-in beside the Skybox toggle.

This checkpoint changes the contract and extraction/serialization paths only. The
following checkpoints update native sky/PBR shader bindings and add physical tests
for background-only, directional-sun and explicit-environment cases.

## Native renderer semantics correction

The native sky pass now derives sun color from an actual `DirectionalLight`; a
missing or zero-intensity directional light cannot create a sun disk or halo. HDR
sky display uses authored intensity/exposure independently from receiver lighting,
so a visible background does not imply an illuminated mesh. Forward PBR IBL and
analytical environment fallback require `EnvironmentLighting`; descriptor maps can
remain prepared for a later opt-in without contributing while the flag is off.

Checkpoint .2 correction verification: Debug native `EnvironmentGPU_dx12`,
`EnvironmentGPU_vulkan`, `CascadedShadowGPU_dx12` and `CascadedShadowGPU_vulkan`
all pass 4/4. The HDR regression explicitly renders the same metallic receiver
with background-only and explicit-environment modes.
