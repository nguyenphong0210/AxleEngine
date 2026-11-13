# M14-U4 — Phase 44.8 Screen-Space Contact Shadows

## Status

Implemented and verified. The depth-backed fullscreen stage now supports a short ray-marched contact-shadow mode alongside GTAO. It uses bounded screen-space steps, depth bias, configurable ray length and strength, and shares the owned depth snapshot and fallback path introduced for 44.7.

## Verification

- Debug and Release `AxleEnvironmentRenderingTests` builds: exit 0.
- D3D12, Vulkan and OpenGL fixtures in Debug and Release: exit 0 for every backend.
- Native smoke fixture enables GTAO and contact shadows together, then restores the default path.
- `python scripts/check_architecture.py`: PASS.

Phase 44.8 is ticked in `docs/milestones/MILESTONE_14_LIGHTING_MATERIALS/PLAN.md`.
