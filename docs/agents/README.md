# Agent collaboration

This is the entry point for Codex/Gemini collaboration in AxleEngine. It separates
live scope and messages from technical reference guides and historical prompts.

## Read in this order

1. [Working protocol](WORKING_PROTOCOL.md): how either agent reads, writes and hands off.
2. [Shared exchange](CODEX_GEMINI_EXCHANGE.json): current task, per-agent state and messages.
3. Only the reference/report linked by the current task card.

Root [AGENTS.md](../../AGENTS.md) points Codex here; root
[GEMINI.md](../../GEMINI.md) points Gemini here. Read the appropriate entry file
explicitly if the running client/session has not loaded it.

## Current checkpoint

- **OPT-00**: Gemini delivered clean retirement; the separate independent manual/full-product acceptance gap remains open.
- **OPT-01**: Accepted baseline and verified corrections; see the roadmap evidence.
- **OPT-02/03**: Accepted after Codex review and verified corrections for EJ-01..EJ-14 and AF-02..AF-04; [acceptance report](../reviews/OPT_02_03_CODEX_REVIEW.md).
- **OPT-04**: Accepted after implementation, consolidated review, correction rechecks and actual D3D12/Vulkan API validation; [execution record](../reviews/OPT_04_CODEX_EXECUTION.md).
- **OPT-05**: Accepted after consolidated review, repairs, separate-context source rechecks and final Debug/Release consumer/API/product gates; [acceptance report](../reviews/OPT_05_CODEX_REVIEW.md).
- **OPT-06**: **Accepted PLG-01..13 / G0/G1/G2** after Codex repairs through `af3f7b2`, separate-context correction rechecks, Debug/Release 59/59 CTest and native external DLL/Editor/restart/recovery/GPU gates. All OPT06-R1..R10 are closed; see the [acceptance review](../reviews/OPT_06_CODEX_REVIEW.md). Its acceptance is scoped to PLG-01..13.
- **Default future workflow**: Gemini implements the released scope; Codex performs one consolidated final review and owns repairs, including substantial redesign. Significant Codex corrections receive a separate-context recheck. See the [shared workflow](WORKING_PROTOCOL.md#default-implementation-and-final-review-workflow). No next phase is activated by this decision.
- **OPT-07/08**: Plugin foundations accepted after repairs through `bd22550` under the user's narrowed review scope. Native SDK DLL/lifecycle, owned data, dependency graph and shipping script boundaries are verified; advanced feature completeness is deferred. See the [bounded review](../reviews/OPT_07_08_FOUNDATION_REVIEW.md).
- **OPT-09**: Implemented and verified by Codex: selected static Runtime, snapshot-bound export, Debug/Release 69/69 CTest and 30/30 native command outcomes each. OPT-10 is now released; see the current task and acceptance record. See the [execution record](../reviews/OPT_09_IMPLEMENTATION.md).

- **OPT-10**: In progress under the explicit user assignment: architecture enforcement, stress/cross-product evidence and maintained documentation. [Current acceptance record](../reviews/OPT_10_ARCHITECTURE_ACCEPTANCE.md) separates implementation, executed tests and open gates.

Read the repository **[ROADMAP.md](../../ROADMAP.md)** for the single Milestone
order/progress system and the [Milestones directory](../milestones/README.md) for current
plan/architecture pairs. Historical Phase and OPT labels in reports are not a
second execution queue.
The live exchange determines the current assignment; no plan releases production work.

## Reference map

| Topic | Reference | Role |
| --- | --- | --- |
| Execution/progress | [Roadmap](../../ROADMAP.md) | Single Milestone order and status authority |
| Architecture contracts | [Master plan](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) | External prerequisite graph, AF definitions and bounded architecture exit |
| Native render integration | [NRI contract](../design/NRI_INTEGRATION_CONTRACT.md) | Source-pinned vendor/service/lifetime rules mapped to P41 |
| Rendering priority | [Milestone 13/14 plans](../milestones/MILESTONE_13_RENDER_CONCURRENCY/PLAN.md) | Current plan and required gates |
| OPT-02/03 acceptance | [Codex review and corrections](../reviews/OPT_02_03_CODEX_REVIEW.md) | Current-source Debug/Release, native products and verified repairs |
| ECS/jobs baseline findings | [Integration review](../architecture/ENTT_ENKITS_REVIEW.md) | Historical reproduced faults and risks; use the current acceptance report for correction status |
| ECS/jobs refactor | [Refactor plan](../archive/design/deferred/ENTT_ENKITS_REFACTOR_PLAN.md) | 18 package contracts; EJ-00..EJ-14 delivered |
| Plugin product/ABI design | [Plugin architecture](../design/PLUGIN_SYSTEM_ARCHITECTURE.md) | Proposed DLL/static products, UX, lifetime and fresh Vehicle scope |
| Plugin execution | [Plugin plan](../archive/design/deferred/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) | 26 unreleased packages and acceptance gates |
| Plugin implementation details | [Plugin coding reference](../archive/design/deferred/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | Proposed algorithms, SDK contracts and real-fixture strategy |
| Feature status | [ROADMAP](../../ROADMAP.md) | Overall plan; not an automatic release |
| Animation requirements/dependencies | [Milestone 17 plan](../milestones/MILESTONE_17_CONTENT_TOOLING/PLAN.md) | Current content/tooling boundary |
| Latest animation foundation | [Verified corrections](../archive/design/reports/ANIMATION_FOUNDATION_FINAL_CORRECTIONS.md) | Existing accepted foundation evidence |
| G1 submission | [Gemini delivery](../archive/design/reports/ANIMATION_G1_GEMINI_DELIVERY.md) | Implementer's report; review still required |
| VFX requirements | [Milestone 17 plan](../milestones/MILESTONE_17_CONTENT_TOOLING/PLAN.md) | Current content/tooling boundary |

## Historical prompts

Superseded one-shot prompts and agent-specific handoffs have been retired. The exchange retains immutable historical messages; it is not a live implementation instruction. See the [historical agent archive](archive/README.md).
Detailed reviews and delivery reports are preserved in the [design archive](../archive/design/README.md).
