# M14 P2 — Surface normal continuity checkpoint

**Status:** Surface-normal continuity and opaque normal-target producer implemented; full P2 material-contract work remains open.
**Base:** `57a4304` (P1 timing checkpoint).

## Correction

`ForwardPBR.hlsl` previously replaced the interpolated shading normal with a screen-derivative normal whenever the generic pattern mode was 0 or 1. That made a smooth sphere shade as a collection of triangle faces even when its mesh normals were smooth. The replacement was removed. ForwardPBR now keeps the interpolated geometric normal and only applies the tangent-space normal-map path when a valid tangent/material normal map is present.

This correction targets the visible faceting/vân on the sphere. A separate opaque `SurfaceNormal` pass now writes the interpolated world normal into `ScreenNormal`, and the render graph declares that target as a producer and as an input to GTAO/SSR. The pass reuses the ForwardPBR vertex stage and does not force custom/transparent materials through the built-in color shader.

## Verification

The existing native environment fixture was run against the updated shader source:

| Backend | Configuration | Exit | Result |
|---|---|---:|---|
| D3D12 — NVIDIA GeForce RTX 3070 | Debug | 0 | Existing HDR/IBL/reflection fixture PASS; shader compiled and executed |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | Debug | 0 | Existing HDR/IBL/reflection fixture PASS; shader compiled and executed |

The fixture now requires a valid `ScreenNormal` target and a non-empty RGBA16-float center readback after GTAO/contact rendering; both native runs passed this assertion. The existing image checks are not yet a complete smoothness proof.

## Remaining P2 work

- Ensure normal maps, mirrored/nonuniform transforms and built-in/Material Graph materials use one surface contract.
- Add a dedicated smooth-sphere readback/quality assertion before P2 is closed.
