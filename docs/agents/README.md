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
- **Next phase in the queue**: OPT-04 (NRI device and GPU lifetime foundation). The current review assignment is complete; no later implementation was started.

Read **[docs/ROADMAP.md](../ROADMAP.md)** for OPT-00..11 order/progress and the
[active design index](../design/README.md) for current phase contracts.
Historical Phase 41/42/43 and G labels are domain references, not execution order.
The live exchange determines the current assignment; no plan releases production work.

## Reference map

| Topic | Reference | Role |
| --- | --- | --- |
| Execution/progress | [Optimization roadmap](../ROADMAP.md) | OPT-00..11, one package checklist and deferred queue |
| Architecture contracts | [Master plan](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) | External prerequisite graph, AF definitions and bounded architecture exit |
| Native render integration | [NRI contract](../design/NRI_INTEGRATION_CONTRACT.md) | Source-pinned vendor/service/lifetime rules mapped to P41 |
| Rendering priority | [Phase 41 plan](../design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md) | Canonical packages and required gates |
| OPT-02/03 acceptance | [Codex review and corrections](../reviews/OPT_02_03_CODEX_REVIEW.md) | Current-source Debug/Release, native products and verified repairs |
| ECS/jobs baseline findings | [Integration review](../architecture/ENTT_ENKITS_REVIEW.md) | Historical reproduced faults and risks; use the current acceptance report for correction status |
| ECS/jobs refactor | [Refactor plan](../design/ENTT_ENKITS_REFACTOR_PLAN.md) | 18 package contracts; EJ-00..EJ-14 delivered |
| Plugin product/ABI design | [Plugin architecture](../design/PLUGIN_SYSTEM_ARCHITECTURE.md) | Proposed DLL/static products, UX, lifetime and fresh Vehicle scope |
| Plugin execution | [Plugin plan](../design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) | 26 unreleased packages and acceptance gates |
| Plugin implementation details | [Plugin coding reference](../design/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | Proposed algorithms, SDK contracts and real-fixture strategy |
| Feature status | [ROADMAP](../../ROADMAP.md) | Overall plan; not an automatic task queue |
| Animation requirements/dependencies | [Phase 40 plan](../archive/design/deferred/ANIMATION_PHASE_40_COMPLETION_PLAN.md) | Package contracts |
| Animation implementation details | [Animation coding reference](../archive/design/deferred/ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md) | Algorithms and examples; inspect current APIs |
| Latest animation foundation | [Verified corrections](../archive/design/reports/ANIMATION_FOUNDATION_FINAL_CORRECTIONS.md) | Existing accepted foundation evidence |
| G1 submission | [Gemini delivery](../archive/design/reports/ANIMATION_G1_GEMINI_DELIVERY.md) | Implementer's report; review still required |
| VFX requirements | [Phase 39 plan](../archive/design/deferred/VFX_PHASE_39_COMPLETION_PLAN.md) | Historical/scoped feature contracts |
| VFX implementation details | [VFX coding reference](../archive/design/deferred/VFX_PHASE_39_GEMINI_IMPLEMENTATION_GUIDE.md) | Reference only unless a task selects it |
| Material implementation details | [Material coding reference](../archive/design/deferred/MATERIAL_PHASE_38_3_GEMINI_IMPLEMENTATION_GUIDE.md) | Reference only unless a task selects it |

## Historical prompts

Superseded one-shot prompt dumps have been retired; see [historical agent archive](archive/README.md).
Detailed reviews and delivery reports are preserved in the [design archive](../archive/design/README.md).
