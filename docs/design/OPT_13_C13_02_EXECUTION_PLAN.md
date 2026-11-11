# C13-02 HDR environment and IBL checkpoints

User release: 2026-11-11. Baseline: `2ad65a1`.
Each verified checkpoint receives its own local commit, per user instruction.
C13-03 and later packages are not released.

| Checkpoint | Scope | State |
| --- | --- | --- |
| C13-02.1 | Pure CPU linear HDR bake, cube convention, cosine diffuse, GGX roughness mips and split-sum DFG | Implemented/self-verified; Debug/Release focused CTest 1/1 each |
| C13-02.2 | Authored source resolution/cache, transactional native resources, HDR sky and PBR receivers | Pending |
| C13-02.3 | Physical D3D12/Vulkan images, reload/fallback/ownership, product regression and separate-context acceptance | Pending |

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
