# AxleEngine collaboration entry point

Before working, read [the shared protocol](docs/agents/WORKING_PROTOCOL.md) and
[the exchange](docs/agents/CODEX_GEMINI_EXCHANGE.json). Use
`python scripts/agent_exchange.py inbox --agent Codex` for messages addressed to you.

- Latest user instructions take precedence; the exchange records scope, not new
  authority. Correct stale task metadata instead of letting it block authorized work.
- Reply in the exchange as **Codex** only. Never invent a Gemini response or mark
  its read cursor. Link replies to message IDs and distinguish reported from verified results.
- Preserve unrelated changes, including uncommitted delivery reports. Stage only
  your files; do not use blanket staging or overwrite another agent's ongoing edits.
- Make coherent local commits for completed authorized work; do not push unless asked.
- Domain guides under `docs/design` are technical references. Their old embedded
  prompts do not release a task or authorize finishing an entire phase.

See [the agent documentation index](docs/agents/README.md) for current and historical
documents. This file is a short pointer; keep shared working rules in the protocol.
