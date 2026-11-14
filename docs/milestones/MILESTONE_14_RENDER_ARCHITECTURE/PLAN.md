# Milestone 14 - Render Architecture Refactor

**Status:** `U0/U1/U2/U3/U4/U5/U6/U7 ACCEPTED - U8 is the next released Unit.`
**Baseline:** `ac4941c` (M14 architecture split and target contract).
**Target:** establish a modular RHI-primary render architecture that can host the former lighting/material milestone as M15 without growing the current renderer monolith.

This milestone changes ownership and contracts. It does not claim that SSR, dynamic reflection capture, DDGI, advanced materials or other M15 features are complete. Every checkbox below requires source evidence, a build, and the stated regression evidence.

## 1. Milestone hierarchy

```text
Milestone 14 - Render Architecture Refactor
  `- Phase 44 - Modular renderer and material foundation
      |- U0 Baseline and migration contract
      |- U1 ViewContext and frame ownership
      |- U2 Pass interface and renderer orchestration
      |- U3 Canonical material and surface contract
      |- U4 Resource lifetime and RenderGraph authority
      |- U5 Lighting, reflection and GI service boundaries
      |- U6 Temporal, post-process and diagnostics ownership
      |- U7 Backend and fallback parity
      |- U8 Migration, tests and tooling
      `- U9 Final acceptance and M15 handoff
          `- each Unit contains independently buildable SubUnits
              `- high-risk SubUnits contain focused WorkItems
```

Dependency order:

```text
U0 -> U1 -> U2 -> U3 -> U4 -> U5 -> U6 -> U7 -> U8 -> U9
```

U1 and U3 may be designed in parallel, but neither is accepted until U0 is closed. U5 and U6 may not introduce feature behavior that belongs to M15.

## 2. Acceptance vocabulary

- **Verified:** source, build and focused runtime/test evidence all exist.
- **Partial:** code exists but an ownership, fallback, lifetime or parity gate remains open.
- **Not run:** the required backend, GPU, editor or fixture was unavailable.
- **Blocked:** a required dependency is unavailable; it cannot be converted to PASS by omission.
- **Accepted:** every exit condition of the named Unit passes and the review record identifies the exact commit and evidence.

## 3. Unit and SubUnit checklist

### U0 - Baseline and migration contract

**Exit:** the existing RHI path still builds and renders a reference scene, the source map is frozen, and every moved responsibility has an owner. Evidence: [U0_BASELINE_EVIDENCE.md](U0_BASELINE_EVIDENCE.md).

- [x] **S0.1 Source inventory**
  - [x] Record current callers of `SceneRenderer`, `RHISceneRenderer`, `RenderWorld`, `RHIFrameContext` and `RenderGraph`.
  - [x] Record current material paths: asset, component, snapshot, `RHIMaterial`, Material Graph and preview.
  - [x] Record all active native backends, feature toggles and fallback paths.
- [x] **S0.2 Contract freeze**
  - [x] Freeze `RenderWorld` as the only scene input while GPU commands are recorded.
  - [x] Freeze RHI ownership rules for resource leases, descriptor generations and completion tokens.
  - [x] Freeze the M15 feature boundary and list behavior explicitly deferred from M14.
- [x] **S0.3 Reference evidence**
  - [x] Capture baseline build/configuration and current reference images/readbacks.
  - [x] Record baseline draw calls, target formats, backend and device capability snapshot.
  - [x] Record known failures separately from accepted baseline behavior.

### U1 - ViewContext and frame ownership

**Exit:** every render view has one explicit context that owns view state and completion lifetime. Evidence: [U1_VIEW_CONTEXT_EVIDENCE.md](U1_VIEW_CONTEXT_EVIDENCE.md).

- [x] **S1.1 ViewContext data contract**
  - [x] Define camera matrices, viewport dimensions, jitter, scene revision and feature policy.
  - [x] Define target generation, temporal history generation and exposure state ownership.
  - [x] Define completion token and reset reasons: resize, camera cut, device epoch, feature change and material generation.
- [x] **S1.2 Frame recording**
  - [x] Route command recording through `RHIFrameContext` owned by the view execution.
  - [x] Prohibit pass code from owning submission, queue selection or global mutable frame state.
  - [x] Make descriptor and upload arena reuse depend on completion, not frame index alone.
- [x] **S1.3 View lifecycle tests**
  - [x] Add resize and view-switch generation tests.
  - [x] Add camera-cut and device-epoch history reset tests.
  - [x] Add abandoned-frame and failed-recording cleanup tests.

### U2 - Pass interface and renderer orchestration

**Exit:** `RHISceneRenderer` coordinates passes through a deterministic lifecycle scheduler; existing implementation bodies remain behind explicit legacy adapters until the later resource migration checkpoint. Evidence: [U2_PASS_ORCHESTRATION_EVIDENCE.md](U2_PASS_ORCHESTRATION_EVIDENCE.md).

- [x] **S2.1 Pass API**
  - [x] Define `IRenderPass` lifecycle: `Configure`, `Declare`, `Prepare`, `Record`, `Finalize`.
  - [x] Define pass-local state and immutable per-view inputs.
  - [x] Define explicit feature capability and fallback result values.
