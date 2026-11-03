# Phase 40 foundation implementation

Updated: 2026-11-03. Status: **in progress; full Phase 40 is not complete**.

This report tracks the foundation work requested after the planning handoff.
The [completion plan](ANIMATION_PHASE_40_COMPLETION_PLAN.md) remains the scope
reference; [ROADMAP.md](../../ROADMAP.md) records package status. No new animation
panel, RHI skinning, importer parity or packaged-animation acceptance is claimed
by a passing CPU foundation test.

| Package | Status and evidence |
| --- | --- |
| A0 | PASS: `AxleAnimationUnitTests` links the existing Animation library and evaluates a tiny rig/clip without starting the application or creating a window/device. The Release harness uses always-active checks. CTest's expected-failure case verifies a failing expectation returns a nonzero status. |
| A1 | REVIEW OPEN: Core topology/partial-bind/quaternion tests pass. Acceptance reopened for equal-length incompatible-rig and invalid-pose validation; see additional source findings in the review. |
| A2 | REVIEW OPEN: CPU mesh preparation/lazy GL upload is present. Acceptance reopened: STEP/cubic interpolation and root helper transforms fail targeted imports (R4/R5); skin/mesh/basis and source-independent import acceptance remain incomplete. |
| A3 | REVIEW OPEN: Independent typed-rule instance data is present. Acceptance reopened: Clone copies legacy closures that still observe the source (R6), and authoring-reset versus runtime-snapshot semantics need an explicit policy. |
| B1 | REVIEW OPEN: Versioned definition types and appended classifications are present. Acceptance reopened for complete rig compatibility signatures and durable authored identity through compilation/extraction; see R1 and additional source findings. |
| B2 | REVIEW OPEN: Atomic helpers and shared text/binary codecs are present. Acceptance reopened: skeleton round trips lose inverse binds (R2), geometry accepts out-of-range indices (R8), and complete validated resource resolution remains required. |
| C1 | REVIEW OPEN: Typed declarations and pure condition APIs are present. Acceptance reopened: names/IDs diverge, ID consumption leaves name triggers active, and wrong-type setter writes are accepted (R3). |
| C2 | REVIEW OPEN: Priority and crossfade machinery is present. Acceptance reopened: looping exit gates can activate before the requested phase after the first cycle (R7); explicit entry/reset is also inconsistent (R9). |
| B3 | REVIEW OPEN: Scene and both assembly formats carry animation metadata. Acceptance reopened: controller reconstruction loses motions/rules and stable identity (R1); imported/cooked resource and unique/reference behavior require functional pose-equivalence tests. |
| D1-K3 | Pending; see the completion plan. |

## Superseding corrective review

The follow-up review at `a6efa8c` reproduced **13 regressions** through production
APIs while both supplied CTest cases and the Release Editor/Runtime build passed.
The corrected package statuses above supersede the original completion claims.
See [Gemini foundation review](ANIMATION_PHASE_40_GEMINI_REVIEW.md) for exact
source locations, expected/observed results and the correction sequence. No
production fix is included in this documentation-only review commit.

## Commands and evidence

```powershell
cmake --build build --config Release --target AxleAnimationUnitTests --parallel 12
ctest --test-dir build -C Release -R '^Animation' --output-on-failure
```

A0: both CTest cases pass (the harness failure is intentionally expected to
exit nonzero). Build log: `build/animation-a0-build.log`.

Phase 38.3 material checks and Phase 39 interactive/OpenGL acceptance remain
separately open. Existing untracked user assets and crash logs are preserved.
