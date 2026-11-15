# Milestone 17 - Scalable Geometry and Virtual Geometry Architecture

## 1. Target and scope

Milestone 17 adds a scalable geometry path without removing the Forward raster path. It combines:

- Tier 1 offline meshoptimizer LODs for skinned/deforming assets, transparency and legacy hardware.
- Tier 2 meshlet/cluster data, GPU instance streaming and Hi-Z culling for dense opaque scenes.
- An opaque Visibility Buffer plus compute material resolve.
- Clustered Forward+ lighting shared by opaque resolve and transparent raster.
- Capability gates that route missing mesh shaders, bindless descriptors or visibility support to indexed indirect or legacy Forward rendering.

M16 owns RT acceleration structures and secondary lighting. M17 consumes a bounded RT proxy identity when RT is enabled; it does not build AS or own RT shader tables.

Supporting references: POST_OPT10_PROGRAM_PLAN.md and POST_OPT10_FEASIBILITY.md in the archive.

---

## 2. Architecture and pipeline

The frame path is:

    Raw mesh -> asset cooker -> LODs, meshlets, cluster DAG and optional RT proxy
    RenderWorld snapshot -> GPUInstanceUpload -> GPU culling/Hi-Z
    RenderWorld lights -> ClusterLightCulling
    GPU culling -> VisibilityBufferPass -> VisBufferResolvePass -> opaque HDR
    ClusterLightCulling -> VisBufferResolvePass and Transparent Forward+ -> post process

The modern branch uses task/mesh rasterization when supported. The baseline branch uses indexed indirect rasterization. Both branches produce the same visibility identity.

The current RenderPassScheduler can host these as explicit ordered passes with named RenderGraph dependencies. Async overlap and true topological scheduling remain a separate graph upgrade and are not assumed by this milestone.

### 2.1 Tier 1: offline LOD

The cooker uses meshopt_simplify to produce documented LOD0..LOD3 chains, screen-size hysteresis and shadow-caster LODs. The same cooker may emit a bounded RT proxy for M16, but M16 must remain runnable when that future M17 artifact is absent.

### 2.2 GPU instance and culling contract

GPUInstanceData is a renderer-owned, packed, generation-checked record derived from RenderWorld. It contains stable instance identity, transform/previous transform, bounds, meshlet and fallback-LOD ranges, material-table index and visibility flags. The upload ring and indirect argument buffers are frame-owned and retained until completion.

Culling emits deterministic indexed indirect arguments first. Task/mesh shader dispatch is an optional path that must produce the same visibility identity and fallback image.

### 2.3 Meshlet and cluster data

meshopt_buildMeshlets produces bounded clusters (at most 64 vertices and 126 triangles in the default profile), tight bounds/cones and a stable cluster identity. Cluster DAG transitions use locked boundary loops and automated crack tests. Scene and asset limits must reject identity overflow rather than truncate IDs.

### 2.4 Visibility Buffer and resolve

VisibilityBufferPass covers opaque geometry that has a stable cluster/triangle/primitive identity. The format, depth convention, multi-draw partitioning, alpha-test boundary and overflow behavior are frozen before shader work.

VisBufferResolvePass is a dedicated scheduler pass. It reads visibility, material tables, cluster light data and optional RT secondary outputs, reconstructs barycentrics/derivatives and writes the opaque HDR target. It is independent from the legacy Forward material callback so that graph lifetime, readback and fallback behavior can be tested separately.

Bindless resolve uses typed texture/sampler tables only on a validated descriptor-indexing tier. Each table entry has a generation/default-resource policy. Fixed-slot ForwardPBR remains the fallback for OpenGL 4.5, unsupported descriptor tiers and incompatible custom graphs.

### 2.5 Clustered Forward+ contract

ClusterLightCulling builds a view-frustum grid with frozen tile dimensions, logarithmic/exponential z slicing, light bounds and index-list capacity. It emits deterministic cluster offsets/indices plus overflow counters. Directional, point, spot and rect lights are classified explicitly.

VisBufferResolvePass and transparent Forward+ consume the same grid. Overflow selects a bounded deterministic fallback and publishes a diagnostic; it never silently drops an arbitrary subset of lights.

### 2.6 Material divergence policy

The first resolve implementation uses tile/material bins and a bounded set of built-in PBR shader families. Waves with a common material key use a fast path; mixed waves use scalar table fetches or a generic graph path. Global sorting and large permutation growth are opt-in experiments after the tile path has parity and timing evidence.

### 2.7 Capability and fallback policy

- Mesh/task shader unavailable: GPU-culled indexed indirect draws.
- Descriptor indexing/unbounded arrays unavailable: fixed-slot material bindings.
- Visibility resolve unavailable or unsupported material: ForwardPBR raster.
- OpenGL 4.5: existing Forward path is the final safe tier.
- RT unavailable: M16 policy resolves SSR, planar reflection, local probes and sky IBL.

All fallbacks use the same RenderWorld/material contract and do not mutate authored scene data.

---

## 3. RenderGraph and lifetime constraints

Visibility, cluster, material-table and indirect buffers are graph-declared resources. Future RT secondary targets are declared by M16 and read by resolve only after their generation is complete. The graph must validate resource generation, pass dependencies, overflow/readback buffers and per-view history invalidation.

The present graph is sequential and whole-texture oriented. A later graph revision may add buffer/AS/SBT resource kinds, queue ownership and transient aliasing, but those features must not be inferred from the RFC alone.

---

## 4. Technology and constraints

1. Task/Mesh Shaders require a declared D3D12/Vulkan capability and a new RHI command/pipeline seam; indexed indirect is the baseline.
2. DXIL and SPIR-V shader reflection must agree for visibility IDs, descriptor indexing and material tables.
3. Visibility derivative reconstruction, alpha-tested geometry and transparent materials require dedicated parity fixtures.
4. Dense-scene benchmarks must report instance/triangle counts, resolution, GPU, visibility bandwidth, resolve time and cluster overflow.
5. OpenGL 4.5 and other fallback backends remain supported throughout the milestone.

## 5. Acceptance boundary

M17 closes only when GPU instance identity, visibility reconstruction, bindless/fixed-slot parity, clustered overflow policy, mesh/indexed fallback parity and deterministic image/readback evidence pass. RT proxy consumption is validated jointly with M16 but M17 does not own AS lifetime.
