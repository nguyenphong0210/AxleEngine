# OPT-06 Codex completion work record

User direction: finish the previously authorized consolidated review, repairs and
verification. Codex's earlier stop at blocker reporting was premature. This is
the same PLG-01..13 boundary; no OPT-07 work, vendor upgrade or push is released.
Preserve Sandbox (`3e3a423`), the RHI correction (`0fd041f`) and authored assets.

## Current checkpoint (2026-11-08)

- Candidate reviewed: `20e0714`; resumed base: `c3a862d`.
- Acceptance remains **NOT ACCEPTED**, with completion work in progress.
- R7 codec ownership now uses one owned record, retains executable code through
  destruction, revokes owner registration, detaches before destructor callbacks,
  and checks revocation during snapshot invocation. Mutable storage API retired;
  runtime codec registration migrated. Rejection, reentry and revocation regressions
  added without dropping original persistence/copy checks.
- R6 nested Assembly parsing now scopes built-in markers by indentation and
  preserves opaque nested component names, entity keys, comments and blank lines.
  Private preview/required-data lifecycle is still outstanding.
- R8 discovery now models Engine/project origin, Engine API and target platform;
  selected duplicate IDs are rejected. Physical aliases and overlapping roots are
  deduplicated; Windows physical spelling is normalized before fingerprinting.
- R9 architecture policy includes metadata/native modules and protects lower layers.
- R4 artifact foundation uses a single strict receipt parser and actual binary
  hash, separate plugin/module identities, SDK/toolchain/configuration/plan checks.
  Native descriptor/host ABI is version 2; receiptless loading is rejected.
  Fixture now reports its actual build configuration (not hardcoded Debug).
- Debug rebuilt foundation/runtime/asset/probe executables; four CTests pass:
  PluginFoundationTests, PluginRuntimeTests, AssetIdentityAndPersistenceTests,
  ArchitectureBoundaryChecks. Logs: `build/opt06-completion/artifact-build-final.log`
  and `artifact-tests.log`.
- Explicit contract probes now pass 5/7. The two remaining failures are the old
  builder's incomplete prebuilt receipt validation and invalid-plan success.
  Probe exit remains 1; this is not phase acceptance.
- Separate-context reviewer `/root/opt06_foundation_recheck` rechecked R6/R7/R8/R9
  and found no remaining P1/P2 after follow-up fixes. It inspected logs, did not
  independently rerun tests, and excluded unfinished receipt/SDK/product work.

## Work still required before final acceptance

1. Replace builder's duplicate receipt logic with canonical identity validation.
   Generate a selected-only standalone CMake graph; configure and compile real
   DLL modules using a frozen SDK/toolchain plan. Use cross-process writer locking,
   immutable staging/generations, actual compiler/configure failure, Unicode/spaces,
   cancellation and unchanged artifact/receipt rollback tests. Replace the text-DLL
   process fixture with real external source modules.
2. Implement shared runtime/editor SDK host ownership and prove that two real DLLs
   observe canonical host project/jobs/physics/render/registry/UI services without
   linking private Engine/vendor state. Explicitly export the supported surface.
   Finish the descriptor/host service/lifetime ABI and retain host tables/module
   leases through callbacks, components, empty pools and teardown.
3. Implement real plugin per-scene component/system lifecycle, copy/remap and owned
   persistence/readiness across scenes, Assembly and private preview. Integrate
   shared simulation phases, owner jobs/main-thread callbacks and retained render
   contributions. Prove nontrivial component destruction before code unload.
4. Connect Editor discovery, plan/build/native session, Active/Desired/Draft states,
   dependency transactions and real Plugins UI. Implement dirty scene/private
   document Save/Discard/Cancel, save failure, frozen build/export edits, owned
   cancellable build UI, safe startup/recovery and one receipt-bound project/backend
   restart. Do not quit an active session on a failed build/launch.
5. Supply the project plugin SDK/template, external component/Inspector/update
   proof and complete discover/select/build/restart/save/reopen/Play workflow.
6. Rebuild/run final Debug and Release gates and native product/UI/DLL failure
   cases, obtain focused separate-context rechecks of significant later corrections,
   update all finding statuses and roadmap, then accept only verified final source.

`docs/reviews/OPT_06_CODEX_REVIEW.md` remains the acceptance ledger. No missing
implementation or unavailable required gate may be relabeled as a pass. Existing
`crash.log` is unrelated generated work and must not be staged or removed blindly.
