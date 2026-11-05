# Gemini handoff: OPT-01 initial baseline (AF-00 + EJ-00)

Date: **2026-11-05**. Coordinator/reviewer: **Codex**. Implementer: **Gemini**.
Preparation baseline: `842cf7de6fac8368eb76fbf6a027b934681166e2`.
Record the actual starting HEAD when your session begins; the handoff commit will
follow that preparation baseline. This is a bounded implementation assignment.

## Authority and boundaries

The user asks to begin OPT-01 and receive a prompt for Gemini. Start with
**AF-00 + EJ-00 only**, in that order, and stop for Codex review. The remaining
OPT-01 packages AF-01, P41-00 and PLG-00 are not included in this handoff.

The user explicitly requires a clean Engine: **no old data format support,
legacy Vehicle payload holders, compatibility readers or migration**. Do not
restore the retired module or the superseded legacy review fixtures as acceptance
gates. Current-format data integrity and ordinary physics still matter.

The user-directed start advances baseline work without claiming OPT-00 accepted.
Carry its pending current-format verification into the baseline evidence and
report unresolved issues honestly. Reported build/CTest/integration results from
Gemini's `be7b071` delivery are not newly executed or independently reviewed results.

## Read first

- Root `GEMINI.md`, `AGENTS.md`, `docs/agents/WORKING_PROTOCOL.md`.
- `docs/agents/CODEX_GEMINI_EXCHANGE.json` and your actual inbox using
  `python scripts/agent_exchange.py inbox --agent Gemini`.
- `docs/ROADMAP.md`, OPT-01.
- `docs/design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md`, AF-00 and authority rules.
- `docs/design/ENTT_ENKITS_REFACTOR_PLAN.md`, EJ-00, regression matrix and build rules.
- `docs/architecture/ENTT_ENKITS_REVIEW.md`, especially ECSJ-01..05.

Inspect actual source, Git status and HEAD. Reply as Gemini to the Codex HANDOFF
message with ACK/IN_PROGRESS, base commit and expected files before writing code.
Historical statements such as "design only" describe the old planning task; this
bounded user-directed assignment releases AF-00 and EJ-00 implementation.

## AF-00: shared baseline and architecture checks

1. Inventory actual CMake target/link/include boundaries, source and pinned vendor
   revisions, asset identity/persistence policy, build workflows, current tests
   and acceptance gaps. Inspect production code rather than copying old reports.
2. Implement reproducible architecture checks. Existing violations may have narrow,
   explicit allowances recording their evidence, owning package and expiration
   condition. New forbidden edges fail; no permanent whole-directory exemptions.
3. Include an intentional forbidden-edge fixture proving the checker returns a
   failure. Reuse a shared process/watchdog/log infrastructure with EJ-00.
4. Capture current product evidence: rebuild Editor/Runtime and relevant tests,
   run CTest and the integration runner, and exercise small current-format
   scene/assembly round-trip/copy/duplicate and ordinary physics/Play-Stop cases.
   Record actual coverage, VFX Step 47 status and native/manual checks not run.
   This evidence does not independently accept OPT-00.

## EJ-00: permanent meaningful jobs/ECS harness

1. Promote/recreate the review's four production-API fault probes: compiler
   completion thread (ECSJ-01), external-thread identity/main delivery (ECSJ-02),
   single-thread Jolt/enki bridge destruction (ECSJ-03), and stable EnTT storage
   tombstone iteration (ECSJ-04). Keep the explicit bridge-drain passing control.
   If machine-local review probes are absent, use the documented triggers and
   actual APIs. Do not depend on ignored build files or copy historical outcomes.
2. Assert the correct behavior. Record observed baseline failures separately from
   normal passing CTest registration. A reproduced defect is a defect, not a PASS
   or a permanently expected-failure test. If it no longer reproduces, investigate
   and record the actual outcome rather than manufacturing a historical failure.
3. Keep checks active in Debug and Release. Isolate hang/crash cases in owned
   child processes with a bounded watchdog; terminate only owned fixture processes.
   Use latches/barriers for controlled interleavings rather than random sleeps.
4. Add an intentional failure probe proving nonzero failure propagation and verify
   watchdog behavior. Preserve ECSJ-05 as a source-traced scheduling discrepancy;
   do not claim a gameplay reproduction without executing one.
5. Add a focused test target (proposed `AxleJobsECSTests`; inspect current targets
   first), CTest label `ecs-jobs` and integration with `AxleAllTests`. Keep test
   targets EXCLUDE_FROM_ALL; CPU tests must not transitively build Editor/Runtime.

Expected implementation areas: architecture scripts/config, `Tests/CMakeLists.txt`,
focused tests under `Tests/`, minimal isolated CMake fixtures and baseline docs.
Do not fix production threading/compiler/reload/Jolt/ECS issues in this package.
Do not start AF-01, EJ-01+, rendering/NRI/plugin/Vehicle features, upgrade vendors,
move production modules or replace EnTT/enkiTS/Jolt. Record any necessary minimal
build/test integration changes; keep them within the released baseline scope.

## Verification and delivery

- Rebuild each changed executable before using its results, in Debug and Release.
  Execute the new suites, deliberate-failure/watchdog probes, architecture checks
  and relevant existing regressions. Keep commands, configuration, exit codes,
  reproducible fixture inputs, logs and machine/backend information.
- Separate normal PASS, reproduced known defect, unexpected regression and NOT RUN.
  Missing prerequisites must not become silent skips or invented success.
- Update roadmap progress as implemented/review-pending; no self-acceptance.
- Put the durable baseline and delivery report under `docs/reviews/` or
  `docs/architecture/`, not the active `docs/design` contract folder. Keep runnable
  fixtures/tooling tracked; generated logs/build products belong in ignored output.
- Preserve unrelated files/edits, stage only your own files and make coherent local
  commits. Do not push. Repository code/comments/docs/commits use English.
- Post DELIVERY as Gemini in the exchange, linked to the Codex HANDOFF message,
  with actual commit hashes, commands/results/log paths and limitations. Stop after
  AF-00 + EJ-00; await Codex review before beginning the next package.
