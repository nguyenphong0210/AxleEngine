# Architecture and Design Documentation

Reviewed: **2026-11-01**. This documentation describes the working tree inspected on that date. It separates implemented behavior from the proposed modular, layered architecture.

## Reading order

For current work order, start with the [optimization roadmap](../ROADMAP.md).
The source survey below is dated 2026-11-01, predating Vehicle retirement and
other later changes; it is not a freshly verified current module inventory.
Start with the [overview](OVERVIEW.md), [module reference](MODULES.md) and [current assessment](ASSESSMENT.md) when reviewing the engine before reading the detailed design.

| Document | Purpose |
|---|---|
| [OPT-01 current baseline inventory](OPT_01_BASELINE_INVENTORY.md) | 2026-11-05 source/vendor revisions, measured CMake target/include graphs, current asset persistence and known gaps |
| [Current architecture assessment](ASSESSMENT.md) | Strengths, limitations, priorities and improvement exit criteria |
| [Software Architecture Document](SAD.md) | System context, module responsibilities, dependencies, deployment, data and architectural assessment |
| [Software Design Description](SDD.md) | Class responsibilities, ownership, interfaces, lifecycle and extension mechanisms |
| [Class relationships](CLASS_RELATIONSHIPS.md) | Subsystem UML diagrams: inheritance, composition, shared ownership and associations |
| [Execution flows](RUNTIME_FLOWS.md) | Startup, frame updates, play/stop, script reload, assets and UI sequences |
| [Dependency map](DEPENDENCY_MAP.md) | Direct CMake link dependencies, build ordering and cross-module includes |
| [Source inventory](SOURCE_INVENTORY.md) | Files covered by the structural survey |
| [Class index](CLASS_INDEX.md) | Extracted C++ class/struct declarations and source locations |
| [Proposed layered architecture](TARGET_LAYERED_ARCHITECTURE.md) | Future boundaries, dependency rules and incremental migration criteria |
| [Verification and limitations](VERIFICATION.md) | Evidence, survey method, documentation checks and unverified behavior |
| [EnTT/enkiTS integration review](ENTT_ENKITS_REVIEW.md) | 2026-11-04 focused audit, native fault reproductions, concurrency/lifetime findings and limits |
| [EnTT/enkiTS refactor plan](../design/ENTT_ENKITS_REFACTOR_PLAN.md) | 18 proposed correctness/refactor packages, threading/lifetime/clock contracts and acceptance matrix |
| [Architecture Foundation master plan](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) | AF contracts and combined prerequisites; execution order is in docs/ROADMAP.md |
| [NRI integration contract](../design/NRI_INTEGRATION_CONTRACT.md) | Pinned native API boundary, readiness, lifetime, frame/present and real Runtime UI rules |

For a shorter introduction, read [Architecture Overview](OVERVIEW.md). [Subsystems](MODULES.md) provides a quick module reference. Existing domain guides remain useful for individual workflows; this architecture set is the reference for current dependency boundaries and cross-subsystem behavior.

## Diagram conventions

Diagrams use Mermaid and require a Markdown viewer with Mermaid support. A dependency arrow `A -> B` means A knows about or uses B; it does not establish execution order. Sequence diagrams describe ordering. Class diagrams distinguish inheritance, exclusive/value ownership, shared ownership and non-owning references.

The class diagrams emphasize relationships that matter to architecture. The class index covers additional declarations, including local test types and sample scripts. No single diagram attempts to display every engine type.

The 2026-11-04 [plugin design](../design/PLUGIN_SYSTEM_ARCHITECTURE.md) and
[26-package plan](../design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) specialize the
optional-toolkit proposal: Editor DLL modules, selected static Runtime modules,
project plugin authoring and a fresh Vehicle replacement. They are documentation
only; the current source survey is not a claim that this architecture exists.

The [optimization roadmap](../ROADMAP.md) orders architecture work before
feature expansion. The master retains dependency contracts. Its rendering domain is
[Phase 41: RHI-primary rendering](../design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md),
with its [detailed package plan](../design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md).
It is a target design, not a change to the as-is survey below.

## Scope

The survey covers first-party engine, editor, runtime, plugins, samples, shaders and text asset/configuration files. Third-party libraries are considered through their integration points, CMake targets and APIs used by the engine. Generated build output, cache binaries, media and the implementation of every vendored library are outside the class analysis.

All selected text files were scanned completely by tooling. Manual interpretation focused on public APIs, build configuration, ownership and major integration paths; this is not a line-by-line algorithm audit. Extraction uses lexical patterns rather than a compiler AST. No build, application execution, performance measurement or engine test run is claimed by this documentation update.

Repository-relative links make the documentation portable between checkouts. Source line anchors are navigation aids and may move as the code changes. The proposed module names and build options in the target architecture are design suggestions, not newly implemented targets.

For feature workflows and format references, return to the [documentation hub](../README.md). Future editor feature proposals are in the [tooling roadmap](../archive/design/deferred/EDITOR_TOOLING_ROADMAP.md).

The [PLG-00 baseline inventory](PLUGIN_BASELINE_INVENTORY.md) records verified current startup, ownership and retirement consumers.
