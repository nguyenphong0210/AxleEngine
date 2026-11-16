# M15 U3 batch 5 — Codex remediation follow-up

**Parent review:** [M15_U3_BATCH_5_CODEX_REVIEW.md](M15_U3_BATCH_5_CODEX_REVIEW.md)  
**Reviewed implementation:** Gemini batch `739d0228` plus the follow-up working tree changes  
**Reviewer:** Codex  
**Date:** 2026-11-16

## Result

The high-risk SurfaceNormal descriptor bug is fixed for the supported Material
Graph scope. The graph compiler/runtime now carries a companion `PSNormalMain`
bytecode generated from the same graph function and TBN construction. The
SurfaceNormal pass binds the graph generation's own constant buffers and
descriptor sets, creates the normal pipeline from that companion bytecode, and
restores the built-in layout before continuing.

Material Graph normal coverage is deliberately restricted to opaque,
depth-writing surfaces. Graphs do not currently expose an authored alpha output,
so cutout and transparent graph coverage still use the existing explicit
fallback path until the graph schema/fallback diagnostics package is completed.

## Verified changes

- `MaterialSurface.hlsl` adds `AxleEvaluateWorldNormal` and `PSNormalMain`.
- `CompiledMaterialGraphProgram`, `ShaderCode`, and `CustomReadyGeneration` carry
  the normal bytecode through compile, cache, reload, and native generation.
- `CreateCustomMaterialPipeline(..., normalPass)` rejects missing normal bytecode
  instead of silently creating a color pipeline.
- `RHISceneRendererPasses.cpp` declares and binds custom graph resources in the
  `SurfaceNormal` pass; it no longer uses the default material descriptor for
  supported graph normals.
- Compiler, runtime, and rendering contract tests assert the companion bytecode
  and its backend format.

## Test evidence

| Configuration | Result |
|---|---|
| Debug `AxleRenderUnitTests --m15-surface-material-contract` | PASS |
| Release `AxleRenderUnitTests --m15-surface-material-contract` | PASS |
| Debug Material Graph compiler policy | PASS (DXIL + SPIR-V) |
| Release Material Graph compiler policy | PASS (DXIL + SPIR-V) |
| Debug Material Graph runtime native | PASS (D3D12 + Vulkan) |
| Release Material Graph runtime native | PASS (D3D12) |
| `git diff --check` | PASS |

## Remaining acceptance gates

The parent review's F1/F3 remain open and are not hidden by this fix:

1. `MaterialSurface.hlsl` still has a reduced single-sun/analytical-ambient
   lighting path; it does not yet share ForwardPBR's multi-light, shadow and IBL
   implementation.
2. There is still no native pixel-by-pixel built-in-vs-graph parity fixture under
   identical lights, shadows and IBL.
3. Editor visual evidence for the smooth-sphere gate remains user-provided.

These belong to the explicit graph resolver/lighting parity work in
`M15-U3-21.2` and `M15-U3-23.1..23.3`; the milestone must remain HOLD for full
U3 acceptance until those gates pass.
