# Animation foundation: scoped Gemini continuation

Date: 2026-11-03. Reviewed code baseline: `30d1e91`.
Assignment: **two work packages only: remaining A3, then remaining A2.**
Stop after these packages and their report. Do not implement the rest of Phase 40.

## 1. Read this before changing code

1. Inspect current HEAD and `git status`; preserve any newer user changes.
2. Read [Foundation Corrections](ANIMATION_PHASE_40_CORRECTIONS.md).
3. Read the Phase 40 section in [ROADMAP.md](../../ROADMAP.md).
4. Consult the original [completion plan](ANIMATION_PHASE_40_COMPLETION_PLAN.md)
   and [coding guide](ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md) for the
   two assigned packages only. Their full-phase instructions do not expand this
   assignment. Actual current headers take precedence over illustrative APIs.

The [original review](ANIMATION_PHASE_40_GEMINI_REVIEW.md) describes `a6efa8c`.
Its nine finding groups were subsequently corrected. Do not treat its old
observations as proof that the current implementation is still broken, or
replace the corrections with the earlier Gemini implementation.

## 2. What Codex already corrected

These are local commits already present in the baseline, not work to recreate.

| Commit | Result |
| --- | --- |
| `cc8b474` | Canonical typed parameter/trigger storage by stable ID. |
| `6dc50dc` | Safe clone policy, authored entry/reset and per-cycle exit gates. |
| `093b348` | Lossless bind matrices, validated publication and rig/pose compatibility. |
| `6a0d584` | Validated cooked geometry v2, submeshes, materials and bounds. |
| `e98df13` | glTF STEP/cubic interpolation, signed bind TRS and helper ancestors. |
| `9475c7e` | Executable controller compilation, stable identity, keyed clip/rig fallbacks. |
| `c77908f` | Shared scene/assembly resource binding and complete atomic saves. |
| `525b167` | FBX helpers, normals, UV seams, materials and canonical mesh basis. |
| `fdc7557` | Canonical component accessors and deterministic serialized overrides. |
| `56d6d31` | Additional definition/matrix validation and standalone clone rig ownership. |
| `5970895` | Real cooked dependency graph loading through a mounted VFS package. |
| `86b573c` | Hidden GL contexts for legacy rendering regressions. |
| `30d1e91` | Verified correction evidence and explicit remaining acceptance. |

Preserve these contracts:

- Names and IDs resolve to one declared runtime value. A trigger is consumed once.
- Wrong-type/nonfinite writes fail; invalid conditions do not silently pass.
- Opaque predicates cannot safely be cloned or saved; retain the diagnostic.
- Controller/state/transition IDs, graph layout, typed rules and actual keyed
  motions survive save/reopen. Missing clips must not become bind-pose success.
- Rig compatibility covers the full bind basis and explicit signed TRS fallback.
- Geometry/asset decoding and import publish validated candidates, keeping the
  previous valid output on failure. Failed saves preserve the existing file.
- Scene and both assembly paths use `AnimationSceneBinding`; respect `IsUnique`,
  owner-relative dependencies, fresh snapshots and stable-ID override precedence.
- Resource reads use `FileSystem` so mounted cooked packages continue working.

## 3. Existing evidence and its limits

Codex verified the baseline, in Release, on Windows with an RTX 3070:

- Editor, Runtime and the relevant test targets built successfully.
- All six registered CTests passed, including physical DX12/Vulkan **VFX** tests.
- Legacy animation Steps 36-41 passed; rendering steps used hidden OpenGL contexts.
- Permanent CPU tests verified actual keyed skinning after scene/assembly and
  mounted-package round trips, not just names or serialized metadata.

See `build/animation-fix-final-build.log`, `build/animation-fix-final-ctest.log`
and `build/animation-fix-legacy-36.log` through `build/animation-fix-legacy-41.log`
when available locally. These build artifacts are ignored and may be absent in
another checkout. Re-run the checked-in tests for new evidence.

These results do **not** establish new animation DX12/Vulkan pixel acceptance,
interactive Editor authoring, application pre-cook or standalone packaged
animation playback. Gemini must not claim those checks ran in its environment.

## 4. Package 1: finish A3 mesh-resource publication

### Problem to solve

Runtime rigs/motions are independently cloned and compiled definition metadata
is immutable. Published mesh data still exposes mutation through
`SkeletalMesh::GetData()` and a mutable `shared_ptr<Skeleton>` reachable inside
the data. Shared entities can therefore change one another's authored resource.
Making only the outer pointer `const` does not freeze the nested rig or detach
aliases retained by the builder.

### Implementation flow

1. Trace `SkeletalMesh`, `SkeletalMeshData`, `Skeleton`, `Animator`, importers,
   `AnimationResources`, `AnimationSceneBinding`, component copies and renderer
   callers. Separate editable CPU draft data from the published resource.
2. Keep import/build drafts mutable. At publication, normalize only where the
   existing contract permits it, validate, and create an owned immutable snapshot.
   An invalid load/replacement must keep the old published resource alive.
3. Expose read-only vertices, indices, ranges, materials, bounds and rig data to
   consumers. Do not leak a mutable rig through a const container. Detach any
   retained mutable builder aliases, including the original skeleton pointer.
4. Keep runtime clocks, parameters, poses and palettes per animator. If adapting
   legacy mutable animator rig APIs, use explicit independent copies/adapters;
   do not cast away resource constness or silently share mutable runtime state.
