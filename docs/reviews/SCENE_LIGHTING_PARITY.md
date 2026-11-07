# Authored scene lighting and backend color parity

Date: 2026-11-07. Baseline: b78fa6e (accepted OPT-05).
Status: IN PROGRESS; final verification pending.

The user requested similar OpenGL/Direct3D colors and black scenes without
lights, following UE Lit semantics. This is a new bounded post-OPT-05 correction,
not OPT-06. Unreal's Lit/Unlit distinction is the behavioral reference:
https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-modes-in-unreal-engine
No full Unreal renderer, Lumen or advanced feature parity is claimed.

## Confirmed causes and changes

- Native rendering synthesized a full sunlight source, analytical studio ambient
  and a procedural sky without scene entities; GL used different fallback light
  and ambient/specular equations. Both now consume shared authored lighting
  policy. No authored sun/environment means zero illumination; zero values stay
  zero and removing a light removes its contribution. Emissive/unlit particles
  remain intentionally visible. Default empty background is black.
- Native HDR presentation transformed the default gray clear through ACES/gamma;
  GL directly cleared its SDR target. The new default black is invariant under
  either display path. Basic material lighting now shares authored light units,
  inverse-square/window/falloff/cone terms, isotropic Smith geometry, analytical
  ambient tint and per-view exposure/gamma/mode.
- Private Asset/Assembly/thumbnail views already author their studio lights. VFX
  preview now explicitly owns a studio light in its private scene. No view can
  activate stale native IBL when its world lacks an enabled environment.
- A zero or absent sun no longer produces a procedural sky solar disk/halo.
  Explicit sky backgrounds remain an authored environment.
- GL first HDR loading happened during uniform upload and changed program,
  framebuffer and viewport. IBL generation now precedes view setup and owns a
  save/restore boundary with deterministic raster state, including culling.
- Canonical GL and hand-maintained fallback fragment sources had drifted; the
  fallback lacked BRDF helpers used by dynamic lights. Renderer now embeds the
  exact canonical vertex/fragment source through CMake, with dependency tracking
  and split string tokens for MSVC. Scene consumes those strings without a second
  shader implementation.

## Review and verification

A separate-context read-only reviewer found/rechecked absent-sun sky radiance,
stale IBL, first-HDR target/program/raster state, point falloff and lit-particle
ambient/intensity gaps. Those were repaired. Its final source recheck found no
remaining P1/P2 issue in the corrections. Builds/GPU/numerical comparisons are
executed by implementing Codex, not by that reviewer.

Production physical regressions cover no-light opaque/metal surfaces, emission,
zero/positive/removed directional lights, point/spot lights, explicit environment,
zero/disabled environment, actual no-sun/zero/positive/removed sun sky pixels,
cached IBL isolation, first GL HDR bake after cull-enabled rendering, postprocess
black preservation and canonical embedded shader compilation. Lit VFX has a
separate no-light/authored-light check. A numerical chart compares 18 cases on
D3D12, Vulkan and GL using identical scene/material/camera parameters and a
9x9 interior pixel average. Threshold: 2.5 RGB levels out of 255, with exact zero
required for no-light/removal cases. Final results are pending below.

Unrelated user edits to Samples/TestProject project and scene files are preserved.
NRI remains pinned; no push or later package is authorized.

Intermediate executed evidence: Debug consumer matrix 12/12 passes
(build/agent-runs/OPT-05/lighting-complete-debug-results.json). The numerical
chart passes all 36 backend comparisons, maximum channel delta 0.7161/255
(build/agent-runs/scene-lighting/debug/color-comparison.json). The physical GL
lit-particle test exposed an obsolete per-particle UV flag gate; GL now follows
the same authoritative emitter LightingEnabled as native, and the regression
passes. Earlier build failures (MSVC string-token limit, a vexing-parse test
construction and a corrupt generated test executable) are retained in lighting-*
logs; the literal split/probe syntax were corrected and the generated executable
was rebuilt. No production assertion or required comparison was weakened.
Final-source full Debug/Release checks remain pending.
