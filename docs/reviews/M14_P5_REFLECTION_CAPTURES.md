# M14 P5 — Reflection probe data flow checkpoint

**Status:** Authored-probe capture data flow implemented and verified; dynamic scene capture remains open.
**Base:** `45cff29` (P3 checkpoint).

## Implemented

- `ReflectionProbeComponent.CubemapPath` is now consumed by the native renderer.
- Probe selection uses camera influence: box probes use distance outside their extents, sphere probes use radius falloff, and the strongest authored probe wins.
- Each authored probe path owns a cached `RHIEnvironment` generation using the existing HDR/CubemapHDR decode, irradiance and prefiltered environment bake path. This keeps probe resources separate from the active sky environment.
- SSR receives a real `TextureCube` probe sample at `t10`; the fallback samples the parallax-corrected probe direction instead of returning constant gray.
- The probe fallback retains sky/neutral fallback behavior when no authored cubemap is available or preparation fails.
- The fixture now assigns `red.hdr` as a probe source and requires the probe-enabled SSR image to differ from the no-probe SSR image.

## Verification

| Backend | Configuration | Exit | Result |
|---|---|---:|---|
| D3D12 — NVIDIA GeForce RTX 3070 | Debug | 0 | Authored probe bake/bind/sample fixture PASS |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | Debug | 0 | Authored probe bake/bind/sample fixture PASS |

The native fixture also retains the existing Hi-Z, normal-target, timestamp and environment checks. Logs contain no SSR/probe descriptor or shader failure.

## Explicit remaining P5 work

This checkpoint is not the full dynamic Reflection Captures contract yet. The renderer still needs a capture scheduler that renders six scene views into a cubemap, invalidates/rebuilds captures when scene geometry changes, prefilters the captured result, and blends multiple valid probes. The current probe path proves authored cubemap data flow and real cubemap sampling, not automatic scene capture.

P6 will refine SSR traversal, roughness cone tracing and material-aware reflection resolve after the dynamic capture contract is closed.
