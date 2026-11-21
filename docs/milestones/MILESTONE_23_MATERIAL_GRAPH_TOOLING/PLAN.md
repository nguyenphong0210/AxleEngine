# Milestone 23 — Material Graph and Advanced Scene Tooling Plan

**Status** — Proposed. Release by checkpoint after M22 acceptance.
**Phase 52** — Graph and scene authoring.
**Architecture** — [Graph authoring and scene tools](ARCHITECTURE.md).
**Inputs** — Accepted [M15 U12 Material Graph Studio and raster materials](../MILESTONE_15_LIGHTING_MATERIALS/PLAN.md#u12-material-graph-studio), [M21 visibility and graph routing](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md), [M20 packaged products](../MILESTONE_20_COOKER/PLAN.md).

## 1. Product boundary

Build reflection-probe, decal and prefab variant workflows with the accepted Studio and material assets. Use the M15 raster fixtures to verify interactions with M21's supported material routes and M20's package pipeline. [M19](../MILESTONE_19_CANVAS2D/PLAN.md) owns in-game Canvas2D.

[M15 U12](../MILESTONE_15_LIGHTING_MATERIALS/PLAN.md#u12-material-graph-studio) owns implementation and acceptance of former `M23-01` (interactive graph canvas) and the raster scope of `M23-02` (advanced surface nodes). The identifiers below retain their existing numbering.

## 2. Checkpoints and acceptance

- [ ] **M23-03 · Reflection probe integration.** Revalidate M15 Box/Sphere placement, capture, gizmos and scene/assembly persistence in the new authoring workflows. Preserve working cubemap capture and sampling. Keep octahedral encoding as a scoped checkpoint within this requirement, with a measured memory/quality decision, parity fixture and explicit migration/fallback decision before promoting it.
- [ ] **M23-04 · Decal volumes.** Deliver authored volumes or mesh-projected decals with persisted placement/material, reliable clipping and declared receiver/raster route policy. Verify overlap order, transforms, failure handling and packaged Runtime.
- [ ] **M23-05 · Prefab variants.** Implement stable nested override identity, property diff, revert-to-base and save/reopen. Validate missing and updated bases, cycle rejection, dependency discovery and scene/assembly instantiation without mutating the base.
- [ ] **M23-06 · Product and acceptance.** Rebuild graph/scene/product tests, Editor and Runtime in Debug/Release. Verify strict native D3D12/Vulkan Editor and detached Runtime fixtures, applicable OpenGL compatibility, graph generations, invalid input and asset replacement. Exercise U12 advanced graphs on the supported M21 routes and their declared Forward fallback. Archive command/results, artifacts and requirement dispositions for consolidated final review.

Publish the verified authoring, scene and packaged route matrix for [M24](../MILESTONE_24_PERFORMANCE_ROLLOUT/PLAN.md). Use equivalent image inputs with recorded tolerances and exact comparisons for canonical authored graph and instance state where applicable.
