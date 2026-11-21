# Milestone 22 — Simulation Scale & Logic LOD Plan

**Status:** Proposed; packages released per explicit user instruction.
**Baseline:** Post-OPT-10 core simulation clock, Jolt physics integration, and EnTT ECS architecture.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).
**Entry:** Accepted [M21 geometry and visibility](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md) and the [M17 tick, pose and palette contract](../MILESTONE_17_ANIMATION/ARCHITECTURE.md) for URO.

---

## 1. Executive Summary

Milestone 22 addresses CPU-side simulation scaling for large open worlds and high-density entity counts. The objective is to decouple simulation costs from total entity population by dynamically throttling inactive or distant work while strictly preserving authoritative physics, gameplay state, and determinism (formerly OPT-15).

---

## 2. Completed Work Packages

*(None yet — Milestone 22 is an unreleased, proposed milestone).*

---

## 3. Checklist (Phase 51, formerly OPT-15)

### Phase 51.1: Monotonic Tick Identity & Significance Policy
- [ ] **C22-01: Public Monotonic Tick Identity** — Standardize monotonic tick counter in `SimulationClock`; expose fixed-timestep tick indices to `ScriptableEntity` and native simulation listeners.
- [ ] **C22-02: Significance Policy & Distance Tiers** — Define `SignificanceComponent` with camera distance scoring (Tier 1 `< 20m` @ 60/144 Hz, Tier 2 `20m - 100m` @ 15-30 Hz, Tier 3 `100m - 250m` @ 1-2 Hz, Tier 4 `> 250m` Dormant) and hysteresis bands.

### Phase 51.2: Staggered Scheduling & Spatial Partitioning
- [ ] **C22-03: Deterministic Staggered Scheduler** — Build modulo bucket scheduler `(SimulationTick + EntityID) % Interval == 0` distributing non-critical entities across ticks with stable UUID ordering.
- [ ] **C22-04: Spatial Grid Indexing** — Scene-owned dynamic 3D spatial hash grid ($64\\text{m} \\times 64\\text{m}$ cells) with fast entity migration, radius queries, and frustum filtering.

### Phase 51.3: Spatial Dormancy Bubble & Interpolation
- [ ] **C22-05: Spatial Dormancy Bubble & Wake-Up Protocol** — Suspend component updates for entities in dormant zones (`DormantTag`, 0.000% CPU hibernation); event-driven wake-up triggers for physics impulses and player proximity.
- [ ] **C22-06: Transform Interpolation** — Buffer previous and current transforms for sub-tick entities; implement smooth cubic/linear interpolation during `RenderWorld` snapshot extraction.

### Phase 51.4: Physics Fast-Path & Animation URO
- [ ] **C22-07: Jolt Physics Sleeping & Fast-Path Math** — Pass sleeping hints to Jolt body interfaces; deactivate distant rigid bodies and switch movement to kinematic translation.
- [ ] **C22-08: Plugin Query API** — Expose read-only significance and tier query interfaces to external gameplay and simulation plugins.
- [ ] **C22-09: Animation Update Rate Optimization (URO) & Bone LOD** — Implement skeletal update skipping based on significance tier (LOD0 full skeleton, LOD1 primary bones, LOD2 root/limb pivots) with cached matrix reuse.

---

## 4. Review and Evidence Requirements

1. **Determinism:** Headless test runner verifying identical simulation states across repeated runs.
2. **Contact Integrity:** Physics bodies must not fall through floors or fail triggers when throttled.
3. **Performance Evidence:** CPU profiling logs showing measurable CPU time reduction without gameplay divergence.
