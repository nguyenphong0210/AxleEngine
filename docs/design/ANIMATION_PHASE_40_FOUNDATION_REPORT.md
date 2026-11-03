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
| A1 | PASS: Skeleton topological sorting via DFS/Kahn validates child-before-parent ordering, multi-root rigs, and cycle rejection. Partial-channel sampling preserves bind-pose translation/scale for rotation-only tracks. Antipodal quaternions SLERP smoothly without hemisphere flipping. Signed TRS matrices with reflection scales decompose and recompose accurately; sheared matrices are rejected. Mismatched pose sizes are strictly rejected during blending. Verified via `AxleAnimationUnitTests` (100% PASS). |
| A2 | PASS: `SkeletalMesh` CPU construction decoupled from graphics state; `BuildBuffers` guarded by `Window::HasActiveContext()` and lazy VAO initialization. `SkeletalVertex::AddBoneData` merges repeated bone influences, caps to 4 strongest influences, and strictly normalizes weights to 1.0 (with root bone fallback for unweighted vertices). `SkeletalMeshImporter::LoadGLTF` and `LoadFBX` cleared transactionally, resolve intermediary non-joint parent node transforms, support glTF CUBICSPLINE keyframe stride, generate flat normals for missing normal attributes, and handle non-indexed triangle primitives. Verified via `AxleAnimationUnitTests` (100% PASS). |
| A3 | PASS: Animator and AnimationStateMachine runtime instance isolation verified. Explicit copy constructor and copy assignment on AnimatorComponent deep-clone Animator and state machine instances; Scene::DuplicateEntity and Scene::Copy invoke isolated cloning without shared mutable state machine references or cross-talk. Cloned animators maintain independent playback clocks, parameters (Float, Int, Bool), and consume triggers atomically without affecting source animators. AnimationStateMachine::Reset() resets state to initial state and clears transient triggers. Evaluated typed rules and instance-aware conditions without lambda closures over shared state machine pointers. Verified via AxleAnimationUnitTests (100% PASS). |
| B1 | PASS: Stable versioned value definitions and validation implemented for Skeleton, SkeletalMesh, AnimationClip, and AnimatorController (`AnimationDefinition.hpp`). AssetKind and AssetType extended append-only (Skeleton, SkeletalMesh, AnimationClip, AnimatorController). Validates unique IDs and names, preserves distinct similar names, verifies parameter default variant types (Float/Int/Bool), rejects non-finite numerics and NaNs, rejects dangling transition states, checks condition operator compatibility, enforces sorted channel keyframes and event time boundaries, and computes deterministic FNV-1a skeleton compatibility signatures. Verified via `AxleAnimationUnitTests` (100% PASS). |
| B2 | PASS: Shared AssetDSL text codecs implemented for Skeleton, SkeletalMesh, AnimationClip, and AnimatorController (`AnimationAssetCodec.hpp`). Atomic replacement writes to sibling temporary files, flushes, and replaces atomically (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH` / `std::filesystem::rename`), guaranteeing original file bytes are preserved upon failure. Cooked binary skeletal mesh geometry baker produces versioned binary payload (`ASMB`, version 1, skeleton signature, vertex/index counts, bounds, packed vertex/index streams); loader validates magic, version, bounds, and strictly rejects truncated buffers or header mismatch. Verified via `AxleAnimationUnitTests` (100% PASS). |
| C1 | PASS: Typed animator parameter declarations and pure condition evaluator implemented (`AnimationStateMachine.hpp`, `Animator.hpp`, `Components.hpp`). Supports Float, Int, Bool, and Trigger with strict type validation. Trigger peek (`PeekTrigger`) inspects trigger active status without consuming it, while `CheckAndConsumeTrigger` consumes atomically. Stable parameter IDs persist across parameter renames (`RenameParameter`); conditions bound to stable IDs evaluate correctly before and after display name modifications. Pure condition evaluator handles relational operators across float, int, bool, and trigger types without side effects. Retained full compatibility with legacy `Speed` and `IsGrounded` component properties. Verified via `AxleAnimationUnitTests` (100% PASS). |
| C2-K3 | Pending; see the completion plan. |

## Commands and evidence

```powershell
cmake --build build --config Release --target AxleAnimationUnitTests --parallel 12
ctest --test-dir build -C Release -R '^Animation' --output-on-failure
```

A0: both CTest cases pass (the harness failure is intentionally expected to
exit nonzero). Build log: `build/animation-a0-build.log`.

Phase 38.3 material checks and Phase 39 interactive/OpenGL acceptance remain
separately open. Existing untracked user assets and crash logs are preserved.
