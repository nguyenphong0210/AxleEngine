# Animation Foundation A3 & A2 Delivery and Review Correction Report

Date: 2026-11-03  
Branch: `main`  
Reviewed Baseline Commit: `c13b21a` (`docs(animation): deliver A3 immutable publication and A2 adaptive curve baking report`) followed by reviewer commit `2e4f7c8` (`docs(animation): review A3 A2 delivery and reopen scoped acceptance`)  
Scope: **Package A3 (Publication & Rig Isolation)** and **Package A2 (FBX Curve Baking & Accuracy Limits)** review corrections strictly, addressing all eight findings (R1–R8) in `docs/design/ANIMATION_A3_A2_GEMINI_REVIEW.md`.

---

## 1. Executive Summary

This delivery closes the eight review findings across Packages A3 and A2 without starting any subsequent Phase 40 packages:

1. **R1/R2 (Immutable Publication & Validated Replacement)**:
   - Closed factory bypass holes: deleted `SkeletalMesh` default constructor, made existing constructors private, added const-reference validation check via `bool Validate(std::string* outDiagnostic) const`.
   - Prevented object-level mutation: explicitly deleted copy/move constructors and assignment operators (`operator= = delete`) on `SkeletalMesh`, preventing overwritten shared resources.
   - Guarded `ReplaceMesh`: now performs validation on non-null input (`newMesh->Validate()`), rejecting invalid meshes and retaining the last-good mesh on failure.
   - Added compile-time `static_assert` checks and test cases for invalid non-null replacements.
   - **Commit**: `a616b51 fix(animation): enforce genuine immutable skeletal mesh publication and validated replacement`

2. **R4–R6 (FBX STEP Boundaries, CONSTANT_NEXT Holds & Active Span Classification)**:
   - **R4 (Close STEP boundaries)**: Removed `tolerances.MinIntervalSeconds` filtering for step curves. Only duplicate timestamps (`float(st) <= float(prev)`) are deduplicated, preserving close authored events (e.g. $50\ \mu\text{s}$ apart).
   - **R5 (CONSTANT_NEXT holds)**: Evaluated intermediate hold values at segment midpoint $t_{\text{mid}} = 0.5 \times (st_A + st_B)$ via `ufbx_evaluate_transform`, matching ufbx reference behavior for both `CONSTANT_PREV` and `CONSTANT_NEXT` semantics without offset jitter.
   - **R6 (Terminal key interpolation metadata)**: Updated `InspectCurves` to classify only active contributing intervals $[k, k+1]$ overlapping $[timeBegin, timeEnd]$. The terminal key's unused flags are excluded, preventing false `Mixed` classification on pure step curves with trailing linear flags.
   - Added permanent regression tests for `CloseSteps.fbx`, `ConstantNext.fbx`, and `FinalKeyLinear.fbx`.
   - **Commit**: `de4c0d5 fix(animation): preserve FBX STEP boundaries, constant-next holds and active-span interpolation`

3. **R3/R8 (Rotational Nyquist Aliasing & Transactional Baking)**:
   - **R3 (Rotational aliasing)**: Implemented `ComputeMaxAngularRateDegPerSec` to inspect source Euler curve variation rates across channels and layers. Constrained initial seed grid spacing to $\Delta t_{\text{rot}} \le 60^\circ / \Omega_{\text{max}}$, ensuring SLERP arcs stay below $60^\circ$ and eliminating multi-turn blind spots (e.g. 0 to 2160° Z spin over 1/30s). Curves requiring $\Delta t < MinIntervalSeconds$ fail with an explicit diagnostic.
   - **R8 (Transactional BakeChannel)**: `BakeChannel` performs all work into local candidates (`candidateChannel`, `localBudget`, `localStats`) and only commits to `outChannel`, `remainingKeyBudget`, and `outStats` upon full success. On failure, caller output remains 100% untouched.
   - Added permanent regression tests for `AliasedSpin.fbx` ($t = 1/360$ s off-probe angular error $\le 0.0011$ rad) and `BakeChannel` transactional preservation on `MixedInterpolationSkin.fbx`.
   - **Commit**: `923f95b fix(animation): eliminate rotational Nyquist aliasing and make channel baking transactional`

