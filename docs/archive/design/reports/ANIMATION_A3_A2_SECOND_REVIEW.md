# Animation A3/A2: second review and bounded continuation

> **Archived delivery/review:** Evidence retains its recorded revision and verification limits; archival does not establish acceptance.
> Follow the [optimization roadmap](../../../ROADMAP.md) for current work;
> recorded instructions/commands below describe the original task.

Date: 2026-11-03. Reviewed HEAD: `fbfbf58`.
Range: `2e4f7c8..fbfbf58` (four implementation/test commits and one report).

At review start, `NonlinearSkin.fbx` had an uncommitted extra trailing blank line.
It was preserved. It does not change the fixture's semantics; tests below used
the current working tree. This review changes documentation only.

**Superseded by verified implementation:** S1-S4 are corrected in the
[final corrections report](ANIMATION_FOUNDATION_FINAL_CORRECTIONS.md). The
findings below remain historical evidence for `fbfbf58`; they are not open work
for the latest Gemini assignment. The current prompt assigns G1 CPU models only.

## Assessment

The previous delivery made measurable progress. Mesh copy/move assignment is now
deleted, invalid non-null `ReplaceMesh` calls are rejected, the known 2160-degree
spin regression passes, representable close STEP keys survive, terminal flags
are classified by active intervals, budget tests execute, and `BakeChannel`
buffers its outputs transactionally.

However, **six supplementary expectations still fail in four areas**. A3's
factory-only publication claim remains incomplete. A2 has remaining endpoint and
validation defects; its open status must not be described as exporter coverage
alone. The supplied two Animation CTests pass.

## Findings

### S1 / P1: Public construction still bypasses validated mesh publication

Sources: `Engine/Animation/include/Axle/SkeletalMesh.hpp:44`;
`Engine/Animation/src/SkeletalMesh.cpp:119`;
`Engine/Scene/include/Axle/Components.hpp:1371`.

The report says constructors were made private. The draft constructor is still
public and does not call `Validate` before storing data or optionally uploading
buffers. The guarded `ReplaceMesh` path is fixed, but direct construction and
component initialization still bypass it.

Reproduced checks:

1. `std::is_constructible_v<SkeletalMesh, SkeletalMeshData>` remains true.
2. Construct a three-vertex mesh with indices `{0,99,2}` directly, then construct
   `SkeletalMeshComponent` with that pointer. It exposes the invalid mesh without
   any failed publication result.

Close the constructor bypass and migrate all supported callers. Prefer private
validated construction through the existing factory; keep the read-only API,
builder detachment, non-assignability and last-good replacement behavior. Update
tests that deliberately construct invalid published objects to the new supported
validation boundary. Do not merely hide this path in documentation.

### S2 / P1: CONSTANT_NEXT midpoint sampling changes exact key values

Sources: `Engine/Animation/src/FbxCurveBaker.cpp:268`;
`Engine/Animation/src/AnimationClip.cpp:24`.

The new baker stores the interval midpoint's value at the left endpoint. This
matches constant-next *inside* the open interval, but changes the source value
at the key itself. A previous-value STEP channel cannot express both semantics
by shifting values to the preceding key.

Using the checked-in `ConstantNext.fbx`, compared with `ufbx_evaluate_transform`:

| Local time | Source X | Baked X | Result |
| --- | --- | --- | --- |
| 0 | 2 | 5 | FAIL |
| 0.25 | 5 | 5 | PASS |
| 0.5 | 5 | 8 | FAIL |

The new regression checks only `t=0.25`, so it misses the two endpoint failures.
Represent exact and open-interval semantics correctly, or explicitly reject
constant-next as unsupported before publication. Do not use epsilon-offset keys
or silently change the first pose. Test endpoints and both sides of boundaries.

### S3 / P2: Distinct source keys that collapse to one float are still dropped

Source: `Engine/Animation/src/FbxCurveBaker.cpp:233`.