- [x] **S2.2 Pass extraction**
  - [x] Extract opaque/sky preparation into a named scheduler group.
  - [x] Extract shadow and light preparation into a named scheduler group.
  - [x] Extract screen-space, reflection, fog/GI and post/temporal pass groups.
  - [x] Keep the orchestration order in one renderer-owned schedule.
- [x] **S2.3 Pipeline/resource ownership seam**
  - [x] Define pass-local ownership through `IRenderPass` and the legacy callback adapter.
  - [x] Remove pass ordering and result handling from scattered renderer control flow.
  - [x] Keep shared RHI caches and device ownership below the pass layer; concrete resource relocation is explicitly queued for the later migration checkpoint.

### U3 - Canonical material and surface contract

**Exit:** built-in PBR, Material Graph, preview and screen-space consumers use one resolved surface representation. Evidence: [U3_MATERIAL_SURFACE_EVIDENCE.md](U3_MATERIAL_SURFACE_EVIDENCE.md).

- [x] **S3.1 ResolvedSurface schema**
  - [x] Define linear base color, metallic, roughness, AO and emission units.
  - [x] Define geometric/shading normal, tangent handedness and normal-map convention.
  - [x] Define clearcoat, anisotropy, sheen, subsurface, absorption, IOR and dispersion fields.
  - [x] Define reflection flags, motion data, validity and fallback markers.
- [x] **S3.2 Material resolution**
  - [x] Define one conversion path from `Material` to runtime material data.
  - [x] Make `MaterialComponent` synchronization explicit and generation-tracked at the resolver boundary.
  - [x] Make `RenderMaterialData` a snapshot view rather than a second GPU packing path.
  - [x] Make canonical resolved values consume the existing GPU constant layout.
- [x] **S3.3 Shader parity**
  - [x] Make ForwardPBR and Material Graph expose the same surface fields or explicit unsupported flags.
  - [x] Route preview/runtime GPU material use through the same resolved defaults and color-space contract.
  - [x] Add canonical CPU parity fixtures for smooth/PBR/advanced surface packing and graph template fields.

### U4 - Resource lifetime and RenderGraph authority

**Exit:** resources are allocated by view-generation services, dependencies are declared once, and raw command code cannot silently add undeclared reads. Evidence: [U4_RESOURCE_GRAPH_EVIDENCE.md](U4_RESOURCE_GRAPH_EVIDENCE.md).

- [x] **S4.1 Target generation**
  - [x] Define `RHIViewTargets` generation creation, validation, publication and retirement.
  - [x] Validate format, sample count, UAV/SRV/RTV/DSV usage and dimensions before publish.
  - [x] Preserve last-good generation on failed replacement.
- [x] **S4.2 Graph contract**
  - [x] Make each pass declare every read, write, load/store operation and dependency.
  - [x] Reject missing resources, incompatible layouts and undeclared ordering.
  - [x] Retain exact resource generations through queue completion.
- [x] **S4.3 Descriptor and upload lifetime**
  - [x] Define immutable-after-bind descriptor generations.
  - [x] Track texture, buffer and descriptor leases through `RHICommandBuffer::KeepAlive`.
  - [x] Add failure tests for stale descriptors, generation replacement and device loss/lifetime boundaries.

### U5 - Lighting, reflection and GI service boundaries

**Exit:** lighting-related state has stable service boundaries; M14 provides interfaces and migration seams, while M15 owns feature fidelity.

- [x] **S5.1 Lighting service**
  - [x] Define directional, point, spot and area-light input contracts.
  - [x] Define shadow atlas/cascade publication and sampling interfaces.
  - [x] Keep light packing and unit conversion independent from pass orchestration.
- [x] **S5.2 Reflection service**
  - [x] Define authored probe loading and generation status.
  - [x] Define dynamic capture, prefilter, invalidation, scheduling and multi-probe blend interfaces.
  - [x] Define SSR/probe/sky fallback policy without implementing the full M15 algorithm here.
- [x] **S5.3 GI service**
  - [x] Define DDGI update, irradiance, visibility and sampling interfaces.
  - [x] Mark experimental capability and fallback to IBL/ambient diffuse.
  - [x] Prevent DDGI state from leaking into the core material contract when disabled.

### U6 - Temporal, post-process and diagnostics ownership **(ACCEPTED)**

**Exit:** temporal history, exposure, post-process state and GPU telemetry have explicit owners and reset rules.

- [x] **S6.1 Temporal state**
  - [x] Define history read/write slots and flip timing.
  - [x] Define motion/disocclusion validity and first-frame behavior.
  - [x] Define reset behavior for camera cut, target generation and scene revision.
- [x] **S6.2 Post-process services**
  - [x] Define exposure, bloom, tone mapping and anti-aliasing interfaces.
  - [x] Define HDR/SDR color-space and output ownership.
  - [x] Keep post-process resources view-local.
- [x] **S6.3 Diagnostics**
  - [x] Define per-pass CPU/GPU timing scope and validity status.
  - [x] Expose draw calls, dispatches, target generation, feature fallback and error reason.
  - [x] Ensure zero or unavailable GPU timestamps are reported as unknown, not as a valid zero budget.

