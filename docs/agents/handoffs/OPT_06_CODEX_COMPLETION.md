# OPT-06 Codex completion handoff

Date: **2026-11-08**. Status: **ACCEPTED / complete for PLG-01..13**.
Canonical findings, verification and limits:
[OPT-06 acceptance review](../../reviews/OPT_06_CODEX_REVIEW.md).

## Source and corrections

Candidate `20e0714` was rejected; Codex resumed completion from `c3a862d` under
the user's original review/repair assignment. The final corrected production
source is **`af3f7b2`**. Coherent correction commits:

- `25b4e7c`: durable Apply transaction and held-project identity.
- `8a46ac7`: discovery compatibility, scoped codecs, nested Assembly data,
  canonical receipts and architecture boundaries.
- `52a24ce`: frozen native source builder, matched SDK, shared host/loader ownership.
- `22cfca0`: canonical Engine service/scene backends and three real DLL lifecycle proof.
- `c263840`: reject compiler sources/includes outside frozen build inputs.
- `e2b9cc4`: authored/private-document preservation, stable Assembly identities,
  cooker readiness and executable leases retained by actual RHI submissions.
- `af3f7b2`: complete Editor session/build/selection/documents/restart/recovery,
  shipped external template/generator, native product/restart/submission probes.

The SDK uses shared C/POD gateway tables to canonical host services rather than
exporting Engine/vendor C++ implementations. The architecture decision and
[authoring guide](../../design/PLUGIN_SDK_AUTHORING.md) describe the supported surface.

## Verification and correction review

- Full final Debug and Release builds succeed; CTest **59/59 per configuration**.
- Original contract probes reproduce **0/7 defects per configuration**.
- Each configuration passes all 17 expected results in the external/native driver,
  including exact receipt-bound startup, rejected plan, partial module startup,
  fifth-Present failure, recovery/restored startup and exception cleanup.
- Physical RTX3070 D3D12/Vulkan submission tests report **38 green pixels each
  per configuration**, retaining DLL code after scene/session release until GPU
  completion and command retirement.
- Actual Editor/Runtime simulation verifies 30/60/144 Hz and pause/step/resume on
  both backends/configurations. Default Sandbox startup passes both backends.
- Native Debug computer use verifies normal/narrow Plugins layout, selection state,
  native Inspector edits, close Cancel, explicit Save and clean close.
- Separate-context reviewers checked the foundation/native/scene/product repairs.
  Product reviewer independently ran Debug 8/8 plus snapshot 2/2 and rebuilt Editor.
  Final source-only reviewer found/rechecked all-return cleanup and Run failure
  propagation; no remaining concrete P1/P2 in its focused correction scope.
  Independent Release/native UI/GPU execution is not claimed.

Logs and disposable fixture metadata: `build/opt06-completion/` (ignored).
Final report names exact commands, logs, expected-negative exits and review limits.
Native driver marker cleanup is fixture setup, not a claim of automated UI retry;
the real-DLL session test exercises production ClearAbandonedSessions separately.

## Boundary and preservation

All OPT06-R1..R10 are closed. G0/G1/G2 are accepted. No OPT-07+ release, fresh
Vehicle, static selected player, packaged plugin content, vendor upgrade or push.
PLG-22/23 own selected Runtime composition/export; current export refuses an
unsupported native player and cooker validates required plugin documents first.

Sandbox/default startup is retained. Authored scene SHA256:
`0FA1BA5710996941D82512A7703727C18C75C1363748D680243EF103E4CC5E93`.
Unrelated source/design commits remain preserved. Generated `crash.log` remains
untracked and is not staged. Work stops at the accepted OPT-06 boundary.
