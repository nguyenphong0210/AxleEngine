# Milestone 01 — Render and Material Foundation Plan

**Status:** Bounded OPT-11 and OPT-12 packages completed and accepted; full asynchronous product rollout remains deferred to a future dedicated release.
**Supporting Architectural Document:** [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. Executive Summary

Phase 01 delivers the low-level rendering concurrency and shader compilation pipelines for AxleEngine. It decouples game simulation from GPU command generation via a multi-threaded Render Thread pipeline with VRAM aliasing (OPT-11), and establishes a versioned, transactional Material Graph document model and cross-compilation system targeting Direct3D 12 and Vulkan (OPT-12).

---

## 2. Completed Work Packages

### OPT-11: Render Threading, Parallel Command Recording & VRAM Aliasing
- [x] **C11-01: Ownership Design** — Specified device, resource, RenderService, presentation, shutdown, and device-loss ownership contracts.
- [x] **C11-02: Snapshot Queue** — Implemented bounded lock-free RenderWorld queue, back-pressure handling, frame-drop policy, and one-frame latency diagnostics.
- [x] **C11-03: Offscreen Worker & Presentation** — Dedicated render worker managing swapchain acquire, present, surface resize, and minimize handling without cross-thread CanSubmit violations.
- [x] **C11-04: Parallel Command Recording** — Evaluated NRI command-allocator and enkiTS worker jobs to record independent render passes in parallel, merging into the primary command list.
- [x] **C11-05: Transient VRAM Aliasing** — Implemented topological lifetime analysis in RenderGraph; allocated overlapping placed resources (ID3D12Heap / Vulkan memory aliasing) with automatic aliasing barriers (D3D12_RESOURCE_BARRIER_TYPE_ALIASING).
- [x] **C11-06: Plugin & Lifetime Stress** — Stress-tested plugin callbacks, resource leases, and engine restarts while frames were queued and transient heaps active.
- [x] **C11-07: Acceptance & Benchmarks** — Verified identical image output, bounded latency, clean shutdown, measured CPU overlap, and verified 40%–60% VRAM reduction on transient targets.
- *Evidence:* [docs/reviews/OPT_11_C11_07_ACCEPTANCE.md](../../reviews/OPT_11_C11_07_ACCEPTANCE.md).

### OPT-12: Shader and Material Pipeline
- [x] **C12-01: Material IR & Schema** — Implemented approved authoring schema (.axleasset), stable node IDs, parameter-name keys, versioning, parsing, and serialization.
- [x] **C12-02: Compiler Backend (DXC)** — Registry-driven lowering of graph AST to HLSL, compiling to real DXIL (D3D12) and SPIR-V (Vulkan), with native rendering verification.
- [x] **C12-03: Reflection & Pipeline Layout** — Strict descriptor/storage compatibility, backend-scoped shader identities, bounded code cache, immutable native generations, last-known-good reload, and named parameter overrides.
- [x] **C12-04: Resource Semantics** — Rooted raw/descriptor/VFS image dependencies, explicit color/sampler/fallback contracts, and transactional native generations.
- [x] **C12-05: Bounded Slang Evaluation** — Optional compiler prototype evaluating Slang vs DXC equivalence. DXC remains the default production compiler.
- [x] **C12-06: Explicit OpenGL Policy** — Material graph compilation carries explicit RHI policy; unsupported OpenGL targets fail transactionally with MG_OPENGL_UNSUPPORTED diagnostics.
- *Evidence:* [docs/reviews/OPT_12_C12_01_DELIVERY.md](../../reviews/OPT_12_C12_01_DELIVERY.md) through [OPT_12_C12_06_DELIVERY.md](../../reviews/OPT_12_C12_06_DELIVERY.md).

---

## 3. Pending / Deferred Work Packages

- [ ] **Asynchronous / Parallel Product Rollout:** Reopen full asynchronous multithreaded rendering as the engine-wide default only with a dedicated release following Phase 06 performance verification.
- [ ] **Dedicated GLSL Lowering for Material Graph:** Direct compilation from graph AST to GLSL 4.50 for full OpenGL parity (currently explicitly diagnosed as unsupported).

---

## 4. Review and Evidence Requirements

1. **Parity Testing:** Material graph outputs must render identically across D3D12 and Vulkan.
2. **Crash Resilience:** Corrupted node graphs or compilation errors must trigger last-known-good fallback rather than crashing the graphics pipeline.
3. **Memory Safety:** VRAM aliasing passes must produce zero GPU validation layer warnings or barrier hazards.
