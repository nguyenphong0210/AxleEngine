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

**Latest user scope (2026-11-04): plugin planning/documentation only.** Read the
[plugin architecture](../design/PLUGIN_SYSTEM_ARCHITECTURE.md),
[26-package plan](../design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) and
[implementation guide](../design/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md).
The selected direction is Editor DLL modules, project-selected static Runtime,
a new Plugins window and support for project-local plugins. A fresh Vehicle
replacement may retire the old implementation when implementation is assigned;
this planning turn does not authorize deleting code or assets. No PLG package or
Gemini implementation is released by these documents. No new relative priority
between plugin implementation and rendering is inferred. Consult the live task card.

The user has made **Phase 41: RHI-Primary Rendering Foundation** the highest
priority. The [architecture](../design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md),
[30-package plan](../design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md) and
[coding guide](../design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md) are references.
The Phase 41 request released documentation only. P41-00 then P41-01 are next
implementation candidates, not an automatic assignment of the entire phase.

Animation G1 (`7ba4361`, report `d4dbb75`) remains delivered awaiting independent
review. D1/D2 and new authoring are deferred behind rendering foundations. Narrow
animation renderer dependencies are mapped by P41-23/24. The live exchange owns
subsequent task state; this checkpoint is navigation, not a competing task queue.

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
| Rendering priority | [Phase 41 plan](../design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md) | Canonical packages and required gates |
| Plugin product/ABI design | [Plugin architecture](../design/PLUGIN_SYSTEM_ARCHITECTURE.md) | Proposed DLL/static products, UX, lifetime and fresh Vehicle scope |
| Plugin execution | [Plugin plan](../design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) | 26 unreleased packages and acceptance gates |
| Plugin implementation details | [Plugin coding reference](../design/PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | Proposed algorithms, SDK contracts and real-fixture strategy |
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
