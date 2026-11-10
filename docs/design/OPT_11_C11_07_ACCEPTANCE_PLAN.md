# OPT-11 C11-07 Acceptance and Rollout Execution

User release: Codex implements C11-07 after C11-06. Baseline `ebc3a79`.
Execution/decision delivered through source `119123c`; see the
[final acceptance report](../reviews/OPT_11_C11_07_ACCEPTANCE.md) for measured
evidence and explicit deferred product integrations.
Decide readiness per feature/backend/product using actual evidence; do not infer
whole-product readiness from offscreen fixtures. Full hybrid/NRI fork, vendor
upgrades and OPT-12 remain outside this package.

## Audit and acceptance boundary

Current Editor/Runtime/preview/thumbnail callers use RenderService synchronously.
They own UI/native resources on that owner and use persistent/imported targets.
RenderWorker owns a separate process-level RHI device in the offscreen API;
it cannot attach to the product's already initialized UI-owned device. Transient
allocation/aliasing is an opt-in graph API, not a migrated product scene graph.
Therefore default product worker/parallel-native/scene-alias flags must remain
disabled unless their actual product integration, output, lifecycle and measured
benefit pass. A hardware/probe bit alone cannot enable them.

C11-07 produces an implemented selection/reporting policy and a concrete rollout
decision. An acceptance decision may retain synchronous production behavior and
defer unimplemented product integrations. Such a result must say partial OPT-11
acceptance, with explicit deferred rows, rather than claim full OPT-11 deployment.

## Sequential delivery slices

1. **C11-07.1: Shared rollout policy.** Parse product feature requests through one
   API used by Editor and Runtime. Defaults preserve synchronous rendering.
   Unsupported requests either produce an explicit fallback with reasons or fail
   closed in strict-feature mode. Surface actual product mode and distinct native
   API probe capabilities in diagnostics; malformed arguments reject startup.
2. **C11-07.2: Measured ownership/output/latency.** Timestamp worker admission,
   dequeue, rendering and result publication without per-frame GPU waits. Add a
   bounded explicit RGBA8 CPU export request for offscreen consumers/verification;
   only that opt-in path waits for readback. Return immutable CPU bytes, never
   worker-owned native pointers. Compare exact same-backend serial/worker pixels
   across independent device epochs and measure queue age/record/export latency.
3. **C11-07.3: Product and backend matrix.** Build Debug/Release products and
   affected tests. Verify real Editor startup/presentation and Runtime loose/cooked
   project paths, previews/thumbnails, D3D12/Vulkan and OpenGL compatibility.
   Rerun native lifetime/alias gates and architecture checks against final source.
   Fix concrete regressions. Missing prerequisites stay NOT RUN.
4. **C11-07.4: Consolidated review/decision.** One separate-context source review,
   necessary correction rechecks, committed evidence/artifacts, feature/backend
   matrix and clear default/deferred decisions. Notify Gemini at package end.

## Frozen measurement policy

- Physical RTX 3070 on the current recorded driver; Debug and Release separately.
- Same immutable builtin geometry/material scene, 64x64 RGBA8 output, fixed camera
  and quality toggles; no temporal effects or random animation.
- Exact same-backend pixel equality (zero differing bytes), plus a nonempty
  geometry/color assertion so matching empty images cannot pass.
- At least 8 warm-up frames then 64 measured frames per serial/worker path.
  Report nearest-rank p50/p95/p99 CPU render/queue/export/roundtrip times and
  accepted/rejected/cancelled counts. CPU task timing is not GPU timestamp timing.
- Readback runs separately from ordinary worker measurements and is synchronous
  only when explicitly requested. Do not report capture waits as recording speed.
- Queue-pressure measurements reconcile all accepted requests; capacity bounds,
  sequence/epoch validity and terminal results remain mandatory.
- Alias memory numbers remain native allocation-request totals, excluding device
  overhead and imported targets. Use existing real alias/readback fixtures.

Performance numbers describe this frozen workload/machine only. No universal FPS
target, automatic speedup claim, unsupported native worker recording, physical
GPU-reset evidence or product aliasing savings may be inferred from them.
