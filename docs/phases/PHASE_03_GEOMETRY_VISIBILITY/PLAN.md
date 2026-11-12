# Phase 03 — Geometry and Visibility Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 architecture foundation, unified RenderService, and Hi-Z depth pyramid infrastructure.

---

## 1. Work Breakdown Structure

### C14-01: Unified GPU Instance Buffer
- [ ] Define compact GPU instance structure (Transform, Bounds, Material ID, Visibility Flags).
- [ ] Implement GPU instance buffer allocation, streaming, and generation invalidation in `RHISceneRenderer`.
- [ ] Unit tests for multi-instance buffer updates and memory alignment.

### C14-02: GPU-Driven Culling & Indirect Dispatch
- [ ] Compute shader for frustum culling and normal cone backface culling.
- [ ] Two-phase Hi-Z occlusion culling compute pass against depth pyramid.
- [ ] Indirect draw command buffer generation (`ExecuteIndirect` / `vkCmdDrawIndexedIndirect`).
- [ ] Automated regression verifying culling correctness against reference CPU frustum culling.

### C14-03: Meshlet Asset Generation & Schema
- [ ] Integrate `meshoptimizer` cluster builder (`meshopt_buildMeshlets`, $\le 64$ verts, $\le 126$ tris).
- [ ] Serialize meshlet clusters, vertex indices, and bounding cones into `.axlebin` format.
- [ ] Add debug cluster coloring mode in Viewport for authoring inspection.

### C14-04: Cluster DAG Hierarchy & Watertight Transitions
- [ ] Implement cluster grouping and decimation into a multi-level DAG hierarchy.
- [ ] Enforce locked shared boundary loops to eliminate cracks during LOD transitions.
- [ ] Automated geometric crack-detection fixture testing against high-contrast backgrounds.

### C14-05: Hardware Task & Mesh Shaders
- [ ] Implement Task Shader evaluating projected screen error and culling.
- [ ] Implement Mesh Shader rasterizing cluster primitives via NRI pipeline states.
- [ ] Native D3D12 and Vulkan rendering verification on supported GPUs.

### C14-06: Capability Detection & Indexed Fallback
- [ ] Runtime capability detection querying NRI device mesh shader features.
- [ ] Seamless fallback path mapping culled clusters to classical indexed draw calls.
- [ ] Pixel-parity regression ensuring fallback matches mesh shader output within tolerances.

### C14-07: Dense-Scene Scale Verification
- [ ] Benchmark dense scene fixture ($100,000+$ instances, $50\text{M}+$ polygons).
- [ ] Measure CPU frame-time reduction vs legacy CPU draw submission.
- [ ] Final delivery report and acceptance review.

---

## 2. Review and Evidence Requirements

1. **Correctness Fallback:** Systems must produce valid visuals on both modern (mesh-shader capable) and legacy (indexed fallback) hardware.
2. **Watertight Integrity:** No seam cracks or LOD popping artifacts permitted during dynamic camera motion.
3. **Evidence Archiving:** Performance benchmarks and image comparisons archived in `docs/reviews/`.
