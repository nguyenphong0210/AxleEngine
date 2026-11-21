# M15 U10 consolidated baseline acceptance review

Updated 2026-11-18 following the user's request for a final architecture and
feature-by-feature review, and scoped 2026-11-21 to pair cleanly with the
post-U10 `M15-U11` architecture/pipelining refactor unit. This is a future
acceptance specification, not an execution report or a release of U10 or U11
implementation. It does not change another active package's assignment or claim
that any review/check has run.

[PLAN.md](PLAN.md) Section 4 remains the only atomic checkbox authority.
This guide defines the procedure and evidence for U10/S10.5 (pre-refactor
baseline acceptance, `M15-U10-17.1.1..17.1.8` and `M15-U10-17.2.1..17.2.4`).
After U10 freezes the feature, visual, and 1080p/1440p measurement baseline,
[PLAN.md](PLAN.md) `U11` closes the remaining structural refactor, pipelining,
and transient aliasing work plus post-refactor parity/soak verification prior
to full M15 milestone closure.
[ARCHITECTURE.md](ARCHITECTURE.md) supplies the technical contracts; the
[shared protocol](../../agents/WORKING_PROTOCOL.md) supplies roles, repair
authority, source ownership and independent correction rechecks.

## 1. Source boundary and execution sequence

1. Record the reviewed source commit, permitted dirty paths, configuration,
   feature/backend support, and all U0-U10 requirement IDs. Reconcile historical
   package aliases against those IDs, including the U5-G/H naming changes.
2. Freeze concurrent source edits within the assigned review boundary. Existing
   package reviews remain evidence inputs; perform the consolidated review
   against the final integrated source and its actual callers.
3. Audit architecture, each feature, integration, stability and measurement
   evidence. Record findings before making acceptance claims.
4. Repair findings within the released scope in buildable, verified checkpoints
   and coherent local commits. Preserve unrelated work and required behavior.
5. Obtain a separate-context source recheck of significant corrections and
   affected callers. Reuse the reviewer when additional repairs need rechecking.
6. Rebuild changed executables; rerun affected checks and the required final
   consolidated gates against the corrected source. Update evidence provenance
   after code changes; a documentation-only commit need not repeat rendering.
7. Reconcile every leaf and finding, then publish the final acceptance decision,
   supported defaults, limits and remaining optional/deferred work.

This sequence is performed when U10 is explicitly assigned. It does not start
another session, authorize a push, or move M16/M17 work into M15.

## 2. Required review artifacts

| Artifact | Required contents |
| --- | --- |
| Source and support manifest | Reviewed/final commits, dirty-path policy, exact builds/binaries, fixtures and camera/settings, physical adapter/backend, supported combinations and declared limits. |
| Architecture audit | Actual module/pass/dataflow map; contract-to-source/caller references; ownership/lifetime/failure analysis; findings and evidence. Architecture scripts supplement this source audit. |
| Complete requirement matrix | One row per U0-U10 atomic leaf, including the new U10 review leaves; requirement, required/optional classification, implementation and caller paths, independent evidence, status and finding IDs. Derived parent summaries reference their children. |
| Integration and lifecycle matrix | Every minimum scenario in Sections 5 and 6, its supported/fallback policy, fixture, configuration, assertions, images/readbacks/logs and result. |
| Measurement scorecard | Per-pass and whole-view/frame CPU/GPU timing, memory, cold-start/compile cost, workload/preset, frozen budgets and actual comparisons. |
| Findings and correction register | Stable ID, severity, affected requirements, source/callers, trigger, expected/observed behavior, reproduction, owner, correction commit, recheck and final verification. |
| Final acceptance report | All mandatory gates reconciled, optional deferrals and limits listed, supported defaults/presets, evidence links, remaining findings and explicit decision. |

Use `PASS`, `MISSING`, `FAIL`, `NOT RUN` or `BLOCKED` for mandatory dispositions.
`DEFERRED` is permitted only where the plan and user scope allow deferral; it is
not PASS. An opt-in default policy does not by itself permit omitting a promised
feature. Preserve the existing separately deferrable P6B/DDGI boundaries and
record the exact applicable scope decision.

