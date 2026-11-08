# OPT-10 architecture acceptance and enforcement

Date: 2026-11-08. Baseline: `6ee333d31afac8845055cd6f291db5597ad55ed9`.
Implementer/coordinator: Codex, explicitly assigned by the user after planning.
Scope: EJ-15, PLG-24, EJ-16, EJ-17, PLG-25 and AF-05.

Status: **implementation in progress; final architecture acceptance OPEN**.
The optional plugin foundation limits accepted in OPT-07/08 remain in force.
Full Vehicle/Spline/Terrain feature completion and OPT-11 are not released here.

## Delivered corrections

- Extracted the two graphics debug consumers into `Axle::PhysicsDebug`, above
  Physics/Renderer/RHI. Physics now links only Core/Jolt; Scene links the consumer.
  Removed Physics' concrete graphics overload; existing callers use the vendor
  debug primitive interface. Rendering behavior is preserved for native/GL gates.
- Removed the upward RHI-to-Renderer ECS forwarding header and migrated its test
  caller. ECS streaming belongs to Renderer.
- Retired all eight historical baseline allowances, and strengthened low-level,
  domain and vendor include rules, including resolved relative vendor headers.
  Deliberate source/link/property/dynamic/optional-domain/ECS violations are
  exercised by maintained regression fixtures.
- Added a graphics-independent stress target for scheduler sizes 1/2/hardware,
  stale owner generations, 2,048 constrained Jolt submissions, bounded record
  reuse, parallel ECS mutation batches and two independent Jolt worlds.
- Added a maintained Debug/Release acceptance runner with exact commands/logs,
  owned process watchdogs, source/binary identities and explicit skipped stages.
  Expected startup rejection requires the reached-fault marker, not exit 1 alone.
- Updated current build/plugin/job/architecture guides. Dated dependency surveys
  remain historical; retained GL/native-resource bridges serve current consumers
  and do not imply completion of the later renderer cutover.

## Separate-context correction review

The protocol-required source reviewer found four P2 issues in the new candidate:

| ID | Finding | Correction/recheck |
| --- | --- | --- |
| OPT10-R1 | Stress initialized the hardware-sized scheduler before the one-thread case. | Requested scheduler now initializes before conditional PhysicsEngine initialization. Source rechecked. |
| OPT10-R2 | Vehicle and direct/relative vendor ECS includes could evade low-level rules. | Lower-layer Vehicle exclusions, vendor roots/prefixes and mutation regressions. Source rechecked. |
| OPT10-R3 | An unrelated exit 1 could validate the intentional startup rejection. | Reached-fault marker plus exact-exit/proof requirement and regression. Source rechecked. |
| OPT10-R4 | Plugin-submission test could return success despite validation errors during teardown. | Enable available API validation; check the diagnostics count after Run and all its destructors. Owning diagnostics header included. Cleanup order source rechecked. |

No remaining concrete P1/P2 was reported in that reviewed structural/stress/tool
scope. The reviewer executed no builds/tests/native cases. Coordinator execution
evidence is recorded separately below.

## Acceptance-row coverage

These are exercised gates, not inferred feature completion. Final results below
determine PASS/FAIL/NOT RUN; existing package acceptance is not revoked by a new
test fixture requiring correction.

| Master rows | Current maintained evidence |
| --- | --- |
| FOUND-01 | AssetIdentityAndPersistenceTests: copied root, external native rename, raw relocate/relink, no sidecars |
| FOUND-02..04 | Foundation/SceneData/AssetFormat suites; separate Editor/Runtime-free CPU build and actual target references |
| FOUND-05..07 | Typed jobs harness, real script-DLL lifecycle suite, Scene mutation/transform suites and ArchitectureStressTests |
| FOUND-08 | Actual Editor/Runtime simulation probes at 30/60/144 Hz plus clock/input phase suites |
| FOUND-09..10 | Physical foundation/resource/identity/retirement/state/frame/error/presentation cases on D3D12 and Vulkan |
| FOUND-11..13 | Physical Canvas/consumer/private-preview/physics/particle/readback, GL/fallback and color comparisons; interactive UI remains a separate open gate |
| FOUND-14..15 | Real native DLL/service/session suites, generated external project plugin, submitted code leases and actual Editor workflow |
| FOUND-16 | OptionalPluginFoundationTests and selected Vehicle export prove the bounded foundation; advanced Jolt handling/audio/VFX shipping features remain deferred by user scope |
| FOUND-17..18 | Five detached static products per configuration; exact graph/link map/import exclusion, receipts, package preflight, real backend launch and missing-package rejection |
| FOUND-19 | Asset/Scene/Assembly passive sections, transactional document/save/build/restart/export failure suites |
| FOUND-20 | Zero baseline allowances; deliberate include/vendor/domain/link/property/dynamic-composition and evidence-integrity rejection fixtures |
| FOUND-21 | Full registered regression suites, default integration and explicit material/VFX/animation/native consumer gates |

EJ-15 stress/failure rows map to the jobs/DLL/ECS/simulation cases. EJ-16 maps the
implemented owner/task/mutation/simulation/render seams in the maintained guide.
PLG-24 maps the metadata/ABI/lifecycle/data/build/export tests and native products;
its interactive UI rows remain open. PLG VEH-01..03 detailed feature rows are
explicitly deferred under the preserved OPT-07/08 scope, not passed by prototypes.
AF-05 cannot close until all applicable mandatory rows, including UI, are verified.

## Verification and open gates

Fresh build tree: `build-opt10`. The user accidentally removed the previous build
directory during this session; historical logs are not represented as current
results. Native hardware: NVIDIA GeForce RTX 3070, driver 610.88, 8,192 MiB.
MSVC 19.44.35228.0, Windows SDK 10.0.22621.0; pinned NRI v190 and Jolt retained.

- Current architecture scan: PASS, 521 source files and 25 CMake files.
- Architecture/harness regression tools: PASS; final suite evidence pending.
- Fresh Debug/Release builds, full CTest, repeated stress/DLL, native render/product
  and detached export matrix: in progress; no PASS claimed yet.
- Native interactive UI observation/click/capture: **NOT RUN**. Computer Use
  `@oai/sky` initialization succeeded, but list_apps failed with native pipe
  unavailable / OS error 2 on the initial call, retry and fresh-kernel retry.
  No UI screenshot, click or independent manual observation is invented.

The native UI gate keeps PLG-24/AF-05 and final OPT-10 acceptance open. Automated
product UI probes are useful evidence but cannot substitute for that observation.
No push, vendor upgrade, backend-default change or performance improvement is claimed.
