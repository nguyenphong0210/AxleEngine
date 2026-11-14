# M15 Documentation Review

Review boundary: M15 Lighting and Material Fidelity documentation only. No M15
implementation was changed or released by this review. The M14 prerequisite is
accepted at code source `19ac2a1`; M15 remains `HOLD`.

## Result

**PASS after documentation corrections.** The M15 plan, architecture and
recovery plan now agree on the Phase 45 hierarchy, package aliases, M14 entry
contracts, M19 measurement ownership and U10/S10.5 final acceptance boundary.
All local Markdown links in the reviewed documents resolve.

## Findings

| ID | Result | Finding and correction |
|---|---|---|
| M15-DOC-01 | FIXED | `PLAN.md` mixed stale Phase 44 labels with the canonical Phase 45 hierarchy. Unit rows now use Phase 45 labels. |
| M15-DOC-02 | FIXED | The Unit table referenced undefined `P14` and disagreed with `REMEDIATION_PLAN.md` package ownership. Aliases now map P8=post/temporal, P9=atmosphere, P10=advanced materials, P11=DDGI and P12=authoring/final acceptance; P6B/P13 remain separately gated. |
| M15-DOC-03 | FIXED | Historical source references `77d6b70` and `791c344` were not clearly separated from the accepted M14 source. They are now explicitly historical, with `19ac2a1` recorded as the future M15 code boundary. |
| M15-DOC-04 | FIXED | Measurement text named M18 even though Milestone 19 owns rollout metrics. The recovery plan now names M19 and keeps thresholds proposed until P0 freezes them. |
| M15-DOC-05 | FIXED | Recovery plan final acceptance said U0-U9 / Phase 44.18. It now points to U10/S10.5 / Phase 45.18. |
| M15-DOC-06 | FIXED | M15 architecture topology listed removed `RHISceneRenderer::m_Frame`, used the old `m_Scheduler` spelling, and attributed scheduler groups to the wrong source file. It now reflects `ViewContext`, `RenderPassScheduler`, the weak active-target identity and `RHISceneRendererPasses.cpp`. |
| M15-DOC-07 | ACCEPTED HOLD | Feature/runtime evidence for SSR fidelity, captures, DDGI, fog, advanced materials, RT and performance is absent because M15 is unreleased. Every related checkbox remains open and the acceptance matrix requires the missing evidence. |
| M15-DOC-08 | FIXED | The plan had no dedicated Unit checklist for U1 Direct BRDF, U2 Dynamic Environment, or U3 S3.3/S3.4 despite listing them in the hierarchy. Added explicit work, exit criteria, work items and tracking gates. |
| M15-DOC-09 | FIXED | The WorkItem tracking table still omitted U1/U2 after their Unit sections were added. Added W1/W2 rows and tracking gates so every declared feature Unit has a traceable smallest verification item. |
| M15-DOC-10 | FIXED | The plan carried SubUnit checkboxes and a second WorkItem checklist, which could drift. Sections 4 onward are now one canonical 160-item atomic checklist; parent status is derived, and visual items carry explicit Editor build/open/expected-display gates. |

## Files reviewed

- `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md`
- `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/ARCHITECTURE.md`
- `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/REMEDIATION_PLAN.md`
- `docs/milestones/MILESTONE_14_RENDER_ARCHITECTURE/U9_FINAL_ACCEPTANCE_AND_M15_HANDOFF.md`
- `docs/reviews/M14_U9_FINAL_ACCEPTANCE_REVIEW.md`

## Entry recommendation

M15 documentation is ready as a controlled, unreleased execution contract. Before
implementation begins, release a concrete package or Unit in the exchange, run
P0's historical-finding re-audit against source `19ac2a1`, freeze the proposed
numeric/image/performance thresholds, and preserve the distinction between
implemented, self-verified, independently reviewed and accepted states.