Each evidence record identifies source commit, executable/build configuration,
backend/adapter, exact command or interactive steps, exit/result, fixture/settings,
assertion/reference/tolerance, and artifact path. Clearly label implementer claims
versus reviewer-executed checks. An old PASS needs current-source applicability;
final required runtime gates still execute on the integrated corrected baseline.

## 3. Architecture audit

| Concern | PASS condition and evidence |
| --- | --- |
| Layer boundaries | ECS/authoring is extracted into immutable `RenderWorld` data; command recording does not read live ECS/UI state. RHI remains independent of Scene/Editor ownership. Trace actual production callers and the build/dependency graph. |
| Pass and service responsibility | Audit M15's progressive `IRenderPass` extraction against the architecture contract. Feature algorithms have dedicated owners; renderer orchestration, services and product UI do not contain competing implementations. Record remaining monolithic adapters or duplicated policy as findings. *(Scoped zero-debt refactor work explicitly assigned to `U11` — full core + secondary `IRenderPass` extraction and lambda/`friend class` removal in `RHISceneRenderer`, unified built-in/graph HLSL lighting kernel, revision-cached material extraction, and `RenderContracts` test-oracle separation — is logged and tracked for `U11` rather than blocking the `U10` pre-refactor baseline freeze, provided behavior and validation pass.)* |
| Resource generations and submission | Allocation/reconfiguration/publication is transactional; descriptors bound to a submission remain immutable; leases/completion retain all referenced resources; failure selects valid last-good data or an explicit fallback. Inspect creation, recording, completion and destruction callers. *(Migration from queue-drain/in-place descriptor updates and committed `RHIViewTargets` slots to per-frame descriptor rings, multi-frame-in-flight sync, and cached `RenderTransientGraph` aliasing is tracked in `U11`; U10 verifies that current conservative lifetimes pass clean validation without leaks or races.)* |
| RenderGraph declarations | Every actual texture/buffer read/write, subresource and copy/resolve dependency is declared, including companion material outputs, Hi-Z/color mips, histories and captures. Verify state/barrier behavior through native execution, not source tokens alone. |
| View isolation and invalidation | Main view, Editor/Preview, reflection captures and planar captures own the appropriate targets, exposure, jitter/history and reset state. No recursive capture, cross-view history contamination, stale device epoch or cache generation reuse. |
| Shared feature policy | Allocation, pass configuration/declaration/recording, capability checks, MSAA/resolves and quality settings use a consistent resolved policy. Supported combinations execute; unsupported ones produce the documented diagnostic/fallback. |
| Material and radiometric contracts | Builtin and graph paths interpret supported fields consistently. Trace color spaces, world units, roughness/F0, sky rotation/intensity, AO composition, reflection weights, pre-exposure and exactly-once tonemapping. Unsupported authored controls are diagnosed rather than silently ignored. |
| Threading, cache and error ownership | Review owner-thread boundaries, immutable snapshots, cache keys/invalidation, bounded updates/eviction, partial initialization and retry. No lifetime depends on an undocumented wait or an unrelated view's completion. |

Every identified violation is mapped to requirement IDs and actual source paths.
A passing architecture script or an updated diagram cannot close a source finding.

## 4. Feature completeness audit

The requirement matrix includes every atomic leaf, not only recently changed files
or tests. The following families set the minimum review coverage; they are not a
second checkbox list.

