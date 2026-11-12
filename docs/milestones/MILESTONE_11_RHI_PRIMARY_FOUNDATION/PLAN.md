# Milestone 11 — Modern RHI-Primary Rendering Foundation Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [ ] **Phase 41: Shared Rendering Architecture, GPU Safety & Product Integration**
  - **Status (2026-11-03): planning only; highest-priority implementation backlog.** Execute ahead of new Phase 40 authoring and VFX feature expansion. Preserve Phase 38.3/39 acceptance and delivered Phase 40 CPU work.
  - **Design**: [RHI-Primary Architecture](docs/design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md). **Packages/dependencies/acceptance**: [Implementation Plan](../../archive/design/deferred/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md). **Coding workflow**: [Implementation Guide](../../archive/design/deferred/RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md).
  - **G0 - Safety and visible primitives**:
    - [ ] **P41-00**: Always-active Debug/Release render harness, failure probe and honest backend baseline.
    - [ ] **P41-01**: Backend selection/readiness, explicit and automatic GL fallback equivalence, checked complete startup.
    - [ ] **P41-02**: Transactional GPU creation/replacement, descriptor validation and partial-allocation cleanup.
    - [ ] **P41-03**: Structured GPU errors, stopped submissions on device loss and bounded teardown.
    - [ ] **P41-04**: Canonical backend-independent primitive creation, resolution and scene/assembly persistence.
  - **G1 - Frame/resource foundation**:
    - [ ] **P41-05**: Resource handles, device epochs, generation ownership and shared GPU assets.
    - [ ] **P41-06**: Recording/submission leases and fence-safe retirement across all consumers.
    - [ ] **P41-07**: First-use/subresource tracking and commit of states only after successful submission.
    - [ ] **P41-08**: Frame contexts, bounded arenas and completion-safe allocator reuse.
    - [ ] **P41-09**: One presentation owner, native UI recording bridge, surface/VSync/minimize handling.
    - [ ] **P41-10**: Transactional per-view targets, resize/format changes and leased GUI textures.
    - [ ] **P41-11**: Context-free decode, central upload and device-aware resource caching.
  - **G2 - Common scene rendering**:
    - [ ] **P41-12**: Immutable RenderWorld, world-space scene extraction and shared mesh/material resolution.
    - [ ] **P41-13**: RenderService, sequential declared-usage graph and main Editor viewport integration.
    - [ ] **P41-14**: OpenGL compatibility adapter using the common data/service contract.
  - **G3 - All rendering consumers**:
    - [ ] **P41-15**: Private asset/material service views with preserved authoring and controls.
    - [ ] **P41-16**: Private VFX service views with preserved deterministic simulation/input/document behavior.
    - [ ] **P41-17**: Multi-backend Assembly Editor preview using actual hierarchy and resources.
    - [ ] **P41-18**: Bounded mesh/assembly thumbnail jobs and safe cache invalidation.
    - [ ] **P41-19**: Standalone Runtime backend/service/present integration and cooked startup.
    - [ ] **P41-20**: Shared shader/material/color/depth contracts and honest renderer capabilities.
  - **G4 - Functional parity**:
    - [ ] **P41-21**: Modern directional shadow/cascade passes and receiver/cutout pixel evidence.
    - [ ] **P41-22**: Authored HDR environment loading, sky rendering and real IBL integration.
    - [ ] **P41-23**: Reviewed animation CPU integration, GPU skinning and animated shadows; cross-map Phase 40 F1/F2.
    - [ ] **P41-24**: Geometry/pose/material-consistent picking and stale-readback rejection; cross-map Phase 40 F3.
    - [ ] **P41-25**: Correct Edit/Play/Pause physics debug and isolated overlays.
  - **G5 - Production cutover**:
    - [ ] **P41-26**: Real module/ownership boundaries and Runtime build with Editor/UI dependencies disabled.
    - [ ] **P41-27**: Measured frame/upload performance and safe removal of per-view queue-idle waits.
    - [ ] **P41-28**: CPU, physical D3D12/Vulkan, GL, native UI and standalone failure/stress acceptance matrix.
    - [ ] **P41-29**: Accepted modern defaults, removal of obsolete active paths and maintained GL fallback.
  - **Completion gate**: Every required acceptance row has real evidence. Proposed APIs/targets and historical tests do not establish Phase 41 completion. No live backend switching, GPU VFX graph or ray tracing is required.
