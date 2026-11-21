# Milestone 23 — Material Graph and Advanced Scene Tooling Plan

**Status** — Proposed. Release by checkpoint after M22 acceptance.
**Phase 52** — Graph and scene authoring.
**Architecture** — [Graph authoring and scene tools](ARCHITECTURE.md).
**Inputs** — Accepted M15 raster/material contracts, [M21 visibility and graph routing](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md), [M20 packaged products](../MILESTONE_20_COOKER/PLAN.md).

## 1. Product boundary

Build a visual material graph authoring workflow on the existing graph IR, HLSL lowering and compiler pipeline. Preserve the existing textual graph interchange and supported material paths. M23 also owns material nodes, decals and prefab variant authoring. [M19](../MILESTONE_19_CANVAS2D/PLAN.md) owns in-game Canvas2D. Animation, VFX and cooking have their own current plans in the [milestone index](../README.md).

## 2. Checkpoints and acceptance

- [ ] **M23-01 · Interactive graph canvas.** Add transactional typed node/pin editing, layout, parameter editing, selection, save/reopen, previews and DXC/backend diagnostics associated with nodes. Verify invalid graphs preserve the active valid preview, and text/visual edits preserve graph semantics. Reuse the existing DXC/Slang boundary when applicable.
- [ ] **M23-02 · Advanced surface nodes.** Author Clearcoat, Anisotropy, SSS, Refraction and Cauchy Dispersion through supported existing material/lighting contracts. Test opaque and transparent variant routing, diagnostics, resource binding, save/reopen and reproducible native pixels. Record limits per view and route.
- [ ] **M23-03 · Reflection probe integration.** Revalidate M15 Box/Sphere placement, capture, gizmos and scene/assembly persistence in the new authoring workflows. Preserve working cubemap capture and sampling. Keep octahedral encoding as a scoped checkpoint within this requirement, with a measured memory/quality decision, parity fixture and explicit migration/fallback decision before promoting it.
- [ ] **M23-04 · Decal volumes.** Deliver authored volumes or mesh-projected decals with persisted placement/material, reliable clipping and declared receiver/raster route policy. Verify overlap order, transforms, failure handling and packaged Runtime.
- [ ] **M23-05 · Prefab variants.** Implement stable nested override identity, property diff, revert-to-base and save/reopen. Validate missing and updated bases, cycle rejection, dependency discovery and scene/assembly instantiation without mutating the base.
- [ ] **M23-06 · Product and acceptance.** Rebuild graph/scene/product tests, Editor and Runtime in Debug/Release. Verify strict native D3D12/Vulkan Editor and detached Runtime fixtures, applicable OpenGL compatibility, graph generations, invalid input and asset replacement. Archive command/results, artifacts and requirement dispositions for consolidated final review.

Publish the verified authoring, scene and packaged route matrix for [M24](../MILESTONE_24_PERFORMANCE_ROLLOUT/PLAN.md). Use equivalent image inputs with recorded tolerances and exact comparisons for canonical authored graph and instance state where applicable.