| Family | Minimum behavior to trace and verify |
| --- | --- |
| U0 baseline | Reproducible fixtures, physical/display comparison space, diagnostics, protected user scenes, and known historical findings. |
| U1 direct lighting | GGX/F0/energy compensation, light units/radius/orientation, direct-light and existing shadow contracts, independent numeric references and material flags. |
| U2 environment | Procedural/authored environment, rotation/intensity, diffuse/specular bake, cache/publication, reload and failure retention. |
| U3 infrastructure | Surface/depth/normal/motion data, target formats/dimensions, resolves, descriptor/resource generations, timing, view/history ownership and graph integration. |
| U4 occlusion | GTAO, denoising/reconstruction and contact shadows; intended lighting terms, preservation of direct/emissive terms, edges and unoccluded smooth/mirror sphere fixtures. |
| U5 reflections/lights | Hi-Z, six-face capture, prefilter, scheduling/invalidation, multi-probe influence/parallax, material-aware SSR, planar capture/resolve and LTC rectangle/disk lights. Check hit/miss, material variants, energy/fallback, source visibility and declared shadow/shape/capacity limits. |
| U6 atmosphere | Froxel/media injection, light/blocker response, temporal behavior, depth/transparency composition, zero-density identity, resolution and cost. |
| U7 DDGI | Real declared radiance acquisition, world-space probes/atlases/visibility, bounded updates, wall leakage, temporal/invalidation and no-valid-probe fallback. Preserve the explicit experimental/optional disposition. |
| U8 post/temporal | Histogram/exposure using real delta time, HDR bloom reconstruction/gain, motion vectors/jitter, TAA rejection/reset, transparency/reactive policy and unjittered UI. |
| U9 advanced materials | Sheen, absorption, dispersion and separable SSS; correct diffuse/specular partition; current-format assets/components/codecs, builtin/graph lowering, Inspector/Preview/Runtime and unsupported-field diagnostics. |
| U10 products/rollout | Creation/editing, current-format scene/assembly/runtime round-trips, scene-matched previews, all quality tiers/budgets, capability diagnostics, final visual/performance evidence and promotion policy. |

For each feature follow authoring -> extraction/resolution -> GPU input -> actual
pass/shader output -> composition/presentation, including save/reopen and Runtime
where applicable. Verify enabled, disabled, boundary and supported fallback paths.
Use independent analytic/numerical references or controlled scene comparisons;
CPU/GPU copies of the same algorithm alone cannot establish physical correctness.
Existing unsupported limits must remain explicit and do not satisfy missing
mandatory behavior.

## 5. Minimum integration scenarios

Freeze the expected behavior and numeric/image tolerances before evaluating each
scenario. Use the same authored scene, camera, settings and declared comparison
space for on/off and parity captures. Run supported cases on both native backends.

| Scenario | Required check |
| --- | --- |
| GTAO/contact + reflections + material flags | Occlusion affects the declared terms once; direct/emission and disabled reflection behavior remain correct; no new edge halo or double attenuation. |
| Planar + SSR + local probes + sky | Confidence/coverage follows the shared energy policy with no duplicate reflected energy, black miss, wrong parallax or discontinuous fallback. Include overlapping sources and failed/unavailable updates. |
| Captures + post/temporal + multiple views | Non-recursive captures preserve main-view jitter/exposure/history and exclude the declared UI/receiver/feedback inputs. Preview/Runtime rendering cannot corrupt another view. |
| Fog + transparency + reflection composition | Pass order and depth policy prevent unintended double fogging or invalid transparent/reflected sources; zero fog preserves the defined image. |
| Exposure + bloom + tonemapping + UI | Meter the declared HDR source without bloom/UI feedback; apply exposure, bloom gain and output mapping once in the correct spaces; UI remains stable. |
| TAA + moving geometry/camera/reflections/fog | Motion/jitter conventions agree; disocclusion and reset events reject stale history; ghosting/shimmer remain within frozen criteria. |
| Area lights + advanced materials + graph parity | Supported dielectric/metal/roughness/coat/aniso and other declared material fields retain their behavior. Builtin/canonical comparisons cannot silently use fallback shaders or ignore authored fields. Record explicit approximation and shadow limits. |
| Feature switches + quality/MSAA + capabilities | Allocation and recording policies agree at small/odd sizes and supported sample counts. Disabled/unsupported features keep a valid declared fallback and authored settings; diagnostics identify unsupported combinations. |

A combination declared unsupported receives a real rejection/fallback test and
policy evidence. Record `N/A` only for genuinely inapplicable combinations with
a source-backed reason. A required supported combination that does not work is
MISSING/FAIL, not an unsupported-policy exemption.

