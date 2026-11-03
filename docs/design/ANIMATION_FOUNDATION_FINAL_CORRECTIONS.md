# Animation foundation: final corrective implementation

Date: 2026-11-03. Starting code/document baseline: `3f9da1d`.
Follow-up to [the second review](ANIMATION_A3_A2_SECOND_REVIEW.md).

| Finding | Correction and evidence |
| --- | --- |
| S1 | Mesh construction is private and factory-only. The factory detaches/rebuilds the rig, normalizes the supported draft, validates geometry, derives bounds and publishes once. GPU buffers remain lazy. All CPU/scene/legacy GL callers use the factory. Compile-time bypass checks and invalid index/rig/attribute tests PASS; Release Animation CTests 2/2 PASS. Logs: `build/animation-final-publication-build.log`, `build/animation-final-publication-tests.log`. |
| S2 | Added append-only `StepNext` interpolation, preserving exact key values and next-value open intervals for translation, quaternion and scale tracks. FBX classification keeps previous/next directions distinct and rejects genuinely mixed directions. Adaptive checks use each property's actual interpolation. Exact-source boundary and actual text codec round-trip tests PASS; Release Animation CTests 2/2 PASS. Logs: `build/animation-final-step-next-build.log`, `build/animation-final-step-next-tests.log`. |
| S3 | Mandatory source boundaries are kept independently of the adaptive grid; distinct times collapsing to one runtime float fail transactionally. Refinement cannot silently skip collapsed timestamps. Animation-only numeric formatting uses round-trip float precision, retaining adjacent timestamps, values and bind signatures through real asset text. STEP/continuous collision fixtures and adjacent-float codec tests PASS; Release Animation CTests 2/2 PASS. Logs: `build/animation-final-time-precision-build.log`, `build/animation-final-time-precision-tests.log`. |
| S4 | Bake settings validate finite/positive numeric values and bounded probes, depth, output keys and work before use. A shared per-stack work budget bounds source inspection/evaluation; grid arithmetic is checked before integer conversion. Invalid source samples, bindings, counter overflow and exhausted budgets fail transactionally, including stack stats. Quaternion distance uses double intermediates. Accuracy documentation describes sampled validation honestly. Permanent NaN/infinity/range/work/rollback tests PASS; Release Animation CTests 2/2 PASS. Logs: `build/animation-final-bake-limits-build.log`, `build/animation-final-bake-limits-tests.log`. |

## Commit manifest

| Commit | Scope |
| --- | --- |
| `6faa235` | S1: private validated mesh publication, bounds and caller migration. |
| `ce8251b` | S2: exact constant-next interpolation and text round-trip regressions. |
| `94254ba` | S3: source timestamp collision rejection and float-precise animation codecs. |
| `96ea6f7` | S4: finite/ranged settings, source work limits, transactional stack stats and validation tests. |

## Final integration evidence

- Release build PASS (exit 0): `AxleEditor`, `AxleRuntime`, `AxleTests`, Animation/Material/VFX CPU targets and VFX GPU target.
- CTest: 6/6 PASS, including actual physical DX12/Vulkan **VFX** regression tests.
- Legacy animation Steps 36-41: 6/6 PASS. Rendering-related steps use hidden OpenGL contexts.
- Final logs: `build/animation-foundation-final-build.log`, `build/animation-foundation-final-ctest.log`, `build/animation-foundation-final-legacy-36.log` through `build/animation-foundation-final-legacy-41.log`.
- Permanent production-API regressions cover the six failing expectations from the second review. Mesh bypasses are now impossible through the public constructor; compile-time checks enforce that invariant instead of trying to instantiate invalid published objects.
- Shader/driver/linker warnings are not represented as a warning-free build. New animation RHI pixel acceptance, native authoring UI and standalone packaged application playback were not tested here.

## Supported contracts and compatibility

`AnimationInterpolation::StepNext` is appended as numeric value 3; existing Step,
Linear and CubicSpline values remain 0/1/2. A next-value track keeps the authored
value exactly at a key and holds the next value strictly inside the interval.
The codec persists the mode, timestamps, tangents and values at round-trip float
precision. Assets using the new mode require the updated engine/runtime.

The FBX importer supports one interpolation direction per authored TRS property;
unsupported mixed previous/next/continuous directions fail explicitly. Distinct
mandatory source boundaries that collide in runtime float precision are rejected.
Optional grid samples cannot replace mandatory source evaluation timestamps.

Bake tolerances and minimum interval must be finite and positive; minimum interval
is at most 3600 seconds. Probe count is 1-64, depth 0-24, output key budget 3 to
3,000,000, and source work budget 1 to 8,000,000 (default 2,000,000). Work includes
curve/layer inspection and transform evaluations, shared across the stack. Error
stats describe accepted validation probes, not a universal bound between probes.
Failure does not publish a partial channel, consume caller key budget or publish
partial stack stats. Geometry/rig builders remain mutable before publication;
all supported resource construction then validates and freezes an owned snapshot.

S1-S4 are corrected. A3's scoped publication/isolation acceptance is verified.
A2 remains OPEN for broader weighted/layered/extrapolated source behavior, stronger
accuracy guarantees and legitimate multi-exporter fixtures. Finite sample probes
are not advertised as proof for every possible source curve. Phase 38.3/39 manual
acceptance and full Phase 40 completion remain separately OPEN.

## Gemini continuation

[The current scoped prompt](ANIMATION_GEMINI_NEXT_SCOPED_TASKS.txt) assigns **G1
CPU document models only**. The correction gate has already been completed by
Codex. Gemini should implement revision/history, guarded lifecycle and persistence
with CPU tests, commit that work, and stop for review. No renderer, visible panel,
dopesheet, graph, preview, application cooking or other phase is assigned.
