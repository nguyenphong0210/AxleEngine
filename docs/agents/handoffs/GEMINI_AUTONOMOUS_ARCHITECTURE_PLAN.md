# Gemini autonomous architecture execution plan

Prepared by Codex on 2026-11-05, from `5a44a60`. This is an operating plan for the
existing [sequential roadmap](../../ROADMAP.md), not a replacement technical design.
**Prepared, not activated:** creating this file does not expand the currently released
EJ-01 assignment. The user activates the broader range by sending the prompt below
(or an equivalent explicit instruction) to Gemini. No production code changes are
part of preparing this plan.

## Outcome and authority

After activation, Gemini implements **OPT-02 through OPT-10**, one package at a time
in roadmap order. It owns implementation, first-pass review, corrections, verification,
local commits and progress records. It proceeds after each verified delivery gate
without waiting for Codex to return. Codex/user review may happen at checkpoints;
its absence is not a per-package stop condition in this explicitly delegated range.

This changes the delivery cadence, not the technical exit criteria. Gemini's own
checks are **self-review/self-verification**, never an independent Codex review.
Final architecture acceptance remains a review-ready candidate until an actual
reviewer accepts it. OPT-11 and later feature work are outside this range.

The roadmap remains the only package checklist. Do not create a second progress
checklist here. Read only the relevant domain contracts for the current package.
The prior single-package EJ-01 handoff remains the detailed EJ-01 reference; its
instruction to stop before EJ-02 is superseded only after this broader activation.

## Fixed project decisions

- Keep the Engine clean. No retired Vehicle module/holders, old-data preservation,
  compatibility readers, legacy conversion or migration implementation is required.
  Unsupported old schemas may be rejected with a useful diagnostic.
- Raw PNG/external resources use canonical root-relative paths. Native Engine assets
  embed their UUID. No .meta/.uid/.import identity sidecars or authored central registry.
- Preserve the pinned EnTT, enkiTS, Jolt and NRI versions. Apply the pinned native
  integration contract; do not invent vendor APIs or upgrade dependencies to bypass a bug.
- Preserve supported material/VFX/animation/physics behavior while separating ownership.
  Existing supported OpenGL behavior is distinct from retired data-format support.
- Use the planned generic plugin seams before implementing the fresh Vehicle plugin.
  Editor shared-host DLL modules and selected static Runtime composition remain the design.
- Preserve unrelated edits. Use explicit staging and coherent local commits. Do not push,
  publish, deploy, delete user projects or reset someone else's work.
- One source-writing task at a time in this checkout. This plan does not require
  additional agents, background sessions or any automatic chat-to-chat relay.

## Execution range and phase gates

There are 68 remaining architecture packages. A long run may span multiple sessions.
The ranges below describe order; package definitions and individual gates stay in
the [roadmap](../../ROADMAP.md) and [active design index](../../design/README.md).

| Order | Work | Evidence before proceeding to dependent work |
| --- | --- | --- |
| OPT-02 | EJ-01..07: identity, owner executor, compiler/DLL handoff, physics job lifetime/waits and shutdown | Real owner-thread completion, native DLL reload, one-thread physics progress and safe draining; ECSJ-01/02/03 corrected by their owning packages |
| OPT-03 | AF-02; EJ-08..10; AF-03/04; EJ-11..14, in exact roadmap order | Core/SceneData/CPU asset boundaries, correct EnTT iteration and one simulation driver; Editor/Runtime fixed-step, pause/step and lifecycle parity |
| OPT-04 | P41-01..11 | Real device readiness, failed-construction rollback, GPU identity/leases/retirement/state/frame/present ownership and shared uploads |
| OPT-05 | P41-12..20, then P41-25 | Shared RenderWorld/service used by real previews and Runtime; real Canvas submission and physics debug, with supported backend behavior checked |
| OPT-06 | PLG-01..13 | Generic project plugin discovery/resolution/selection, shared-host ABI, owned registrations and a real external project DLL; unselected modules are not configured/built/loaded |
| OPT-07 | PLG-14..18 | Fresh Vehicle plugin through generic seams; real physics and Editor lifecycle, codecs and scoped audio/VFX; no restoration of Engine Vehicle |
| OPT-08 | PLG-19..21 | Spline/Terrain and project scripts use plugin/runtime boundaries; no accidental Editor dependencies in Runtime |
| OPT-09 | PLG-22/23, then P41-26 | Editor-disabled selected Runtime build and launched packaged player from the same validated selection/build/cook snapshot |
| OPT-10 | EJ-15; PLG-24; EJ-16/17; PLG-25; AF-05 | Current stress/failure/native DLL/GPU/UI/player evidence, enforced boundaries and a review-ready final candidate; no implied OPT-11 rollout |

