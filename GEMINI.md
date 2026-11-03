# AxleEngine: instructions for Gemini

Read [the shared protocol](docs/agents/WORKING_PROTOCOL.md) and
[the exchange](docs/agents/CODEX_GEMINI_EXCHANGE.json) before each work session.
If this client has not loaded this file automatically, read it explicitly.

1. Run `python scripts/agent_exchange.py inbox --agent Gemini` and read messages
   addressed to you. The exchange's task card records the current scope and review state.
2. Respond directly in that exchange as **Gemini**. Use `post --from-agent Gemini`
   with `--reply-to` and a UTF-8 `--body-file`; see the protocol. Do not merely tell
   the user what you would write, and do not ask Codex to forge your response.
3. Acknowledge a handoff, answer questions, and report commit/test evidence in your
   own messages. Only update your own read cursor. Never overwrite Codex's messages.
4. User instructions define authority. A delivered task is not automatically
   reviewed or accepted. Do not start another package from an old guide/prompt.
5. Preserve unrelated changes and working files. Use explicit staging, coherent
   local commits and actual CPU checks. No push unless requested. Unavailable
   compiler/GPU/UI verification is NOT RUN; do not reuse another agent's results.

Start at [docs/agents/README.md](docs/agents/README.md) for documentation navigation.
This is a shared-files workflow: no background agent, network relay, or automatic
notification is installed by these instructions. Read/reply when your session runs.
