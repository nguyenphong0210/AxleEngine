# Render flow

Updated: **2026-11-09**

This flow shows the implemented ownership seams and the remaining OPT-10
acceptance boundary. Renderer extracts immutable data; RHI owns native device and
GPU lifetimes; OpenGL is an explicit compatibility backend.

```mermaid
sequenceDiagram
    participant App as Editor / Runtime
    participant Scene as Scene + SceneData
    participant Sim as SimulationDriver
    participant Ext as Render extraction
    participant RS as RenderService
    participant RHI as RHI backend
    participant GPU as D3D12 / Vulkan / OpenGL

    App->>Sim: frame(dt, input)
    Sim->>Scene: apply owner-thread commands
    Sim->>Scene: fixed controls and physics phases
    Scene-->>Ext: immutable transforms/components/lights
    Ext->>RS: RenderWorld + ViewRequest
    RS->>RHI: acquire frame and resolve resources
    RHI->>GPU: record passes and submit
    GPU-->>RHI: completion / present result
    RHI-->>RS: retire frame resources
    RS-->>App: RenderResult and diagnostics
```

## Backend selection

```mermaid
flowchart LR
    Request[Product render request] --> Ready{Backend ready?}
    Ready -->|NRI D3D12| D3D[RHI D3D12 device]
    Ready -->|NRI Vulkan| VK[RHI Vulkan device]
    Ready -->|OpenGL compatibility| GL[OpenGL adapter]
    Ready -->|No| Error[Explicit failure / diagnostic]
    D3D --> Common[RenderService lifetime + view contract]
    VK --> Common
    GL --> Common
    Common --> Submit[Submit, present, retire]
```

A failed native backend is not silently converted into a different backend for a
native acceptance claim. Physics debug data reaches this flow through
`PhysicsDebug`; Physics itself remains graphics-independent.
