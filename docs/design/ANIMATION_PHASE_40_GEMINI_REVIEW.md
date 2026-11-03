# Phase 40 Gemini foundation review

Date: 2026-11-03. Reviewed HEAD: `a6efa8c`.
Range: `7c72f35..a6efa8c` (eight implementation commits, A1/A2/A3/B1/B2/C1/C2/B3).
Status: **foundation implementation present; acceptance reopened**.

This is a review, not a production-code correction. The prior A0 harness commit
is the baseline. The working tree was clean when the reviewed range was recorded.
All findings below refer to that exact revision; subsequent fixes need new evidence.

## Assessment

The implementation follows the intended module placement and reuses existing
animation types. Topological rig evaluation, partial bind-component sampling,
device-free mesh preparation, versioned definition types, atomic file helpers
and typed transition APIs are useful foundations. Release builds and the supplied
unit suite pass.

The tested behavior is narrower than the completion claims. Supplementary CPU
probes reproduced **13 failing expectations in nine groups**. In particular,
runtime compilation and scene persistence lose animation data, while importer
and parameter semantics remain incorrect. Fix these before expanding the RHI
renderer or editor on top of this resource/runtime contract.

## Findings, ordered by impact

### R1 / P1: Controller compilation/extraction loses executable motion and conditions

Source: `Engine/Animation/src/AnimationAssetCodec.cpp:792-907`.

`CompileStateMachineFromDefinition` constructs states without resolving
`MotionAssetRef`, assigning `Clip` or constructing a blend space. A decoded
controller can therefore transition between names while sampling bind poses.
`ExtractDefinitionFromStateMachine` omits motion references and copies only
`PureConditions`; transitions authored through the existing `Rules` API become
unconditional after save/reopen. It also regenerates IDs from names/counters,
losing original stable identity and graph/rig metadata.

The scene resolver uses this compiler. Its locally imported `clips` vector is
discarded rather than connected to states (`SceneSerializer.cpp:1935-1940`,
`1975-1992`). An apparently successful scene round trip is not pose equivalence.

Reproduced: motion reference/clip absent after compile/extract; a Speed rule
becomes an empty condition list. Repair by retaining the authored controller
definition and using an explicit validated resource resolver for all motions.
Missing/incompatible dependencies must return diagnostics or preserve last-good
resources, not publish an apparently valid empty animation state.

### R2 / P1: Skeleton codec drops inverse bind matrices

Source: `AnimationAssetCodec.cpp:105-117`, `136-146`.

Encoding writes local TRS only. Decoding constructs new Bones with default
identity inverse binds. A valid bind offset of -2 on Y becomes zero after the
round trip, so the skin palette changes even at bind pose. Recomputing inverse
binds from sampled/local transforms is not a general repair for imported assets.

Reproduced: inverse bind Y **-2 -> 0**. Persist every canonical inverse bind and
validate the complete matrix/basis contract. Compare palettes and deformed
vertices before/after save, using nonidentity bind and mesh bases.

### R3 / P1: Parameter names and IDs have separate mutable values

Source: `AnimationStateMachine.cpp:117-229`, `271-274`.

Setters write a name entry plus an ID entry only when the input is a name.
Reads prefer whichever entry was requested. Updating by stable ID leaves the
name value stale. Consuming a trigger by ID leaves its name entry set, allowing
the same trigger to be observed/consumed again. The public setters also do not
call `ValidateParameterType`, so an Int declaration accepts a float write.

Reproduced: Speed reads **3 by ID, 0 by name**; a trigger remains true by name
after ID consumption; a float is stored under a declared Int parameter.
Resolve name -> ID first and keep exactly one typed runtime slot per declared
parameter. Validate writes/defaults/renames and preserve an explicit legacy
undeclared-parameter adapter rather than mixing authorities.

### R4 / P1: glTF interpolation modes are silently changed

Source: `SkeletalMeshImporter.cpp:437-460` and channel interpolation in
`AnimationClip.cpp`.

CUBICSPLINE reads only the middle value of each three-element group, discarding
tangents. STEP has no retained interpolation mode. Both subsequently use linear
interpolation. Reading the correct accessor stride is not sufficient support
for a cubic track.

Reproduced at t=0.25 for a 0 -> 2 translation track:

| Mode | Expected | Observed |
| --- | --- | --- |
| STEP | 0 | 0.5 |
| Cubic with zero tangents | 0.3125 | 0.5 |

Retain/evaluate source interpolation, bake correctly with a documented error
bound, or explicitly reject unsupported data. Test sampled interior times,
not only extracted key values or successful import return codes.

### R5 / P1: Non-joint root ancestors disappear from the rig basis

Source: `SkeletalMeshImporter.cpp:285-308`.

The importer accumulates helper transforms but applies them only when it finds
another joint parent. For a root joint below non-joint ancestors,
`resolvedParentIdx` remains -1 and the accumulated transform is discarded.
Additionally, animated joint keys are read in their original local basis rather
than the collapsed bind hierarchy, so helper handling needs one coherent bind
and animation policy.

Reproduced: helper translation 10 plus root-joint translation 1 produces a local
root translation **1 instead of 11**. Preserve helper nodes or bake transforms
and animation into a validated canonical basis, including root ancestors.

### R6 / P1: Cloning copies closures that still observe the original instance

Source: `AnimationStateMachine.cpp:355-381`.

