# M16/M17 Hybrid Rendering RFC — Independent Architectural Review

**Reviewed document:** [`RFC_HYBRID_NANITE_CLUSTERED_RAYTRACING.md`](../design/RFC_HYBRID_NANITE_CLUSTERED_RAYTRACING.md)  
**Review status:** **Conditionally feasible; proposal accepted as a staged direction, not as an implementation acceptance**  
**Review scope:** NVIDIA NRI, Slang/DXC, RenderGraph, RenderPassScheduler, ViewContext, RenderWorld and EnTT extraction boundary  
**Baseline:** post-M15 roadmap shift commit `fad0df92`

## 1. Decision summary

The three-part pipeline is architecturally sound if it is delivered as separate capability-gated stages:

1. M15 keeps the authored/probe/SSR/planar raster reflection chain and closes the material, view and graph contracts.
2. M16 owns hardware ray tracing, AS/SBT lifetime, RT shadows/reflections, denoising and raster fallback.
3. M17 owns meshlets, GPU-driven visibility, visibility-buffer resolve and clustered Forward+ lighting.

The RFC's four gaps are real and should be recorded in the plans. The RFC should not be treated as proof that the engine is already ready: the current code has capability discovery and useful primitives, but not the complete production path. In particular, the current `RenderGraph` is explicitly sequential and borrows resources, `RenderPassScheduler` executes an ordered vector, `RHICommandBuffer` has indexed/indirect draw and compute dispatch but no mesh dispatch wrapper, and the scene renderer still owns fixed texture/light arrays.

The recommendation is therefore **accept with conditions**:

- keep the hybrid topology and the OpenGL 4.5 Forward fallback;
- downgrade “100% Ready” claims to “foundation present / implementation required”;
- add explicit proxy, visibility-buffer, bindless, cluster and divergence gates to M16/M17;
- require deterministic parity, resource-generation and budget evidence before enabling each tier.

## 2. Feasibility against the current engine

| Subsystem | Verified current state | Feasibility judgement | Required condition |
|---|---|---|---|
| **NVIDIA NRI** | `RHIDevice` probes `desc.tiers.rayTracing`, `desc.features.meshShader` and bindless tiers. The bundled NRI source contains AS build, RT pipeline and `CmdDispatchRays` backends for Vulkan/D3D12. | **Feasible, capability layer only.** Native API primitives exist, but Axle does not yet expose an engine-owned AS/pipeline/SBT lifecycle or ray dispatch path. | Complete M16 RHI wrappers, leases, graph states, shader table and reset/fallback tests. Do not let NRI handles escape into Scene. |
| **Slang/DXC** | DXC is the normal shader compiler. Slang is an optional graph compiler and can pass through DXC; its reflection adapter currently supports a constrained resource model, including individual read-only `Texture2D` bindings. | **Feasible with a toolchain gate.** Mesh/RT syntax is not equivalent to having validated entry points, reflection, descriptor indexing and both backend targets. | Add shader profiles, descriptor-indexing validation, RT/mesh compile fixtures and a last-good fallback. Do not claim universal Slang support for arbitrary bindless graphs. |
| **RenderGraph** | `RenderGraph.hpp` describes a “sequential, whole-texture graph”; resources are borrowed. It has named dependencies and buffer declarations, but no general topological scheduler or transient aliasing allocator. | **Not ready for the RFC's async/DAG claim.** It can host a first ordered implementation, but parallel compute/RT and AS/SBT lifetime need extensions. | Add explicit resource kinds/states for AS/SBT/indirect/visibility/cluster buffers, dependency validation and only then enable async queues or aliasing. |
| **RenderPassScheduler** | Scheduler stores `std::vector<std::unique_ptr<IRenderPass>>` and runs passes in registration order. | **Suitable seam, limited scheduling.** Dedicated passes are possible now; automatic parallel ordering is not. | Register explicit pass nodes and dependency names; add topological validation before relying on overlap. |
| **ViewContext** | View input includes size, jitter, scene/material/feature generations and camera-cut invalidation. It is non-copyable and currently represents one recording view. | **Good primary-view boundary.** Secondary planar/RT captures need a child/owned capture context or a service that creates an isolated `ViewContextInput`. | Keep RT/planar history per view and generation; never mutate the primary view while recording a secondary capture. |
| **RenderWorld + EnTT** | RenderWorld is an immutable, ECS-free snapshot. `RenderMeshInstance` contains stable/entity IDs, transforms, native mesh/view and material data, but no meshlet, GPU-instance or RT-proxy records. | **Good extraction boundary; data contract incomplete.** EnTT storage alone does not provide a packed, generation-safe GPU stream. | Add a renderer-owned packed upload stage with stable identity, bounds, meshlet/proxy ranges and material-table indices. Keep GPU records out of ECS and RenderWorld. |