The existing OPT-00 independent product-review gap stays recorded. Current-format
product tests may supply additional evidence, but Gemini must not invent independent
acceptance or rerun obsolete Vehicle-preservation requirements. Resolve actual
current-format regressions that affect an active gate.

## The loop for every package

1. **Inspect:** read the active task, relevant package/dependency contracts, inbox,
   current HEAD/status and actual source. Confirm predecessor delivery gates. Do not
   reimplement an already delivered package without an unresolved finding.
2. **Declare:** post STATUS/IN_PROGRESS as Gemini, linked to the activation/status
   thread. Record the package, base commit, intended files and concrete pass criteria
   in the live task card. Keep `execution_range` and the current package distinct.
3. **Implement:** finish the current package and required caller migrations. Do not
   combine future features with a correctness fix. Add small real fixtures for new
   behavior, relevant negative paths, ownership failures and known reproductions.
4. **Verify:** rebuild the affected binaries before execution. Run the applicable
   checks below and inspect the actual results. Fix failures and add a regression
   that exercises the failure mechanism. Rerun failed/affected checks after changes.
5. **Self-review:** inspect the complete diff as a reviewer. Trace real callers,
   error/rollback paths, object/module/task/GPU lifetimes and teardown. Try invalid
   inputs and adversarial ordering; a happy-path PASS alone is insufficient. Read
   relevant local vendor declarations when a native API or lifetime rule is uncertain.
6. **Deliver:** commit the coherent change with explicit staging. Update the roadmap
   entry as implemented/self-verified with report links and actual evidence; preserve
   any separate independent-acceptance requirement. Post DELIVERY as Gemini with
   commit hashes, commands, exit codes, limits and next package.
7. **Continue:** when the package delivery gate passes, move to the next package in
   the activated range. Do not stop merely because the historical prompt says
   "wait for Codex" or because Codex is unavailable. This continuation authority
   comes from the user's activation, not an old domain guide.

If self-review finds a defect in an earlier delivered prerequisite, reopen its
status, fix it in a focused commit and rerun dependent checks before proceeding.
Do not label the earlier evidence current after a relevant source change.

## Verification requirements

At every package, run focused production-API regression tests plus architecture
checks when dependencies/ownership change. Keep checks active in Debug and Release,
use owned fixture roots and bounded process watchdogs, and validate the exact cause
of intentional failures. Never weaken assertions, swallow unrelated errors, expand
allowances merely to pass, or substitute a fake implementation for a native gate.

At each OPT phase checkpoint, rebuild Debug and Release AxleAllTests, AxleEditor
and AxleRuntime (or their documented replacement targets), then run:

- Complete default CTest and affected/full integration tests, including current
  scene/assembly/project/asset/material/VFX/animation behavior and VFX Step 47.
- Architecture checks, their deliberate forbidden-edge controls, and baseline tool
  regressions; record allowances retired by their owning package.
- The jobs/ECS typed collector. Keep faults not yet owned by a completed package
  visible. Once fixed, update/add positive regression expectations with actual
  assertions, rather than continuing to accept a known defect as the desired state.
- Available physical D3D12/Vulkan readback and real Editor startup/present/teardown
  checks for changes affecting rendering, composition or global lifecycle.
- The current phase's specific real DLL, plugin build/selection, Editor Play/Stop,
  disabled-module, Editor-disabled Runtime and packaged-player gates when applicable.

