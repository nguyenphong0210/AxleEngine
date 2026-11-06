# AxleEngine Documentation

## Start with the current work

Read the **[Architecture Optimization Roadmap](ROADMAP.md)** first. It is the
single execution and progress checklist: OPT-00..OPT-10 cleans up the architecture;
OPT-11 and later feature work are deferred.

- **Current Status**: Phase OPT-03 completed (AF-02, EJ-08..EJ-10, AF-03, AF-04, EJ-11..EJ-14 delivered and verified).
  - Clean Level-0 `Core` leaf library.
  - `Application` in `Platform` (Level 1).
  - Parallel EnTT view iteration with tombstone safety and `SceneCommandBuffer` mutation boundaries.
  - Read-only transform evaluation with monotonic revisions (`GetTransformRevision()`).
  - `SceneData` separated from subsystem bridges.
  - Pure CPU `AssetManager` with native embedded UUIDs and raw root-relative paths (companion `.meta` sidecars eliminated).
  - Unified `SimulationDriver`, `SimulationClock`, and `SimulationInputAdapter` resolving defect `ECSJ-05`.
- **Next Phase**: [OPT-04](ROADMAP.md#opt-04---nri-device-and-gpu-lifetime-foundation) (NRI device and GPU lifetime foundation).

| Read next | Use it for |
| --- | --- |
| [Optimization roadmap](ROADMAP.md) | What to do now, what follows, package progress and phase exits |
| [Design catalog](design/README.md) | Find detailed contracts for active phases; older/deferred documents are in the archive |
| [Architecture index](architecture/README.md) | Architectural overviews, execution flows, subsystem boundaries, and target layering |
| [Build workflows](guides/BUILD_WORKFLOWS.md), [Tests](../Tests/README.md) | Actual build, test, startup, and export workflows |
| [Agent collaboration](agents/README.md) | Working protocol, current assignments, and delivery/review exchange |
| [Historical product roadmap](../ROADMAP.md) | Original feature milestones and development history |

## Documentation roles

| Location | Status and purpose |
| --- | --- |
| `ROADMAP.md` in this directory | Current optimization sequence and package checkboxes |
| `design/` | Architectural design contracts linked by the active OPT phase |
| `archive/design/` | Deferred feature plans and historical delivery/review reports |
| `architecture/` | Current architecture overviews, execution flows, and target layered architecture |
| `guides/` | Working development and feature workflows (ECS, Physics, Scripts, Rendering, UI) |
| `reference/` | File formats, syntax DSL, and technical reference |
| `agents/` | Live task authority, protocol, exchange, and handoff tracking |

## Working guides

- [Build/startup/export](guides/BUILD_WORKFLOWS.md) and [development conventions](guides/DEVELOPMENT.md).
- [Scene and ECS](guides/SCENE_AND_ECS.md), [native scripts](guides/SCRIPTING.md) and [physics](guides/PHYSICS.md).
- [Rendering](guides/RENDERING.md), [materials/shaders](guides/MATERIALS_AND_SHADERS.md) and [UI documents/binding](guides/UI.md).
- [Asset format reference](reference/ASSET_FORMAT.md).

Vehicle simulation is retired from the core engine. A standalone replacement plugin is planned in [OPT-07](ROADMAP.md#opt-07---write-vehicle-as-a-complete-plugin).

## Maintenance rules

1. Track execution/progress in `docs/ROADMAP.md`; keep detailed contracts in their domain documents.
2. Label proposals, delivered work, and verified acceptance separately.
3. Update touched guides when APIs or ownership change. Preserve dated evidence and repair links when reports move.
4. Use repository-relative links. Do not present removed source files as live APIs.
5. Check links and package/dependency consistency for documentation edits.
