# Animation foundation: final corrective implementation

Date: 2026-11-03. Starting code/document baseline: `3f9da1d`.
Follow-up to [the second review](ANIMATION_A3_A2_SECOND_REVIEW.md).

| Finding | Correction and evidence |
| --- | --- |
| S1 | Mesh construction is private and factory-only. The factory detaches/rebuilds the rig, normalizes the supported draft, validates geometry, derives bounds and publishes once. GPU buffers remain lazy. All CPU/scene/legacy GL callers use the factory. Compile-time bypass checks and invalid index/rig/attribute tests PASS; Release Animation CTests 2/2 PASS. Logs: `build/animation-final-publication-build.log`, `build/animation-final-publication-tests.log`. |
| S2 | Added append-only `StepNext` interpolation, preserving exact key values and next-value open intervals for translation, quaternion and scale tracks. FBX classification keeps previous/next directions distinct and rejects genuinely mixed directions. Adaptive checks use each property's actual interpolation. Exact-source boundary and actual text codec round-trip tests PASS; Release Animation CTests 2/2 PASS. Logs: `build/animation-final-step-next-build.log`, `build/animation-final-step-next-tests.log`. |

Remaining rows and final integration results are added only after verification.
Full Phase 40, animation RHI pixels and native Editor authoring are not accepted
by CPU foundation checks.
