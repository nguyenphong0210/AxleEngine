# AxleEngine Roadmap

This is the only execution roadmap. Work is ordered by Phase; each Phase owns
one `PLAN.md` and one `ARCHITECTURE.md` under `docs/phases/`.

| Phase | Name | Status | Documents |
| --- | --- | --- | --- |
| 00 | Architecture foundation | OPT-00..10 tracked; OPT-10 open gates remain | [Plan](docs/phases/PHASE_00_FOUNDATION/PLAN.md) · [Architecture](docs/phases/PHASE_00_FOUNDATION/ARCHITECTURE.md) |
| 01 | Render and material foundation | OPT-11/12 bounded work recorded | [Plan](docs/phases/PHASE_01_RENDER_FOUNDATION/PLAN.md) · [Architecture](docs/phases/PHASE_01_RENDER_FOUNDATION/ARCHITECTURE.md) |
| 02 | Lighting and material fidelity | OPT-13 released scope accepted; later lighting work deferred | [Plan](docs/phases/PHASE_02_LIGHTING_MATERIALS/PLAN.md) · [Architecture](docs/phases/PHASE_02_LIGHTING_MATERIALS/ARCHITECTURE.md) |
| 03 | Geometry and visibility | Proposed | [Plan](docs/phases/PHASE_03_GEOMETRY_VISIBILITY/PLAN.md) · [Architecture](docs/phases/PHASE_03_GEOMETRY_VISIBILITY/ARCHITECTURE.md) |
| 04 | Simulation scale | Proposed | [Plan](docs/phases/PHASE_04_SIMULATION_SCALE/PLAN.md) · [Architecture](docs/phases/PHASE_04_SIMULATION_SCALE/ARCHITECTURE.md) |
| 05 | Content and authoring tools | Proposed | [Plan](docs/phases/PHASE_05_CONTENT_TOOLING/PLAN.md) · [Architecture](docs/phases/PHASE_05_CONTENT_TOOLING/ARCHITECTURE.md) |
| 06 | Performance and rollout | Proposed | [Plan](docs/phases/PHASE_06_ROLLOUT/PLAN.md) · [Architecture](docs/phases/PHASE_06_ROLLOUT/ARCHITECTURE.md) |

Phase is the only active ordering system. OPT and C-package identifiers remain
inside plans and review reports as historical references and evidence keys.
They do not create a second queue.

Start with the current Phase plan, then read its architecture target before any
implementation. Review and test evidence lives in [docs/reviews](docs/reviews/).
Historical material lives in [docs/archive](docs/archive/).
