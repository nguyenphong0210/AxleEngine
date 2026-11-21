# Milestone 24 — Performance Profiling, Parity & Master Rollout Plan

**Status:** Proposed; individual packages released per explicit user instruction.
**Baseline:** Post-OPT-10 stabilized engine code and Phase 42–48 deliverables.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## 1. Executive Summary

Milestone 24 consolidates measured performance benchmarks, multi-backend visual parity, device recovery stress testing, and official release cutover across AxleEngine (formerly OPT-20). It serves as the authoritative quality gate determining when advanced experimental pipelines are promoted to engine-wide defaults. Reuse the [M20 gameplay and packaging fixture](../MILESTONE_20_COOKER/PLAN.md), accepted M21/M22 scale evidence and [M23 route matrix](../MILESTONE_23_MATERIAL_GRAPH_TOOLING/PLAN.md). Measure BCn profile, archive compression and shader prewarm tuning against those accepted package products.

---

## 2. Completed Work Packages

*(None yet — Milestone 24 is an unreleased, proposed milestone).*

---

## 3. Checklist (Phase 53, formerly OPT-20)

- [ ] **Phase 53.1: Benchmark Matrix & Standardized Environments (C24-01)** — Define standardized benchmark scenes: `MaterialReference.axlescene`, `CarTest.axlescene`, `OffRoadTrack.axlescene`, and a dense geometry fixture; deterministic spline camera paths with fixed time-step loops; resolution presets: 1080p, 1440p, 4K across Standard, High, and Ultra quality tiers.
- [ ] **Phase 53.2: Image Quality & Cross-Backend Parity (C24-02)** — Capture baseline golden images across D3D12, Vulkan, and OpenGL backends; implement automated image comparison tool computing PSNR, SSIM, and maximum channel error; verify fallback image parity on simulated lower-tier capability profiles.
- [ ] **Phase 53.3: Performance Profiling & Metrics Telemetry (C24-03)** — Integrate GPU timestamp queries across NRI command buffers for granular pass timing (G-Buffer, Shadow Depth, Lighting, Post-Processing); implement CPU main-thread, worker job, and frame-time percentile statistics (p50, p95, p99, max); add VRAM commit and transient heap aliasing efficiency metrics.
- [ ] **Phase 53.4: Reliability, Device Recovery & Boundary Stress (C24-04)** — Implement automated stress tests for window resizing, fullscreen toggling, and DPI switching; add simulated GPU device-loss recovery validation on D3D12 and Vulkan; run long-soak stability runs (1,000+ continuous frame loops) checking zero memory leaks and clean descriptor disposal.
- [ ] **Phase 53.5: Cutover & Cleanup (C24-05)** — Deprecate temporary fallback shims and obsolete prototype code paths; update public API documentation and Editor tooltips to reflect promoted defaults; clean up debug profiling overhead from final Release builds.
- [ ] **Phase 53.6: Acceptance & Master Release Closure (C24-06)** — Execute complete multi-configuration test matrix (Debug + Release, D3D12 + Vulkan + OpenGL); verify architectural rule compliance (`check_architecture.py` passes 100%); compile final release delivery report and publish performance scorecard.

---

## 4. Review and Evidence Requirements

1. **Reproducibility:** Every performance metric must specify hardware environment, resolution, backend, and commit hash.
2. **Quality Verification:** Any performance gain that degrades image quality beyond defined tolerances (PSNR `< 40 dB`) fails acceptance.
3. **Traceability:** Delivery reports are archived in `docs/reviews/` under standard naming conventions.
