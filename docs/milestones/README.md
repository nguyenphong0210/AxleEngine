# AxleEngine Milestones Directory

This directory organizes all milestone-level plans and technical architecture documents across the entire engine lifecycle:

Every milestone (01 through 24) owns two canonical planning documents:

- **`ARCHITECTURE.md`**: Subsystem architectures, Mermaid data-flow diagrams, data contracts, and technology limits.
- **`PLAN.md`**: Granular work breakdown, status checkboxes, child Phases, and review criteria.

The root [ROADMAP.md](../../ROADMAP.md) is the master index and single source of authority for milestone execution order.

## Milestones after M16

| Milestone | Plan | Architecture |
| --- | --- | --- |
| M17 Animation | [Plan](MILESTONE_17_ANIMATION/PLAN.md) | [Architecture](MILESTONE_17_ANIMATION/ARCHITECTURE.md) |
| M18 VFX | [Plan](MILESTONE_18_VFX/PLAN.md) | [Architecture](MILESTONE_18_VFX/ARCHITECTURE.md) |
| M19 Canvas2D | [Plan](MILESTONE_19_CANVAS2D/PLAN.md) | [Architecture](MILESTONE_19_CANVAS2D/ARCHITECTURE.md) |
| M20 Cooker and Linter | [Plan](MILESTONE_20_COOKER/PLAN.md) | [Architecture](MILESTONE_20_COOKER/ARCHITECTURE.md) |
| M21 Geometry and Visibility | [Plan](MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md) | [Architecture](MILESTONE_21_GEOMETRY_VISIBILITY/ARCHITECTURE.md) |
| M22 Simulation Scale | [Plan](MILESTONE_22_SIMULATION_SCALE/PLAN.md) | [Architecture](MILESTONE_22_SIMULATION_SCALE/ARCHITECTURE.md) |
| M23 Material Graph and Scene Tools | [Plan](MILESTONE_23_MATERIAL_GRAPH_TOOLING/PLAN.md) | [Architecture](MILESTONE_23_MATERIAL_GRAPH_TOOLING/ARCHITECTURE.md) |
| M24 Performance and Rollout | [Plan](MILESTONE_24_PERFORMANCE_ROLLOUT/PLAN.md) | [Architecture](MILESTONE_24_PERFORMANCE_ROLLOUT/ARCHITECTURE.md) |

## Previous numbering

| Previous milestone or requirement family | Current owner |
| --- | --- |
| Former M17 Geometry, `M17-*` batches and `C17-*` leaves | M21 Geometry, `M21-*` batches and `C21-*` leaves with the same suffixes |
| Former M18 Simulation Scale, `C15-01..09` in Phase 47 | M22 Simulation Scale, `C22-01..09` in Phase 51 |
| Former M19 Phase 48.1 Animation | M17 Animation (`M17-01..07`) |
| Former M19 Phase 48.2 VFX | M18 VFX (`M18-01..09`) |
| Former M19 Phase 48.3 Material Graph and scene tools | [M16 Material Graph Studio](MILESTONE_16_RAY_TRACING/PLAN.md#material-graph-studio) owns Studio and advanced raster surfaces (former `M23-01..02` and `M15-U12`). M23 owns scene integration (`M23-03..06`). |
| Former M19 Phase 48.4 Cooker and linter | M20 Cooker and Linter (`M20-01..07`) |
| Former M20 Performance Rollout, `C20-01..06` | M24 Performance and Rollout, `C24-01..06` |

Historical reviews, commits and exchange messages retain their original identifiers. M19 Canvas2D builds on the authored in-game UI from historical M08 Phase 34.
