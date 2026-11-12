# Post-OPT-10 feasibility assessment

Updated: **2026-11-11** (C12-05 evaluation checkpoint).

This review compares the three Post-OPT-10 roadmaps with the current source
contracts. It is a feasibility assessment, not an implementation release.
The live exchange and direct user assignments own current releases; this assessment
does not activate later work. The original baseline below predates OPT-11/12 delivery.

The directly released C12-01..04 packages now establish graph documents, registry
lowering, strict reflection/reload and resource semantics. C12-05 adds a bounded,
optional Slang evaluation for the same generated HLSL. Actual DXIL/SPIR-V layouts
and native output match DXC in the tested fixtures; local warm graph compilation
costs support retaining DXC as the default. See the
[C12-05 report](../reviews/OPT_12_C12_05_DELIVERY.md) for final gates, measurements
and deployment/profile limits. Graph UI, full Slang authoring and C12-06 OpenGL
policy remain separate work.

## Overall decision

The direction is **feasible as a staged program**. It is not feasible as one
combined jump with the current source, and several numbers in the roadmaps are
performance hypotheses rather than acceptance criteria.

The following claims need to be treated as targets to measure, not promises:

- 120–144+ FPS on an RTX 2060/3060 class machine;
- a 3.5–5.0 ms complete lighting budget;
- 40–60% VRAM reduction from aliasing;
- 70–80% animation CPU reduction;
- 100,000 visible mesh instances;
- exactly 0% CPU for dormant entities.

A future package should publish a representative scene, hardware tier,
resolution, quality preset, frame-time percentile and image-quality threshold
before any of those numbers can be accepted.

## Original assessment baseline (2026-11-09)

| Foundation | Current evidence | Consequence |
| --- | --- | --- |
| Immutable render extraction | [`RenderWorld`](../../Engine/Renderer/include/Axle/RenderWorld.hpp) and `ExtractRenderWorld` produce a snapshot without the ECS registry | A render queue and render-thread experiment have a valid data seam |
| Unified render coordinator | [`RenderService`](../../Engine/Scene/include/Axle/RenderService.hpp) dispatches RHI or OpenGL views | New passes should extend this service rather than create another renderer |
| Sequential render graph | [`RenderGraph`](../../Engine/Renderer/include/Axle/RenderGraph.hpp) validates pass/resource order and leases imported textures | Pass scheduling is usable; transient allocation and aliasing do not exist yet |
| Shader compilation | `RHIShaderCompiler` compiles HLSL to DXIL/SPIR-V and reflects bindings | A first material graph can target the existing compiler; Slang and GLSL are new work |
| Capability detection | `RHIDevice` reads mesh-shader and ray-tracing capabilities from NRI | Hardware feature probing exists, but renderer-level support is currently disabled for ray tracing and modern cascaded shadows |
| Asset cooking | [`AssetCooker`](../../Engine/Asset/include/Axle/AssetCooker.hpp) creates deterministic project packages | Offline LOD can be added at the cooker boundary with a versioned derived-artifact schema |
| Simulation phases | `SimulationClock`, `SimulationDriver` and phase bridges define fixed controls, physics, post-step and animation phases | Tick LOD can be introduced at a phase boundary without changing the clock model |
| Physics activation | `PhysicsScene::ActivateBody` and `DeactivateBody` exist | Interest management is possible, but safe gameplay/joint/contact policy is still required |
| Animation extraction | `Animator::Update` and `RenderWorld::Skins` already carry skinning matrices | URO can begin as controlled update skipping with cached matrices |

## Feasibility by proposal

