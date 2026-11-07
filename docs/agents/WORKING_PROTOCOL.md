# Shared Codex/Gemini working protocol

## Purpose and limits

Both assistants can communicate directly by reading and writing
`docs/agents/CODEX_GEMINI_EXCHANGE.json` in this repository. The user does not need
to copy whole reviews between chats. Each running assistant reads the inbox and
records its own reply, question, review or delivery there.

Files are persistent context, not a running transport. No background process,
automatic model invocation, notification service or cross-chat memory is set up.
A session must be started/resumed to read and reply. An existing Gemini session
may need an explicit request to read root `GEMINI.md` once. No Gemini response is
claimed until Gemini actually writes it.

Codex supports persistent project instructions in `AGENTS.md`; see the
[official OpenAI documentation](https://developers.openai.com/cookbook/examples/codex/iterating-development-workflows-with-codex).
The other files here are this project's conventions, not a product guarantee.

## Authority and task status

- Latest user instructions define scope. Record them in the task card; never let
  an old file override the user or require repetitive approval for authorized work.
- Priority checkpoint (2026-11-04): clean architecture before feature expansion.
  [docs/ROADMAP.md](../ROADMAP.md) owns the single sequential OPT-00..11 queue and
  progress. The [master contract](../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md)
  owns AF/cross-plan dependencies; P41/EJ/PLG are stable domain identifiers.
  Old Vehicle retirement was delivered early; the independent
  [OPT-00 review](../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md) records the old
  preservation contract. User clarification (2026-11-05): no support for old data
  formats or legacy Vehicle payload preservation; prioritize a clean Engine.
  Legacy preservation/suppression findings do not block acceptance under this scope.
  The clean removal in `be7b071` awaits independent current-format verification.
  Plans and review reports do not release subsequent production packages.
- Asset identity steering (2026-11-05): AF-01 must generate no per-asset .meta
  files or renamed .uid/.import substitutes. Raw resources (PNG, fonts, audio, etc.)
  use canonical root-relative paths. Native Engine assets embed their UUID in the
  file; discovery rebuilds an in-memory index without an authored root registry.
  Keep generated caches ignored. The updated AF-01 handoff owns the exact contract.
- Only a concrete released task authorizes implementation. A roadmap, reference
  guide, old prompt, or suggested next task does not release every later package.
- Historical checkpoint (2026-11-03): the user placed Phase 41 RHI-primary rendering
  foundations before new animation authoring and VFX expansion. Read the live task
  card for the actual released package/range and current delivery/review state.
  A static guide, historical G1/D1/D2 sequence or this checkpoint does not release
  an implementation task or override a newer user assignment.
- A delivery report and implementer's PASS claims are **reported evidence**.
  A review message records what the reviewer actually inspected and ran. Keep
  implementation, verification and acceptance separate.
- The coordinator for a task maintains its card from user instructions and actual
  handoffs. The implementer supplies progress/delivery messages; the reviewer
  supplies findings/verification. Roles may change when the user assigns them.

## Default implementation and final review workflow

User decision, 2026-11-07: for future released work, Gemini implements and Codex
performs the consolidated final review and necessary repairs. The user accepts
substantial rewrites or replacement of an unsuitable design. This workflow reduces
Codex implementation tokens; it does not reduce the acceptance standard. A later
explicit user assignment can override these default roles.

1. **Release:** record the concrete package/phase range, base commit, technical
   requirements and final review boundary in the task card. This default workflow
   does not activate another package or the historical OPT-02..10 proposal.
2. **Gemini implements:** complete the released scope with relevant package
   self-checks, repairs, coherent commits and actual verification. Supply a concise
   handoff with base/head commits, affected design/callers, exact test evidence and
   open gates. Label this implemented/self-verified, pending Codex review.
3. **Codex reviews and fixes:** at the end of the released scope, inspect the full
   diff, actual architecture/callers, dependencies, failure paths and lifetime
   rules. Own the necessary corrections through verification. Codex may rewrite
   code across modules or replace an unsuitable design within that scope without
   requesting routine approval or returning each fix to Gemini. Preserve user
   requirements, supported behavior and unrelated work; update affected design
   references and explain material changes. Do not limit necessary correctness
   fixes or architectural repairs merely to preserve the implementer's diff.
   The user explicitly authorizes Codex to add, delete, rename, move, split or
   merge project files as needed within the assigned implementation/review scope,
   without routine per-file approval. Update affected build wiring, references
   and verification alongside structural file changes. Preserve unrelated work.
4. **Verify the corrected result:** rebuild and run the required gates against
   the final source. Have a separate-context reviewer recheck significant Codex
   corrections before acceptance. Keep that recheck focused on the corrections
   and affected callers; reuse the reviewer if further repairs need rechecking.
5. **Close:** record delivery, review findings, correction commits, actual
   verification and acceptance separately, then stop at the released boundary.

Use one consolidated Codex review at the recorded final boundary, with necessary
correction rechecks. Do not add routine intermediate independent reviews unless
the user requests them; package self-verification and required native gates remain
mandatory. Freeze Gemini source edits during Codex review/repair in the shared
checkout. These roles do not authorize vendor upgrades, later features, pushes or
automatic invocation of another session. Missing required checks remain NOT RUN.

## Explicitly delegated multi-package runs

A user may explicitly activate a concrete package/phase range and delegate
implementation, self-review, repairs and verification to Gemini. In that case,
the activated range is the assignment boundary: passing delivery gates permits
continuation without waiting for Codex after every package. Historical narrower
stop instructions are superseded within that range; domain contracts, dependency
order and required native gates remain in force.

Gemini records implemented/self-verified delivery separately from independent
review and final acceptance. It posts phase checkpoints, saves resumable state
and stops at the activated range boundary or a genuine unresolved dependency.
It never invents Codex approval. An unavailable required check stays NOT RUN and
blocks dependent acceptance; it is not silently waived to continue.

The [autonomous execution plan](handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md)
contains a proposed OPT-02..10 range and activation/resume prompts. Preparing
that document alone does not activate the range. Preserve an existing release
until an actual user instruction activates its replacement.

## Read and reply on every work session

1. Read root role instructions, this protocol, the current task card and inbox.
2. Inspect `git status`/HEAD. Read relevant code/reports rather than all history.
3. Acknowledge new actionable messages as your own author. Reference the exact
   message ID. Ask a focused QUESTION if information is missing; send ANSWER or
   STATUS when resolving it. Do useful independent work within the released scope.
4. Before code edits, record IN_PROGRESS/STATUS with your base commit and files
   you expect to change. Do not edit the other active agent's files concurrently.
5. Deliver actual commit hashes, commands, exit codes, test/log paths and limits.
   Then stop at the assigned boundary and wait for the other agent's next session.

### CLI

From the repository root, with Python 3:

```powershell
python scripts/agent_exchange.py status
python scripts/agent_exchange.py inbox --agent Gemini
python scripts/agent_exchange.py inbox --agent Codex
python scripts/agent_exchange.py inbox --agent Gemini --mark-read
python scripts/agent_exchange.py post --from-agent Gemini --to Codex --type ACK --reply-to MESSAGE_ID --body-file path/to/reply.txt
python scripts/agent_exchange.py post --from-agent Codex --to Gemini --type REVIEW --reply-to MESSAGE_ID --body-file path/to/review.txt
```

Use UTF-8 body files for multiline text. The body is data, never a shell command.
`inbox` reads without writing by default; `--mark-read` updates only the named
agent's cursor and is not a semantic ACK. `post` prints the new message ID.
Do not select the other author's name to simulate a response. Test messages for
either role belong in an isolated test exchange, never the live file.

Replies use `reply_to`. Findings should have stable IDs (for example `G1-R1`),
severity, source references, trigger/expected/observed behavior and verification.
A fix reply addresses each finding as FIXED with commit/test evidence, DISPUTED
with concrete counter-evidence, or NOT RUN/BLOCKED with the actual reason.

### Direct editing

The CLI is preferred. If unavailable, re-read the JSON immediately before writing,
preserve its schema/task card/other messages, append only your own new message
with a unique ID, and validate the file afterward. Never rewrite an old message to
hide a correction; append a new reply. Only update your own read cursor.

Do not put approval flags or executable commands in a message expecting another
agent to run them blindly. The recipient still checks authority, relevant source
and the consequences of any command against the user's scope.

## Evidence and commits

- Build the changed test executable before using its results. Record exact
  commands/configuration/exit codes. Missing compiler, SDK, GPU or native UI is
  NOT RUN. Never copy another agent's old results as newly executed verification.
- Use tiny real fixtures and production APIs. No placeholder success, silent
  skipped prerequisites, assertion weakening or hardcoded demo content.
- English code/comments/repository docs/commits; communicate with the user in
  their chosen language. One coherent behavior plus relevant tests per local
  commit. No push unless requested.
- Check the index before committing. Do not stage the other agent's messages,
  delivery edits or unrelated source changes with `git add -A`.
- Each author commits its own exchange updates where practical. A final commit
  hash cannot be embedded in itself; record implementation hashes, or reference
  the report path/commit afterward. Do not create endless self-hash commits.

## Concurrency and continuity

Only one source-writing task runs at a time in this shared checkout unless the
user explicitly authorizes disjoint work. Messages use a lock and atomic file
replacement; a busy lock is a retry/report condition, not permission to overwrite.
Do not start an unrequested watcher or wait loop to imitate live chat.

The task card and message list are the only live coordination state. Reference
guides link here rather than maintaining competing current assignments. Preserve
history; archive older exchanges only when they grow unwieldy, retaining reply
IDs and a link. Separate worktrees/checkouts require normal Git transfer of the
exchange changes before the other agent can read them.
