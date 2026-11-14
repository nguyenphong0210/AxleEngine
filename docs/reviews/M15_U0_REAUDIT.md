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
