# Milestone 21 — Scalable Geometry and Virtual Geometry Plan

**Status** — Proposed execution plan. Implementation starts at the explicitly released batch or range.
**Baseline** — Accepted M15 U11-E renderer contracts, M16 core at RT-08D and M20 packaged gameplay fixture. Reuse the accepted M17 skeletal path and M18 native indirect fixture.
**Architecture** — [M21 geometry, visibility and lighting contracts](ARCHITECTURE.md).

## 1. Scope and dependencies

Phase 50.1 owns cooked autoLOD. Phase 50.2 owns GPU-driven geometry, meshlets, Visibility Buffer and its resolve. Phase 50.3 owns clustered lighting shared by Forward and visibility shading. These phases identify scope. The execution order below brings forward resource contracts and clustered lighting where another feature needs them.

Phase 50.2 selects continuous LOD from cooked geometry generations admitted to GPU memory. Fine-grained, on-demand geometry page streaming is a separate feature scope.

- Record [M15 U11-E](../MILESTONE_15_LIGHTING_MATERIALS/PLAN.md#s115-batch-u11-e--post-refactor-ab-scorecard-parity-and-zero-debt-m15-closure) source and evidence for stateful passes, shared surface and lighting evaluation, view and capture policy, descriptor lifetimes, frame completion and transient resources.
- Use [M16 RT-01.1 and RT-08D](../MILESTONE_16_RAY_TRACING/PLAN.md) for the backend-neutral capability matrix and the integrated M16 core reference. Verify RT receiver inputs, per-light shadow visibility, reflection resolve and proxy identity against that reference. M16 owns AS, SBT, proxy eligibility and RT coverage. Optional M16 RT-09 has a separate release and acceptance boundary.
- Use the [M17 native skeletal path](../MILESTONE_17_ANIMATION/PLAN.md) for deformed draws, the [M18 shared native indirect fixture](../MILESTONE_18_VFX/PLAN.md) for baseline argument, count and completion validation, and the [M20 derived-product contract](../MILESTONE_20_COOKER/ARCHITECTURE.md) for import/cook integration. Preserve M20's packaged gameplay fixture as the authored-product regression reference. M21 owns geometry-specific indirect arguments, culling, LOD, package sections and fallback validation.
- Use CPU indexed Forward as the reference for GPU culling and ForwardPBR as the reference for visibility shading. First prove indexed visibility with flat meshlets. Add cluster hierarchy and task/mesh rasterization as separate checkpoints against that reference.

## 2. Execution batches

Groups A–I define the delivery order. Each of the 22 numbered batches below owns the listed atomic leaves and delivers a buildable checkpoint. In the table, `NN.n` means `C21-NN.n`. The checklist in Section 3 carries acceptance status.

### A–E — Contracts, autoLOD and indexed geometry

| Batch | Atomic leaves | Deliverable and batch gate |
|---|---|---|
| **M21-A1** | `11.1`, `18.1` | Freeze capability branches, supported view policies and graph contracts. Test rejection of undeclared reads, incompatible generations and invalid resource order. |
| **M21-A2** | `11.2`, `18.2` | Extend and rerun the M18 compute-to-indexed-indirect fixture for geometry argument ABI, count modes, barriers and leases across overlapping frames on D3D12 and Vulkan. |
| **M21-B1** | `01.1`, `03.1` | Static-mesh LOD generation and versioned derived cache. Verify reimport, cache invalidation, package parity and LOD0 recovery. |
| **M21-B2** | `01.2` | Skeletal LOD generation and decode. Render explicitly selected levels in multiple poses on the real skeletal path before automatic selection lands. |
| **M21-C1** | `02.1` | Projected coverage and per-view hysteresis in production camera views. Compare the selected LOD sequence against a reference trace. |
| **M21-C2** | `02.2`, `03.2` | Temporal transitions and shadow-specific LOD policy. Inspect static and animated camera sequences and compare caster silhouettes and contact shadows. |
| **M21-D1** | `04.1`, `04.2` | Pack instances from the immutable snapshot and retain frame generations. Verify reload, ID reuse, capacity pressure and slot reuse after completion. |
| **M21-D2** | `05.1` | GPU frustum culling and discrete-LOD indexed indirect rendering. Verify parity and measure submission and binding costs for repeated and distinct geometry. |
| **M21-E1** | `06.1`, `06.2`, `06.3` | Cook and load flat meshlets. Verify schema, triangle mapping and GPU memory admission with a forced budget and hot reload. |
| **M21-E2** | `05.2` | Add normal-cone culling for eligible meshlets. Compare against a cone-disabled reference including mirrored, nonuniform and two-sided geometry. |
| **M21-E3** | `05.3` | Add a conservative depth pyramid and explicit two-phase graph edges. Verify same-frame recovery at openings and after occluder motion, history resets and view changes. |

### F–I — Lighting, visibility and acceptance

| Batch | Atomic leaves | Deliverable and batch gate |
|---|---|---|
| **M21-F1** | `13.1`, `14.1` | Build the view-frustum grid and emit deterministic light lists. Compare GPU readback against a CPU reference for all light types. |
| **M21-F2** | `14.2`, `15.1` | Integrate clustered opaque and transparent ForwardPBR and supported generated graphs. Validate overflow and light accumulation with overlapping lights and transparent surfaces. |
| **M21-G1** | `08.1`, `08.2` | Freeze visibility identity and record indexed opaque coverage. Verify multi-draw IDs, mixed Forward coverage, clear values and capacity fallback. |
| **M21-G2** | `09.1` | Run the dedicated resolve on a bounded built-in PBR fixture using the existing clustered list. Check reconstructed attributes, derivatives and scene-linear HDR against ForwardPBR. |
| **M21-G3** | `09.2`, `09.3`, `15.2`, `17.1` | Extend resolve to typed bindless materials and supported generated graph paths. Verify shared clustered shading and graph order in mixed visibility/Forward scenes. |
| **M21-G4** | `15.3`, `17.2`, `18.3` | Prepare shared receiver inputs and integrate secondary lighting with M16. Verify RT Off, Shadows and Reflections, per-sample history validity, and graph/resource lifecycle. |
| **M21-H1** | `07.1`, `07.2` | Cook the cluster DAG and select cuts on the GPU for the indexed path. Verify group replacement, budget fallback, seam integrity and temporal behavior before adding mesh shaders. |
| **M21-H2** | `10.1`, `10.2` | Compile, reflect and run native Task/Mesh rasterization. Compare selected clusters, per-pixel identity, depth and final image against indexed rendering of the same cluster cut. |
| **M21-I1** | `16.1`, `16.2` | Measure bounded material bins and wave fast paths against the generic resolve. Verify the generic fallback and record which variants qualify for promotion. |
| **M21-I2** | `11.3`, `12.1`, `12.2`, `17.3` | Freeze the integrated candidate. Run product, fallback, lifecycle, visual and scale gates and archive the scorecards. |
| **M21-I3** | `12.3` | Consolidated independent review, corrections and required rechecks on the final source. Publish the supported configurations and defaults with evidence. |

## 3. Canonical atomic checklist

### Phase 50.1 — Cooked autoLOD

#### C21-01 — Automated mesh decimation

- [ ] **C21-01.1 — Generate static LOD chains during import/cook.** Integrate a pinned meshoptimizer simplifier in the shared derived-geometry build path. Preserve LOD0, material assignment, attribute discontinuities and authored data.
  - **PASS when** suitable fixtures produce LOD1/2/3 near the 50/25/10% triangle targets. A mesh that reaches an error or topology limit retains a valid level, with actual triangle counts, measured errors and the stop reason recorded. Verify valid bounds and indices and deterministic results for repeated equivalent inputs.
- [ ] **C21-01.2 — Generate skeletal and supported deforming LODs.** Preserve the skeletal geometry contract, vertex attributes, weights, joints and material slots. Keep a valid base representation for unsupported deformation.
  - **PASS when** imported and cooked products render supported LODs in multiple poses without invalid weights, slot changes or seams beyond the frozen tolerance. An incompatible derived level selects the valid base representation with a diagnostic.

#### C21-02 — Screen-size selection and transitions

- [ ] **C21-02.1 — Implement projected coverage and hysteresis.** Use conservative projected bounds and view-specific selection history. Freeze thresholds, projection behavior, hysteresis and resets.
  - **PASS when** threshold jitter does not oscillate across levels, expected crossings and resets occur, and distinct views select independently. Cover near-plane intersections, scale changes, viewport resizing and camera cuts.
- [ ] **C21-02.2 — Integrate temporal transitions.** Use consistent dither coverage across geometry, depth, normal, motion and picking as applicable. Preserve blended-surface ordering and alpha semantics under the declared transition policy.
  - **PASS when** motion sequences show no LOD pop, depth holes, doubled coverage, incorrect picking or persistent ghosting. Exercise supported opaque, cutout, blended and animated assets with the applicable TAA and MSAA modes.

#### C21-03 — Shadow LOD and cache lifetime

- [ ] **C21-03.1 — Version and validate derived mesh data.** Key cache entries by source identity and revision/content, cook profile, schema and tool version. Publish complete products with validated section ranges and material mapping. Include derived payloads in the normal packaged asset path.
  - **PASS when** changing relevant inputs invalidates the product, identical inputs produce deterministic results, and Editor loose and Runtime packaged paths use equivalent LODs. Missing or corrupt optional data recovers LOD0 from a valid base product. Mounted packages remain read-only.
- [ ] **C21-03.2 — Select a shadow-caster LOD independently.** Use light-space/cascade coverage, geometry error and material policy to permit coarser casters within the shadow tolerance.
  - **PASS when** the selected caster reduces triangles on the scale fixture and preserves contacts, cutouts and silhouette within the frozen limits. Off-camera casters and independently rendered cascades remain correct under camera and light movement.

### Phase 50.2 — GPU-driven geometry and visibility

#### C21-04 — GPU instance stream

- [ ] **C21-04.1 — Define and pack the instance ABI.** Include stable instance identity and generation, current/previous transforms, conservative bounds, geometry/material references, meshlet and discrete LOD ranges and visibility flags. Pack from the immutable RenderWorld snapshot.
  - **PASS when** CPU layout assertions and DXIL/SPIR-V use agree, and extraction fixtures preserve stable identity, material mapping and motion across reorder, spawn/despawn and reload.
- [ ] **C21-04.2 — Retain frame stream generations.** Use completion-owned upload slices and retain the exact geometry/material generations they reference.
  - **PASS when** overlapping views/frames, ring exhaustion, failed submission and device reset preserve valid in-flight data, publish deterministic fallback/diagnostics on failure and safely reclaim retired slices.

#### C21-05 — GPU culling and indirect commands

- [ ] **C21-05.1 — Emit frustum-culled discrete-LOD indexed commands.** Group compatible pipelines and geometry bindings. Define the draw record lookup, capacity, deterministic output ordering and submission policy for count and fixed-range modes.
  - **PASS when** native argument and instance readbacks, submitted draws and image coverage match a CPU reference at identical LOD. Include mixed meshes, nonzero index/vertex/instance offsets, empty outputs and command overflow. Record CPU submission time, binding changes and indirect batch counts for repeated and distinct geometry, and document whether the observed binding cost warrants shared geometry storage.
- [ ] **C21-05.2 — Add safe meshlet normal-cone rejection.** Define transform and material conditions for cone use, with a conservative path for unproven bounds.
  - **PASS when** visible coverage agrees with the cone-disabled reference for near-plane, nonuniform-scale, mirrored and two-sided cases, while a suitable workload demonstrates genuine cone rejection.
- [ ] **C21-05.3 — Add two-phase conservative Hi-Z occlusion.** Use compatible history to defer candidates, render first-phase depth from valid current-view coverage, and retest deferred work against current conservative depth before final visibility. Preserve the existing SSR reduction semantics.
  - **PASS when** the graph proves first-phase depth feeds the current pyramid and retested draws contribute to final coverage in the same frame. Verify no false rejections in margin, thin-occluder, opening, odd-size and moving-camera/occluder fixtures. Include a change of occluder LOD or route that exposes an opening, and verify history invalidation, fail-open behavior and measured rejection for genuinely occluded geometry.

#### C21-06 — Flat meshlet product

- [ ] **C21-06.1 — Cook bounded meshlets and retain source correspondence.** Generate meshlets of at most 64 vertices and 126 triangles in the default profile. Preserve stable cluster IDs, source triangle and material mapping, conservative bounds/cones and locked boundary metadata.
  - **PASS when** deterministic static fixtures reproduce original triangle coverage and material assignment, and every meshlet respects the profile limits and measured bounds.
- [ ] **C21-06.2 — Serialize and validate meshlet data.** Version ranges and identity within the derived geometry product, and define invalidation on geometry revisions.
  - **PASS when** loose and cooked loads agree, malformed ranges/IDs fail safely, and an unavailable or incompatible meshlet product routes to the corresponding discrete-LOD draw.
- [ ] **C21-06.3 — Admit complete geometry generations within a GPU memory budget.** Account for meshlet and hierarchy data, fallback geometry and overlapping generations. Track upload staging separately by memory domain. Establish an available indexed fallback before admitting an optional product. Report admission and fallback reasons by asset.
  - **PASS when** constrained-budget fixtures on both native backends reject an oversized product or replacement, retain complete raster coverage through an available budgeted fallback, and recover when the replacement fits. Hot reload and failed upload keep memory bounded while protecting in-flight generations.

#### C21-07 — Cluster DAG

- [ ] **C21-07.1 — Cook a deterministic hierarchy with locked shared boundaries.** Record complete group replacements with shared conservative bounds and error. Validate parent/child coverage, stable IDs, material mapping and compatible seams.
  - **PASS when** topology validation detects cycles, invalid ranges, incomplete group replacements and broken coverage. Adjacent levels preserve shared boundary loops in deterministic fixtures. With the production hierarchy payload, recheck admission, replacement and fallback under `C21-06.3`.
- [ ] **C21-07.2 — Select legal cluster cuts on the GPU by projected error.** Traverse the cooked DAG per view with declared work and output bounds. Apply each group transition as a unit. Publish one complete cut for indexed rasterization and the later Task/Mesh route. Select a valid coarser cut or discrete LOD when traversal cannot finish within its bounds.
  - **PASS when** both native backends match a CPU reference on deterministic cuts and budget decisions without reading back to submit the frame. Exercise invalid hierarchy fallback and force traversal/output exhaustion during a group refinement. Verify complete coverage and compare mixed-level neighbors and moving-camera transitions for cracks, duplicate coverage and visible pop. Rerun the per-sample history fixture from `C21-15.3` with an actual DAG cut change.

#### C21-08 — Visibility Buffer

- [ ] **C21-08.1 — Freeze the visibility and depth ABI.** Define instance/cluster/primitive identity, integer attachment format, clear sentinel, capacity, multi-draw partitioning, depth convention, sample count and supported material coverage.
  - **PASS when** native readback disambiguates repeated mesh instances, primitive offsets and partitions, preserves exact background IDs, and exercises identity capacity without wrapping or aliasing.
- [ ] **C21-08.2 — Record indexed visibility in mixed views.** Use a dedicated pass and share depth ownership with applicable Forward opaque/cutout draws. Preserve sky, picking and the downstream surface and motion inputs.
  - **PASS when** mixed coverage and depth match the product raster reference, unsupported materials retain their applicable Forward behavior, and each opaque pixel is shaded by its selected owner.

#### C21-09 — Visibility resolve and bindless material table

- [ ] **C21-09.1 — Reconstruct and shade supported built-in PBR surfaces.** Use a dedicated `VisBufferResolvePass` with perspective-correct barycentrics, UV gradients, tangent basis and shared surface/lighting evaluation.
  - **PASS when** attribute/HDR readbacks and images agree with fixed-slot ForwardPBR within the frozen tolerances, including clipping, minified textures, normal maps, UV seams and primitive boundaries.
- [ ] **C21-09.2 — Integrate generation-safe typed material tables.** Define descriptor index domains, default resources, nonuniform lookup, descriptor tier budgets and retirement.
  - **PASS when** native DXIL/SPIR-V use and reflection agree, mixed materials sample the intended resources, and stale, missing and exhausted entries yield specified diagnostics and valid default or fixed-slot raster output.
- [ ] **C21-09.3 — Add validated dynamically indexed bindings for supported generated Material Graphs.** Publish the supported graph/resource classes and share the existing material interpretation. Route graphs outside that contract to their compatible fixed-slot Forward path.
  - **PASS when** supported generated shaders run with typed dynamic indexing and match their fixed-slot raster references on both native backends. Test mixed supported/unsupported graphs and a descriptor-tier downgrade in the production renderer.

#### C21-10 — Task and mesh shaders

- [ ] **C21-10.1 — Add native RHI pipeline and command support.** Gate task/mesh on the required feature, limits, shader reflection and validated native commands. Retain indexed indirect as the equivalent geometry path.
  - **PASS when** D3D12 and Vulkan compile, reflect and run a native fixture, with unsupported features, limits or shader failures selecting the declared indexed fallback.
- [ ] **C21-10.2 — Rasterize the same cluster cut and identity.**
  - **PASS when** native mesh and indexed modes match selected cluster/primitive coverage, depth, visibility IDs and images at a fixed cluster cut. Test route changes and capture diagnostics.

#### C21-11 — Capabilities and fallbacks

- [ ] **C21-11.1 — Define independent capability and effective-view policy.** Consume the shared M16 matrix and distinguish indexed indirect, draw count, visibility format/resolve, descriptor indexing, Task/Mesh, clustered shading and RT. Classify unsupported view, sample-count and material combinations.
  - **PASS when** policy tests cover each independent axis, resolve eligibility before recording and preserve authored settings while reporting the effective route and reason.
- [ ] **C21-11.2 — Prove a portable indexed-indirect baseline.**
  - **PASS when** a native fixture exercises supported GPU count and bounded fixed-range command modes, verifies draw/instance lookup and invalid range behavior, and demonstrates indexed direct/Forward when indirect is unavailable.
- [ ] **C21-11.3 — Verify production downgrades and recovery.**
  - **PASS when** forced Off, capability masks, resource/shader failures and budget overflow preserve applicable visuals and authored data across Editor, Preview, Runtime loose/cooked and declared subcaptures. Archive OpenGL 4.5 Forward evidence and distinguish physical adapter limits from forced downgrades.

#### C21-12 — Scale and final acceptance

- [ ] **C21-12.1 — Measure dense-scene geometry and lighting.** Compare the M16 raster baseline and eligible M21 routes under identical inputs. Isolate draw submission, triangle reduction, visibility bandwidth, resolve and cluster costs. Include a close-to-far path with subpixel triangles and a layered opaque case with substantial overdraw.
  - **PASS when** the Release scorecards meet frozen fixture-specific CPU/GPU and memory budgets, show the target gain for promoted routes and record reproducible counts and distributions. Report GPU geometry memory, derived package growth, and cold cook/load costs for the dense assets.
- [ ] **C21-12.2 — Verify integrated visual and lifecycle stability.**
  - **PASS when** the final candidate passes the native, fallback, deterministic readback, Editor visual and lifecycle/soak matrix in Section 4. Report validation errors, effective routes, resource retirement and image/temporal comparisons.
- [ ] **C21-12.3 — Complete the consolidated independent acceptance.**
  - **PASS when** the report maps every atomic leaf to production callers and verified evidence, closes required findings and rechecks, and records the accepted feature matrix, defaults and source.

### Phase 50.3 — Clustered Forward+ and shared scheduling

#### C21-13 — View-frustum cluster grid

- [ ] **C21-13.1 — Build a view-local grid with explicit tile and depth slicing.** Freeze tile dimensions, logarithmic depth mapping, clip range, projection policy, precision and resource identity. Reuse M15 froxel projection math where equivalent.
  - **PASS when** CPU and native GPU references agree over tile/clip/slice boundaries, odd view sizes and different views. Transparent samples select clusters using their own fragment depth.

#### C21-14 — Light culling and overflow

- [ ] **C21-14.1 — Emit deterministic light offsets and indices.** Source the eligible RenderWorld light lists and classify directional, point, spot and rect lights conservatively. Keep shadow-map budgets explicit and distinct from cluster list capacity.
  - **PASS when** readbacks match reference classification for all types, resolve stable light identity across order changes and cover a workload beyond the legacy fixed-array limits.
- [ ] **C21-14.2 — Handle capacity limits with diagnostic fallback.** Bound total and per-cluster work, define a deterministic over-capacity route and publish counters and the effective policy.
  - **PASS when** repeated overflow produces the same documented result and diagnostics, writes stay in bounds, remaining supported lights are evaluated once, and removing pressure recovers the normal route.

#### C21-15 — Shared clustered consumers and hybrid lighting

- [ ] **C21-15.1 — Validate clustered lighting on Forward opaque and transparent surfaces.**
  - **PASS when** built-in ForwardPBR and supported generated Material Graph variants consume the same view grid with consistent attenuation, shadow identity and transparent fragment depth. Check against reference shading with overlapping lights, filters and refraction.
- [ ] **C21-15.2 — Use the same grid for visibility shading.**
  - **PASS when** mixed visibility/Forward frames match the shared lighting reference and avoid double accumulation or stale cluster reads on either route.
- [ ] **C21-15.3 — Preserve M16 receiver and secondary lighting contracts.** Prepare the required fields from final opaque coverage and reuse shared surface terms in later shading. Provide sample validity for reprojection when an LOD or route change alters geometry.
  - **PASS when** depth/normal/material/motion inputs are ready for their consumers even with SSR, planar and TAA disabled, RT shadow visibility reaches the matching light before direct evaluation, and reflected radiance applies the shared response once. Cover off-camera RT hits/casters, an eligible omission, and an M21 proxy if emitted. Move a camera through a geometry LOD or route transition on a reflective surface and verify affected samples suppress invalid history while unchanged samples retain valid history. Record surface preparation cost and target bandwidth with RT On and Off.

#### C21-16 — Material divergence

- [ ] **C21-16.1 — Add bounded tile/material bins and built-in PBR families over the generic resolve.**
  - **PASS when** classification and permuted dispatch match the generic result, mixed/overflow cases recover through the generic path, and controlled coherent and mixed scenes report classification, resolve and pipeline costs.
- [ ] **C21-16.2 — Add a same-material wave fast path.**
  - **PASS when** supported wave modes preserve output across wave sizes and heterogeneous material keys, fall back to the generic path when required, and have measured evidence and a documented shader-family/permutation cap.

#### C21-17 — Pass order and evidence

- [ ] **C21-17.1 — Schedule one grid before its Forward and resolve consumers.**
  - **PASS when** production graph records cluster build before reads and binds one compatible grid generation across all consumers in each view.
- [ ] **C21-17.2 — Enforce hybrid pass ordering.**
  - **PASS when** visibility/Forward receiver production, RT, opaque direct/indirect evaluation, opaque scene-color consumers, transparency and post-processing satisfy the declared edges in supported mixed paths. A missing producer, circular dependency or incompatible view configuration selects a safe route or fails the graph before submission.
- [ ] **C21-17.3 — Archive integrated cluster and fallback evidence.**
  - **PASS when** the final report links cluster parity, capacity/overflow, timings and OpenGL 4.5 Forward evidence, including the actual backend and effective route.

#### C21-18 — RenderGraph resource and feature contract

- [ ] **C21-18.1 — Declare resource identity, usage and dependencies.** Define texture/buffer access for visibility, depth, clusters, indirect args/count, material tables and readbacks. Use the shared feature matrix for effective pass compatibility.
  - **PASS when** graph fixtures reject undeclared or incompatible access, wrong physical usage, stale generations and reads before producers.
- [ ] **C21-18.2 — Validate compute-to-draw transitions and completion.**
  - **PASS when** native tests verify storage writes become visible to indirect and shader reads with correct barriers, preserve descriptor/buffer leases through overlapping submissions, and reject unsafe reuse after a recording/submission failure.
- [ ] **C21-18.3 — Validate mixed-view resource and history lifetime.**
  - **PASS when** live generation checks, per-view isolation and frame retirement hold across same-size views, resize, scene replacement, feature toggles, RT consumers and device reset.

## 4. Verification and evidence

Build changed targets before using their results. For each delivered batch, record base/head, requirement IDs, production entry points, exact commands and exit status, adapter/driver, effective route, fixtures, tolerances and artifacts. Save self-verification separately from independent review. Follow the [shared protocol](../../agents/WORKING_PROTOCOL.md) for commits, required correction rechecks and final disposition. Required checks without execution evidence remain `NOT RUN`.

| Gate | Minimum evidence |
|---|---|
| Native and product matrix | Strict physical D3D12 and Vulkan in Debug/Release with validation. Cover Editor main/game and Preview, Runtime loose/cooked, and declared planar/probe policies. Exercise CPU Forward, indexed indirect with discrete LOD, visibility indexed and visibility Task/Mesh on applicable hardware. Test OpenGL 4.5 on its supported Forward route. Exercise RT Off, Shadows, Reflections and the supported combination in mixed M21 views. |
| Deterministic signals and images | Freeze fixtures, camera paths, seeds, thresholds and reference products before scoring. Use exact comparisons for identities, expected command/cluster structures and policy outcomes. Use predeclared tolerances for linear HDR, depth, derivatives, temporal convergence and images. Freeze identical geometry and light sets for parity comparisons. Separately verify the error bounds of LOD, hierarchy and overflow approximations. |
| Failure and lifecycle | Exercise capacity pressure, malformed derived payloads, missing shaders/descriptors, failed recording/submission, camera cut, scene/asset reload, view switch, odd/rapid resize, minimize/restore and device reset. Run at least 1,000 steady-state frames on each native backend on the integrated route and verify bounded memory and resource counts. |
| Editor visuals | Use the [Editor GPU capture procedure](../../agents/EDITOR_VISUAL_CAPTURE.md). Inspect LOD and cluster transitions, moving occluders, cutout/shadow contacts, UV/normal maps, transparent refraction, clustered overflow and route diagnostics. Correlate view captures with selected LOD/cluster counts and asset-specific fallback reasons. Save raw viewport and relevant UI captures alongside native readbacks. |
| Scale and budgets | Use accepted M16 core with M21 Off and comparable M21 routes at Release 1080p/1440p. Use separate submission-heavy, geometry-heavy, lighting-heavy and mixed-material workloads. Record resolution, scene/view/quality/RT settings, instance/triangle/meshlet/light counts and isolation toggles. Report cold start/compile separately from at least 120 warm-up and 1,000 measured frames per case. Record CPU extraction/submission/wait, GPU cull/raster/cluster/resolve/frame `p50/p95/p99/max`, visible counts, visibility bandwidth and peak committed/transient VRAM. Freeze tolerances and promotion budgets after calibration and before acceptance measurements. |
| Consolidated audit | Archive reproducible commands, evidence manifests, comparisons and performance reports in `docs/reviews/` or link the stored artifacts from the review. Map each atomic leaf to caller paths, independently verified evidence and disposition. |

M21 closes at M21-I3 when all mandatory leaves pass on the final integrated source. Publish verified backend, view and material support, effective fallback routes, promoted defaults and measured tradeoffs.
