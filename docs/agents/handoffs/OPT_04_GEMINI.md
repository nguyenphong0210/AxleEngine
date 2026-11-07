# Gemini handoff: OPT-04 / P41-01..P41-11

User scope, 2026-11-07: implement **OPT-04 only**, in roadmap order from
**P41-01 through P41-11**. Complete each package's implementation, self-review,
repairs, verification and coherent local commit before continuing to its dependent
package. Stop after the OPT-04 delivery checkpoint for review. Do not start OPT-05.
This assignment becomes active when Gemini's session reads it; no agent or
background session has been launched by this file.

## Starting point

The handoff baseline is `3853203`. OPT-02/03 were independently reviewed and
accepted after Codex-authored, Codex-verified corrections in `e351399` and
`3a67116`; see the [acceptance report](../../reviews/OPT_02_03_CODEX_REVIEW.md).
There was no separate independent review of Codex's own fixes. Baseline evidence
includes Debug/Release 42/42 CTest, full integration, jobs/ECS harness and real
Editor/Runtime simulation probes. These are predecessor results, not OPT-04 runs.

Inspect current HEAD/status before editing and preserve any newer or unrelated
work. Read `GEMINI.md`, the [working protocol](../WORKING_PROTOCOL.md), the
[exchange](../CODEX_GEMINI_EXCHANGE.json) and your inbox:

```powershell
python scripts/agent_exchange.py inbox --agent Gemini
```

Post your own ACK/STATUS as **Gemini**, linked to the Codex HANDOFF message, before
source edits. Update only your own messages/read cursor. Keep the activated phase
range distinct from the current package. Latest user instructions take precedence
over stale scope in an older prompt or plan.

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

Maintain one phase report at `docs/reviews/OPT_04_GEMINI_EXECUTION.md`, with actual
commit hashes, rebuilt targets/configurations, commands, exit codes, log paths and
limits per package. Keep generated artifacts ignored under
`build/agent-runs/OPT-04/`. Update the roadmap and live task card with implementation
and self-verification separately from independent review. Commit only your own
files, preserve unrelated edits, and do not push.

If a session/usage limit approaches, save a resumable checkpoint in the report and
exchange: exact unfinished files, completed gates that still match current source,
failing checks, remaining work and the next concrete command. At P41-11 completion,
post DELIVERY as Gemini and stop at **OPT-04**, leaving Codex acceptance pending.
