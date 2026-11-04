# Review: Gemini A3/A2 continuation

> **Archived delivery/review:** Evidence retains its recorded revision and verification limits; archival does not establish acceptance.
> Follow the [optimization roadmap](../../../ROADMAP.md) for current work;
> recorded instructions/commands below describe the original task.

Date: 2026-11-03. Reviewed HEAD: `c13b21a`.
Range: `0b83135..c13b21a`, with a clean working tree at review start.

## Result

Gemini respected the two-package scope and produced separate implementation and
delivery commits. Read-only geometry/rig inspection, builder alias detachment,
runtime rig copies and a separate CPU FBX baker follow the intended direction.

The current build and supplied tests pass, but supplementary production-API
probes reproduce **eight failing expectations**. A3 acceptance must reopen. A2
remains open for technical baking defects as well as missing exporter coverage.
This review does not change production code.

| Commit | Reviewed work |
| --- | --- |
| `3fabd5e` | Mesh/rig publication and runtime ownership. |
| `0463c3e` | Adaptive FBX curve baker and fixtures. |
| `c13b21a` | Delivery report and acceptance claims. |

## Findings

### R1 / P1: Invalid non-null meshes bypass last-good replacement

Sources: `Engine/Scene/include/Axle/Components.hpp:1374`;
`Engine/Animation/src/SkeletalMesh.cpp:119`;
`Engine/Animation/include/Axle/SkeletalMesh.hpp:43`.

`ReplaceMesh` checks only whether the pointer is null. The public constructor
still accepts unvalidated data, and the default constructor creates an empty
resource. These paths bypass `Create` validation. Invalid resources can therefore
replace a valid component and reach consumers, despite the last-good guarantee.

Reproduction: construct a three-vertex mesh with indices `{0,99,2}` using
`std::make_shared<SkeletalMesh>(invalidDraft)`, then call `ReplaceMesh`. It returns
true and replaces the valid resource. The existing new test passes only a null
factory result, so it never exercises an invalid non-null object.

Require validated construction/publication on every supported path and a genuine
replacement invariant. Preserve the previous component and diagnostic on failure;
do not remove legacy callers without migrating them coherently.

### R2 / P2: The published mesh object itself is still assignable

Source: `Engine/Animation/include/Axle/SkeletalMesh.hpp:41`.

Read-only accessors protect members through those accessors, but `Create` returns
`shared_ptr<SkeletalMesh>` and copy/move assignment remains public and implicit.
Two entities can share a resource that is subsequently overwritten in place:

```cpp
auto published = SkeletalMesh::Create(originalDraft);
auto otherEntityMesh = published;
*published = SkeletalMesh(replacementDraft);
// otherEntityMesh now observes replacementDraft as well.
```

The probe observes the shared name change from `Original` to `Overwritten`.
Close the object-level mutation path as well as nested rig/vector mutation.
Keep GPU cache mutation logically separate from authored resource mutation.

### R3 / P1: Finite angular probes can accept a 180-degree baking error

Source: `Engine/Animation/src/FbxCurveBaker.cpp:259` and `:293`.

The baker accepts an interval after checking equally spaced quaternion probes.
Euler rotations can perform complete turns between those probes. Endpoint and
probe orientations then coincide modulo quaternion sign even though intermediate
orientations differ substantially. No source variation/extrema bound guards this.

Reproduction: adapt `FastRotationSkin.fbx` to a linear Z rotation from 0 to 2160
degrees over `1/30` second. The five probes and endpoints see complete turns. The
import succeeds with only two rotation keys. At `t=1/360` second, ufbx evaluates
180 degrees while the baked clip evaluates identity: **error 3.14159 radians**
against the declared **0.001 radian** tolerance.

Increasing probe count or frame rate alone does not establish the claimed bound.
Account for source angular variation and supported source interpolation, or
explicitly reject cases without a justified accuracy guarantee. A probe-grid-only
guarantee must be named honestly and cannot close continuous-error acceptance.

### R4 / P1: Minimum subdivision interval silently deletes real STEP keys

Source: `Engine/Animation/src/FbxCurveBaker.cpp:165`.

`uniqueStepTimes` discards a source timestamp whenever it is closer than
`MinIntervalSeconds` to the preceding timestamp. Source hold boundaries are not
optional refinement samples. Distinct float-representable source keys are lost
without a diagnostic.

Reproduction: adapt `SteppedSkin.fbx` to values 2, 5, 8 at times 0, 0.5,
approximately 0.50005 seconds, keeping the take end at 1 second. Import succeeds.
At `t=0.5001`, ufbx evaluates **8**, while the baked clip evaluates **5**.

Keep exact supported source boundaries. Reject unrepresentable/collapsed times
instead of silently merging distinct authored events. Apply a separate policy to
adaptive subdivision limits and source-key preservation.

