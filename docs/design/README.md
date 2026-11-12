# Active Architecture Designs

Start with **[the optimization roadmap](../ROADMAP.md)**. It owns execution order
and package progress. This folder contains only the contracts supporting the
current architecture cleanup; historical Phase 41/42/43 labels are domain IDs.

**User scope, 2026-11-05:** prioritize a clean Engine. Support for old data formats,
legacy Vehicle payload retention and automatic migration are not requirements.
Generic persistence for components of the future supported plugin schema is a
separate contract; it must not reintroduce retired Vehicle compatibility code.

| Read when | Document | Purpose |
| --- | --- | --- |
| OPT-10 | [Implemented seams and acceptance](../guides/ARCHITECTURE_ACCEPTANCE.md) | Current APIs, reproducible evidence and mandatory open gates |
| OPT-01..10 | [Architecture diagrams](../architecture/SAD.md) | Current system context, ownership and product boundaries |
| OPT-01..10 | [Render flow](../architecture/RENDER_FLOW.md) | Frame extraction, backend selection and GPU retirement |
| OPT-06..10 | [Plugin flow](../architecture/PLUGIN_FLOW.md) | Discovery, dependency resolution, activation and export |
| OPT-01..10 | [Master contract](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) | Fixed decisions, AF packages and cross-plan dependencies |
| OPT-01..03 | [EnTT/enkiTS plan](ENTT_ENKITS_REFACTOR_PLAN.md) | Threading, task lifetime, ECS and shared simulation |
| OPT-04..05 | [NRI contract](NRI_INTEGRATION_CONTRACT.md) | Pinned vendor, device/resource/frame ownership |
| OPT-04..05 | [Rendering architecture](RHI_PRIMARY_RENDERER_ARCHITECTURE.md) | Shared rendering services and boundaries |
| OPT-04..05, 09/11 | [Rendering packages](RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md) | P41 contracts and acceptance |
| During rendering implementation | [Rendering coding reference](RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md) | Algorithms and test fixtures |
| OPT-06..09 | [Plugin architecture](PLUGIN_SYSTEM_ARCHITECTURE.md) | Editor DLLs, selected static Runtime and project plugins |
| OPT-06..09 | [Plugin packages](PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) | SDK, Plugins window, fresh Vehicle and export |
| During plugin implementation | [Plugin coding reference](PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | Lifecycle/build algorithms and fixtures |
| Post-OPT-10 | [Engine Upgrade Master Roadmap](POST_OPT10_ENGINE_UPGRADE_ROADMAP.md) | Unified High-Performance Lighting (3.5ms), autoLOD (Meshlets) and LogicLOD |
| Post-OPT-10 | [Advanced rendering roadmap](POST_OPT10_ADVANCED_RENDERING_ROADMAP.md) | Multi-threading, VRAM aliasing, materials, geometry/culling and GI |
| Post-OPT-10 | [Logic & Simulation LOD roadmap](POST_OPT10_LOGIC_AND_SIMULATION_LOD_ROADMAP.md) | Staggered tick rates, fast-path math, spatial dormancy and animation URO |
| Post-OPT-10 | [Feasibility assessment](POST_OPT10_FEASIBILITY.md) | Current-source blockers, confidence levels, measurement requirements and recommended order |
| Post-OPT-10 | [Master roadmap](POST_OPT10_MASTER_ROADMAP.md) | Single source for Phase/OPT/C-package order, dependencies and acceptance boundaries |
| Post-OPT-10 detail | [OPT-11..OPT-20 program plan](POST_OPT10_PROGRAM_PLAN.md) | Detailed package contracts and historical execution notes referenced by the master roadmap |
| OPT-12 C12-01 | [Approved Material Graph format](OPT_12_MATERIAL_GRAPH_FORMAT.md), [CPU document delivery](../reviews/OPT_12_C12_01_DELIVERY.md) | Verified CPU document/IR, extensible node definitions and persistence; shader/render/graph UI work remains separate |
| OPT-12 C12-02 | [Existing compiler backend delivery](../reviews/OPT_12_C12_02_DELIVERY.md) | Complete compiler/native baseline: extensible HLSL lowering, real DXIL/SPIR-V, Debug 95/95 and Release focused 5/5; later packages remain separate |
| OPT-12 C12-03 | [Reflection/layout and reload delivery](../reviews/OPT_12_C12_03_DELIVERY.md) | Complete: strict graph ABI, code/pipeline keys, native last-known-good generations and authored-name instances; Debug 98/98, focused Release 8/8, independent review pass |
| OPT-12 C12-04 | [Resource semantics delivery](../reviews/OPT_12_C12_04_DELIVERY.md) | Complete: rooted/VFS textures, explicit sampler/color/fallback/normal policy and native resources; Debug 101/101, focused Release 11/11, independent correction rechecks pass |
| OPT-12 C12-05 | [Optional Slang evaluation](../reviews/OPT_12_C12_05_DELIVERY.md) | Complete bounded opt-in Slang backend for generated graph HLSL; DXC remains default, with real DXIL/SPIR-V, reflection/layout and D3D12/Vulkan comparisons |
| OPT-12 C12-06 | [OpenGL graph policy](../reviews/OPT_12_C12_06_DELIVERY.md) | Complete explicit OpenGL unsupported diagnostic; no automatic HLSL/DXIL/SPIR-V claim on the legacy compatibility path |
| OPT-13 C13-01 | [Execution checkpoints](OPT_13_C13_01_EXECUTION_PLAN.md), [native acceptance](../reviews/OPT_13_C13_01_NATIVE_DELIVERY.md) | Complete: CPU contract and D3D12/Vulkan CSM/PCF, native images, Debug 109/109, Release focused 18/18 and separate-context correction rechecks |
| OPT-11 detail | [OPT-11 execution plan](OPT_11_EXECUTION_PLAN.md) | Ownership, snapshot queue, worker presentation, recording probe, transient aliasing and acceptance matrix |
| OPT-11 implementation | [OPT-11 architecture and algorithms](OPT_11_ARCHITECTURE_AND_ALGORITHMS.md) | Gemini implementation contract, invariants, state machines, pseudocode and handoff checklist |
| OPT-11 C11-04 hybrid | [Hybrid recording implementation plan](OPT_11_C11_04_HYBRID_IMPLEMENTATION_PLAN.md) | Deferred NRI fork/child recording design; benchmark worker primary value before resuming |
| OPT-11 C11-05 safety | [Transient allocation and aliasing safety plan](OPT_11_C11_05_SAFETY_PLAN.md) | Pure planner, unaliased allocation, rollback, packing, alias barriers and native verification |
| OPT-11 C11-05.1 delivery | [CPU transient planner delivery](OPT_11_C11_05_1_DELIVERY.md) | Implemented/self-verified pure planner; native allocation and aliasing remain later slices |
| OPT-11 C11-05 delivery | [Transient graph delivery](OPT_11_C11_05_DELIVERY.md) | Native committed/placed generations, interval packing, validated aliasing, diagnostics and backend evidence |
| OPT-11 C11-06 stress | [Lifetime stress execution plan](OPT_11_C11_06_STRESS_PLAN.md) | Snapshot/module cancellation, worker lifecycle, native resource faults and consolidated verification |
| OPT-11 C11-06 delivery | [Lifetime stress delivery](OPT_11_C11_06_DELIVERY.md) | Lifetime repairs, real DLL/GPU completion, resize/restart stress and actual verification limits |
| OPT-11 C11-07 acceptance | [Acceptance execution plan](OPT_11_C11_07_ACCEPTANCE_PLAN.md), [final decision](../reviews/OPT_11_C11_07_ACCEPTANCE.md) | Qualified foundation/synchronous-product acceptance; async/parallel/scene-alias product integrations remain deferred |
| OPT-11 product flags | [Rendering selection guide](../guides/OPT_11_RENDER_ROLLOUT.md) | Shared startup flags, strict rejection, explicit fallback and remaining integration |

Detailed contracts can include later packages needed to describe an end-to-end
architecture. Their presence does not advance them ahead of the OPT sequence.

## Removed from the active folder

Deferred feature plans and dated delivery/review reports are in the
[design archive](../archive/design/README.md). Material/VFX/Animation expansion,
old handoffs and completed delivery reports no longer occupy this folder.
Original standalone prompts remain in the [agent archive](../agents/archive/README.md);
redundant redirect files were removed.

Archive placement does not mean every old phase is accepted or complete. In
particular, [Vehicle retirement](../archive/design/reports/VEHICLE_RETIREMENT_DELIVERY.md)
was followed by the complete purge in `be7b071`. The
[independent review](../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md) records the
original preservation contract and a scope addendum. Use the roadmap for current status.