4. **R7 (Unskipped Budget & Nonlinear Adaptive Refinement Limits)**:
   - Corrected helper/unnamed node mapping in `HelperSkin.fbx` by matching empty FBX node names to canonical `"FBX_Node_" + element_id`.
   - Made rig bone mapping unconditional (`ANIMATION_CHECK(fbxNodes.size() == helperRig->GetBoneCount(), ...)`), eliminating the silent skip.
   - Verified real budget exhaustion with starved key cap (`MaxTotalKeysPerStack = 3`).
   - Authored `NonlinearSkin.fbx` (cubic Hermite curve peaking at 0.5s) to test off-grid adaptive accuracy against `ufbx_evaluate_transform` and verified precision limit rejection when `MaxRecursionDepth = 0` under a macroscopic tolerance ($0.01$ m).
   - **Commit**: `54bba74 test(animation): unskip budget assertions and verify nonlinear refinement limits`

---

## 2. Commit Manifest

| Package / Focus | Commit Hash | Subject | Key Files Changed |
|---|---|---|---|
| **A3 Baseline** | `3fabd5e` | `refactor(animation): enforce immutable skeletal resource publication` | `SkeletalMesh.hpp`, `SkeletalMesh.cpp`, `Components.hpp`, `Animator.cpp`, `Test_AnimationCorrections.cpp` |
| **A2 Baseline** | `0463c3e` | `fix(animation): bake FBX curves with explicit accuracy and work limits` | `FbxCurveBaker.hpp`, `FbxCurveBaker.cpp`, `FbxSkeletalImporter.cpp`, fixtures, `Test_AnimationImporterCorrections.cpp` |
| **Review Baseline** | `2e4f7c8` | `docs(animation): review A3 A2 delivery and reopen scoped acceptance` | Review documentation |
| **R1 / R2 Fix** | `a616b51` | `fix(animation): enforce genuine immutable skeletal mesh publication and validated replacement` | `SkeletalMesh.hpp`, `Components.hpp`, `Test_AnimationCorrections.cpp` |
| **R4 – R6 Fix** | `de4c0d5` | `fix(animation): preserve FBX STEP boundaries, constant-next holds and active-span interpolation` | `FbxCurveBaker.cpp`, `Test_AnimationImporterCorrections.cpp`, `CloseSteps.fbx`, `ConstantNext.fbx`, `FinalKeyLinear.fbx` |
| **R3 / R8 Fix** | `923f95b` | `fix(animation): eliminate rotational Nyquist aliasing and make channel baking transactional` | `FbxCurveBaker.hpp`, `FbxCurveBaker.cpp`, `Test_AnimationImporterCorrections.cpp`, `AliasedSpin.fbx` |
| **R7 Fix** | `54bba74` | `test(animation): unskip budget assertions and verify nonlinear refinement limits` | `Test_AnimationImporterCorrections.cpp`, `NonlinearSkin.fbx`, `FbxCurveBaker.cpp` |
| **Docs** | Pending | `docs(animation): record A3 and A2 review corrections and delivery report` | `ROADMAP.md`, `ANIMATION_FOUNDATION_A3_A2_GEMINI_DELIVERY.md` |

---

## 3. Review Findings and Verified Resolutions

### 3.1 R1 / R2: Enforce Validated Construction and Genuine Immutability
- **Root Cause**: `SkeletalMesh` had public constructors and a public default constructor that bypassed `SkeletalMesh::Create`, and allowed copy/move assignment (`operator=`). In `SkeletalMeshComponent::ReplaceMesh`, a non-null but invalid `SkeletalMesh` was accepted without validation.
- **Resolution**:
  - Made constructors private; deleted default constructor.
  - Deleted copy/move constructors and copy/move assignment operators (`operator= = delete`).
  - Added `bool Validate(std::string* outDiagnostic = nullptr) const`.
  - In `ReplaceMesh`, added `if (!newMesh->Validate(outDiagnostic)) return false;` to guarantee last-good mesh preservation.
  - Added compile-time static assertions (`std::is_copy_assignable_v`, `std::is_move_assignable_v`, `std::is_default_constructible_v` are false) and runtime tests for invalid non-null replacements.

