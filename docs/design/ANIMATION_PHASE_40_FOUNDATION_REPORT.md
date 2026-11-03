# Phase 40 foundation implementation

Updated: 2026-11-03. Status: **in progress; full Phase 40 is not complete**.

This report tracks the foundation work requested after the planning handoff.
The [completion plan](ANIMATION_PHASE_40_COMPLETION_PLAN.md) remains the scope
reference; [ROADMAP.md](../../ROADMAP.md) records package status. No new animation
panel, RHI skinning, importer parity or packaged-animation acceptance is claimed
by a passing CPU foundation test.

| Package | Status and evidence |
| --- | --- |
| A0 | PASS: `AxleAnimationUnitTests` links the existing Animation library and evaluates a tiny rig/clip without starting the application or creating a window/device. The Release harness uses always-active checks. CTest's expected-failure case verifies a failing expectation returns a nonzero status. |
| A1 | CPU PASS: topology, signed/partial bind fallback, inverse binds, pose/rig compatibility, finite validation and derived data. |
| A2 | PARTIAL: glTF STEP/cubic/helper/bind tests and FBX normals/UV seams/helper/axis/keyed-pose tests pass. FBX currently uses bounded 30 Hz resampling; error-bounded curve baking and broader exporter fixtures remain open. |
| A3 | PARTIAL: typed runtime copies, owned rig/legacy motion snapshots, source-destruction lifetime and independent scene/assembly instances pass. Opaque predicate clones fail explicitly. Enforced immutable mesh-resource publication remains open. |
| B1 | CPU PASS: complete bind signatures, stable controller/state/transition identity, complete keyed motion extraction and appended asset classifications. |
| B2 | CPU PASS: validated complete text/binary codecs, atomic publication, last-good decode outputs, cooked geometry and a real VFS package dependency graph. Application hot-reload workflow and automated pre-cook remain J1/J2 integration work. |
| C1 | CPU PASS: one canonical typed parameter slot, name/ID aliases, atomic trigger consumption and component override/accessor integration. |
| C2 | CPU PASS: authored entry/reset, priority/typed conditions, per-cycle exit gates, independent clocks and coarse/fine residual-time regression. |
| B3 | CPU PASS: scene, spawned assembly and assembly document save/reopen compare real keyed skinning; referenced/unique resources, complete rig/geometry/motion fallbacks and ID overrides are covered. |
| D1-K3 | Pending; see the completion plan. |

## Review and corrective implementation

The [review at `a6efa8c`](ANIMATION_PHASE_40_GEMINI_REVIEW.md) reproduced 13
failing expectations in nine groups despite the original passing suite. Its
findings remain historical evidence for that revision. The
[corrections report](ANIMATION_PHASE_40_CORRECTIONS.md) records the subsequent
implementation commits, permanent regressions and remaining acceptance limits.
The current package statuses above supersede the review-open table.

## Commands and evidence

```powershell
cmake --build build --config Release --target AxleAnimationUnitTests --parallel 12
ctest --test-dir build -C Release -R '^Animation' --output-on-failure
```

Final correction verification:

- Release Editor, Runtime, AxleTests, Animation/Material/VFX CPU and VFX GPU targets build successfully.
- All 6 registered CTests PASS, including the intentional expected-failure harness and physical DX12/Vulkan VFX checks.
- Legacy animation Steps 36-41 PASS. Rendering-related legacy steps create hidden OpenGL contexts on the RTX 3070. These exercise old shader/buffer paths; they do not establish new animation RHI pixel acceptance.
- Build: `build/animation-fix-final-build.log`; CTest: `build/animation-fix-final-ctest.log`; legacy evidence: `build/animation-fix-legacy-36.log` through `build/animation-fix-legacy-41.log`.

Phase 38.3 material checks and Phase 39 interactive/OpenGL acceptance remain
separately open. No native authoring UI session or standalone packaged animation
application playback was performed in this correction pass.