## 6. Stability and failure scenarios

The lifecycle matrix covers repeated resize/minimize/restore; view switches and
camera cuts; scene replacement and settings/format changes; device reinit;
asset/material/light/probe changes; missing/corrupt resources; failed capture,
upload or target reconfiguration; in-flight generation replacement; and bounded
cache/update behavior. Use focused fault injection or real failure fixtures as
appropriate, with explicit expected last-good/retry/fallback and cleanup behavior.

Required checks establish that successful frames remain finite and valid, rejected
frames are diagnosed, resources remain live through completion, resets clear the
appropriate history, and retries recover. Measure memory/resource high-water and
steady-state behavior during the soak. A run that merely avoids crashing does not
prove these outcomes.

## 7. Visual and measurement gates

- Execute every required Editor visual gate still open from earlier Units,
  including the user-deferred U4/U5 checks. Record actual project/fixture, build,
  backend, controls and camera, on/off/diagnostic images and reviewer observations.
  Empty/project startup is integration evidence, not a visual inspection.
- Freeze references, comparison space, thresholds and workload/preset budgets.
  Explain any legitimate reference change with an independent expected result;
  do not regenerate goldens or relax assertions solely to obtain PASS.
- Use Release 1080p and 1440p with fixed named workloads. Record at least 120
  warm-up and 1,000 measured frames per declared workload. Record cold-start,
  first-use shader compilation/pipeline creation and cache-warm behavior separately.
- Report CPU/GPU p50/p95/p99/max with valid timing/completion provenance, per named
  lighting pass and whole view/frame. Separate CPU waits/readbacks and presentation
  from GPU execution. Invalid/missing timestamps are NOT RUN, not zero cost.
- Record peak/steady memory and resource/cache counts, view/probe update workload,
  feature settings, hardware/driver/OS/backend and final source/binary identity.
  Compare measurements against each frozen preset budget and report violations.
- A required preset/workload that exceeds its agreed timing or memory budget
  remains FAIL and cannot be promoted through a measurement-only PASS. Any
  authorized scope/budget revision is explicit and does not retroactively satisfy
  the original target.
- The existing 3.5-5.0 ms target applies to the explicitly named lighting-pass
  workload on its named GPU/preset. Report total frame time separately; a lighting
  subset measurement alone cannot establish a frame-rate claim.
- Test current-format save/reopen and builtin/graph, Editor/Preview/Runtime parity
  where supported. OpenGL/unsupported capability checks validate their declared
  fallback rather than implying native feature parity.

## 8. Findings, corrections and closure

All findings have a disposition linked to evidence: FIXED with commit/reproduction
and verification; DISPUTED with independently checked counter-evidence; or OPEN
with affected gates and owner. Separate-context rechecks identify what was inspected;
they do not inherit another agent's execution claims.

U10 acceptance closes the **pre-refactor fidelity, authoring, and baseline
performance gate** when every mandatory U0-U10 atomic leaf and required visual,
integration, stability and measurement gate is PASS on the integrated U0-U9
baseline source (with `M15-U10-03.5` unifying `RHIViewTargetDesc` across
`RenderService` and `RHISceneRenderer`). Remaining modularization, pipelining,
and transient-aliasing refactors explicitly scoped to `U11` are tracked as open
U11 handoff items rather than blocking the U10 baseline freeze, provided all
U10 runtime validation, lifetime, and parity checks pass.
Full M15 milestone closure additionally requires `M15-U11` (`S11.1–S11.5`) to
complete and pass its post-refactor A/B scorecard, visual parity, 1,000-frame
soak, and independent review/recheck gates in [PLAN.md](PLAN.md).

Promote only verified supported features/presets. Keep failed, incomplete or
deferred features out of defaults as required by `M15-U10-19.2`. Attach final source
and evidence to every completed leaf; update derived summaries only from children.
Earlier automated-only acceptance remains scoped until its required visual gates
are closed. The final report explicitly distinguishes U10 pre-refactor baseline
acceptance, full M15 post-U11 acceptance, and an unaccepted milestone.
