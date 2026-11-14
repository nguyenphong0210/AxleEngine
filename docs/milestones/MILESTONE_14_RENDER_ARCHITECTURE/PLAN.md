# Milestone 14 - Render Architecture Refactor

**Status:** `PROPOSED - architecture and migration contract; no runtime implementation is released by this document.`
**Baseline:** `7265281` (source-grounded Renderer/RHI architecture review).
**Target:** establish a modular RHI-primary render architecture that can host the former lighting/material milestone as M15 without growing the current renderer monolith.

This milestone changes ownership and contracts. It does not claim that SSR, dynamic reflection capture, DDGI, advanced materials or other M15 features are complete. Every checkbox below requires source evidence, a build, and the stated regression evidence.

## 1. Milestone hierarchy

```tex
Milestone 14 - Render Architecture Refactor
  `- Phase 44 - Modular renderer and material foundation
      |- U0 Baseline and migration contrac
      |- U1 ViewContext and frame ownership
      |- U2 Pass interface and renderer orchestration
      |- U3 Canonical material and surface contrac
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

```tex
U0 -> U1 -> U2 -> U3 -> U4 -> U5 -> U6 -> U7 -> U8 -> U9
```

U1 and U3 may be designed in parallel, but neither is accepted until U0 is closed. U5 and U6 may not introduce feature behavior that belongs to M15.

## 2. Acceptance vocabulary

- **Verified:** source, build and focused runtime/test evidence all exist.
- **Partial:** code exists but an ownership, fallback, lifetime or parity gate remains open.
- **Not run:** the required backend, GPU, editor or fixture was unavailable.
- **Blocked:** a required dependency is unavailable; it cannot be converted to PASS by omission.
- **Accepted:** every exit condition of the named Unit passes and the review record identifies the exact commit and evidence.

## 3. Unit and SubUnit checklis

### U0 - Baseline and migration contrac

**Exit:** the existing RHI path still builds and renders a reference scene, the source map is frozen, and every moved responsibility has an owner.

- [ ] **S0.1 Source inventory**
  - [ ] Record current callers of `SceneRenderer`, `RHISceneRenderer`, `RenderWorld`, `RHIFrameContext` and `RenderGraph`.
  - [ ] Record current material paths: asset, component, snapshot, `RHIMaterial`, Material Graph and preview.
  - [ ] Record all active native backends, feature toggles and fallback paths.
- [ ] **S0.2 Contract freeze**
  - [ ] Freeze `RenderWorld` as the only scene input while GPU commands are recorded.
  - [ ] Freeze RHI ownership rules for resource leases, descriptor generations and completion tokens.
  - [ ] Freeze the M15 feature boundary and list behavior explicitly deferred from M14.
- [ ] **S0.3 Reference evidence**
  - [ ] Capture baseline build/configuration and current reference images/readbacks.
  - [ ] Record baseline draw calls, target formats, backend and device capability snapshot.
  - [ ] Record known failures separately from accepted baseline behavior.

### U1 - ViewContext and frame ownership

**Exit:** every render view has one explicit context that owns view state and completion lifetime.

- [ ] **S1.1 ViewContext data contract**
  - [ ] Define camera matrices, viewport dimensions, jitter, scene revision and feature policy.
  - [ ] Define target generation, temporal history generation and exposure state ownership.
  - [ ] Define completion token and reset reasons: resize, camera cut, device epoch, feature change and material generation.
- [ ] **S1.2 Frame recording**
  - [ ] Route command recording through `RHIFrameContext` owned by the view execution.
  - [ ] Prohibit pass code from owning submission, queue selection or global mutable frame state.
  - [ ] Make descriptor and upload arena reuse depend on completion, not frame index alone.
- [ ] **S1.3 View lifecycle tests**
  - [ ] Add resize and view-switch generation tests.
  - [ ] Add camera-cut and device-epoch history reset tests.
  - [ ] Add abandoned-frame and failed-recording cleanup tests.

### U2 - Pass interface and renderer orchestration

**Exit:** `RHISceneRenderer` coordinates passes but does not own the implementation details of every pass.

- [ ] **S2.1 Pass API**
  - [ ] Define `IRenderPass` lifecycle: `Configure`, `Declare`, `Prepare`, `Record`, `Finalize`.
  - [ ] Define pass-local state and immutable per-view inputs.
  - [ ] Define explicit feature capability and fallback result values.
- [ ] **S2.2 Pass extraction**
  - [ ] Extract opaque/sky preparation.
  - [ ] Extract shadow and light preparation.
  - [ ] Extract screen-space, reflection, fog/GI and post/temporal pass groups.
  - [ ] Keep the orchestration order in one renderer-owned schedule.
- [ ] **S2.3 Pipeline/resource ownership**
  - [ ] Move pass pipeline layouts, shaders, descriptor pools and samplers behind pass objects.
  - [ ] Remove pass-specific mutable state from the renderer where the pass can own it safely.
  - [ ] Keep shared RHI caches and device ownership below the pass layer.

### U3 - Canonical material and surface contrac