Representable 50-microsecond boundaries are now preserved. The deduplication
still silently merges different source times when their float timestamps match.
That is an unrepresentable input contract, not a redundant source key.

Reproduction: derive the stepped fixture with tick times
`{0,23093079000,23093079200}` and values `{2,5,8}`, keeping the take end at one
second. The last two times differ by about 4.3 ns near 0.5 s, but both round to
the same float. Import succeeds with no diagnostic instead of rejecting the
collapsed source boundaries.

Distinguish duplicate source times from float conversion collisions. Reject
unsupported precision loss transactionally; do not merge different authored
holds. Audit equivalent continuous-key and emit paths for silent skipping.

### S4 / P2: NaN bake configuration passes validation and publishes bad output

Sources: `Engine/Animation/src/FbxCurveBaker.cpp:164`, `:170`.

Checks use only `<= 0`, which does not reject NaN. On the linear `HelperSkin.fbx`
track, passing `MinIntervalSeconds = quiet_NaN()` makes seed filtering collapse
to a single endpoint. `BakeChannel` returns true, clears the caller's old channel
and consumes budget despite invalid configuration.

Reproduced: success=true, empty diagnostic, one emitted position key. The
expected behavior is failure with the prior channel and budget unchanged.

Validate finiteness/ranges before inspecting curves or allocating seeds. Bound
probe/depth settings and use overflow-safe work/count arithmetic. Positive-only
checks are insufficient for floating-point tolerances and interval settings.

## Accuracy claim still needs qualification

`FbxCurveBaker.hpp:16` calls the mechanism a continuous accuracy guarantee.
The known linear-spin alias is now covered, but a finite probe grid alone does
not establish a bound between probes. The rate helper is also source/layer and
curve-policy dependent. This review does not claim to have disproved every
supported continuous-curve case; the contract needs a stated supported subset
and evidence, or an honest sampled-validation description. Do not replace one
unsupported universal guarantee with another.

## Verification

- Release `AxleAnimationUnitTests` build: PASS.
- Registered `AnimationUnitTests` and expected `AnimationHarnessFailure`: 2/2 PASS.
- Supplementary production-API probe: exit 1, six failing expectations in S1-S4.
- Known mesh assignment, representable close STEP and constant-next interior
  controls in the probe pass; these are not reported as unfixed.
- Release `AxleTests` build including Editor/Runtime dependencies: PASS.
  Existing linker warnings remain; this is not a warning-free-build claim.
- Native animation UI, new animation RHI pixels and standalone packaged playback
  were not tested in this review.

Local ignored evidence:

- `build/animation-a3-a2-rereview-build.log`
- `build/animation-a3-a2-rereview-ctest.log`
- `build/animation-a3-a2-rereview-editor-build.log`
- `build/animation-a3-a2-rereview-probe-results.log`
- `build/AnimationA3A2RereviewProbe.cpp`
- `build/animation-a3-a2-rereview-fixtures/CollapsedSteps.fbx`

The probe creates no device/window and evaluates production APIs against ufbx.
Temporary CMake injection was cleared after execution. Promote these behaviors
into permanent registered regressions; ignored review artifacts are not a test
suite and may be absent in another checkout.

## Next assignment

See [historical agent archive](../../../agents/archive/README.md) for archived context.

1. Fix S1-S4, preserve the previous regressions, correct acceptance/evidence, and
   run the available CPU checks. This is a mandatory gate.
2. Only after that gate passes, implement **G1 device-free clip/controller
   documents**: revision identity, transactional Undo/Redo, guarded lifecycle and
   atomic persistence. G1's declared dependencies are B2/B3, already implemented.
   This deliberately takes the CPU document slice before the GPU/UI wave.
3. Stop for review. Do not start G2 preview, a panel/dopesheet/graph, RHI rendering,
   events/blend spaces or another phase. Full Phase 40 remains open; A2 exporter
   and broader accuracy acceptance are not closed by adding documents.
