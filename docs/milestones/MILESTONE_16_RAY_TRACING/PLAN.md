# Milestone 16 — Ray Tracing Backend Plan

**Status:** Proposed; packages are released only by explicit user instruction.
**Baseline:** M15 raster lighting/material fidelity, planar reflection, reflection capture contracts, `ViewContext`, `RenderPassScheduler`, `RenderGraph` and `RenderBackendPolicy`.
**Supporting architecture:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

M16 adds a capability-safe hardware ray-tracing backend. Every RT feature has a raster fallback and a separate evidence gate. The milestone does not claim support merely because a GPU reports an RT capability bit.

## 1. Dependency order

```text
M15 material/surface + view contracts
  -> M16-RT-01 capability and RHI foundation
  -> M16-RT-02 BLAS/TLAS lifecycle
  -> M16-RT-03 pipeline and shader table
  -> M16-RT-04 RT shadows
  -> M16-RT-05 RT reflections
  -> M16-RT-06 denoise and temporal history
  -> M16-RT-07 material parity and resolve
  -> M16-RT-08 fallback, budget and acceptance
  -> M16-RT-09 optional RT GI
```

## 2. Atomic checklist

### M16-RT-01 — Capability policy and RHI foundation

- [ ] **M16-RT-01.1 — Freeze D3D12/Vulkan capability contract.** Define required AS, pipeline, shader-table, inline-ray and descriptor features.
- [ ] **M16-RT-01.2 — Add RHI acceleration-structure descriptors and handles.** No backend-specific handle leaks into Scene.
- [ ] **M16-RT-01.3 — Add RT pipeline/SBT descriptors and lifetime leases.**
- [ ] **M16-RT-01.4 — Add RenderGraph RT access states and validation.**
- [ ] **M16-RT-01.5 — Add explicit raster fallback diagnostics and quality-tier policy.**

### M16-RT-02 — BLAS/TLAS lifecycle

- [ ] **M16-RT-02.1 — Build BLAS for static opaque geometry.**
- [ ] **M16-RT-02.2 — Refit BLAS for supported deforming geometry.**
- [ ] **M16-RT-02.3 — Retire and compact BLAS generations after completion.**
- [ ] **M16-RT-02.4 — Build TLAS with stable instance identity and masks.**
- [ ] **M16-RT-02.5 — Update TLAS on transform, visibility, scene and material generation changes.**
- [ ] **M16-RT-02.6 — Test resize, device reset, scene replacement and failed rebuild retention.**

### M16-RT-03 — RT pipeline and shader table

- [ ] **M16-RT-03.1 — Compile ray-generation, miss and closest-hit libraries for D3D12/Vulkan.**
- [ ] **M16-RT-03.2 — Build hit groups and shader binding tables.**
- [ ] **M16-RT-03.3 — Bind shared geometry/material records with generation checks.**
- [ ] **M16-RT-03.4 — Register `RayTracingASBuild` and RT scheduler passes.**
- [ ] **M16-RT-03.5 — Validate missing shader/descriptor fallback without corrupting the raster frame.**

### M16-RT-04 — Ray-traced shadows

- [ ] **M16-RT-04.1 — Trace directional-light shadow visibility.**
- [ ] **M16-RT-04.2 — Add point/spot shadow rays and light masks.**
- [ ] **M16-RT-04.3 — Match raster shadow bias/normal policy at contact boundaries.**
- [ ] **M16-RT-04.4 — Add spatial shadow denoise and temporal rejection.**
- [ ] **M16-RT-04.5 — Compare RT/raster shadows on static and moving fixtures.**

### M16-RT-05 — Ray-traced reflections

- [ ] **M16-RT-05.1 — Trace primary reflection rays with bounded distance and recursion policy.**
- [ ] **M16-RT-05.2 — Evaluate shared material Fresnel, roughness and metallic inputs.**
- [ ] **M16-RT-05.3 — Add reflection miss and confidence metadata.**
- [ ] **M16-RT-05.4 — Add reflection denoise and history validation.**
- [ ] **M16-RT-05.5 — Resolve RT, planar, SSR, local probe and sky IBL without duplicate energy.**
- [ ] **M16-RT-05.6 — Test off-screen, self-intersection, moving-object and roughness cases.**

### M16-RT-06 — Temporal and denoising system

- [ ] **M16-RT-06.1 — Add per-view RT history targets and generation identity.**
- [ ] **M16-RT-06.2 — Reject history on camera cut, resize, view switch, AS/material/scene generation and device epoch.**
- [ ] **M16-RT-06.3 — Add variance/spatial reconstruction for shadows and reflections.**
- [ ] **M16-RT-06.4 — Validate stable output under camera/object motion.**

### M16-RT-07 — Material and renderer parity

- [ ] **M16-RT-07.1 — Match ForwardPBR base color, metallic and roughness.**
- [ ] **M16-RT-07.2 — Match normal, clearcoat, sheen and transmission policy.**
- [ ] **M16-RT-07.3 — Match custom Material Graph support or publish explicit unsupported fields.**
- [ ] **M16-RT-07.4 — Validate deterministic RT/raster material comparisons within documented tolerance.**

### M16-RT-08 — Fallback, performance and acceptance

- [ ] **M16-RT-08.1 — Validate RT-capable D3D12 and Vulkan startup and shutdown.**
- [ ] **M16-RT-08.2 — Validate non-RT hardware and disabled RT fallback to raster.**
- [ ] **M16-RT-08.3 — Record AS memory, SBT memory, dispatch cost and denoise cost.**
- [ ] **M16-RT-08.4 — Add deterministic image/readback and artifact identity manifests.**
- [ ] **M16-RT-08.5 — Complete Editor visual evidence for shadows/reflections/fallback.**
- [ ] **M16-RT-08.6 — Complete independent architecture and source review.**

### M16-RT-09 — Optional RT GI

- [ ] **M16-RT-09.1 — Trace bounded indirect irradiance.**
- [ ] **M16-RT-09.2 — Share geometry and visibility data with DDGI.**
- [ ] **M16-RT-09.3 — Validate bounce, leak, denoise, temporal stability and budget.**

## 3. Exit criteria

M16 core closes when RT shadows and RT reflections pass on declared D3D12/Vulkan RT hardware, all raster fallback gates pass, material parity and lifetime/reset tests pass, and timing/memory evidence is archived. M16-RT-09 remains separately marked optional until its GI acceptance criteria pass.