**Exit:** built-in PBR, Material Graph, preview and screen-space consumers use one resolved surface representation.

- [ ] **S3.1 ResolvedSurface schema**
  - [ ] Define linear base color, metallic, roughness, AO and emission units.
  - [ ] Define geometric/shading normal, tangent handedness and normal-map convention.
  - [ ] Define clearcoat, anisotropy, sheen, subsurface, absorption, IOR and dispersion fields.
  - [ ] Define reflection flags, motion data, validity and fallback markers.
- [ ] **S3.2 Material resolution**
  - [ ] Define one conversion path from `Material` to runtime material data.
  - [ ] Make `MaterialComponent` synchronization explicit and generation-tracked.
  - [ ] Make `RenderMaterialData` a snapshot view rather than a second authoring model.
  - [ ] Make `RHIMaterial::BuildConstants` consume the canonical resolved values.
- [ ] **S3.3 Shader parity**
  - [ ] Make ForwardPBR and Material Graph emit the same surface fields or report unsupported fields.
  - [ ] Make Material Preview use the same material defaults and color-space rules.
  - [ ] Add parity fixtures for smooth sphere, metal, roughness sweep, normal map and emission.

### U4 - Resource lifetime and RenderGraph authority

**Exit:** resources are allocated by view-generation services, dependencies are declared once, and raw command code cannot silently add undeclared reads.

- [ ] **S4.1 Target generation**
  - [ ] Define `RHIViewTargets` generation creation, validation, publication and retirement.
  - [ ] Validate format, sample count, UAV/SRV/RTV/DSV usage and dimensions before publish.
  - [ ] Preserve last-good generation on failed replacement.
- [ ] **S4.2 Graph contract**
  - [ ] Make each pass declare every read, write, load/store operation and dependency.
  - [ ] Reject missing resources, incompatible layouts and undeclared ordering.
  - [ ] Retain exact resource generations through queue completion.
- [ ] **S4.3 Descriptor and upload lifetime**
  - [ ] Define immutable-after-bind descriptor generations.
  - [ ] Track texture, buffer and descriptor leases through `RHICommandBuffer::KeepAlive`.
  - [ ] Add failure tests for stale descriptors, generation replacement and device loss.

### U5 - Lighting, reflection and GI service boundaries

**Exit:** lighting-related state has stable service boundaries; M14 provides interfaces and migration seams, while M15 owns feature fidelity.

- [ ] **S5.1 Lighting service**
  - [ ] Define directional, point, spot and area-light input contracts.
  - [ ] Define shadow atlas/cascade publication and sampling interfaces.
  - [ ] Keep light packing and unit conversion independent from pass orchestration.
- [ ] **S5.2 Reflection service**
  - [ ] Define authored probe loading and generation status.
  - [ ] Define dynamic capture, prefilter, invalidation, scheduling and multi-probe blend interfaces.
  - [ ] Define SSR/probe/sky fallback policy without implementing the full M15 algorithm here.
- [ ] **S5.3 GI service**
  - [ ] Define DDGI update, irradiance, visibility and sampling interfaces.
  - [ ] Mark experimental capability and fallback to IBL/ambient diffuse.
  - [ ] Prevent DDGI state from leaking into the core material contract when disabled.

### U6 - Temporal, post-process and diagnostics ownership

**Exit:** temporal history, exposure, post-process state and GPU telemetry have explicit owners and reset rules.

- [ ] **S6.1 Temporal state**
  - [ ] Define history read/write slots and flip timing.
  - [ ] Define motion/disocclusion validity and first-frame behavior.
  - [ ] Define reset behavior for camera cut, target generation and scene revision.
- [ ] **S6.2 Post-process services**
  - [ ] Define exposure, bloom, tone mapping and anti-aliasing interfaces.
  - [ ] Define HDR/SDR color-space and output ownership.
  - [ ] Keep post-process resources view-local.
- [ ] **S6.3 Diagnostics**
  - [ ] Define per-pass CPU/GPU timing scope and validity status.
  - [ ] Expose draw calls, dispatches, target generation, feature fallback and error reason.
  - [ ] Ensure zero or unavailable GPU timestamps are reported as unknown, not as a valid zero budget.

### U7 - Backend and fallback parity

**Exit:** D3D12, Vulkan and OpenGL fallback have explicit supported behavior and no false parity claims.

- [ ] **S7.1 Capability policy**
  - [ ] Map device capabilities to pass support and quality tier.
  - [ ] Define compute, UAV, timestamp, descriptor and format fallbacks.
  - [ ] Ensure unsupported features leave a stable direct/IBL image.
- [ ] **S7.2 Native validation**
  - [ ] Run Debug and Release builds for the renderer and unit tests.
  - [ ] Run native D3D12 and Vulkan validation paths.
  - [ ] Run OpenGL fallback tests and record intentionally reduced behavior.
- [ ] **S7.3 Failure behavior**
  - [ ] Test failed shader/pipeline creation.
  - [ ] Test failed target generation and failed pass recording.
  - [ ] Test device epoch change and recovery boundary.

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
