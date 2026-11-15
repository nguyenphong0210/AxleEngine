# Milestone 17 — Scalable Geometry & Virtual Geometry Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 architecture foundation, unified RenderService, and Hi-Z depth pyramid infrastructure.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## 1. Executive Summary

Milestone 17 establishes scalable, high-throughput geometry rendering for AxleEngine. It combines an offline algorithmic decimation pipeline (**Phase 46.1 autoLOD**, formerly OPT-16) for universal compatibility across animated/skinned characters and fallback devices, with a real-time GPU-driven virtual geometry pipeline (**Phase 46.2 Meshlets, Visibility Buffer & Task/Mesh Shaders**, formerly OPT-14) and **Phase 46.3 Clustered Forward+** for dense opaque and transparent scenes.

---

## 2. Completed Work Packages

*(None yet — Milestone 17 is an unreleased, proposed milestone).*

---

## 3. Checklist

### Phase 46.1: Offline autoLOD Pipeline via `meshoptimizer` (formerly OPT-16)
- [ ] **C17-01: Automated Mesh Decimation via `meshoptimizer`** — Integrate `meshopt_simplify` into the Asset Cooker to automatically generate discrete fallback LODs (LOD0 100%, LOD1 50%, LOD2 25%, LOD3 10%) on mesh import without DCC manual modeling.
- [ ] **C17-02: Screen-Size Metrics & Hysteresis** — Implement projected bounding-sphere screen coverage thresholds and temporal dither cross-fading to eliminate visual pop during LOD transitions.
- [ ] **C17-03: Shadow Caster LODs & Cooker Caching** — Implement aggressive LOD selection for shadow depth passes and binary asset caching in `.axleasset` / `.axlebin`.

### Phase 46.2: Nanite-Style Virtual Geometry, Visibility Buffer & GPU Culling (formerly OPT-14)
- [ ] **C17-04: Unified GPU Instance Buffer** - Define a compact, generation-checked `GPUInstanceData` record (stable instance ID, transform/previous transform, bounds, meshlet and fallback-LOD ranges, material-table index and visibility flags) plus a frame-owned streaming ring buffer populated from the immutable RenderWorld snapshot.
- [ ] **C17-05: GPU-Driven Culling & Indirect Arguments** - Frustum, normal-cone and two-phase Hi-Z occlusion culling compute passes emit indexed indirect arguments and diagnostics. Keep a CPU/legacy indexed fallback until parity passes.
- [ ] **C17-06: Meshlet Asset Generation & Schema** - Partition dense meshes into clusters ($\le 64$ verts, $\le 126$ tris) via `meshopt_buildMeshlets`; serialize bounds, cones, locked seams and stable cluster identity.
- [ ] **C17-07: Cluster DAG Hierarchy & Locked Seams** - Recursively group clusters into a DAG with locked shared boundary loops and deterministic transition tests.
- [ ] **C17-08: Visibility Buffer Pass** - Add an opaque visibility pass with a documented cluster/triangle/primitive identity and depth format, overflow policy, alpha-test boundary and graph-declared output lifetime.
- [ ] **C17-09: Visibility Resolve & Bindless Material Table** - Register a dedicated `VisBufferResolvePass` after visibility, clustered-light and material-table preparation. Reconstruct barycentrics/derivatives, use typed bindless texture/sampler tables where supported, and retain a fixed-slot fallback.
- [ ] **C17-10: Hardware Task & Mesh Shaders** - Add NRI task/mesh pipeline and command seams only after capability and shader-reflection gates pass; rasterize the same visibility identity as the indexed fallback.
- [ ] **C17-11: Capability Detection & Indexed Fallback** - Route devices without mesh shaders, descriptor indexing or visibility resolve support to GPU-culled discrete LOD indirect draws or the existing Forward path without mutating authored scene data.
- [ ] **C17-12: Dense-Scene Scale Verification** - Benchmark dense scenes (instance and polygon counts declared) for CPU submission reduction, visibility bandwidth, resolve cost and frame-time stability.

### Phase 46.3: Clustered Forward Light Culling & Scalable Lighting Pipeline
- [ ] **C17-13: 3D Frustum Cluster Grid Compute Pass** - Reuse M15 froxel math where valid, but freeze tile dimensions, logarithmic/exponential z slicing, camera range and resource identity as a lighting contract.
- [ ] **C17-14: GPU Light Culling & Index List Emission** - Test directional, point, spot and rect bounds against clusters; emit deterministic offsets/indices, overflow counters and a bounded fallback policy.
- [ ] **C17-15: ForwardPBR and Transparency Clustered Integration** - Update opaque visibility resolve and transparent ForwardPBR/Material Graph to consume the same cluster grid, with no duplicate light accumulation.
- [ ] **C17-16: Material-Divergence Policy** - Add measured tile/material bins, specialized built-in PBR families and wave-level same-material fast paths with a generic fallback; cap shader-family/permutation growth.
- [ ] **C17-17: Shared Scheduling and Evidence** - Register `ClusterLightCulling` before opaque resolve and transparent Forward+, validate graph dependencies and archive overflow, parity, timing and OpenGL 4.5 fallback evidence.

---

## 4. Review and Evidence Requirements

1. **Correctness Fallback:** Systems must produce valid visuals on both modern (mesh-shader capable) and legacy (indexed fallback) hardware.
2. **Watertight Integrity:** No seam cracks or LOD popping artifacts permitted during dynamic camera motion.
3. **Evidence Archiving:** Performance benchmarks and image comparisons archived in `docs/reviews/`.