5. Make edits produce a new validated resource and explicit replacement. Keep
   scene/assembly/VFS snapshots and legacy GL callers working. GPU upload caches
   may be internally mutable, but upload must not mutate published CPU geometry.
6. Use the smallest acyclic change to the existing types. A new global cache,
   resource manager rewrite, RHI renderer or Editor panel is not required here.

Choose names after inspecting current code; this is a required ownership flow,
not a claim that a `Publish`/`Freeze` API already exists:

```text
mutable draft -> validation -> owned immutable mesh + rig snapshot
                                    |
                           shared read-only resource
                                    |
                       independent animator runtime instances
```

### Required CPU tests

- Two entities may share the same published mesh but cannot mutate its geometry
  or rig through their public resource API. Add compile-time API checks where useful.
- After publication, mutate the original draft vectors and original skeleton
  alias; the published resource, signature and sampled skinning remain unchanged.
- Entity duplication and `Scene::Copy` keep clocks, parameters, triggers, poses
  and legacy mutable motion snapshots independent.
- A failed edit/replacement keeps the last good resource, compatible palette and
  diagnostic. Destroying a builder/source does not invalidate live instances.
- Existing scene, spawned assembly, assembly document and package tests pass.
- Publication and these tests require no window, GPU device or GL context.

Suggested commit: `refactor(animation): enforce immutable skeletal resource publication`.

## 5. Package 2: finish the scoped A2 FBX baking gap

### Problem to solve

`FbxSkeletalImporter.cpp` currently samples local TRS uniformly at 30 Hz, with
duration/key limits. That limits work and allocation, **not interpolation error**.
Existing FBX fixtures cover linear motion and geometry/basis behavior; they do
not prove preservation of sharp, stepped, nonlinear or fast rotational motion.

### Implementation flow

1. Keep geometry extraction and helper/cluster/bind-basis corrections intact.
   Isolate the FBX curve-baking policy in a small device-free helper as needed.
2. Inspect the embedded `ufbx.h` and actual source interpolation/key data. Specify
   supported source curves and explicit tolerances for translation in meters,
   quaternion angular error in radians, and scale error. Handle quaternion sign
   equivalence when measuring rotation error.
3. Seed sampling with clip boundaries, relevant source key times and known
   discontinuities. Respect nonzero take start times and helper-node animation.
4. Preserve STEP discontinuities explicitly. Do not turn a hold into an ordinary
   linear ramp, or rely on epsilon-separated keys that collapse to one float time.
   Mixed interpolation must be represented correctly or rejected with diagnostics.
5. Refine continuous intervals against the runtime's actual linear/SLERP sampling.
   Check off-grid interior times, not only emitted keys. Bound sample/evaluation
   count, recursion, minimum representable interval and duration. Never silently
   accept a clip after a budget is exhausted while error remains above tolerance.
6. State exactly what the error guarantee means for the supported source subset.
   A few midpoint probes are not a mathematical bound over an arbitrary curve.
   Use source-aware extrema/bounds where available; diagnose unsupported cases.
   If only a validation-grid guarantee is achieved, describe it honestly and keep
   stronger continuous-error acceptance open rather than declaring A2 complete.
7. Publish complete validated channels only on success. Preserve the previous
   mesh, rig and clips on any failed bake. Reuse existing clip/codec semantics.

### Required CPU tests

- Retain `HelperSkin.fbx`, `ConvertedSkin.fbx` and all glTF importer regressions.
- Add tiny fixtures for a stepped track, a nonlinear/fast track that fixed 30 Hz
  misses, signed scale, quaternion rotation, animated helpers and nonzero take start.
- Compare production clip samples with the ufbx source evaluator at deterministic
  times not used as emitted keys; test exact hold boundaries and both interval ends.
- Exercise budget exhaustion, unrepresentable/unsupported input and last-good
  outputs. Increasing a hardcoded frame rate is not a solution to this package.
- For broader exporter coverage, record fixture provenance/license and the actual
  exporters represented. Do not fabricate a multi-exporter claim from generated
  fixtures. Missing legitimate exporter samples remain an explicit acceptance gap.

Suggested commit: `fix(animation): bake FBX curves with explicit accuracy and work limits`.

## 6. Validation, reporting and stop condition

Use the current configured build or an isolated compatible build directory. Do
not replace another platform's CMake cache merely to make these commands run.

```powershell
cmake --build build --config Release --target AxleAnimationUnitTests --parallel 6
if ($LASTEXITCODE -ne 0) { throw "Animation build failed" }
ctest --test-dir build -C Release -R '^Animation' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Animation tests failed" }
```

Only run available CPU/build checks. Preserve the permanent always-active
assertions and CTest failure propagation. Missing SDK/compiler/GPU/UI means
**NOT RUN**, not PASS or permission to remove a test.

Create a separate local commit for each package, including its production code,
tests and documentation. A small final documentation commit may record both
implementation hashes. Do not push. Add
`docs/design/ANIMATION_FOUNDATION_A3_A2_GEMINI_DELIVERY.md` containing:

- Baseline/commit hashes, changed files and ownership/baking contracts.
- Exact commands, exit codes, test results and unavailable checks.
- Error tolerances, unsupported cases, fixture provenance and remaining limits.
- Any migration implications and the precise A2/A3 checkboxes justified by evidence.

**Stop after these two packages and that delivery report.** Do not start D/E
blend spaces/events, F rendering, G/H/I Editor UI, J application cooking, K GPU
harnesses, or another phase. Do not close all of Phase 40. Its later packages
require a new explicit assignment. Phase 38.3/39 manual acceptance stays open.
