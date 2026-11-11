# OPT-13 conventional Point/Spot shadows

User release: 2026-11-11, baseline `9bd4856`. Each completed checkpoint is verified
and committed separately. C13-03 and later packages remain unreleased.

1. SpotLight projections and PCF, shared native depth caster implementation,
   controls/persistence and real shadow image regression.
2. PointLight six-face projections and PCF, face/seam coverage, bounded selection,
   cache/culling, backend policy and lifecycle verification.
3. Sandbox starter metal/rubber/glass assets with embedded identities and no
   texture dependencies; validate with production material loaders.
4. Final product/backend regression and separate-context source review.

## Contract

Direct shadows modulate only the owning light's direct radiance. Ambient/IBL and
other lights remain independent. Opaque and alpha-cutout built-in static meshes
cast; transparent/additive and arbitrary custom vertex programs do not cast via
this interface. Existing CSM and C12 custom layouts remain supported separately.

Native depth rendering is shared with CSM. Local maps use at most sixteen square
tiles in a 4x4 atlas: up to four spots and two points (six tiles each). The default
tile size is 512, configurable from 128 to 2048. Camera-distance/identity ordering
selects lights deterministically within the budget; excess lights remain lit
without shadows and receive a diagnostic. Casters are culled conservatively to
each light projection using immutable geometry-generation bounds. Receiver bias
and normal offsets use world units; PCF taps clamp inside each atlas tile.

Cache reuse requires exact projection, geometry-generation, transform, material
and cull-state equality. Dynamic textured cutout casters invalidate every frame
because in-place texture updates can preserve resource identity. Cache contents
become valid only after successful submission. Native resource leases cover
recording/submission, and retired draw owners/descriptors are released after the
preceding frame drains. Allocation failure rejects the view and retains the old
atlas for retry. No performance target is claimed before measurement.

## Deferred: Virtual Shadow Maps

VSM is explicitly **not implemented by this package**. Track it as a later,
separately released optimization requiring virtual page tables, physical page
allocation/budgeting, screen-driven page requests, geometry/light invalidation,
stable caching, eviction and missing-page fallback. Evaluate its interaction with
virtual geometry only after conventional local shadows have actual quality and
GPU/memory measurements. Do not add UE-sized virtual resolutions or assume VSM
performance without implementing and measuring the complete page system.

## Spot checkpoint self-verification

Shared caster rendering and SpotLight receiver integration are implemented.
Debug Editor and affected targets build; CPU/local native/CSM/RenderUnit CTest
passes 6/6 after the final culling change. Native spot off/on fixtures darken 520
pixels on each backend. Opt-out, cache hits/invalidation, caster flags, cutout,
allocation retry and tone-map/FXAA/MSAA paths pass. Architecture check passes
575 source / 25 CMake files. Point receiver activation, OpenGL policy, Sandbox
assets and final independent acceptance remain in the following checkpoints.

## Point and backend-policy checkpoint self-verification

Point receivers now select all six projections, including biased-receiver face
selection at seams. Physical D3D12/Vulkan fixtures cover depth on all faces,
shadowed pixels on both sides of a face boundary, per-light opt-out, additional
directional-light GPU-slot prefixes and cache reuse. In-place textured-cutout
alpha changes redraw the local maps. OpenGL explicitly advertises no local-light
shadow support and reports requested shadows in Inspector/RenderService; ordinary
Point/Spot lighting remains available. A real GL view verifies warning/reset
behavior. Debug Editor and affected targets build; focused CTest passes 7/7
(`build/c13_local_point_checkpoint_ctest_debug.log`). Final acceptance is pending.

## Sandbox material checkpoint self-verification

Seven texture-free ForwardPBR assets are present in
`Samples/Sandbox/assets/materials`: polished steel, satin aluminum, gold, black
and gray rubber, clear glass and blue tinted glass. Each has a unique embedded
identity; no sidecars are generated. Glass uses transparent queue 3000, depth
writes off and IOR 1.5. Existing MaterialUnitTests load every asset through both
production material loaders, compare rendering parameters/identity and confirm
read-only validation. Debug MaterialUnitTests passes (`build/c13_sandbox_materials_ctest_debug.log`).
See the materials README for lighting and native-refraction limitations.

## Review corrections checkpoint

The separate-context source review found Assembly persistence, descriptor-budget,
mesh-mutation-contract and malformed-number issues. Corrections and actual
verification are recorded in [the review](../reviews/OPT_13_LOCAL_SHADOWS_REVIEW.md).
Native local tests now additionally cover 4096 material draws, complete mesh
generation replacement and cache validity after abandoned/successful recording.
All focused repairs pass; final rebuilt product/full-suite/Release gates follow.