### 2.1 Current source anchors

- `Engine/Renderer/include/Axle/RenderGraph.hpp`: sequential graph, borrowed resources, explicit named dependencies.
- `Engine/Renderer/include/Axle/RenderPass.hpp`: ordered scheduler and only a boolean `RayTracing` capability.
- `Engine/Renderer/include/Axle/RenderWorld.hpp`: immutable snapshot boundary; no GPU-instance/meshlet/proxy fields.
- `Engine/RHI/include/Axle/RHI/RHICommandBuffer.hpp`: compute and indirect draw wrappers; no `DispatchMesh`/task-mesh abstraction.
- `Engine/RHI/src/RHIDevice.cpp`: capability discovery is present, but discovery is not an implementation.
- `Engine/Scene/src/RHISceneRendererPasses.cpp` and `RHISceneRenderer.cpp`: fixed `ForwardTextureCount` and per-draw descriptor setup.
- `Engine/RHI/src/RHILight.cpp`: fixed `RHIMultiLightBufferData` and `MAX_POINT_LIGHTS`/`MAX_SPOT_LIGHTS` limits.
- `Engine/RHI/src/RHISlangCompiler.cpp`: optional Slang graph compiler with a constrained reflection/resource contract.

## 3. Review of the four RFC gaps

### Gap 1 — GPU-driven instance buffer

**Verdict: valid and high priority for M17.** A `StructuredBuffer<GPUInstanceData>` is the right GPU boundary, but the RFC should define the record and lifetime rather than only the type name. The record needs a stable instance ID/generation, transform or transform index, bounds, meshlet and fallback-LOD ranges, material-table index, visibility/shadow flags and (if motion vectors are required) previous-transform data. Uploads should be packed from the immutable RenderWorld snapshot into a frame-owned ring or retained generation.

The implementation must also define culling output and indirect argument ownership. The current command abstraction can issue indexed indirect draws, so that is the first fallback. Mesh/task shader execution requires a new RHI command/pipeline contract. A CPU draw loop must remain available until GPU-driven output passes image and timing parity.

**Plan disposition:** included as M17 `C17-04` and `C17-05`, with stable identity, upload lifetime, indirect arguments and capability fallback.

### Gap 2 — unbounded bindless texture array

**Verdict: valid, but the RFC overstates current readiness.** The engine already has a Tier-2 bindless descriptor pool/registry, while ForwardPBR still allocates a fixed `ForwardTextureCount` array. Moving resolve to descriptor indexing requires:

- backend-specific descriptor indexing/resource-binding feature gates;
- typed texture/sampler tables and default resources (one `Texture2D g_Textures[]` cannot represent every dimension/format);
- stable texture and sampler indices with generation validation and deferred eviction;
- shader reflection/compiler checks for unbounded ranges;
- a fixed-slot fallback for OpenGL 4.5, unsupported Vulkan/D3D12 tiers and custom graphs that cannot be indexed safely.

The first implementation should bind built-in PBR material tables. It should not force every custom Material Graph permutation to become bindless in one change.

**Plan disposition:** included as M17 `C17-09`; M15 remains the owner of the descriptor registry/lifetime foundation.

### Gap 3 — 3D clustered light grid

