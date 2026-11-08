# AxleEngine Documentation

## Start with the current work

Read the **[Architecture Optimization Roadmap](ROADMAP.md)** first. It is the
single execution and progress checklist: OPT-00..OPT-10 cleans up the architecture;
OPT-11 and later feature work are deferred.

- **Current status**: OPT-01..09 architecture foundations are delivered and verified;
  OPT-10 acceptance/enforcement is now in progress. Detailed optional plugin
  capabilities and OPT-11 remain deferred. Read the
  [current acceptance record](reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md) for executed
  evidence and required open gates.
- **Implemented contracts**: CPU Core/SceneData/assets, owner-scoped jobs and shared
  simulation, checked native rendering/resource lifetimes, SDK DLL/static plugin
  composition and snapshot-bound export. See
  [architecture acceptance and seams](guides/ARCHITECTURE_ACCEPTANCE.md).

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

The built-in Vehicle is retired. Selected Vehicle/Spline/Terrain plugin foundations are delivered; detailed domain features remain deferred under the bounded [OPT-07/08 review](reviews/OPT_07_08_FOUNDATION_REVIEW.md).

## Maintenance rules

1. Track execution/progress in `docs/ROADMAP.md`; keep detailed contracts in their domain documents.
2. Label proposals, delivered work, and verified acceptance separately.
3. Update touched guides when APIs or ownership change. Preserve dated evidence and repair links when reports move.
4. Use repository-relative links. Do not present removed source files as live APIs.
5. Check links and package/dependency consistency for documentation edits.
