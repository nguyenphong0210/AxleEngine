Latest user steering (2026-11-07): implement all P41-01..11, then perform one consolidated final review. This supersedes intermediate review checkpoints; package verification and native acceptance gates remain required.

# Codex implementation and review handoff: OPT-04 / P41-01..P41-11

User scope, 2026-11-07: **Codex implements OPT-04**, with a separate-context
Codex review at P41-03, P41-06 and P41-11. Gemini provides supplementary review.
This supersedes the previous Gemini implementation assignment. Work in roadmap
order from **P41-01 through P41-11**, completing each package's implementation,
self-review, repairs, native/CPU verification and coherent local commit. Stop at
each review checkpoint before dependent production work, and stop after OPT-04.
OPT-05 is outside the assignment.

The user prioritizes output quality. GPT-6 Astra is the recommended model for the
implementation and separate-context reviews; the recommendation does not change
the current session's model or global settings. The user chooses the model in the
client. Use the selected model for the next implementation session.

## Starting point

The accepted implementation baseline is `3853203`; the preceding Gemini
coordination handoff is `cf168d2` and contains no OPT-04 implementation. OPT-02/03 were independently reviewed and
accepted after Codex-authored, Codex-verified corrections in `e351399` and
`3a67116`; see the [acceptance report](../../reviews/OPT_02_03_CODEX_REVIEW.md).
There was no separate independent review of Codex's own fixes. Baseline evidence
includes Debug/Release 42/42 CTest, full integration, jobs/ECS harness and real
Editor/Runtime simulation probes. These are predecessor results, not OPT-04 runs.

Inspect current HEAD/status before editing and preserve newer or unrelated work.
Read `AGENTS.md`, the [working protocol](../WORKING_PROTOCOL.md), the
[exchange](../CODEX_GEMINI_EXCHANGE.json) and the Codex inbox:

```powershell
python scripts/agent_exchange.py inbox --agent Codex
```

Post STATUS as **Codex** before source edits. Only update the Codex cursor and
messages. Do not invent Gemini replies or launch a source-writing task alongside
another source writer. Latest user instructions supersede older scope metadata.

## Review checkpoints

At **P41-03**, review backend readiness, fallback, transactional creation and
error/device-loss handling before starting P41-04. At **P41-06**, review resource
identity, epoch/generation protection and recording/submission retirement before
starting P41-07. At **P41-11**, review the full phase and dependent product callers.

The user authorizes a separate-context Codex reviewer at these checkpoints. Keep
that reviewer separate from the implementation conversation: provide the relevant
contracts, exact base/head diff, actual source, acceptance criteria and test/log
locations. It must inspect callers and failure/rollback/teardown paths itself.
Do not treat the implementer's PASS narrative as proof. Review starts read-only
for tracked files; native tests may use ignored build workspaces. Pause source
editing while review/tests run in this shared checkout.

The implementing Codex fixes findings with stable IDs, source references and
regressions. The separate reviewer rechecks significant corrections before the
checkpoint closes. Rebuild/retest affected gates after changes. Distinguish
self-review, context-separated Codex review, independent model perspective and
actual native verification. A fresh context using the same model can retain
shared blind spots; automated and native evidence remain required.

Gemini's review is supplementary. Request findings grounded in source/callers and
reproduction evidence; record an actual reply only when Gemini supplies it. It
must not resume implementation under the superseded handoff. Missing supplementary
feedback alone does not substitute for or invalidate the required Codex/native gates.

## Technical contracts

Use the [OPT-04 roadmap section](../../ROADMAP.md#opt-04---normalize-the-nri-device-and-gpu-lifetime-foundation)
as the package checklist. Read the P41-01..11 contracts and dependencies in the
[implementation plan](../../design/RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md),
the [NRI contract](../../design/NRI_INTEGRATION_CONTRACT.md), the
[render architecture](../../design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md) and the
[master prerequisite graph](../../design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md#6-cross-plan-prerequisite-edges).
Use the coding reference only after checking actual local declarations/callers.

The scope covers real backend readiness/fallback, transactional creation and
error/device-loss handling, primitive/resource identities, recording/submission
leases and retirement, state tracking, frame reuse, one presentation owner,
view/GUI leases and CPU decode/shared GPU uploads. Required caller adaptations
inside those contracts are included. Full RenderWorld/RenderService and consumer
migration packages P41-12+ belong to later phases.

Preserve these accepted constraints:

- Keep pinned EnTT, enkiTS, Jolt and NRI. Inspect the actual local NRI headers,
  revision and dirty state; do not fetch/upgrade a vendor or invent its API.
- Retain the real Core-only SceneData and CPU asset boundaries. Keep GPU/native
  types out of generic scene data and asset identity definitions.
- Preserve owner-thread completion and scene mutation rules, publication-safe
  job/physics lifetimes, separate Editor edit/Play worlds and shared simulation.
  `RunPinnedTasks` does not pump completions. Script ABI is **4**; rebuild affected
  modules rather than bypassing ABI/build checks. Keep the consistent exception ABI.
- Raw resources use canonical root-relative paths; native assets embed UUIDs.
  No identity sidecars, authored central registry, old-format compatibility,
  legacy migration or retired Vehicle implementation is requested.
- Explicit GL and automatic GL fallback must use the same supported resource
  interpretation. GL fallback is not evidence of successful D3D12/Vulkan work.
- Preserve supported material/VFX/animation/physics behavior. Do not implement
  later renderer/plugin features to avoid a bounded ownership correction.

## Verification and delivery

For every package, exercise the real production APIs and negative paths specified
by its contract, with active Debug/Release checks. In particular, cover partial
native construction rollback, abandoned recordings, failed submit/present,
generation/epoch reuse, resources retained by queued GUI images, busy frame slots,
resize/minimize/restore and teardown. Windowed presentation cannot be certified
by offscreen-only tests. Use failure injection at real operation boundaries;
do not exhaust GPU memory or induce a device reset.

At the phase checkpoint, rebuild Debug/Release `AxleAllTests`, `AxleEditor` and
`AxleRuntime`; run full CTest, full default integration, the typed jobs/ECS harness,
architecture checks and baseline tooling regressions. Retain the native simulation
product probe in `scripts/run_simulation_product_probe.py`. Run the applicable
physical D3D12/Vulkan readback, GL/fallback-equivalence and real windowed product
startup/present/teardown gates from the rendering contracts/current test wiring.
The baseline count is 42 CTest cases; do not freeze that count or drop new cases.

Missing required native checks are **NOT RUN**, with the actual reason; they keep
dependent gates open. Fix reproducible failures without weakening assertions,
swallowing unrelated errors or fabricating successful output. The separate OPT-00
manual/full-product acceptance gap remains recorded; it does not authorize legacy
support or invented independent acceptance.

Maintain one phase report at `docs/reviews/OPT_04_CODEX_EXECUTION.md`, with actual
commit hashes, rebuilt targets/configurations, commands, exit codes, log paths and
limits per package. Keep generated artifacts ignored under
`build/agent-runs/OPT-04/`. Update the roadmap and live task card with implementation
and self-verification separately from independent review. Commit only your own
files, preserve unrelated edits, and do not push.

If a session/usage limit approaches, save a resumable checkpoint in the report and
exchange: exact unfinished files, completed gates that still match current source,
failing checks, remaining work and the next concrete command. At P41-11 completion,
post DELIVERY as Codex and stop at **OPT-04**. Record the actual separate-context
review and correction evidence; do not mark acceptance before those gates finish.