| Proposal | Feasibility | Main blocker or required condition | Recommended disposition |
| --- | --- | --- | --- |
| CSM on modern RHI | **High** | OpenGL already has cascades; modern RHI capability reports `false` and the native scene renderer has no completed cascade pass | First visual package after OPT-10; preserve GL fallback and add image tests |
| SSAO/SSR & GTAO | **Medium-high** | Requires depth/normal history, resource formats, temporal invalidation and backend fallbacks | Half-res GTAO compute pass in OPT-13.3; implement before full scene integration |
| Volumetric Fog (Froxel) & Post-Processing | **Medium** | Requires a measured frustum voxel resolution, temporal reprojection and ACES tone mapping pass | Candidate packages in OPT-13.4 and 13.6; validate cost and quality before fixing a grid or budget |
| Render-thread & Parallel Recording | **Medium, high risk** | `RenderService` is owner-thread bound; `RHIDevice::CanSubmit` and `RHIResourceService` require owner thread | OPT-11.3/11.4 are ownership and backend experiments, not a promise that worker submission is available |
| VRAM aliasing | **Medium** | Current graph imports borrowed physical textures; `RHITexture` allocates own memory without heap suballocation | OPT-11.5 evaluates lifetime analysis and backend-specific heap strategies before enabling aliasing |
| Material node editor + Slang | **Medium-low at original baseline** | Graph IR and optional Slang are now bounded implementations; graph UI, full Slang source authoring and GLSL remain separate | Retain DXC default after C12-05 evaluation; consult its measured report before any broader adoption |
| Offline autoLOD | **High-medium** | `AssetCooker` exists, but meshoptimizer integration, source provenance, LOD schema and cook cache invalidation are absent | Good early package (OPT-16); deterministic CPU tests and fallback LOD first |
| Meshlet DAG virtual geometry | **Medium-low** | NRI/vendor capability plumbing exists, but requires task/mesh pipeline wrapper and cluster DAG builder | OPT-14 defines a measured flat-meshlet experiment first; a DAG remains optional and fallback-gated |
| GPU-driven culling / Hi-Z | **Medium** | Requires GPU instance buffer, indirect argument ownership, depth pyramid and synchronization rules | Specified in OPT-14.1 and 14.2; benchmarked against CPU culling |
| DDGI Probes | **Low-medium** | Requires probe grid resource, octahedral atlases, Chebyshev visibility and RT integration | OPT-13.5 is an optional prototype with capability detection and IBL/ambient fallback |
| Staggered tick LOD | **High-medium** | `SimulationClock` has diagnostics but no public monotonic tick index or significance scheduler | Add explicit scheduler component and deterministic tests in OPT-15.1..15.3 |
| Physics fast path | **Medium-low** | Jolt activation APIs exist, but deactivation changes contacts, constraints, character interactions | Specified in OPT-15.7 with interest hints, kinematic proxies and wake-up regression tests |
| Spatial dormancy | **Medium-high** | EnTT tags are available, but requires spatial index, cell ownership and persistence protocol | OPT-15.4/15.5 define the index and wake/sleep protocol; dormant CPU savings remain a measured result |
| Animation URO / bone LOD | **Medium** | Animator and skinning matrices exist, but events, state transitions, VFX and cached-pose invalidation need semantics | Specified in OPT-15.9 and OPT-17; update intervals and cached pose reuse |

## Important corrections to the roadmaps

1. **Render threading is not a small optimization.** A copied `RenderWorld` is a
   good input, but current `RenderService` and RHI resource services deliberately
   reject calls from a non-owner thread. The first deliverable must transfer
   ownership of device/resource/presentation operations or queue all of them to
   the owner thread.
2. **The current RenderGraph is not a render-graph allocator.** It checks pass
   dependencies and resource states for imported textures. It does not create
   transient textures, compute lifetimes, place resources in shared heaps or emit
   aliasing barriers.
3. **NRI support is not engine feature support.** NRI exposes mesh-shader and
   ray-tracing interfaces, but the engine capability table currently reports
   ray tracing and modern cascaded shadows as unavailable. A hardware flag alone
   cannot close a product feature gate.
4. **Logic LOD must not equate distance with physics deletion.** Deactivating a
   Jolt body can change contacts and constraints. It needs an authored policy for
   authority, wake-up forces, triggers, joints, characters and networked state.
5. **Animation skipping must preserve semantics.** Reusing matrices is safe only
   when state transitions, events, root motion, VFX and visibility changes are
   handled explicitly.
6. **The roadmaps assume components that do not exist yet.** `ShaderCompiler`,
   `RHIMeshShader`, `ShadowPass` and `GIPasses` are proposed names, not current
   public APIs. The current source uses `RHIShaderCompiler`, `ShadowMap`,
   `RHISceneRenderer` and the sequential `RenderGraph`.
7. **The master roadmap's “OPT-06 through OPT-10 active” label is stale while
   OPT-10 is paused.** Post-OPT-10 design can continue as analysis, but it must
   not be interpreted as a released implementation queue.

## Recommended order

1. Close or explicitly defer the remaining OPT-10 acceptance gates.
2. Add a capability/quality matrix and benchmark harness for D3D12, Vulkan and
   the OpenGL compatibility path.
3. Deliver CSM plus a small SSAO/SSR slice using the existing RenderService and
   RenderGraph.
4. Add deterministic offline autoLOD to AssetCooker with versioned derived data.
5. Add LogicLOD foundation: significance tiers, spatial index, deterministic
   scheduling and wake/sleep events. Keep Jolt bodies active in the first slice.
6. Prototype GPU-driven culling with indexed fallback and real device capability
   checks.
7. Design render-thread ownership and transient VRAM allocation as separate
   architecture packages; only then attempt threading and aliasing.
8. Add material IR/node authoring, then evaluate Slang as an optional compiler
   backend rather than making it a prerequisite for all materials.
9. Treat meshlets and DDGI as optional hardware-tier packages after the fallback
   paths and image-quality tests are stable.

The practical conclusion is **yes, the program is technically achievable**, but
only after splitting it into these bounded packages. The first high-confidence
packages are CSM/SSAO, offline autoLOD and a non-destructive LogicLOD scheduler.
Render-thread migration, VRAM aliasing, meshlet DAGs and DDGI should remain later,
measured experiments rather than mandatory assumptions.
