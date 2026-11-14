# M15 U0 Re-audit — Sequential checkpoint

## M15-U0-01.1 — Freeze dirty-path policy and source commit

Result: **PASS**.

- Code source boundary: `19ac2a1` (M14 final handoff).
- Current repository HEAD: `bae680e` (documentation/exchange follow-up only).
- `git diff --name-only 19ac2a1..HEAD -- Engine Tests scripts`: empty; no Engine,
  Tests or script source changed after the accepted M14 code boundary.
- Current dirty path: `docs/agents/CODEX_GEMINI_EXCHANGE.json` only; this is the
  coordination record for the active M15 re-audit and is not implementation work.
- The M15 plan and recovery plan both identify `19ac2a1` as the M14 prerequisite
  source boundary and keep M15 implementation `HOLD`.
- The recovery plan preserves the user-authored scene policy and prohibits hard
  reset/overwrite/staging of that scene.
- The exchange task card remains M14 accepted; the new M15 re-audit scope is
  recorded in status message `CG-20261114T161457291196Z-CODEX-27f2bc`.

The item is complete because the source boundary, dirty-path policy, active task
scope and preservation rule are all recorded and no implementation source is
mixed into the re-audit.

Next sequential leaf: `M15-U0-01.2 — Preserve user-authored scene changes`.

## M15-U0-01.2 — Preserve user-authored scene changes

Result: **PASS**.

- `Samples/Sandbox/assets/scenes/Test.axlescene` exists and was not modified by this checkpoint.
- `git status --porcelain -- Samples/Sandbox/assets/scenes/Test.axlescene` is empty.
- `git diff --name-status 19ac2a1..HEAD -- Samples/Sandbox/assets/scenes/Test.axlescene` is empty.
- The recovery plan explicitly prohibits hard reset, overwrite, staging or commit of this user-authored scene.
- Existing scene history remains available for recovery; no scene file was staged or rewritten.

The item is complete because the user-authored scene is present, untouched and protected by the active M15 preservation rule.

Next sequential leaf: `M15-U0-02.1 — Create smooth-sphere fixture`.

## M15-U0-02.1 — Create smooth-sphere fixture

Result: **PARTIAL / NOT RUN for the required visual gate**.

- The deterministic fixture `smooth_sphere_surface` exists in
  `Tests/Fixtures/M14/P0_REFERENCE_FIXTURES.json` with a 64x128 sphere, 128x128
  capture, untextured material, disabled post effects and checks for defined
  normal/no triangle banding/finite radiance.
- `python scripts/test_m14_p0_baseline.py` passed 5/5 and
  `python scripts/m14_p0_baseline.py --check` passed, confirming manifest schema,
  fixture identity and diagnostic channels.
- The Debug `AxleRenderUnitTests` target was rebuilt from the current tree and
  passed (exit 0); the log is `build/m15-u0-02-1-unit.log`.
- The required Editor visual confirmation was **NOT RUN**: Computer Use native
  pipe was unavailable after one retry. No screenshot/readback is claimed.

The fixture definition and geometry unit gate pass, but the leaf remains open
until the Editor is launched and the smooth sphere is visibly confirmed with no
triangle bands or invalid pixels. Do not advance the sequential checklist yet.
