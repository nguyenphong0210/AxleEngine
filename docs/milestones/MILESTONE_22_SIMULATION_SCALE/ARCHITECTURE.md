# Milestone 22 — Simulation Scale and Logic LOD Architecture

## 1. Target and Scope

Phase 51 addresses CPU-side simulation scaling for large open worlds and high-density entity counts. The objective is to decouple simulation costs from total entity population by dynamically throttling inactive or distant work while strictly preserving authoritative physics, gameplay state, and determinism.

Supporting references: [`POST_OPT10_PROGRAM_PLAN.md`](../../archive/design/deferred/POST_OPT10_PROGRAM_PLAN.md), [`POST_OPT10_FEASIBILITY.md`](../../archive/design/deferred/POST_OPT10_FEASIBILITY.md).

---

## 2. Architecture and Subsystems

```mermaid
flowchart TD
    Clock["SimulationClock: Monotonic Tick Index"] --> Scheduler["C22-03: Deterministic Staggered Scheduler"]

    SpatialIndex["C22-04: Scene Spatial Index Grid"] --> Significance["C22-02: Significance Policy Evaluator"]
    Camera["View / Player Interest Points"] --> Significance

    Significance --> Tier0["Tier 0: Full Tick Rate 60 Hz - Player/Hero Entities"]
    Significance --> Tier1["Tier 1: Half Rate 30 Hz - Close Entities"]
    Significance --> Tier2["Tier 2: Quarter Rate 15 Hz - Distant Entities"]
    Significance --> Dormant["C22-05: Dormant / Suspended - Far/Occluded Entities"]

    Scheduler --> Dispatch["Simulation Dispatch"]
    Tier0 --> Dispatch
    Tier1 --> Dispatch
    Tier2 --> Dispatch

    Dispatch --> Physics["C22-07: Jolt Physics Sleeping Hints"]
    Dispatch --> Animation["C22-09: Animation Update Rate Optimization URO"]
    Dispatch --> Scripts["Native C++ Scripts OnFixedUpdate"]

    Dispatch --> Interp["C22-06: Transform Interpolation for Skipped Ticks"]
    Interp --> RenderWorld["RenderWorld Snapshot"]

```

### 2.1. Monotonic Tick Identity & Significance Policy (`C22-01`, `C22-02`)
- **Monotonic Tick Index:** Standardized tick counter ensuring reliable scheduling across variable frame times.
- **Significance Tiers:** Entities are assigned dynamic tiers based on camera distance, screen-space size, velocity, and gameplay significance.
- **Hysteresis:** State transitions include hysteresis buffers to prevent rapid oscillations between tiers near boundary distances.

### 2.2. Deterministic Staggered Scheduler (`C22-03`)
- Distributes non-critical entities across modulo tick buckets (e.g. half of Tier 1 updates on even ticks, half on odd ticks).
- Prevents frame-time spikes by bounding the maximum number of simulation updates dispatched per frame.
- Enforces stable entity ordering by UUID to guarantee deterministic replayability.

### 2.3. Spatial Partitioning & Dormancy (`C22-04`, `C22-05`)
- **Spatial Grid Index:** Scene-owned loose spatial grid tracking entity locations for fast radius and frustum queries.
- **Explicit Dormancy Protocol:** Entities far beyond visual range enter a dormant state where all ticking is suspended. Wake-up triggers automatically restore active status upon player proximity or external physics impulses.

### 2.4. Visual Transform Interpolation (`C22-06`)
- Entities updated at lower tick rates (15 Hz / 30 Hz) produce smoothly interpolated transforms for rendering, ensuring visual motion remains 60+ FPS fluid without jerky steps.

### 2.5. Physics Sleeping & Animation URO (`C22-07`, `C22-09`)
- **Jolt Physics Integration:** Dormancy states provide sleep hints to Jolt body interfaces, reducing solver island computations while preserving collision contacts.
- **Animation Update Rate Optimization (URO):** Skips skeletal bone palette recalculations for distant actors through the [M17 pose boundary](../MILESTONE_17_ANIMATION/ARCHITECTURE.md). Preserve pose generation, socket and physics authority. Reset incompatible cached poses when a rig, instance or relevant tick policy changes.

---

## 3. Technology and Boundaries

1. **Clean Decoupling:** Simulation logic remains completely independent from rendering; simulation never queries GPU states directly.
2. **Authoritative Authority:** Physics contacts, trigger collisions, and gameplay logic remain authoritative; interpolation is strictly visual.
3. **Deterministic Replay:** Tests verify that simulation runs with identical initial states yield identical end results regardless of frame rate.