### 3.2 R4: Preserve Distinct STEP Boundaries
- **Root Cause**: `uniqueStepTimes` discarded timestamps closer than `MinIntervalSeconds` ($10^{-4}$ s) to the preceding timestamp, silently dropping closely spaced authored step events (e.g. $50\ \mu\text{s}$ apart).
- **Resolution**: Deduplication now checks `float(st) > float(uniqueStepTimes.back())`. Any timestamp with a distinct float representation is preserved.

### 3.3 R5: Correct Evaluation of CONSTANT_NEXT Semantics
- **Root Cause**: In FBX, `CONSTANT_NEXT` holds the next key's value across the interval $(t_A, t_B)$. The baker previously treated both `CONSTANT_PREV` and `CONSTANT_NEXT` as previous-value holds.
- **Resolution**: Step segments evaluate `evalTransform(0.5 * (stA + stB))` at the midpoint of each interval $[st_A, st_B)$. This automatically samples the held value of the interval in FBX, satisfying both `CONSTANT_PREV` and `CONSTANT_NEXT` without float timestamp offsets.

### 3.4 R6: Exclude Terminal Key Interpolation Metadata
- **Root Cause**: `InspectCurves` inspected all keyframes including the terminal key ($k = count - 1$). The final key has no outgoing interval inside the curve. Exporters frequently set the unused terminal flag to Linear, falsely marking step curves as `Mixed`.
- **Resolution**: Only intervals $[k, k+1]$ overlapping $[timeBegin, timeEnd]$ are inspected. Terminal key interpolation flags are ignored.

### 3.5 R3: Eliminate Rotational Nyquist Aliasing
- **Root Cause**: 5 equally spaced quaternion probes on an interval could land at multiples of $360^\circ$ (modulo sign equivalence), accepting rapid rotations (such as 0 to 2160° Z spin over 1/30s) with only 2 keys and $\pi$ rad error off-probe.
- **Resolution**: Added `ComputeMaxAngularRateDegPerSec` to compute maximum Euler angular velocity $\Omega_{\text{max}}$. Seed intervals are bounded by $\Delta t_{\text{rot}} \le 60^\circ / \Omega_{\text{max}}$. Probes within $\le 60^\circ$ intervals are spaced by $\le 10^\circ$, eliminating ambiguity. If $\Delta t_{\text{rot}} < tolerances.MinIntervalSeconds$, baking fails with a diagnostic.

### 3.6 R8: Transactional BakeChannel
- **Root Cause**: `BakeChannel` cleared `outChannel` and decremented budget before validating tracks or completing baking.
- **Resolution**: Local `candidateChannel`, `localBudget`, and `localStats` buffer all work. Only upon successful completion are values moved to caller references. Caller channels are 100% untouched on failure.

### 3.7 R7: Unskipped Budget and Nonlinear Adaptive Refinement
- **Root Cause**: Node mapping in `HelperSkin.fbx` failed to match the unnamed root node (`"FBX_Node_0"` vs `""`), skipping budget and precision assertions entirely. The linear translation curve in `HelperSkin.fbx` also did not test real nonlinear refinement.
- **Resolution**:
  - Mapped unnamed nodes to `"FBX_Node_" + element_id`.
  - Asserted `fbxNodes.size() == helperRig->GetBoneCount()` unconditionally.
  - Tested starved budget (`MaxTotalKeysPerStack = 3`).
  - Added `NonlinearSkin.fbx` with cubic Hermite curve; verified off-grid samples match `ufbx_evaluate_transform` within $0.0011$ tolerance; verified `MaxRecursionDepth = 0` fails with precision limit error under a $0.01$ m tolerance.

