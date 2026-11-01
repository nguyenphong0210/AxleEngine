# Architecture and Design Documentation

Reviewed: **2026-11-01**. This documentation describes the working tree inspected on that date. It separates implemented behavior from the proposed modular, layered architecture.

## Reading order

| Document | Purpose |
|---|---|
| [Software Architecture Document](SAD.md) | System context, module responsibilities, dependencies, deployment, data and architectural assessment |
| [Software Design Description](SDD.md) | Class responsibilities, ownership, interfaces, lifecycle and extension mechanisms |
| [Class relationships](CLASS_RELATIONSHIPS.md) | Subsystem UML diagrams: inheritance, composition, shared ownership and associations |
| [Execution flows](RUNTIME_FLOWS.md) | Startup, frame updates, play/stop, script reload, assets and UI sequences |
| [Dependency map](DEPENDENCY_MAP.md) | Direct CMake link dependencies, build ordering and cross-module includes |
| [Source inventory](SOURCE_INVENTORY.md) | Files covered by the structural survey |
| [Class index](CLASS_INDEX.md) | Extracted C++ class/struct declarations and source locations |
| [Proposed layered architecture](TARGET_LAYERED_ARCHITECTURE.md) | Future boundaries, dependency rules and incremental migration criteria |
| [Verification and limitations](VERIFICATION.md) | Evidence, survey method, documentation checks and unverified behavior |

For a shorter introduction, read [Architecture Overview](../ARCHITECTURE.md). [Subsystems](../SUBSYSTEMS.md) provides a quick module reference. Existing domain guides remain useful for individual workflows; this architecture set is the reference for current dependency boundaries and cross-subsystem behavior.

## Diagram conventions

Diagrams use Mermaid and require a Markdown viewer with Mermaid support. A dependency arrow `A -> B` means A knows about or uses B; it does not establish execution order. Sequence diagrams describe ordering. Class diagrams distinguish inheritance, exclusive/value ownership, shared ownership and non-owning references.

The class diagrams emphasize relationships that matter to architecture. The class index covers additional declarations, including local test types and sample scripts. No single diagram attempts to display every engine type.

## Scope

The survey covers first-party engine, editor, runtime, plugins, samples, shaders and text asset/configuration files. Third-party libraries are considered through their integration points, CMake targets and APIs used by the engine. Generated build output, cache binaries, media and the implementation of every vendored library are outside the class analysis.

All selected text files were scanned completely by tooling. Manual interpretation focused on public APIs, build configuration, ownership and major integration paths; this is not a line-by-line algorithm audit. Extraction uses lexical patterns rather than a compiler AST. No build, application execution, performance measurement or engine test run is claimed by this documentation update.

Repository-relative links make the documentation portable between checkouts. Source line anchors are navigation aids and may move as the code changes. The proposed module names and build options in the target architecture are design suggestions, not newly implemented targets.