**Verdict: valid and necessary for the stated light-count goal.** The static light buffer is a real bottleneck. The grid must be a contract rather than a hard-coded `16 x 16 x 32` promise: tile dimensions, logarithmic/exponential z slicing, camera near/far range, light bounds, directional/spot/rect treatment, index-list capacity, overflow diagnostics and transparent depth policy all need to be frozen.

The same grid/index list should feed opaque visibility resolve and transparent Forward+. Light culling should be deterministic and independently testable, with a bounded overflow fallback (for example, a secondary overflow list or a reduced legacy light set). Reusing M15 froxel math is useful, but the cluster resource is not automatically identical to the fog froxel resource.

**Plan disposition:** included as M17 `C17-13` to `C17-15`, with overflow and transparent-consumer gates.

### Gap 4 — coarse proxy mesh for BLAS/TLAS

**Verdict: valid and required.** Building BLAS over Nanite micro-triangles is an unacceptable default. However, a fixed “10–20%” ratio is not a universal quality contract. Proxy generation must preserve the material/alpha classification, normals/UVs needed by the hit shader, stable primitive/instance identity and the chosen shadow/reflection policy. Skinned/deforming meshes need a refit or rebuild rule; alpha-tested and two-sided geometry need explicit masks.

M16 precedes M17, so M16 cannot depend on a future M17 autoLOD artifact. M16 therefore owns a minimal cooker/runtime **RT proxy contract** and can reuse M17's decimator later. Proxy triangle count is budget-driven per asset and RT tier. A last-good proxy/BLAS must survive a failed rebuild, device reset or scene replacement.

**Plan disposition:** added to M16 `M16-RT-02.1` as a contract and to `M16-RT-02.2` as static proxy BLAS input. M17 autoLOD may produce a compatible proxy, but is not a hard dependency for M16 startup.

## 4. Answers to the RFC review questions

### 4.1 `VisBufferResolvePass` orchestration seam

Register it as a **dedicated `IRenderPass` node** in `RenderPassScheduler`. It should declare and validate these edges:

```text
GPUInstanceUpload -> GeometryCull -> VisibilityBuffer
ClusterLightCulling ----------------------┐
VisibilityBuffer --------------------------+-> VisBufferResolve
RTShadow/RTReflection (optional) ----------┘
VisBufferResolve -> TransparentForward -> PostProcess
```

In practice, RT secondary lighting can either be read by the resolve or be composited by a separate `HybridSecondaryResolve` immediately before/after it. Keeping the resolve independent from the legacy Forward material callback gives it its own resource lifetime, deterministic readback and fallback tests. The current scheduler can enforce registration order plus named graph dependencies; add topological validation before promising asynchronous overlap.

### 4.2 Material divergence in compute resolve

Use a **hybrid tile/material-bin strategy**. Classify pixels into small tiles by a compact material/shader-family key, dispatch specialized built-in PBR bins first, and use a generic graph interpreter for the remainder. Within a bin, use wave-level same-material fast paths; when a wave is mixed, use scalar material fetches rather than a large permutation explosion. Keep material-table layout stable and cap the number of shader families per frame.

Wave-only classification is a useful optimization, not a scheduling contract: it does not create locality when neighboring pixels reference unrelated materials. A global sort can improve coherence but can destroy cache locality and complicate temporal/debug identity, so it should be measured after tile binning is working.

### 4.3 Forward raster fallback on OpenGL 4.5

**Agree.** Preserve the existing Forward raster path as the explicit fallback. OpenGL 4.5 cannot be assumed to provide DXR/Vulkan RT, mesh shaders or the same descriptor-indexing semantics. Capability policy should degrade independently: RT off can still use SSR/planar/probe/IBL; mesh-shader off can use GPU-culled indexed indirect; bindless off can use fixed slots; the full legacy Forward path remains the final safe tier. All tiers must consume the same RenderWorld/material contract and publish a diagnostic without mutating authored data.

## 5. Additional risks and alternatives