Reuse the current command wiring in Tests/CMakeLists.txt and the scripts rather than
assuming today's test count is permanent. The reviewed starting evidence is 21/21
CTest, 14 asset suites, full integration and five hardware/window cases per native
configuration; it is historical evidence, not a substitute for new runs.

If GPU/SDK/UI/process access is unavailable, record NOT RUN and the concrete reason.
Continue independent authorized work only when its dependencies and gates permit it.
A missing required phase gate keeps that gate open and blocks dependent work;
self-review cannot waive it. Do not start an endless retry/wait loop. Report a genuine
external blocker or an unresolved product decision clearly; ordinary technical
implementation choices and repairable failures do not require another user approval.

## Reports and resumable state

Use one evolving phase report, for example docs/reviews/OPT_02_GEMINI_EXECUTION.md,
with sections per package. Store generated logs under build/agent-runs/<phase>/ and
keep a concise tracked results summary with command/configuration/exit/log references.
Do not fill the repo with duplicate delivery documents or generated build assets.

The live exchange task card records: activated range, current OPT/package, base
commit, state, completed package evidence, failing/open gates, next action and the
phase report. Maintain the canonical roadmap rather than another copied checklist.
Only author Gemini messages/cursor updates; never invent Codex review or acceptance.

Before context/session exhaustion, save a recoverable checkpoint with:

- Current branch/HEAD and exact uncommitted files, including which edits are Gemini's.
- Package and completed behavior; remaining work and the next concrete command.
- Last rebuilt targets/configurations and which results still match current sources.
- Failing checks with exact log paths, unresolved findings and required external inputs.
- Commits/report links, temporary test options to restore, and work still pending review.

Commit completed coherent work. Preserve unfinished edits and describe them; do not
commit broken work just to hide an incomplete step. Post the checkpoint as Gemini.
On resume, read it and inspect HEAD/status before continuing. No background worker
or automatic model restart is implied by these files.

## Review checkpoints and finish

Prepare a short review summary after every OPT phase and continue when its delivery
gates pass. Suggested consolidated external reviews are after OPT-02 (thread/DLL
lifetimes), OPT-03 (data/simulation), OPT-06 (plugin ABI), OPT-09 (export/player) and
OPT-10 (final candidate). These reduce review frequency; do not pretend they occurred.

At OPT-10, distinguish implemented, self-verified, independently reviewed and not-run
rows. Deliver the final review-ready architecture candidate, commit/report index,
remaining limitations and reproducible verification instructions. Stop at this range
boundary. Do not begin OPT-11, change pinned vendors, or silently drop outstanding
material/VFX/animation acceptance.

## Activation prompt

Send this to Gemini when ready to activate the larger range:

```text
Activate docs/agents/handoffs/GEMINI_AUTONOMOUS_ARCHITECTURE_PLAN.md.
I authorize sequential implementation of OPT-02 through OPT-10, starting with
EJ-01 and completing EJ-01..EJ-07 first. Follow docs/ROADMAP.md package order
and the technical contracts. Do not implement OPT-11 or later feature work.

Own implementation, self-review, fixes, tests, local commits and reporting.
Continue after each verified package/phase delivery gate without waiting for
Codex approval. Historical single-package stop-for-Codex instructions are
superseded within this activated range; technical gates remain required.
Do not claim independent review or final acceptance from your own checks.

Preserve the clean Engine, raw resource paths/native embedded UUID policy,
pinned vendors, unrelated edits and current supported behavior. Do not add
legacy readers, identity sidecars or an authored central registry. No push.

Read GEMINI.md, the shared protocol, exchange and your inbox first. Record
this activation in the task card and post your own ACK/IN_PROGRESS before
source edits. Save a precise checkpoint before session/context exhaustion.
```

## Resume prompt

```text
Continue the activated Gemini autonomous architecture plan. Read GEMINI.md,
docs/agents/WORKING_PROTOCOL.md, the live exchange and your last checkpoint.
Inspect HEAD/status and resume the current unfinished package. Preserve
valid completed work and its evidence. Continue within the recorded range;
do not restart from EJ-01 or ask for routine per-package Codex approval.
```