**Evidence:** `U6_TEMPORAL_POST_DIAGNOSTICS_EVIDENCE.md`

### U7 - Backend and fallback parity **(ACCEPTED)**

**Exit:** D3D12, Vulkan and OpenGL fallback have explicit supported behavior and no false parity claims.

- [x] **S7.1 Capability policy**
  - [x] Map device capabilities to pass support and quality tier.
  - [x] Define compute, UAV, timestamp, descriptor and format fallbacks.
  - [x] Ensure unsupported features leave a stable direct/IBL image.
- [x] **S7.2 Native validation**
  - [x] Run Debug and Release builds for the renderer and unit tests.
  - [x] Run native D3D12 and Vulkan validation paths.
  - [x] Run OpenGL fallback tests and record intentionally reduced behavior.
- [x] **S7.3 Failure behavior**
  - [x] Test failed shader/pipeline creation.
  - [x] Test failed target generation and failed pass recording.
  - [x] Test device epoch change and recovery boundary.

**Evidence:** `U7_BACKEND_FALLBACK_PARITY_EVIDENCE.md`

### U8 - Migration, tests and tooling

**Exit:** all current callers use the new contracts, old paths are removed or explicitly quarantined, and the architecture is guarded by tests.

- [ ] **S8.1 Caller migration**
  - [ ] Migrate Editor viewport rendering.
  - [ ] Migrate Runtime rendering.
  - [ ] Migrate material preview rendering.
  - [ ] Migrate headless/native render fixtures.
- [ ] **S8.2 Regression suite**
  - [ ] Add compile-time ownership and interface checks.
  - [ ] Add RenderWorld extraction and material synchronization tests.
  - [ ] Add graph dependency, resource lifetime and history reset tests.
  - [ ] Add visual parity fixtures for built-in and graph materials.
- [ ] **S8.3 Architecture tooling**
  - [ ] Update `scripts/check_architecture.py` rules for pass/module boundaries.
  - [ ] Add a report listing pass owners, resource owners and fallback policies.
  - [ ] Add a generated dependency map to the milestone evidence.

### U9 - Final acceptance and M15 handoff

**Exit:** M14 is accepted as an architecture milestone and M15 can begin without extending the old monolith.

- [ ] **S9.1 Structural acceptance**
  - [ ] No new feature pass is implemented directly in `RHISceneRenderer` without an approved exception.
  - [ ] Every pass has an owner, declared resources, capability policy and test entry.
  - [ ] Every material path resolves through `ResolvedSurface`.
- [ ] **S9.2 Verification acceptance**
  - [ ] Debug and Release builds pass.
  - [ ] Architecture checker passes.
  - [ ] D3D12, Vulkan and OpenGL fallback evidence is recorded.
  - [ ] Editor, Runtime, Preview and headless fixtures render successfully.
- [ ] **S9.3 Handoff**
  - [ ] Publish M14 source map and migration report.
  - [ ] Freeze M15 entry contracts for lighting/material fidelity.
  - [ ] Record all deferred behavior as M15 or later scope; do not mark it complete in M14.

## 4. High-risk WorkItems

| WorkItem | Parent | Deliverable | Required evidence |
|---|---|---|---|
| W1.1 | U1/S1.1 | `ViewContext` schema and reset model | Unit tests for resize, cut and epoch |
| W2.1 | U2/S2.1 | `IRenderPass` lifecycle and pass registry | Compile + one migrated pass |
| W3.1 | U3/S3.1 | `ResolvedSurface` layout and packing | CPU/GPU layout test |
| W3.2 | U3/S3.3 | ForwardPBR/Graph/Preview parity fixture | Image/readback comparison |
| W4.1 | U4/S4.2 | Graph-only dependency validation | Negative graph tests |
| W4.2 | U4/S4.3 | Generation and lease lifetime audit | Completion and device-loss test |
| W5.1 | U5/S5.2 | Reflection service interface | Authored probe migration fixture |
| W6.1 | U6/S6.1 | Temporal history owner | First-frame and camera-cut fixture |
| W7.1 | U7/S7.1 | Capability/fallback matrix | Three-backend report |
| W8.1 | U8/S8.1 | Editor/Runtime/Preview migration | Startup and headless render evidence |
| W9.1 | U9/S9.2 | M14 acceptance scorecard | Independent review and logs |

## 5. Explicit non-goals

- [ ] Do not claim final SSR quality, dynamic probe capture, DDGI feedback, advanced material fidelity or ray tracing in M14.
- [ ] Do not replace the NRI backend or rewrite all existing RHI resources.
- [ ] Do not remove the OpenGL fallback; define its supported subset.
- [ ] Do not overwrite user-authored scene files or reset unrelated work.

## 6. M14 completion rule

M14 is complete only when U0 through U9 are accepted, the architecture report is committed, all required backend checks are recorded, and the M15 plan points to the final `ViewContext`, `IRenderPass`, `ResolvedSurface`, resource-generation and capability contracts. A passing compile without those contracts is not M14 acceptance.
