# M14 P2 — Surface normal continuity checkpoint

**Status:** Implemented for the direct ForwardPBR continuity defect; full P2 remains open.
**Base:** `57a4304` (P1 timing checkpoint).

## Correction

`ForwardPBR.hlsl` previously replaced the interpolated shading normal with a screen-derivative normal whenever the generic pattern mode was 0 or 1. That made a smooth sphere shade as a collection of triangle faces even when its mesh normals were smooth. The replacement was removed. ForwardPBR now keeps the interpolated geometric normal and only applies the tangent-space normal-map path when a valid tangent/material normal map is present.

This correction targets the visible faceting/vân on the sphere. It does not yet create a normal producer for `ScreenNormal`; GTAO and SSR still need the separate P2 surface-data pass before their normal inputs can be accepted.

## Verification

The existing native environment fixture was run against the updated shader source:

| Backend | Configuration | Exit | Result |
|---|---|---:|---|
| D3D12 — NVIDIA GeForce RTX 3070 | Debug | 0 | Existing HDR/IBL/reflection fixture PASS; shader compiled and executed |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | Debug | 0 | Existing HDR/IBL/reflection fixture PASS; shader compiled and executed |

The fixture's existing image checks are not a smoothness proof. A later P2 test must inspect the normal field or use a dedicated sphere anti-faceting fixture after the normal target is produced.

## Remaining P2 work

- Add a real opaque surface-normal producer and matching target/graph declarations.
- Ensure normal maps, mirrored/nonuniform transforms and built-in/Material Graph materials use one surface contract.
- Add a dedicated smooth-sphere readback/quality assertion before P2 is closed.
