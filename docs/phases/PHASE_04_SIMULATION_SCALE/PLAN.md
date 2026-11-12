# Phase 04 — Simulation Scale Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 core simulation clock, Jolt physics integration, and EnTT ECS architecture.

---

## 1. Work Breakdown Structure

### C15-01: Public Monotonic Tick Identity
- [ ] Implement monotonic tick counter in `SimulationClock`.
- [ ] Expose fixed-timestep tick indices to `ScriptableEntity` and native simulation listeners.
- [ ] Unit tests verifying rollover safety and deterministic tick intervals.

### C15-02: Significance Policy & Distance Tiers
- [ ] Define significance evaluation component (`SignificanceComponent`).
- [ ] Implement camera distance and viewport visibility scoring with hysteresis bands.
- [ ] Regression testing tier stability under rapid camera movement.

### C15-03: Deterministic Staggered Scheduler
- [ ] Build modulo bucket scheduler distributing non-critical entities across ticks.
- [ ] Enforce stable entity ordering based on UUID.
- [ ] Measure CPU frame-time variance reduction on 10,000 active test entities.

### C15-04: Spatial Grid Indexing
- [ ] Implement scene-owned dynamic 3D spatial grid.
- [ ] Entity migration handling during transform updates.
- [ ] Fast radius and frustum query verification tests.

### C15-05: Dormancy & Wake-Up Protocol
- [ ] Implement dormant entity state suspending component update ticks.
- [ ] Event-driven wake-up triggers for physics impulses and player proximity.
- [ ] Save/load persistence verification for dormant entity states.

### C15-06: Transform Interpolation
- [ ] Buffer previous and current transforms for sub-tick entities.
- [ ] Implement smooth cubic/linear interpolation during `RenderWorld` snapshot extraction.
- [ ] Visual smoothness regression testing.

### C15-07: Jolt Physics Sleeping Policies
- [ ] Pass sleeping hints to Jolt body interfaces for distant/dormant rigid bodies.
- [ ] Verify collision triggers and wake-on-contact events function correctly.

### C15-08: Plugin Query API
- [ ] Expose read-only significance and tier query interfaces to external plugins.
- [ ] Documentation and sample usage for project plugins.

### C15-09: Animation Update Rate Optimization (URO)
- [ ] Implement skeletal update skipping based on significance tier.
- [ ] Cached bone matrix reuse and event/root-motion preservation.

---

## 2. Review and Evidence Requirements

1. **Determinism:** Headless test runner verifying identical simulation states across repeated runs.
2. **Contact Integrity:** Physics bodies must not fall through floors or fail triggers when throttled.
3. **Performance Evidence:** CPU profiling logs showing measurable CPU time reduction without gameplay divergence.
