# Phase 05 — Content and Authoring Tools Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 asset registry, transaction-safe document foundations, and RHI preview viewports.

---

## 1. Work Breakdown Structure

### OPT-17: Animation Authoring & Runtime Pipeline
- [ ] Connect skeletal runtime evaluation to native RHI structured buffers (D3D12/Vulkan bone palettes).
- [ ] Add 2D Blend Space authoring and real-time previewing in Animation Studio.
- [ ] Implement procedural root-motion extraction and kinematic controller ground-snapping.
- [ ] Regression suite testing FBX/glTF import, socket attachments, and continuous blending.

### OPT-18: Visual FX Studio & GPU Particle Integration
- [ ] Refactor particle simulation into cache-aligned SoA layout with enkiTS worker batching.
- [ ] Connect particle renderer to native RHI instanced draws (`RHIParticleRenderer`).
- [ ] Add curve editors for size, color, velocity over lifetime in VFX Editor Panel.
- [ ] Headless tests verifying particle life cycles, emission rates, and bounding volumes.

### OPT-19: Material Graph & Scene Tooling
- [ ] Build interactive node canvas for `.axlemat` Material Graph authoring in AxleEditor.
- [ ] Real-time node graph compilation to HLSL via DXC with live Viewport updates.
- [ ] Add gizmo-based Reflection Probe volume placement and preview rendering.
- [ ] Add prefab variant authoring and multi-entity override tracking.

### OPT-Tools: Packaging & Diagnostic Workflows
- [ ] Implement project export packaging cooker with dependency tree verification.
- [ ] Add asset validation linter detecting missing textures, broken references, and oversized textures.
- [ ] Packaging regression tests building standalone `Data.axlepak` and running in `AxleRuntime.exe`.

---

## 2. Review and Evidence Requirements

1. **Parity Check:** Every editor feature must have an automated test verifying identical behavior in `AxleRuntime.exe`.
2. **Crash Resilience:** Corrupted or missing asset files must emit informative warnings in the Console rather than crashing the process.
3. **Evidence Archiving:** Test results and packaging receipts archived in `docs/reviews/`.