`m_Transitions` is copied wholesale, including legacy `std::function<bool()>`
conditions. A copied closure cannot automatically rebind its captured owner.
Copies of a character using that API can respond to the original character's
parameters; owning captures can also retain cycles. Sharing mutable clip/blend
resources is not an enforced immutable-definition boundary either.

Reproduced with a weak owner capture: source Speed=0, clone Speed=1; clone stays
Idle because its predicate still reads the source. Migrate production conditions
to typed rules. Legacy predicates need an explicit safe recreation policy or
unsupported-clone diagnostic. Keep authoring-copy reset separate from a runtime
snapshot clone; the current Clone copies live clocks/transition state.

### R7 / P1: Loop exit gates can fire before the requested phase

Source: `AnimationStateMachine.cpp:413-425`.

For looping states, the gate skips only if both total normalized time and current
cycle phase are below the exit phase. After the first cycle, total normalized
time is already greater than the phase, making that check permanently permissive.

Reproduced: looping Idle at t=1.2, exit phase=0.5, enable its condition, then
Update(0.1). It transitions immediately at cycle phase 0.2 rather than waiting
for 0.5. Specify per-cycle threshold crossing semantics and test frame subdivision,
late condition activation, multiple cycles and residual crossfade time.

### R8 / P1: Cooked geometry payload validation does not validate its contents

Source: `AnimationAssetCodec.cpp:750-785`.

The loader checks counts and byte size, then copies arrays and normalizes
weights. It does not reject indices outside the vertex array, nonfinite vertex
data, invalid bounds, incompatible joint IDs or skeleton signature mismatch.

Reproduced: one vertex with triangle indices `{0,9,0}` bakes and loads successfully.
Validate topology, finite attributes, weights/joint ranges, bounds, compatibility
and submesh ranges before publishing runtime geometry or uploading it. Preserve
the previous output on failure. A correctly sized byte buffer is not validated
mesh geometry.

### R9 / P2: Reset uses the first state rather than the authored entry

Source: `AnimationAssetCodec.cpp:800-815`; `AnimationStateMachine.cpp:345-346`.

AddState records the first state as the initial state. The compiler sets the
current state to `EntryStateId` without changing that initial binding.
Reproduced: vector order A,B with authored entry B starts in B, but Reset returns
to A. Compile/store an explicit stable entry-state binding and verify Reset,
authoring duplicate and runtime start all use it.

## Additional source-confirmed gaps to cover during correction

These observations are separate from the 13 executed failing expectations:

- Import still silently selects the first mesh-associated skin and then walks
  **all** meshes (`SkeletalMeshImporter.cpp:249-258`, `325-328`); explicit multi-skin
  selection/rejection and preserved submesh/material partitions remain unfinished.
- Scene mesh loading still calls the source importer; the cooked skeletal mesh
  definition/geometry dependencies are not resolved there. Source-unavailable
  packaged loading needs a real immutable resource-library path.
- Scene/assembly controller resolution checks ControllerPath before inline data
  without honoring `IsUnique`; detached versus referenced ownership needs a
  behavioral test, not only a serialized flag assertion.
- `SkeletonDefinition::ComputeSignature` hashes names, parents and translations
  only despite its rotation/scale comment. Rigs differing in rotation/scale or
  inverse binds can have the same compatibility signature.
- `AnimationPose::Blend` checks pose lengths but not different bound skeletons,
  and global-pose evaluation can fill missing transforms from bind pose. Add
  equal-length incompatible-rig, nonfinite-pose and invalid-output tests before
  closing A1/B1 validation.

## Verification performed

- Release `AxleAnimationUnitTests` build: PASS.
- Existing `AnimationUnitTests` and expected `AnimationHarnessFailure`: 2/2 PASS.
- Release Editor build including Runtime dependency: PASS.
- Additional production-API CPU probe: **exit 1, 13 regressions reproduced**.
- Animation GPU pixels, native authoring UI and packaged Runtime playback:
  **NOT RUN**. Successful compilation does not establish these behaviors.

Logs: `build/animation-gemini-review-build.log`,
`build/animation-gemini-review-tests.log`,
`build/animation-gemini-review-editor-build.log`,
`build/animation-review-probe-build.log`, `build/animation-review-probe-results.log`.
The supplementary source is `build/AnimationReviewProbe.cpp`, with tiny generated
glTF fixtures in `build/animation-review-fixtures`. They are ignored local review
artifacts, not permanent registered tests. The temporary CMake injection was
removed from the build configuration after probing.

## Recommended correction sequence

1. **Parameter/runtime repair (C1/C2/A3)**: canonical typed slots, atomic trigger
   consumption, explicit entry/reset, loop crossing semantics and safe clone
   policies. Convert the observed failures into permanent always-active tests.
2. **Asset/resource repair (B1/B2/B3)**: lossless skeleton/controller codecs,
   immutable resource resolution, preserved IDs/conditions/motions/overrides,
   signature/geometry validation and last-good outputs. Prove sampled/deformed
   pose equivalence through scene and both assembly paths, with real keyed clips.
3. **Importer repair (A1/A2)**: source mesh/skin/basis/helper/interpolation contracts,
   limits and malformed-input handling. Use tiny nonidentity/imported fixtures,
   including missing source files after cooking and equal-length incompatible rigs.
4. Rebuild Editor/Runtime, rerun legacy animation regressions and CPU suites,
   and update evidence before restarting D/F/G expansion. Keep unavailable GPU/UI
   checks open. Each correction package receives its own tested local commit.
