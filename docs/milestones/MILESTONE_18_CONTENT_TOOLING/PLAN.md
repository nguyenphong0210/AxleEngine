# Milestone 18 — Content Authoring Studios & Tooling Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 asset registry, transaction-safe document foundations, and RHI preview viewports.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## 1. Executive Summary

Milestone 18 establishes a production-grade, crash-resilient content authoring pipeline across AxleEditor and the standalone Runtime. It consolidates the historical visual authoring milestones (Historical Phase 38.3 Material Inspector, Phase 39 Visual Particle & VFX Studio, and Phase 40 Skeletal Animation Studio) into four unified, modular child phases (Phase 48.1 through 48.4, formerly OPT-17, 18, 19, Tools).

---

## 2. Completed Baseline Foundations

- [x] **Asset Registry & Serialization** — Central `AssetRegistry`, Virtual File System (`VFS`), and text-based serializers (`SceneSerializer`, `ParticleEmitterSerializer`, `AssemblySerializer`).
- [x] **RHI Preview Viewport Architecture** — Isolated offscreen framebuffer targets leasing GUI texture IDs to Dear ImGui docking viewports.
- [x] **Core Panel Layouts** — Wired host panels (`EditorLayer`, `HLSLStudioPanel`, `VFXEditorPanel`) ready for complete document models.
- [x] **Historical CPU Foundations** — Delivered preliminary CPU skeletal data structures (Phase 40 packages A3/A2/G1) and baseline particle emitter simulation (Phase 39 step 102).

---

## 3. Checklist (Phase 48)

### Phase 48.1: Skeletal Animation Studio & Runtime Pipeline (formerly OPT-17 / Phase 40)
- [ ] **C17-01: Rig & Clip Import** — FBX and glTF 2.0 skinned mesh importer; procedural reference rig; retain bind-pose for unkeyed channels; enforce topological joint ordering.
- [ ] **C17-02: Timeline & Dope Sheet Editor** — Keyframe timeline panel in AxleEditor; local TRS key manipulation; box-selection, copy, paste, delete; transactional undo/redo.
- [ ] **C17-03: Animator Controller & Blend Spaces** — Visual state machine graph; typed parameters (Float, Int, Bool, Trigger); serializable transition conditions; 1D and 2D blend spaces with phase continuity.
- [ ] **C17-04: Skeletal Runtime & Modern RHI Drawing** — Stream evaluated joint matrices to 128-bone GPU Structured Buffers on D3D12 and Vulkan; multi-instance isolation (no shared mutable state).
- [ ] **C17-05: Sockets & Bone Attachments** — Authorable socket transforms attached to parent bones for weapons, helmets, and equipable props.
- [ ] **C17-06: Procedural Foot IK & Ground Snapping** — Two-bone analytical inverse kinematics raymarching against collision geometry to eliminate foot sliding.
- [ ] **C17-07: Additive Animation & Upper-Body Layering** — Layered animation masks allowing upper-body actions (aiming, shooting, reloading) while lower-body executes locomotion loops.

### Phase 48.2: Visual FX Studio & GPU Particle Integration (formerly OPT-18 / Phase 39)
- [ ] **C18-01: Document Lifecycle & Preview Isolation** — Standalone `.axlevfx` document model with multi-emitter persistence; isolated preview viewport simulation preventing active scene mutations.
- [ ] **C18-02: Advanced Parameter Modes** — Constant, Two Constants (Random between two constants), Curve, Two Curves, and Color/Alpha gradient modes with deterministic seeds.
- [ ] **C18-03: Advanced Motion & Emitter Modules** — Rate over distance (vehicle trails), start delay, prewarm, per-axis velocity/force over lifetime, orbital motion, and emitter-motion inheritance.
- [ ] **C18-04: Ribbon & Trail Emitters** — Bounded per-particle history tracking, dynamic quad ribbon generation, texture UV stretch/wrap modes, and generation-safe particle handles.
- [ ] **C18-05: Instanced Mesh Particles** — 3D mesh particle rendering with material overrides, custom orientation, and bounding-sphere sorting.
- [ ] **C18-06: Particle Collision & Gameplay Events** — World collision against ground planes and mesh colliders; event-driven sub-emitter spawning on particle birth, collision, and death.
- [ ] **C18-07: Flipbook Sub-Frame Motion Blending** — Sprite sheet UV animation with optical-flow or linear sub-frame blending for ultra-smooth smoke/fire effects.
- [ ] **C18-08: GPU Particle Simulation & Visual Effect Graph** — Compute shader particle simulation pass on modern RHI; bitonic GPU particle sorting; direct indirect draw dispatch (`vkCmdDrawIndirect` / `ExecuteIndirect`).

### Phase 48.3: Visual Material Graph UI & Advanced Scene Tooling (formerly OPT-19 / Phase 38.3)
- [ ] **C19-01: Interactive Node Canvas** — Modern node graph editor in AxleEditor (`imnodes` / `ImGui Node Editor`); real-time AST lowering to HLSL with DXC compiler diagnostics.
- [ ] **C19-02: Advanced Surface Nodes** — Visual nodes for Clearcoat, Anisotropy, Subsurface Scattering, Refraction, and Cauchy Chromatic Dispersion.
- [ ] **C19-03: Reflection Probe Gizmos & Placement** — Scene-placed box and sphere reflection probe volumes with real-time cubemap capture and octahedral encoding.
- [ ] **C19-04: Decal Volumes & Projected Textures** — Screen-space or mesh-projected decal volumes for bullet holes, tire marks, and road dirt.
- [ ] **C19-05: Prefab Variant Authoring** — Nested prefab override tracking, visual property diffing, and revert-to-base workflows.

### Phase 48.4: Packaging Cooker & Diagnostic Pipeline (formerly OPT-Tools)
- [ ] **CTools-01: Asset Cooker & Dependency Verification** — Standalone cooker traversing scene dependency trees, compressing textures to BCn formats, and compiling pre-warmed shader caches.
- [ ] **CTools-02: Monolithic Pak File Packaging** — Pack cooked binary assets into encrypted or compressed `Data.axlepak` containers mounted by the Virtual File System.
- [ ] **CTools-03: Headless Asset Linter** — Command-line tool verifying zero dangling UUID references, missing textures, uncompressed assets, or oversized textures.

---

## 4. Review and Evidence Requirements

1. **Runtime Parity:** Every shader, animation state, or particle effect authored in AxleEditor must render byte-identically in `AxleRuntime.exe`.
2. **Crash Resilience:** Corrupted or missing asset files must emit informative warnings in the Console rather than crashing the process.
3. **Evidence Archiving:** Test results and packaging receipts archived in `docs/reviews/`.
