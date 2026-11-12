# Phase 05 — Content and Authoring Tools Architecture

## 1. Target and Scope

Phase 05 establishes a unified, crash-resilient content authoring pipeline across AxleEditor and Runtime. It guarantees that artist-facing authoring tools (Material Graph Editor, Visual FX Studio, Animation State Machine Designer, and Prefab Workflows) consume the exact same underlying asset and rendering contracts as the standalone Runtime, eliminating visual divergence between Editor viewports and shipping games.

Supporting references: [`POST_OPT10_PROGRAM_PLAN.md`](../../archive/design/deferred/POST_OPT10_PROGRAM_PLAN.md).

---

## 2. Architecture and Subsystems

```mermaid
flowchart TD
    AssetRegistry[Central Asset Registry & VFS] --> DocModels[Transactional Document Models: Undo/Redo, Dirty Flags]

    DocModels --> MatGraphUI[Material Graph Node Editor]
    DocModels --> VFXStudioUI[Visual FX Particle Editor]
    DocModels --> AnimStudioUI[Animation State Machine Designer]
    DocModels --> SceneHierarchy[Scene Hierarchy & Prefab Inspector]

    MatGraphUI --> PreviewService[Private Preview RenderService]
    VFXStudioUI --> PreviewService
    AnimStudioUI --> PreviewService

    PreviewService --> RHI[Modern RHI Native Viewport]

    DocModels --> Cooker[Asset Cooker & Exporter]
    Cooker --> PakFile[Shipping Package Data.axlepak]
```

### 2.1. Transactional Document Architecture
- Authoring tools operate on explicit document models with structured undo/redo command stacks.
- Modification marks documents dirty; saving writes canonical UTF-8 text formats (`.axleasset`, `.axlemat`, `.axlevfx`, `.axlescene`).
- Asset reloads use last-known-good fallback caching to ensure invalid edits never crash the editor.

### 2.2. Isolated Preview Viewports
- Asset previewers (Material Inspector, VFX Studio, Animation Player) create isolated, private `RenderWorld` instances with dedicated preview cameras and authored lighting rigs.
- Preview rendering completely avoids mutating the active editing scene or sharing global state.

### 2.3. Animation and VFX Runtime Bridges (`OPT-17`, `OPT-18`)
- **Animation Bridge:** Decouples skeletal evaluation from rendering, streaming final 128-bone matrix palettes into GPU Structured Buffers. Supports 1D/2D blend spaces, state machine crossfades, and root motion.
- **VFX Bridge:** High-performance particle simulation (SoA layout) with compute-driven particle sorting and GPU instanced billboard rendering.

### 2.4. Material Graph Visual Authoring (`OPT-19`)
- Interactive node-based graph canvas with real-time shader recompilation.
- Immediate visual reflection of parameter changes (Albedo, Roughness, Normal, Clearcoat) with integrated DXC shader error reporting.

---

## 3. Technology and Boundaries

1. **Decoupled UI Layer:** ImGui functions purely as an input/presentation client. No engine rendering logic or asset persistence logic is embedded inside UI code.
2. **Runtime Parity:** Any shader, particle effect, or animation configured in the Editor must render byte-identically in `AxleRuntime.exe`.
3. **Thread Safety:** Asset loading, baking, and thumbnail generation execute on background worker threads via enkiTS without stalling the main UI loop.
