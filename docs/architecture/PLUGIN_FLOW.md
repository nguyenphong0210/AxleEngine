# Plugin flow

Updated: **2026-11-09**

This flow separates Editor DLL activation from selected static Runtime
composition and keeps export bound to one project snapshot.

```mermaid
sequenceDiagram
    participant User
    participant Project as ProjectPluginSession
    participant Scan as Manifest scanner
    participant Resolve as Dependency resolver
    participant Build as Plugin build/cache
    participant Editor as Editor host
    participant Export as Snapshot/export
    participant Runtime as Runtime

    User->>Project: open project
    Project->>Scan: scan Engine/Plugins and project Plugins roots
    Scan-->>Project: manifests (no DLL execution)
    Project->>Resolve: validate IDs, versions and dependency graph
    Resolve-->>Project: resolved selection or diagnostics
    Project->>Build: locate/build compatible Editor modules
    Build-->>Project: DLL paths, generations and receipts
    Project->>Editor: activate selected DLLs at startup boundary
    User->>Project: change selection
    Project->>Project: mark restart required
    Project->>Export: freeze selected Runtime modules
    Export->>Export: materialize snapshot + registration + receipt
    Export->>Runtime: package selected static Runtime
    Runtime->>Runtime: verify receipt before startup
```

## Ownership rules

```mermaid
flowchart TB
    Session[PluginProjectSession]
    Session --> Manifest[Manifest metadata]
    Session --> Graph[Resolved dependency graph]
    Session --> EditorHost[EditorServices + DLL modules]
    Session --> RuntimeSnapshot[Frozen Runtime snapshot]
    EditorHost --> Scene[SceneData / Scene commands]
    EditorHost --> Sim[SimulationDriver phases]
    RuntimeSnapshot --> Runtime[Static Runtime modules]
    Scene --> Render[RenderService]
    Sim --> Jobs[Core JobSystem]
    Render --> RHI[RHI device and frame owner]
```

Plugins do not own a second scheduler, fixed clock, GPU device, presentation
loop or parallel entity database. A missing plugin or stale receipt is an
explicit product error. The built-in Engine Vehicle is retired; a future Vehicle
plugin must use these same contracts.
