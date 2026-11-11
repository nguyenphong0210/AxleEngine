# OPT-13 local shadows and Sandbox material delivery

Released by the user on 2026-11-11. Baseline `9bd4856`; final source `239eadb`.
C13-03 onward remain unreleased. No push was performed.

## Delivered behavior

Point and Spot lights now have conventional native depth-map shadows on D3D12
and Vulkan. Per-light CastShadows, world-unit bias/slope/normal offsets, filter
radius and near plane are editable and persist through scene and Assembly entity/
editor round-trips. Shadows multiply only their owning direct-light contribution;
CSM, authored ambient/IBL and other lights retain their separate ownership.

The depth caster implementation is shared with CSM. Built-in opaque and cutout
static meshes cast; transparent/additive and arbitrary custom vertex shaders do
not cast through this interface. Point maps have six 90-degree projections and
biased-receiver face selection. Tile-clamped 3x3 comparison PCF avoids atlas
bleeding; the existing cascade filtering contract is retained.

Local budgets are explicit: at most two shadowed Points and four Spots, sixteen
views in a 4x4 atlas. Tiles are 128..2048, default 512. Default atlas storage is
2048 squared D32 = 16 MiB; the configured maximum is 8192 squared = 256 MiB, per
renderer. Disabled views sample a defined tiny fallback and retain the previous
atlas allocation for reuse. These are texture-storage calculations, not total
VRAM or GPU-time measurements.

Selection is deterministic by camera distance, entity identity and input order;
beyond-budget lights remain directly lit without shadows and report a diagnostic.
Casters are conservatively culled using immutable geometry bounds. Exact cache
keys cover projections, transforms, geometry generations and material/cull state;
textured cutout redraws for possible in-place alpha updates. A map becomes cached
only after actual successful submission. Frame drain and resource leases cover
reuse, replacement and descriptor updates. Failed atlas replacement rejects the
view, retains the old allocation and permits retry.

OpenGL compatibility explicitly reports no local-light shadow support through
capabilities, Inspector and successful RenderService diagnostics when requested.
Point/Spot illumination and existing GL/CSM behavior remain available. The real
GL policy regression verifies that the warning clears for per-light/global opt-out.

Seven current-format, texture-free ForwardPBR materials with distinct embedded
identities are in `Samples/Sandbox/assets/materials`: polished steel, satin
aluminum, gold, black/gray rubber, clear glass and blue tinted glass. Glass uses
transparent queue 3000, depth writes off, IOR 1.5 and existing native screen-space
refraction. See that folder's README for usage and backend/visibility limitations.
No source scene was rewritten and no identity sidecars were created.

## Commits and review

- `29a2c52` ? shared depth implementation and verified Spot checkpoint.
- `a2e2ce6` ? Point six-face integration, native coverage and explicit GL policy.
- `e50c811` ? seven Sandbox materials and production-loader validation.
- `239eadb` ? separate-context review corrections: Assembly persistence, safe
  numeric parsing, 4096 material-draw capacity plus reserved sets, immutable mesh
  publication and associated regressions.

[Separate-context review](OPT_13_LOCAL_SHADOWS_REVIEW.md) records LS-R1..R4 and
focused rechecks. The reviewer reports no remaining source findings; it ran
structural/whitespace checks, not independent builds/GPU execution. All execution
below belongs to the implementer and was performed after rebuilding the targets.

## Final verification

Final verification completed after rebuilding the final source. Debug `AxleEditor`,
`AxleRuntime` and `AxleAllTests` build successfully; full Debug CTest passes
117/117 in `build/c13_local_verified_debug_ctest.log`. Release affected products
and focused targets build successfully; Release focused CTest passes 28/28 in
`build/c13_local_verified_release_ctest.log`, including local D3D12/Vulkan,
OpenGL policy, CSM, HDR/IBL, Material Graph, material-loader and RenderUnit
checks. Debug and Release native local tests report zero RHI validation errors.
The architecture checker passes 576 source / 25 CMake files in
`build/c13_local_architecture.log`. Exchange validation passes.


Physical GPU: NVIDIA GeForce RTX 3070; native tests reject software/fallback
adapters and check zero RHI validation errors. Local fixtures at 128x128 darken
520 Spot pixels and 409 Point pixels on each backend; every Point face contains
484 written depth pixels. The seam fixture darkens 8175 pixels on each side.
Six saved RGB off/on images compare byte-identically across D3D12/Vulkan in Debug
(`build/Tests/LocalShadows/Debug/parity.json`). Final artifacts are under
`build/Tests/LocalShadows/<configuration>/<backend>`.

Fixtures cover per-light/caster/global opt-out, cache hits, transform/light/mesh
invalidation, same-generation cutout alpha updates, all Point directions/seams,
additional-directional slot prefixes, bounded selection/culling, failed allocation
and retry, tone mapping/FXAA/MSAA, 4096 material draws, and abandoned versus actual
submitted map publication. Production material loaders validate all seven assets
without changing bytes or resolving external textures.

During verification, the large-draw fixture exposed the second reserved sky set;
the final capacity includes it. The standalone cache fixture was corrected to
reset/drain abandoned recording pins before Prepare, matching production frame
handling. These initial failures were resolved and the final fixtures rerun.

## Deferred scope and limits

Virtual Shadow Maps are explicitly deferred in the
[execution plan](../design/OPT_13_LOCAL_SHADOWS_PLAN.md): future page tables,
physical-page budget/allocation, requests, invalidation, caching, eviction and
missing-page policy require separate release and measurement. No VSM, SSAO/SSR,
volumetric fog, DDGI or later OPT-13 package was implemented here.

No GPU timestamp benchmark or claim of meeting the broader 5 ms lighting target
is made. Conventional finite-resolution PCF retains resolution/bias tradeoffs and
Point face boundaries use clamped taps rather than cross-face seamless filtering.
Transparent shadow transmission and custom vertex/deformation caster interfaces
remain outside this built-in static-mesh depth contract. Direct native writes to
published mesh buffers bypass the supported replacement API and are unsupported.