1. **RenderGraph concurrency claim:** named dependencies do not yet provide a topological/async scheduler. Implement ordered correctness first, then add queue ownership and barriers with tests.
2. **Visibility-buffer limits:** a 24-bit cluster ID and 8-bit triangle ID need a scene-wide identity/generation policy, multi-draw partitioning and overflow diagnostics. Alpha-tested/translucent geometry should remain on specialized raster paths until a separate visibility contract exists.
3. **Derivative reconstruction:** compute reconstruction of UV derivatives, tangent frames and anisotropic filtering can differ from raster interpolation. Add derivative/parity fixtures before replacing ForwardPBR broadly.
4. **RT proxy mismatch:** coarse proxy geometry can create shadow leaks or reflection silhouettes. Compare proxy/source images and expose separate shadow/reflection proxy budgets where needed.
5. **Material/descriptor pressure:** unbounded indexing adds residency, eviction and descriptor-generation hazards. Keep default resources, per-frame validation and a fixed-slot fallback.
6. **Cluster overflow:** thousands of lights can overflow a fixed index list. Record overflow counters and define a deterministic policy instead of silently dropping lights.
7. **Temporal history:** TAA, RT denoise and visibility resolve need camera-cut, resize, scene/material, AS and device-generation rejection. Reusing a stale history is worse than a lower-quality fallback.
8. **Cross-backend shader variance:** DXIL and SPIR-V may differ in descriptor indexing, ray payload layout and wave operations. Compile and reflect both targets in CI; do not infer support from a single DXC success.
9. **Memory and build spikes:** BLAS/TLAS scratch, visibility/material tables and cluster lists need explicit budgets and deferred retirement. Last-good generations are required on failed rebuilds.
10. **Alternative staging:** a Visibility Buffer for opaque geometry plus Forward+ for transparency is the lowest-risk first step. A full Nanite mesh-shader path and RTGI should remain opt-in until the opaque resolve and fallback gates are stable.

## 6. Plan reconciliation

| RFC item | Was already in the plan? | Action taken |
|---|---:|---|
| RT capability, AS/TLAS, SBT, RT shadows/reflections, denoise, fallback | **Yes, M16** | Kept; clarified that capability discovery is not implementation. |
| Coarse proxy geometry for RT | **Partly** (generic BLAS only) | Added explicit M16 proxy contract and budget/lifetime gates. |
| GPU instance buffer and indirect culling | **Partly** (M17, old `C14-*` labels) | Renumbered under M17 and added identity/upload/fallback requirements. |
| Meshlet/task/mesh path | **Yes, M17** | Kept; added RHI mesh-dispatch seam and indexed fallback. |
| Visibility Buffer + `VisBufferResolvePass` | **No explicit checklist item** | Added dedicated M17 passes and scheduler/resource dependencies. |
| Unbounded bindless resolve | **No explicit checklist item** | Added M17 bindless resolve gate; fixed-slot fallback remains. |
| 3D clustered light grid | **Yes, M17** | Expanded with overflow, deterministic indexing and transparent consumer gates. |
| Material divergence policy | **No** | Added a measured tile/bin + wave fast-path gate. |
| OpenGL 4.5 Forward fallback | **Yes as general fallback principle** | Made an explicit per-feature fallback requirement in M17 and the review acceptance. |

The plan changes are intentionally staged. The RFC's ambitious end-state is retained, while no milestone is marked complete merely because a backend exposes a capability bit.

## 7. Acceptance gates for future implementation

- **M16:** D3D12/Vulkan RT fixtures build and retain proxy-backed AS generations; RT shadow/reflection outputs have deterministic parity and raster fallback; reset/resize/device-loss and memory/timing evidence are archived.
- **M17:** GPU instance identity and indirect arguments match the CPU path; visibility IDs and barycentric/material reconstruction pass image/readback fixtures; bindless and fixed-slot paths agree within tolerance; cluster overflow is visible and deterministic; Forward fallback passes on OpenGL 4.5.
- **Cross-milestone:** no raw NRI handle in Scene/RenderWorld; all resources are graph-declared and generation-validated; custom graph/material limitations are named diagnostics.

**Review conclusion:** include the RFC direction and the conditions above in the roadmap. Do not copy its “100% Ready” table into a release or acceptance document until these gates are implemented and evidenced.
