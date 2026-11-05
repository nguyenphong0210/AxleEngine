# AxleEngine Documentation

## Start with the current work

Read the **[Architecture Optimization Roadmap](ROADMAP.md)** first. It is the
single execution/progress checklist: OPT-00..10 cleans up the architecture;
OPT-11 and later work are deferred. Historical Phase 41/42/43 names identify
technical domains, not execution order.

Current checkpoint: complete Vehicle removal in `be7b071` awaits independent
current-format verification. The user confirmed on 2026-11-05 that Engine requires
no old-format support or legacy payload preservation. The
[OPT-00 review](reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md) includes a scope update;
its original preservation requirements are superseded. No new plugin, EJ or NRI
implementation is claimed.

| Read next | Use it for |
| --- | --- |
| [Optimization roadmap](ROADMAP.md) | What to do now, what follows, package progress and phase exits |
| [Design catalog](design/README.md) | Find the detailed contract for the active phase; older/deferred documents are in the archive |
| [Architecture index](architecture/README.md) | Understand the dated source surveys, focused reviews and target architecture |
| [Build workflows](guides/BUILD_WORKFLOWS.md), [Tests](../Tests/README.md) | Actual build/startup/export commands and test layout |
| [Agent collaboration](agents/README.md) | Working protocol, current assignment and delivery/review exchange |
| [Historical product roadmap](../ROADMAP.md) | Original feature milestones and development history |

## Documentation roles

| Location | Status and purpose |
| --- | --- |
| `ROADMAP.md` in this directory | Current optimization sequence and package checkboxes |
| `design/` | Proposed contracts; read only those linked by the active OPT phase. Reports are outside this folder in the design archive |
| `archive/design/` | Deferred feature plans and dated delivery/review evidence removed from the active design folder |
| `architecture/` | Dated source snapshots/reviews plus explicitly proposed target boundaries; pre-retirement snapshots do not describe current Vehicle support |
| `guides/` | Working development and feature workflows; check review dates against current source |
| `reference/` | File formats and terminology |
| `agents/` | Live task authority/evidence and collaboration rules; archived prompts are historical |

## Working guides

- [Build/startup/export](guides/BUILD_WORKFLOWS.md) and [development conventions](guides/DEVELOPMENT.md).
- [Scene and ECS](guides/SCENE_AND_ECS.md), [native scripts](guides/SCRIPTING.md) and [physics](guides/PHYSICS.md).
- [Rendering](guides/RENDERING.md), [materials/shaders](guides/MATERIALS_AND_SHADERS.md) and [UI documents/binding](guides/UI.md).
- [Asset format reference](reference/ASSET_FORMAT.md).

Vehicle is currently retired from Engine. Its future plugin is OPT-07; legacy
payload handling is a compatibility mechanism, not a working vehicle simulation.

## Deferred work and evidence

Use the [deferred queue](ROADMAP.md#deferred-feature-work) and
[design archive](archive/design/README.md) for advanced rendering, material/VFX/animation
completion and new editor authoring. Existing acceptance obligations remain open
where their reports say so. Delivery reports are implementer evidence until an
independent review records what it actually inspected and ran.

## Maintenance rules

1. Track execution/progress in `docs/ROADMAP.md`; keep detailed contracts in their domain documents.
2. Label proposals, dated source snapshots, delivered work and verified acceptance separately.
3. Update touched guides when APIs or ownership change. Preserve dated evidence, index relocations and repair links when reports move.
4. Use repository-relative links. Do not present removed source files as live APIs.
5. Check links and package/dependency consistency for documentation edits. Do not infer tests, performance or backend parity from a written plan.
6. A roadmap or an old embedded prompt is not a production assignment; the latest user scope and live exchange determine actual work.
