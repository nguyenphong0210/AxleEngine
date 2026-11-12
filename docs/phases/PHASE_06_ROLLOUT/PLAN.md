# Phase 06 — Performance and Rollout Plan

**Status:** Proposed; individual packages released per explicit user instruction.
**Baseline:** Post-OPT-10 stabilized engine code and Phase 00..05 deliverables.

---

## 1. Work Breakdown Structure

### C20-01: Benchmark Matrix & Standardized Environments
- [ ] Define standardized benchmark scenes: `MaterialReference.axlescene`, `CarTest.axlescene`, `OffRoadTrack.axlescene`, and a synthetic dense geometry fixture.
- [ ] Implement deterministic spline camera paths with fixed time-step loops.
- [ ] Configure target resolution presets: 1080p, 1440p, 4K across Standard, High, and Ultra quality tiers.

### C20-02: Image Quality & Cross-Backend Parity
- [ ] Capture baseline golden images across D3D12, Vulkan, and OpenGL backends.
- [ ] Implement automated image comparison tool computing PSNR, SSIM, and maximum channel error.
- [ ] Verify fallback image parity on simulated lower-tier capability profiles.

### C20-03: Performance Profiling & Metrics Telemetry
- [ ] Integrate GPU timestamp query queries across NRI command buffers for granular pass timing (G-Buffer, Shadow Depth, Lighting, Post-Processing).
- [ ] Implement CPU main-thread, worker job, and frame-time percentile statistics (p50, p95, p99, max).
- [ ] Add VRAM commit and transient heap aliasing efficiency metrics.

### C20-04: Reliability, Device Recovery & Boundary Stress
- [ ] Implement automated stress tests for window resizing, fullscreen toggling, and DPI switching.
- [ ] Add simulated GPU device-loss recovery validation on D3D12 and Vulkan.
- [ ] Run long-soak stability runs (1,000+ continuous frame loops) checking zero memory leaks and clean descriptor disposal.

### C20-05: Cutover & Cleanup
- [ ] Deprecate temporary fallback shims and obsolete prototype code paths.
- [ ] Update public API documentation and Editor tooltips to reflect promoted defaults.
- [ ] Clean up debug profiling overhead from final Release builds.

### C20-06: Acceptance & Master Release Closure
- [ ] Execute complete multi-configuration test matrix (Debug + Release, D3D12 + Vulkan + OpenGL).
- [ ] Verify architectural rule compliance (`check_architecture.py` passes 100%).
- [ ] Compile final release delivery report and publish performance scorecard.

---

## 2. Review and Evidence Requirements

1. **Reproducibility:** Every performance metric must specify hardware environment, resolution, backend, and commit hash.
2. **Quality Verification:** Any performance gain that degrades image quality beyond defined tolerances (PSNR $< 40\text{ dB}$) fails acceptance.
3. **Traceability:** Delivery reports are archived in `docs/reviews/` under standard naming conventions.