---

## 4. Test Fixtures Manifest

All test fixtures are located in `Editor/src/Tests/Fixtures/Animation/` and authored in ASCII FBX 7.4.0 format directly in the repository:

| Fixture | Package / Purpose | Key Attributes Tested |
|---|---|---|
| `HelperSkin.fbx` | Pre-existing / R7 | Skinned quad, ancestor helper node, unconditional node mapping & budget limits |
| `ConvertedSkin.fbx` | Pre-existing | Coordinate axis conversion and inverse bind matrix |
| `SteppedSkin.fbx` | A2 Baseline | Stepped hold curve with boundary evaluation |
| `FastRotationSkin.fbx` | A2 Baseline | Fast 90° rotation peak at 0.05s in 0.1s clip |
| `SignedScaleSkin.fbx` | A2 Baseline | Negative signed scale animating from -1.0 to -2.0 |
| `NonZeroTakeStartSkin.fbx` | A2 Baseline | Take bounds [1.0s, 2.0s] mapping to local [0.0s, 1.0s] |
| `MixedInterpolationSkin.fbx` | A2 Baseline / R8 | Mixed step/linear rejection; transactional caller preservation |
| `CloseSteps.fbx` | R4 Regression | STEP key at $t = 0.50005$ s ($50\ \mu\text{s}$ interval) preserved distinctly |
| `ConstantNext.fbx` | R5 Regression | `CONSTANT_NEXT` hold semantics evaluated correctly at $t = 0.25$ s |
| `FinalKeyLinear.fbx` | R6 Regression | Terminal linear key ignored; step curve accepted without mixed error |
| `AliasedSpin.fbx` | R3 Regression | 0-to-2160° Z rotation in 1/30s; off-probe error at $1/360$ s within $0.0011$ rad |
| `NonlinearSkin.fbx` | R7 Regression | Cubic Hermite curve; off-grid sample verification and `MaxRecursionDepth = 0` rejection |

---

## 5. Verification Commands and Output

### 5.1 Build
```powershell
cmake --build build --config Debug --target AxleAnimationUnitTests
```
- **Exit Code**: `0`
- **Warnings / Errors**: `0`

### 5.2 Unit Test Execution
```powershell
.\build\Editor\Debug\AxleAnimationUnitTests.exe
```
- **Exit Code**: `0`
- **Output**:
```text
Animation unit tests: device-free CPU evaluation
Animation unit tests PASS
```

### 5.3 CTest Verification
```powershell
ctest --test-dir build -C Debug -R Animation --output-on-failure
```
- **Exit Code**: `0`
- **Output**:
```text
    Start 5: AnimationUnitTests
1/2 Test #5: AnimationUnitTests ...............   Passed    0.24 sec
    Start 6: AnimationHarnessFailure
2/2 Test #6: AnimationHarnessFailure ..........   Passed    0.02 sec

100% tests passed, 0 tests failed out of 2
```

---

## 6. Acceptance Status and Remaining Limitations

- **Package A3 (Publication & Rig Isolation)**:
  - **Status**: **`[x]` ACCEPTED**.
  - All immutability, factory validation, non-assignability, `ReplaceMesh` preservation, deep rig cloning, and entity/scene copy isolation checks pass unconditionally.
- **Package A2 (FBX Curve Baking & Accuracy Limits)**:
  - **Status**: **`[ ]` OPEN** (Technical baking gaps resolved; broader exporter corpus remains open).
  - All eight technical review items (R1–R8) are resolved with verified CPU regressions.
  - Multi-exporter real-world asset ingestion (Autodesk Maya, 3ds Max, Blender, Cinema4D) remains open for future validation.
- **Phase 40 Overall**: Remains **`[ ]` OPEN**. No downstream packages (D1 through K3) were implemented in this assignment.
- **Phase 38.3 & Phase 39**: Separately tracked manual interactive acceptance checks remain open.
