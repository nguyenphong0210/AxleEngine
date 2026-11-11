# C13-01 cascaded shadow execution checkpoints

User release: 2026-11-11. Baseline: `3095780`.
The user requested smaller implementation steps with quality as the priority.
C13-01 remains released; C13-02 and later packages are not released.

| Checkpoint | Scope | Evidence required | State |
| --- | --- | --- | --- |
| C13-01.1 | Pure CPU cascade settings, camera-space splits, stabilized light transforms and failure contract | Debug/Release CPU regression, architecture check, separate-context review | Implemented and verified; see [delivery](../reviews/OPT_13_C13_01_CPU_DELIVERY.md) |
| C13-01.2 | Modern RHI depth resources, shadow caster pass, receiver bindings, PCF 5x5 and bias application | Real shader compilation, resource/state/lifetime tests and D3D12/Vulkan readbacks | Pending |
| C13-01.3 | Product integration, capability reporting and acceptance | Enabled/disabled light/settings, resize/failure, cascade boundaries, image parity and relevant regressions | Pending |

Each checkpoint is a coherent local commit. Completing the CPU checkpoint does
not claim native shadow execution, image quality, performance or full C13-01
acceptance. The remaining C13-01 work is still authorized; this breakdown does
not introduce another approval requirement.

## C13-01.1 CPU contract

`CascadedShadowMap` performs no device or context operations. It exposes up to
four matrices with positive camera-space near/far distances and a [0, 1] light
depth range. Supported counts are three and four. Per-cascade resolution is
32..4096, allowing tiny deterministic test cases and the 1024/2048/4096 product
tiers. The light vector points from the scene toward the light.

Inputs use finite right-handed perspective or orthographic ZO projections and a
rigid camera view. Camera clips must satisfy `0 < near < maxDistance <= far`.
Orthographic cameras with non-positive near clips require an explicit policy in
the integration checkpoint; they are not silently treated as perspective cameras.
Projection metadata is checked at clip endpoints with float-aware tolerance.
Near and interior unprojection avoid loss of far-plane precision for large
far/near ratios. Subdivision uses double precision and rejects intervals that
collapse when published as floats.

Splits blend uniform and logarithmic distances with lambda in [0, 1]. A
rotation-invariant enclosing sphere, quantized radius, fixed world/light basis
and snapped XY center stabilize shadow UVs. Eight texels of padding allow for
snapping and a bounded PCF footprint; integration must keep filter taps inside
that bound. Depth extends by `MaxDistance` beyond the enclosure in each light
direction to include bounded off-frustum casters. This does not guarantee that
arbitrarily distant casters fit, and does not implement scene-bounds fitting.

Bias settings are finite and non-negative. Base/slope bias is normalized shadow
depth; normal bias is in world units. This checkpoint validates and carries
these settings only. Their application and visual tuning belong to C13-01.2.

Invalid `Configure` retains the prior configuration/frame. Successful
configuration invalidates the old frame. Every failed `Update` clears the frame;
partially computed cascades never become visible. An invalid constructor
configuration cannot produce a frame until a successful configuration.

## Integration boundaries

Use the existing RenderWorld/RenderService/RenderGraph ownership and submission
contracts. Keep mutable resources on their render owner and pin exact
generations through submission completion. Respect mesh shadow flags, authored
material semantics and supported surface modes. Define explicit policies for
unsupported custom deformation/skin paths, rather than claiming equivalence.

Modern renderer `SupportsCascadedShadows` remains false until actual execution
and image verification are delivered. The OpenGL compatibility renderer and its
existing `ShadowMap` are preserved. C12 shader/material behavior and C11-07
product deferrals remain unchanged.
