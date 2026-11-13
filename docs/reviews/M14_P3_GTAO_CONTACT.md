# M14 P3 — GTAO and contact-shadow integration

**Status:** Implemented and verified for the current post-lighting composite contract. M14 remains HOLD.
**Base:** `e422768` (Hi-Z checkpoint).

## Correction

The previous GTAO apply pass multiplied the whole scene color by `finalAO * finalContact`. That applied the same full-strength darkening to direct light, specular highlights and emissive materials even though the pass runs after ForwardPBR and does not have separated lighting terms.

The composite now uses independent bounded weights:

- ambient visibility weight: `0.25 * GTAO strength`;
- contact visibility weight: `0.20 * contact strength`;
- each factor is interpolated from 1.0, so the pass cannot crush the complete shaded image.

The shader comments explicitly record this as a conservative post-lighting policy. A future pre-lighting AO path can replace it with true separated indirect lighting; P3 does not claim that separation already exists.

## Verification

The environment fixture now checks a quantitative luma bound in addition to successful execution:

- valid opaque `ScreenNormal` target and non-empty center readback;
- GTAO/contact result remains between 65% and 105% of the no-occlusion reference average luma;
- existing toggle paths for GTAO-only and contact-only remain executable.

Results after rebuilding from a regenerated `build/` directory:

| Backend | Configuration | Exit | Result |
|---|---|---:|---|
| D3D12 — NVIDIA GeForce RTX 3070 | Debug | 0 | P3 luma/normal/toggle checks and existing fixture PASS |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | Debug | 0 | P3 luma/normal/toggle checks and existing fixture PASS |

## Boundary

This closes the full-color multiplication regression and establishes a safe occlusion composite. It does not claim true ambient/direct/emission separation; that requires running AO before lighting or providing an explicit indirect-light target. It also does not close SSR, reflection probes or DDGI.
