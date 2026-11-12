# Milestone 15 — Scalable Geometry & Virtual Geometry Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 architecture foundation, unified RenderService, and Hi-Z depth pyramid infrastructure.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## 1. Executive Summary

Milestone 15 establishes scalable, high-throughput geometry rendering for AxleEngine. It combines an offline algorithmic decimation pipeline (**Phase 45.1 autoLOD**, formerly OPT-16) for universal compatibility across animated/skinned characters and fallback devices, with a real-time GPU-driven virtual geometry pipeline (**Phase 45.2 Meshlets & Task/Mesh Shaders**, formerly OPT-14) for rendering massive static scenes with zero CPU submission bottlenecks.

---

## 2. Completed Work Packages

*(None yet — Milestone 15 is an unreleased, proposed milestone).*

---

## 3. Checklist

### Phase 45.1: Offline autoLOD Pipeline via `meshoptimizer` (formerly OPT-16)
- [ ] **C16-01: Automated Mesh Decimation via `meshoptimizer`** — Integrate `meshopt_simplify` into the Asset Cooker to automatically generate discrete fallback LODs (LOD0 100%, LOD1 50%, LOD2 25%, LOD3 10%) on mesh import without DCC manual modeling.
- [ ] **C16-02: Screen-Size Metrics & Hysteresis** — Implement projected bounding-sphere screen coverage thresholds and temporal dither cross-fading to eliminate visual pop during LOD transitions.
- [ ] **C16-03: Shadow Caster LODs & Cooker Caching** — Implement aggressive LOD selection for shadow depth passes and binary asset caching in `.axleasset` / `.axlebin`.

### Phase 45.2: Nanite-Style Virtual Geometry & GPU Culling (formerly OPT-14)
- [ ] **C14-01: Unified GPU Instance Buffer** — Define compact GPU instance structure (Transform, Bounds, Material ID, Visibility Flags) and streaming ring buffer in `RHISceneRenderer`.
- [ ] **C14-02: GPU-Driven Culling & Indirect Dispatch** — Frustum culling, normal cone culling, and two-phase Hi-Z occlusion culling compute passes emitting directly to indirect draw buffers (`ExecuteIndirect` / `vkCmdDrawIndexedIndirect`).
- [ ] **C14-03: Meshlet Asset Generation & Schema** — Partition dense meshes into clusters ($\\le 64$ verts, $\\le 126$ tris) via `meshopt_buildMeshlets`; serialize bounding cones and cluster debug coloring mode.
- [ ] **C14-04: Cluster DAG Hierarchy & Locked Seams** — Recursively group clusters into a Directed Acyclic Graph (DAG) with locked shared boundary loops to guarantee watertight, 100% crack-free transitions.
- [ ] **C14-05: Hardware Task & Mesh Shaders** — Implement Task (Amplification) Shaders evaluating projected screen error for dynamic continuous LOD selection, and Mesh Shaders rasterizing cluster primitives via NRI.
- [ ] **C14-06: Capability Detection & Indexed Fallback** — Runtime capability query automatically routing non-mesh-shader hardware to GPU-culled discrete LOD indirect draws.
- [ ] **C14-07: Dense-Scene Scale Verification** — Benchmark dense scene fixture ($100,000+$ instances, $50\\text{M}+$ polygons) measuring CPU draw submission reduction and frame-time stability.

---

## 4. Review and Evidence Requirements

1. **Correctness Fallback:** Systems must produce valid visuals on both modern (mesh-shader capable) and legacy (indexed fallback) hardware.
2. **Watertight Integrity:** No seam cracks or LOD popping artifacts permitted during dynamic camera motion.
3. **Evidence Archiving:** Performance benchmarks and image comparisons archived in `docs/reviews/`.
