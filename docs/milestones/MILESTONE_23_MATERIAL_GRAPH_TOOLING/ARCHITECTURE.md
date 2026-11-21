# Milestone 23 — Material Graph and Scene Tools Architecture

## 1. Graph authoring and shared runtime

The Editor owns the working document, graph layout and input interactions. Valid semantic edits go through typed transactions and existing parser/IR validation. Keep stable IDs for nodes and parameters, check connection types and cycles, and attach diagnostics to the authored node/field. Persist semantic graph data through the native asset format and keep UI layout separate from shader semantics and cache keys. Install only valid complete graph generations. An asynchronous compile consumes a frozen revision; late completions cannot overwrite a newer generation. Retain the last usable graph and preview on invalid candidates. Save atomically and pass the canonical saved asset through M20 dependency enumeration.

The shared graph compiler provides reflected backend artifacts for Preview, scene and Runtime. Reuse M15 material/surface evaluation and M21 route policy. Establish transparent/refractive variants before exposing advanced pins as enabled features. Use a matching Forward route when the validated graph or its resources exceed the visibility path's contract. Keep declared unsupported features visible with actionable diagnostics. Preview uses isolated state and targets with the shared render service.

## 2. Scene tools

- Probes use M15 ownership, codecs and range/placement tools. Treat alternate encoding as a versioned render/cache generation with a comparable quality/lifetime reference and effective-route diagnostics.
- Decals carry a stable asset/material reference, authored transform and order; extraction resolves eligible receivers, resources and bounded per-view draw work. Keep composition and mask/order semantics consistent through supported passes. Track pass resources and descriptor lifetime through frame completion.
- Prefab variants store stable base identity and typed operations at stable entity/component/property paths. Compose bases and overrides in deterministic order using Scene/Assembly services. Validate changes before instantiation and show unresolved references separately from valid overrides. Revert removes the authored override and resolves the inherited value. Preserve traceable nested dependencies for M20 and never mutate the base asset while editing a variant.

## 3. Handoff

Ship reproducible fixtures for one basic graph, an advanced transparent variant, overlapping probe/decal use, and nested variants with reverted and unresolved overrides. Establish workflow state equivalence across save/reopen and capture equivalent rendering inputs across the supported paths. M24 owns broad production default qualification.
