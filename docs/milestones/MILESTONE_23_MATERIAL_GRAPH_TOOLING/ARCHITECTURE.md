# Milestone 23 — Material Graph and Scene Tools Architecture

## 1. Graph authoring and shared runtime

Reuse [M15 U12's Studio and graph publication contracts](../MILESTONE_15_LIGHTING_MATERIALS/ARCHITECTURE.md#u12-material-graph-studio) for authoring, diagnostics, preview, native persistence and raster advanced surface nodes. Integrate scene features through the same transaction and asset interfaces. Resolve canonical saved assets and their scene dependencies through M20.

Use the shared compiler and M15 material/surface evaluation across Preview, Scene and Runtime. Verify accepted opaque and refractive graphs with the applicable M21 route policy. Use a matching Forward route when a graph or its resources exceed the visibility path's contract. Surface unsupported combinations in Studio with the effective route.

## 2. Scene tools

- Probes use M15 ownership, codecs and range/placement tools. Treat alternate encoding as a versioned render/cache generation with a comparable quality/lifetime reference and effective-route diagnostics.
- Decals carry a stable asset/material reference, authored transform and order; extraction resolves eligible receivers, resources and bounded per-view draw work. Keep composition and mask/order semantics consistent through supported passes. Track pass resources and descriptor lifetime through frame completion.
- Prefab variants store stable base identity and typed operations at stable entity/component/property paths. Compose bases and overrides in deterministic order using Scene/Assembly services. Validate changes before instantiation and show unresolved references separately from valid overrides. Revert removes the authored override and resolves the inherited value. Preserve traceable nested dependencies for M20 and never mutate the base asset while editing a variant.

## 3. Handoff

Ship reproducible fixtures for one basic graph, an advanced transparent variant, overlapping probe/decal use, and nested variants with reverted and unresolved overrides. Establish workflow state equivalence across save/reopen and capture equivalent rendering inputs across the supported paths. M24 owns broad production default qualification.
