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

G1 has been delivered in `7ba4361`, with report commit `d4dbb75`.
The delivery is **awaiting independent review**, not independently accepted by
Codex yet. The next planned implementation sequence is **D1, then D2**, after the
G1 review and a concrete scoped handoff. No G2/UI/renderer task is released here.
The live exchange, rather than this navigation paragraph, owns subsequent status.

The tool lets **either** author append and reply:

```powershell
python scripts/agent_exchange.py inbox --agent Gemini
python scripts/agent_exchange.py inbox --agent Codex
python scripts/agent_exchange.py post --from-agent Gemini --to Codex --type ACK --reply-to MESSAGE_ID --body-file path/to/reply.txt
```

It uses one local JSON exchange, validates messages, serializes writers with a
short-lived lock and replaces the file atomically. It does not run the other model.
An author flag identifies the caller's claimed role, not authenticated model identity.

## Reference map

| Topic | Reference | Role |
| --- | --- | --- |
| Feature status | [ROADMAP](../../ROADMAP.md) | Overall plan; not an automatic task queue |
| Animation requirements/dependencies | [Phase 40 plan](../design/ANIMATION_PHASE_40_COMPLETION_PLAN.md) | Package contracts |
| Animation implementation details | [Animation coding reference](../design/ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md) | Algorithms and examples; inspect current APIs |
| Latest animation foundation | [Verified corrections](../design/ANIMATION_FOUNDATION_FINAL_CORRECTIONS.md) | Existing accepted foundation evidence |
| G1 submission | [Gemini delivery](../design/ANIMATION_G1_GEMINI_DELIVERY.md) | Implementer's report; review still required |
| VFX requirements | [Phase 39 plan](../design/VFX_PHASE_39_COMPLETION_PLAN.md) | Historical/scoped feature contracts |
| VFX implementation details | [VFX coding reference](../design/VFX_PHASE_39_GEMINI_IMPLEMENTATION_GUIDE.md) | Reference only unless a task selects it |
| Material implementation details | [Material coding reference](../design/MATERIAL_PHASE_38_3_GEMINI_IMPLEMENTATION_GUIDE.md) | Reference only unless a task selects it |

## Historical prompts

Superseded one-shot animation prompts are retained in [archive](archive/README.md).
Their old paths redirect here so existing links still work. Do not replay an old
assignment based on its filename, an old completion checkbox, or a broad guide.

Detailed reviews/delivery reports remain at their original paths to preserve
citations and commit-specific evidence. They describe their recorded revisions;
they do not implicitly release another task.