### R5 / P2: CONSTANT_NEXT is silently converted to previous-value holds

Sources: `Engine/Animation/src/FbxCurveBaker.cpp:48`, `:134`, `:177`.

`InspectCurves` groups `UFBX_INTERPOLATION_CONSTANT_PREV` and `CONSTANT_NEXT`
together and the baker emits both as the runtime's previous-value STEP mode.
These source semantics differ inside the interval.

Reproduction: change the stepped fixture's FBX flags from 24578 to 24834
(constant-next). At `t=0.25`, ufbx evaluates **5** and the imported clip evaluates
**2**, with successful import and no diagnostic.

Represent both boundary semantics correctly, or explicitly reject constant-next
when the runtime cannot represent it. Do not use epsilon key offsets that can
collapse in float timestamps.

### R6 / P2: The last key's unused interpolation falsely marks a curve mixed

Source: `Engine/Animation/src/FbxCurveBaker.cpp:45`.

Interpolation classification includes the final key even though it has no
outgoing interval inside the source curve. A final linear flag turns an otherwise
entirely stepped source into `Mixed`, causing a valid hold animation to fail.

Reproduction: change only the final flag in `SteppedSkin.fbx` from 24578 to
24836. Earlier source intervals remain previous-value holds; import now fails
with `unsupported mixed step and continuous interpolation`.

Classify actual contributing intervals, including their relationship to the take
window. Distinguish genuine unsupported mixed interpolation from unused terminal
metadata or static axes.

### R7 / P2: Budget/refinement assertions are skipped by the supplied test

Source: `Editor/src/Tests/Test_AnimationImporterCorrections.cpp:121`.

The test maps canonical rig names back to raw ufbx node names. The imported
unnamed root has a generated canonical name that does not match the raw empty
name. The mapping finds **2 nodes for a 3-bone rig**. The `if` at line 127 is false,
so both budget-exhaustion and precision/refinement assertions are skipped.

This explains why a green suite is not evidence for the report's claimed budget
coverage. Make fixture/mapping prerequisites unconditional checks; map helpers
explicitly. Use a genuinely nonlinear fixture requiring refinement rather than
floating-point noise in a nominally linear translation as the failure oracle.

### R8 / P2: Public BakeChannel destroys last-good output before failing

Source: `Engine/Animation/src/FbxCurveBaker.cpp:114`.

The public helper overwrites names and clears all tracks before inspecting and
rejecting mixed interpolation. It also emits partial keys directly into the
caller's output while consuming budget. The enclosing importer uses a fresh
candidate, but direct helper callers do not get transactional publication.

Reproduction: initialize a channel named `LastGood` with a position key, then
call `BakeChannel` on `MixedInterpolationSkin.fbx`. It returns false and leaves
the previous channel cleared/renamed.

Bake into a local candidate, validate and publish once. Define budget/stat
accounting on failure explicitly; at minimum preserve the caller's valid channel.

## Verification

- Release `AxleAnimationUnitTests` build: PASS.
- Supplied `AnimationUnitTests` and expected `AnimationHarnessFailure`: 2/2 PASS.
- Release `AxleTests` build including Editor/Runtime dependencies: PASS.
- Supplementary production-API probe: **exit 1; eight expectations fail**.
- New animation GPU pixels, native authoring UI and standalone packaged playback:
  NOT RUN in this review.

Local evidence (ignored build artifacts):

- `build/animation-a3-a2-review-build.log`
- `build/animation-a3-a2-review-editor-build.log`
- `build/animation-a3-a2-probe-results.log`
- `build/AnimationA3A2ReviewProbe.cpp`
- `build/animation-a3-a2-review-fixtures/*.fbx`

The source probe creates no device/window and calls production mesh, component,
importer and baking APIs. Expected transforms come from ufbx's source evaluator.
Its temporary CMake injection was cleared after probing. These ignored artifacts
may be absent in a different checkout; promote the reproductions into permanent
registered regressions as part of each correction.

Fixture derivations use the repository's existing authored FBX files: close-step
times change ticks to `{0,23093079000,23095388308}`; constant-next flags use 24834;
terminal-linear changes only the final flag to 24836; aliased spin uses ticks
`{0,1539538600}` and values `{0,2160}` with linear flags and the matching take end.

## Scoped correction order

1. Close A3 construction, assignment and replacement holes (R1/R2), including
   supported legacy callers and immutable nested resources.
2. Fix STEP interval/terminal semantics and exact boundary handling (R4-R6).
3. Correct the baking accuracy contract and transactional channel output (R3/R8).
4. Make budget/refinement and source-comparison tests actually execute (R7),
   rerun available checks, and correct delivery/roadmap claims.

Keep each correction with its relevant permanent tests and a separate local
commit. Stop at A3/A2 corrections; do not begin other Phase 40 packages.
