# Documentation status

Updated: **2026-11-09**

This is the short orientation page for the architecture delivered through
OPT-01..OPT-10. The execution order and package status live in
[`ROADMAP.md`](ROADMAP.md); current implementation evidence and remaining
acceptance gates live in
[`reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md`](reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md).

## Current architecture

- **CPU ownership:** Core owns jobs, owner-scoped completion and file IO;
  SceneData owns ECS storage, hierarchy, transforms and generic extension data;
  Asset owns CPU asset formats and discovery; Scene owns composition and the
  shared simulation driver.
- **Physics boundary:** Physics depends on Core and Jolt only. PhysicsDebug is
  the higher-level adapter for renderer-facing debug drawing.
- **Rendering boundary:** Renderer owns ECS extraction and the RHI ECS
  streamer. RHI does not depend on Renderer, Scene, EnTT or Jolt.
- **Plugins and products:** Editor plugins are shared-host DLLs; selected
  Runtime plugins are statically composed from the project snapshot. Export
  validates the frozen snapshot and receipt before packaging.
- **Vehicle:** the built-in Engine Vehicle module is retired. Future Vehicle,
  Spline and Terrain features belong to plugin work that is still deferred.

## Status at this checkpoint

OPT-01 through OPT-09 are delivered and recorded as accepted in their review
documents. OPT-10 implementation and enforcement are present, but final
architecture acceptance remains open for the gates listed in its acceptance
record. OPT-11 and advanced feature expansion are deferred.

## Documentation rules

Use the roadmap, active design index, architecture index and current guides for
new work. Review reports remain historical evidence and do not release new
implementation. Live agent scope is recorded in the shared exchange and
protocol. Superseded handoffs and agent-specific coding guides are removed from
the working documentation set; old exchange messages remain immutable history.
