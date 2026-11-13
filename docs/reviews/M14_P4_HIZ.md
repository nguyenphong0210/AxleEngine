# M14 P4 — Hi-Z pyramid resource correction

**Status:** Implemented and verified for the Hi-Z resource checkpoint; SSR algorithm quality remains open.
**Base:** `4193627` (P2 surface-normal producer).

## Correction

The renderer previously reused one texture descriptor set and one storage descriptor set for all eight Hi-Z dispatches. After the first bind, RHI pins the descriptor set for the command recording and rejects later replacement updates. Those return values were ignored, so later dispatches could continue using level-0 bindings.

The Hi-Z descriptor pool now owns eight immutable-per-dispatch texture sets and eight storage sets. Each level updates and binds its own pair, and every update is checked. The SSR render graph now declares all eight Hi-Z textures as shader-resource reads for `SSRCompute`, matching the bindings recorded by the command list.

## Verification

The environment fixture now reads the complete pyramid after an SSR frame and checks:

- all eight targets are valid;
- each target has the expected `max(1, width >> level)` dimensions;
- each target returns non-empty readback data;
- adjacent levels are not identical in the reference scene.

Results from this session:

| Backend | Configuration | Exit | Result |
|---|---|---:|---|
| D3D12 — NVIDIA GeForce RTX 3070 | Debug | 0 | 8-level readback checks and existing fixture PASS |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | Debug | 0 | 8-level readback checks and existing fixture PASS |

## Boundary

This closes the descriptor reuse and graph-declaration defect. It does not yet prove that SSR performs roughness cone tracing, hit refinement, material-aware Fresnel, probe cubemap sampling or energy-correct blending. Those remain P5/P6 work.
