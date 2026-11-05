# Gemini handoff: OPT-02 / EJ-01

Implement **EJ-01 only: thread identity and lifecycle admission** in this repository.

Before editing, read AGENTS.md / GEMINI.md, docs/agents/WORKING_PROTOCOL.md,
docs/agents/CODEX_GEMINI_EXCHANGE.json, and your inbox:

    python scripts/agent_exchange.py inbox --agent Gemini

Read docs/reviews/OPT_01_AF01_P41_PLG_REVIEW.md and the EJ-01 package plus thread/lifecycle contracts in docs/design/ENTT_ENKITS_REFACTOR_PLAN.md. Inspect HEAD and git status. Preserve all unrelated work and exchange messages. Post your own ACK/IN_PROGRESS linked to the Codex handoff before source edits; never speak as Codex. If the task card is stale, correct its scope from this user-authorized assignment rather than stopping for another approval.

The previous OPT-01 packages have been reviewed and corrected by Codex; the review report distinguishes original independent checks from Codex-authored correction verification. Preserve the current storage contract: raw PNG/external resources use root-relative paths; native Engine asset files embed UUIDs. No .meta/.uid/.import sidecars, authored central identity registry, old-format support or migration. Preserve pinned EnTT, enkiTS, Jolt and NRI revisions.

Implement these EJ-01 requirements:

1. Establish the actual OS owner thread during initialization. Preserve enki::NO_THREAD_NUM for unregistered callers. IsMainThread must check real ownership, including stopped and reinitialized states; it must never infer ownership by converting an invalid scheduler index to zero.
2. Define and enforce valid contexts for Dispatch, ParallelFor, Wait, WaitForAll, RunPinnedTasks and direct scheduler access. Owner and registered scheduler execution contexts may enter supported scheduler operations. Unsupported external calls must produce a documented, active result/diagnostic in both Debug and Release before any scheduler indexing. Ordinary external completion producers remain distinct from scheduler participants. Do not automatically register arbitrary threads. Add explicit RAII external registration only if a real existing caller needs it.
3. Define stopped/running/quiescing/draining behavior and initialization epochs. Init/Shutdown belong to the OS owner; invalid or reentrant lifecycle calls have active, defined results. Remove accidental stopped-state thread-zero fallbacks. Distinguish total scheduler threads from actual worker-thread counts in APIs/comments/logs.
4. Synchronize submission admission with shutdown so state cannot be destroyed between a successful check and enqueue. Reject new application work after quiescing. Preserve progress of already-admitted task graphs and their declared internal children using an appropriate retained permit. Never hold the admission lock while executing callbacks/tasks, waiting, or destroying callable captures. Audit the actual affected Engine callers rather than adding an unused guard.
5. Update JobSystem.hpp/.cpp, narrowly necessary Application setup/call sites, and permanent native fixtures. Keep ownership/lifetime of accepted versus rejected work explicit; do not expose an unsafe scheduler reference as a way around admission.

Do not implement EJ-02's always-deferred executor/owner tokens, compiler request handoff, DLL reload redesign, Jolt bridge/task redesign, ECS compaction, shared simulation, AF-02+, rendering/NRI migration, plugin framework or Vehicle. Preserve unrelated known defects and their truthful baseline results. An existing callback probe becoming green after correcting identity does not establish EJ-02's deferred timing contract.

Verification must use real production APIs and checks active in Debug and Release. Add bounded deterministic cases for:

- Owner, enki worker and ordinary external identities; invalid external indices stay invalid.
- Init(1) and Init(2), correct total/worker counts, stopped state and repeated initialization epochs.
- External/reentrant/wrong-owner scheduler and lifecycle calls fail in the documented way without crash, hang or inline ownership impersonation.
- Barrier-coordinated submission-versus-shutdown admission, rejected submission ownership, admitted child-task progress and safe reinitialization. Use existing watchdog infrastructure; avoid timing-only sleeps as the proof.

Rebuild the changed test executable before running it in each configuration. Run the complete default CTest suite, architecture checks and mutation/tool controls, the jobs/ECS collector, affected regression suites, and Debug/Release Editor/Runtime builds. Keep unchanged ECSJ-01/03/04 faults visible; update expected outcomes only with real evidence for code fixed in this package. Never weaken a probe or expand an architecture allowance to hide a new dependency. Report GPU/native UI/export runs separately from CPU tests and product builds.

Write an English delivery report with exact commands/configurations, exit codes, log/report paths, fixed finding IDs, remaining limitations and NOT RUN items. Update roadmap/task state as delivered and pending Codex review, not self-accepted. Make coherent local commits using explicit staging; do not push. Post DELIVERY as Gemini linked to the Codex handoff, then stop for review. Do not begin EJ-02.
