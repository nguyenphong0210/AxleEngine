# AxleEngine Documentation

Documentation is organized by purpose. Current implementation, usage instructions and future proposals are kept separate. Start with the architecture overview if you are new to the engine.

## Codex/Gemini collaboration

Start at [the agent index](agents/README.md). Both assistants read and write the
[shared exchange](agents/CODEX_GEMINI_EXCHANGE.json), using the same
[working protocol](agents/WORKING_PROTOCOL.md). It records current scope,
questions/replies, deliveries and independent review status. Technical guides
below are references, not standing instructions to implement an entire phase.

Build instructions: [empty Editor startup, separate tests and on-demand standalone export](guides/BUILD_WORKFLOWS.md). Test sources and fixture layout: [Tests](../Tests/README.md).

## Latest design request: project-selected plugins

The 2026-11-04 request is **documentation first**, not implementation. The selected
direction is Editor DLL modules, static Runtime modules selected per project, a new
Plugins window, Engine/project plugin support and a fresh Vehicle replacement.
No code has been deleted or implemented by this design task.

- [Plugin architecture](design/PLUGIN_SYSTEM_ARCHITECTURE.md): source audit, selection/UX, shared-host ABI, EnTT/enkiTS ownership, fresh Vehicle scope and exact export.
- [Plugin implementation plan](design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md): 26 gated packages, dependencies, file scopes and acceptance matrix.
- [Plugin implementation guide](design/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md): proposed schemas, algorithms, lifecycle/build contracts and real-fixture strategy.

This request does not close or release Phase 41 packages, change the existing
rendering-versus-feature-expansion priority, or assign another agent. The live
exchange records the next actual implementation assignment.

## Rendering execution priority: RHI-primary rendering

Phase 41 executes before new animation authoring and VFX expansion. All documents
below describe proposed work; implementation/acceptance is not claimed.

- [Target architecture](design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md): responsibilities, resource/frame contracts, fallback and migration boundaries.
- [Implementation plan](design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md): 30 packages, dependencies, gates, source scope and acceptance matrix.
- [Coding guide](design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md): algorithms, proposed APIs, test design and scoped handoff template for either agent.

## Architecture and assessment

| Document | Use it to |
|---|---|
| [Architecture overview](architecture/OVERVIEW.md) | Understand the engine's current structure and boundaries |
| [Current architecture assessment](architecture/ASSESSMENT.md) | Review strengths, limitations, priorities and measurable improvement criteria |
| [Architecture documentation index](architecture/README.md) | Navigate the detailed design set |
| [SAD](architecture/SAD.md) | Understand system context, dependencies, deployment and data |
| [SDD](architecture/SDD.md) | Inspect class responsibilities, ownership and lifecycle |
| [Module reference](architecture/MODULES.md) | Find the responsibilities of each current module |
| [Class relationships](architecture/CLASS_RELATIONSHIPS.md) | Read subsystem UML diagrams |
| [Execution flows](architecture/RUNTIME_FLOWS.md) | Follow startup, simulation, asset, script and UI paths |
| [Dependency map](architecture/DEPENDENCY_MAP.md) | Inspect CMake and source-level dependencies |
| [Proposed layered architecture](architecture/TARGET_LAYERED_ARCHITECTURE.md) | Evaluate future module boundaries and migration stages |
| [Survey verification](architecture/VERIFICATION.md) | Understand the evidence and limitations of the review |

Declaration and file inventories are available through the architecture index. They are reference snapshots, not tutorials.

## Development and feature guides

- [Development conventions](guides/DEVELOPMENT.md): contribution rules, component extension and test organization.
- [Scene and ECS](guides/SCENE_AND_ECS.md): components, hierarchy, transforms and prefab/scene workflows.
- [Native scripting](guides/SCRIPTING.md): lifecycle, reflected fields and reload.
- [Rendering](guides/RENDERING.md): OpenGL pipeline details and links to RHI boundaries.
- [Physics and simulation](guides/PHYSICS.md): Jolt worlds, collision, queries and vehicles.
- [UI documents and binding](guides/UI.md): canvas assets, node ownership, datasource scoping and current rendering limits.
- [Materials and shaders](guides/MATERIALS_AND_SHADERS.md): descriptor editing, preview, HLSL compilation and reload boundaries.

## Reference and future design

- [Asset format reference](reference/ASSET_FORMAT.md): `.axleasset` syntax, consumer boundaries, metadata and package distinctions.
- [Editor tooling roadmap](design/EDITOR_TOOLING_ROADMAP.md): future material, VFX, UI and animation authoring work, with acceptance criteria.
- [Phase 39 VFX editor completion plan](design/VFX_PHASE_39_COMPLETION_PLAN.md): audited gaps, implementation order, atomic work packages and CPU/GPU/UI acceptance for the reopened VFX phase.
- [Phase 39 Gemini coding guide](design/VFX_PHASE_39_GEMINI_IMPLEMENTATION_GUIDE.md): reference C++ interfaces, algorithms and fixtures; active scope is in the shared exchange.
- [Phase 40 animation completion plan](design/ANIMATION_PHASE_40_COMPLETION_PLAN.md): source audit, 28 atomic packages, dependencies, supported scope and CPU/GPU/UI/Runtime acceptance.
- [Phase 40 Gemini coding guide](design/ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md): reference types, algorithms and integration contracts; active scope is in the shared exchange.
- [Project roadmap](../ROADMAP.md): development history and milestones. Milestone completion claims do not substitute for current source or test evidence.

## Maintenance rules

1. Put implemented behavior in architecture, guides or reference documents. Label unimplemented changes as proposals under design or the target architecture.
2. Update the relevant document and navigation when a public API, ownership rule, dependency or execution sequence changes.
3. Use repository-relative links. Link source instead of copying large header implementations or source-asset templates into documentation.
4. Keep one canonical explanation of a subject; link to it from other guides.
5. Record review dates and verification limits. Do not publish benchmark, parity or test-pass claims without evidence.
6. Remove superseded execution plans after their useful contracts are incorporated into maintained documents. Do not retain parallel phase-numbered specifications as current API references.
