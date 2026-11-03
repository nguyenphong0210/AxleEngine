# Phase 40 foundation corrections

Date: 2026-11-03. Follow-up to the
[review](ANIMATION_PHASE_40_GEMINI_REVIEW.md) of `a6efa8c`.
Each row records an implementation commit with permanent production-API tests.
Full Phase 40 rendering/editor acceptance remains separate.

| Review finding | Correction and evidence |
| --- | --- |
| R3 | Canonical declared parameter storage by stable ID; names resolve to that slot. Type/finite checks, collision-safe renames and runtime-only trigger defaults. Permanent regressions cover both alias directions, consumption, wrong-type writes and invalid conditions. Release Animation CTests PASS; `build/animation-fix-parameters-build.log`. |

Other review findings remain open until a correction row and its evidence are
added here. The original review documents the pre-fix behavior, not the current
acceptance result.
