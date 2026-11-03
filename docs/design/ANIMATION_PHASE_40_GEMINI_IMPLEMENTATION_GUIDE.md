# Phase 40: Animation Coding Guide for Gemini

Date: 2026-11-03. Inspected baseline: `538163b`.
Status: **implementation handoff; proposed interfaces, not shipped APIs**.

Read the [completion plan](ANIMATION_PHASE_40_COMPLETION_PLAN.md) first. Its
A0-K3 IDs, dependency table, scope limits and acceptance gates are canonical.
This guide supplies code structure, algorithms, integration flows and fixtures.
All example names/signatures below must be implemented/adapted, not assumed to
exist. Do not start a parallel animation engine beside the existing one.

## 1. Assignment and working rules

Implement Phase 40 production code and meaningful CPU tests, one coherent package
per local commit. Gemini's environment can code and execute CPU unit tests;
physical GPU and interactive UI verification are external responsibilities.
Implement those real integration test entry points even when you cannot run them.

- Use C++20 and English for code, comments, docs and commits. Do not push.
- Inspect `git status`, current HEAD, CMake and touched callers before edits.
  Preserve user changes, their scenes, materials, rigs and default assets.
- Never mark a package tested because it compiles, nor a renderer accepted because
  a texture handle, shader bytecode, CPU palette or draw-call count exists.
- Record exact commands/exit status. Missing compiler/device/UI is **NOT RUN**.
  Known baseline warnings are reported; do not claim a warning-free build falsely.
- Keep Phase 38.3/39 manual acceptance open. The Phase 40 handoff does not close
  those phases or authorize a rewrite of their production paths.
- Add tests with each behavior change, not only at the end. Prefer production
  APIs and tiny fixtures over duplicated test-only evaluators or source-text grep.
- No runtime dependency on an editor panel; no Animation -> UI/Scene dependency;
  no Renderer/Asset -> Animation dependency cycle. Existing debt is not a template
  for adding another reverse dependency.

Before each commit: inspect the diff, run the relevant CPU suite and available
build checks, update the implementation status with remaining external gates,
and use the proposed subject in the plan or an equally specific English subject.
If unavailable checks prevent acceptance, commit an honestly scoped implementation
and keep its acceptance checkbox pending. Do not erase the checks.

## 2. Repository map and extension seams

| Existing source | Responsibility in this work |
| --- | --- |
| `Engine/Animation/include/Axle/{Bone,Skeleton,AnimationClip,AnimationPose}.hpp` and matching `.cpp` | Rig topology, bind TRS, channel sampling, FK, palettes; extend existing types |
| `Engine/Animation/.../AnimationStateMachine.*`, `Animator.*`, `BlendSpace1D.*` | Definition-instance separation, legacy API adapters, typed state evaluation and phase clocks |
| `Engine/Animation/.../SkeletalMeshImporter.*`, `SkeletalMesh.*` | CPU import/mesh normalization; eliminate implicit context requirement |
| `Engine/RHI/include/Axle/RHI/RHIVertexLayout.hpp` | Existing `RHISkinnedVertex3D`, stride 96, joint fields and seven-attribute helpers |
| `Engine/RHI/.../RHIBuffer.*`, `RHIDescriptorPool.*`, `RHIStreamer.*` | Low-level upload/allocation primitives; no knowledge of clip/controller assets |
| `Engine/Scene/.../RHISceneRenderer.*`, `RHIObjectPicker.*`, `MaterialTextureCache.*` | Gather skeletal entities, skinned material draws, ID pass and resource lifetime |
| `Engine/Renderer/.../SkinningPipeline.*`, `Engine/Scene/src/SceneRenderer.cpp` | Preserve legacy OpenGL color/shadow pipeline; isolate its upload bridge from CPU evaluation |
| `Engine/Resources/Shaders/ForwardPBR.hlsl` | Standard material PS and vertex variant integration, rather than a second divergent PBR shader |
| `Engine/Scene/include/Axle/Components.hpp`, `Scene.cpp` | Animator binding/clone/runtime update, character parameter producer and queued event bridge |
| `Engine/Scene/src/{SceneSerializer,AssemblySerializer}.cpp`, `include/Axle/AssemblySerializer.hpp` | All persistence paths, including AssemblyNode editor data |
| `Engine/Scene/include/Axle/ScriptableEntity.hpp`, script reload/registry code | Narrow event virtual API and live-instance delivery; DLL rebuild and lifetime |
| `Engine/Asset/.../{AssetDSL,AssetTypes,AssetManager,AssetCooker}.*` | Kinds/classification, generic package integration and resource resolution |
| `Engine/UI/.../{ContentBrowserPanel,AssetInspector,EntityInspector,SceneHierarchyPanel}.*` | Explicit import/create/open/assign routes, searchable pickers and selection callbacks |
| `Editor/src/EditorLayer.*`, `Engine/UI/CMakeLists.txt` | Panel ownership, update/render/detach, docking and input priority |
| `Editor/src/Test.cpp`, `Editor/src/Tests/TestCommon.hpp`, `Editor/CMakeLists.txt` | Actual runner registration/build; Step 103 does not exist yet |
| `Engine/VFX/.../VFXEditorDocument.*`, `Engine/UI/.../VFXEditorPanel.*` | Read existing revision/preview lessons; do not couple Animation to VFX |

Suggested new sources, subject to an acyclic design:

- Animation: `AnimationDefinition`, `AnimationValidation`, `AnimationAssetCodec`,
  `AnimationResourceLibrary`, `AnimationEvent`, `BlendSpace2D` and an explicit
  controller instance helper if not kept inside AnimationStateMachine.
- Scene: `RHISkeletalMesh`, `RHISkeletalRenderer`, `AnimationSceneBinding` and a
  bone overlay render adapter. A separate narrow AnimationRendering target is
  acceptable if dependencies are stated and tested.
- UI: `AnimationEditorDocument`, `AnimatorEditorDocument`,
  `AnimationPreviewSession`, `AnimationTimelinePanel`, `AnimatorGraphPanel`,
  `SkeletonSelectionModel` and small pure edit/hit-test helpers.
- Shaders: `Skinning.hlsli`, plus shared skinned color/ID/depth vertex variants.
- Tests: `AnimationUnitTestMain.cpp`, `AnimationTestHarness.hpp`, focused
  `Test_Animation*Unit.cpp`, `Test_AnimationRendering.cpp`,
  `AnimationGPUTestMain.cpp` and shared acceptance declarations.

Avoid one enormous panel containing codec, graph evaluator, import and draw
submission. Reuse current classes through adapters; migrate callers in the same
commit that changes their contract.

## 3. Value definitions versus runtime instances (A1/A3/B1)

Use opaque stable string IDs for authoring objects. Bone runtime indices remain
compact integers, derived from a validated skeleton binding; graph node, motion,
parameter, transition, event and editable key identities are not vector offsets.
Display names and graph positions are not runtime identity.

Illustrative definitions:

```cpp
using AnimationId = std::string;
enum class AnimatorParameterType { Float, Int, Bool, Trigger };
using ParameterDefault = std::variant<float, int32_t, bool>;
struct ParameterDefinition {
    AnimationId Id;
    std::string Name;
    AnimatorParameterType Type;
    ParameterDefault Default; // Trigger has no persisted pending=true value.
};
enum class ConditionOperator { Greater, Less, Equal, NotEqual, IsTrue, IsFalse, Triggered };
struct ConditionDefinition {
    AnimationId Parameter;
    ConditionOperator Operator;
    ParameterDefault Operand; // Ignored for unary/trigger operators.
};
struct TransitionDefinition {
    AnimationId Id, FromState, ToState;
    std::vector<ConditionDefinition> AllConditions;
    float CrossfadeSeconds = 0.2f;
    bool HasExitTime = false;
    float ExitPhase = 1.0f;
    int32_t Priority = 0;
};
```

Supporting types are required for referenced motions, schema version, entry state,
skeleton compatibility, graph-node positions and diagnostics. Do not paste this
snippet as a complete definition. Choose explicit validation for default/operand
variants; a Float default cannot secretly hold Bool. Persist Int exactly.

The immutable compiled controller contains stable definition vectors and lookup
maps. Each instance holds its own parameter/trigger arrays, active state ID,
source/target time/phase, crossfade duration/elapsed, current pose/palette,
activation counter and event cursors. Immutable clips/rig/controller may be shared;
mutable runtime objects never are.

- Keep `Animator` as the main scene-facing facade and retain existing SetFloat,
  SetBool, SetTrigger names. Add SetInt/GetInt. Script-name lookup resolves a
  validated unique parameter name to an ID; graph rules bind the ID.
- Remove the data-model dependency on `SkinningPipeline.hpp` from CPU-facing
  headers where possible. Forward declare legacy types; put upload bridge code
  in a separate translation unit so evaluating an Animator never constructs GL.
- If preserving AddState/AddTransition legacy builders, compile their values into
  a definition with independent instance state. Runtime-only predicate adapters
  must be explicitly nonserializable and must not be used for authored assets.
- Replace internal shared-pointer-capturing lambda conditions in production
  defaults with typed rules. Do not use no-op-deleter shared_ptrs or deep-copy a
  closure that still targets the old state machine.
- Scene duplication/copy shares immutable resources and constructs a new runtime
  from authored component values. Reset at the entry state for authoring copies;
  do not copy transient triggers or advance the source to construct the copy.
- Runtime start rebuilds instances from authored values, pushes initial typed
  overrides once and samples at time zero. Stop clears event queues and runtime
  binding. Character controller parameter writes remain a runtime producer.
  Conventional Speed/IsGrounded/VerticalVelocity fields are compatibility adapters,
  not a second serialized authority. Restore legacy saved values before evaluation.

Validation must reject duplicate IDs, dangling links, nonfinite values, invalid
parameter/operator combinations, incompatible skeletons, invalid clip durations,
invalid key ranges and resource count limits. State transition cycles are valid;
this is not the acyclic sub-emitter graph from VFX. Motion recursion is excluded
because Phase 40 blend-space samples reference leaf clips only.

A3 can introduce a minimal immutable definition/typed-rule representation to
repair cloning before the complete B1/C1 schema exists. Extend that representation
in the later packages; do not introduce another controller data model. A generic
opaque closure cannot be safely rebound by copying it: production default rules
must migrate, and a legacy runtime-only predicate requires an explicit safe
recreation policy or a clear unsupported-clone diagnostic.

## 4. Skeleton and sampling invariants (A1/A2)

### Topology and spaces

Keep stable joint indices from import. Build a separate topological evaluation
order via DFS/Kahn validation; parent must be in range or -1, and cycles are an
error. Rebuild Children/root lookup from ParentID instead of trusting stale copies.
Handle multiple roots as a forest or a documented synthesized root with remapped
skin indices/IBMs. Do not sort the bone vector without remapping every joint index,
channel binding and inverse bind matrix.

For the canonical rig convention:

```text
local[i]       = local TRS for this bone, relative to its canonical parent
rigGlobal[i]   = rigGlobal[parent] * local[i]    (or local[i] at a root)
skinPalette[i] = rigGlobal[i] * canonicalInverseBind[i]
worldVertex    = entityWorld * sum(weight[i] * skinPalette[i] * meshVertex)
worldBone      = entityWorld * rigGlobal[i] * boneLocalOrigin
```

Importer canonicalization must bring mesh vertices, joint globals and inverse
bind matrices into that same rig/model basis. If retaining an explicit per-mesh
rig-to-mesh transform, include it consistently in palette evaluation and document
it. glTF mesh-node transforms and non-joint ancestors cannot just disappear.
Do not multiply entityWorld into the palette and then multiply it again in the
vertex shader. Validate the convention against a nonidentity mesh-node fixture,
not only a procedural rig at the origin. Preserve source inverse binds; do not
blindly recompute them from the currently sampled animation pose.

### Partial channels and quaternions

Initialize each sampled bone from its **local bind TRS**, then replace only
components with keys. A rotation-only channel on a child at `(0,2,0)` keeps that
translation and its bind scale. Matrix and AnimationPose sampling overloads must
use the same implementation, not two almost identical evaluators.

- Require finite, strictly increasing timestamps per component; duplicate-time
  edits use the collision policy in section 10. Imported duplicate samples need
  an explicit diagnostic/deterministic normalization policy.
- Empty component -> bind value. One key -> constant. Outside keys -> nearest
  endpoint after the clip time is clamped/wrapped. Zero-duration clips -> t=0.
- Linear translation/scale, shortest-arc normalized quaternion SLERP. Normalize
  valid imported quaternions; reject near-zero quaternion keys. Handle q and -q
  as the same orientation; never LERP Euler degrees as stored rotation.
- Use an explicit validated decomposition for signed TRS. The current column-length
  decomposition loses reflection sign. Preserve signed scale/handedness, verify
  recompose, reject singular/sheared transforms outside supported tolerance.
- Equal skeleton identity/topology and equal pose lengths are required for blends.
  Never silently truncate mismatched poses with `min(sizeA,sizeB)`.
- Cached channel indices/maps are derived. Edits/reload rebuild/validate them;
  do not hand widgets a mutable vector and expect caches to stay synchronized.

### Importer contracts

1. Return a CPU import result containing rig, mesh parts/material slots, takes
   and diagnostics. Clear/replace outputs transactionally; repeated import into
   a reused result cannot append old geometry/clips.
2. Bind primitives through their actual mesh node and selected skin. Preserve
   all relevant intermediary transform nodes and animated ancestors. Stable bone
   paths disambiguate repeated display names. Do not silently select skin zero
   for primitives bound to another skin.
3. Keep four strongest nonnegative finite influences, merge repeated joint
   influences, normalize, validate indices and select a diagnosed root fallback
   only for truly unweighted vertices. Repair missing normals/tangents in CPU
   preparation; handle nonindexed triangle primitives explicitly.
4. Decode glTF STEP/LINEAR/CUBICSPLINE according to sampler layout. Cubic outputs
   contain tangent/value/tangent triples; multiply Hermite tangents by interval
   seconds and normalize quaternion results. Alternatively, bake those curves
   adaptively with documented translation/scale/angular tolerances and a key cap.
   Preserve mode metadata for unbaked sources. Do not index cubic output as LINEAR.
5. FBX import uses the existing ufbx integration; sample/bake supported TRS takes
   in seconds with explicit axis/unit conversion and source time range. Report
   unsupported deformers/morph channels or unsupported mesh/skin combinations.
6. Generate cooked rig geometry and clips for runtime. Runtime package loading
   does not call cgltf_parse_file/ufbx_load_file on nonexistent loose files.
7. Make `SkeletalMesh` CPU construction safe; legacy BuildBuffers is explicit and
   backend/context guarded. Device-free tests must import and evaluate without
   creating any VertexArray, shader, window or RHIDevice.

## 5. Asset format, reload and scene round trip (B1-B3/J2)

Use unified `.axleasset` roots; append new AssetKind/AssetType values without
renumbering existing serialized enum values. Extend StringTo/From, PeekAssetKind,
metadata classification, picker filtering and Content Browser open routes.

Suggested roots and payloads:

| Root | Persisted data |
| --- | --- |
| Skeleton | Version/name/identity, stable bone paths, parents, local bind TRS, inverse bind matrices and compatibility signature |
| SkeletalMesh | Version, skeleton ref, cooked geometry ref, source import recipe, mesh parts/material slots and basis metadata |
| AnimationClip | Version/name/identity, skeleton ref/signature, duration seconds, component keys/modes and event markers |
| AnimatorController | Version/name/identity, skeleton ref/signature, entry state, parameter definitions, states/motions, transitions and editor graph layout |

A source-imported take is immutable data selected by source identity/take key.
**Create Editable Copy** creates an independent clip with its own asset identity
and complete keys; it never writes back into FBX/GLB or mutates another asset's
shared AnimationClip. Keys/events/graph IDs remain stable across ordinary saves.
Save As/duplicate creates a new asset identity; saving the same file preserves it.

- One codec per value definition is used by standalone assets and inline scene
  fallbacks. Scene wrappers only encode the same codec result/path, not a sparse
  hand-maintained subset of controller fields.
- Runtime reads bytes/text using FileSystem and ParseSource, including mounted
  packages. Editor Save writes a sibling temporary file, flushes/closes, then
  atomically replaces the destination. Failure retains old bytes, path and dirty
  revision. Reuse/extract the existing proven atomic-replace pattern without
  creating an Animation -> VFX dependency.
- Serialize floats with round-trip precision, quaternions in an explicitly stated
  order, typed integer values losslessly, arrays/matrices with fixed documented
  element counts, UTF-8 and escaped names/paths. No absolute workspace paths.
- Geometry cache is internal versioned binary data referenced by the SkeletalMesh
  asset, not another editable authoring format. Include magic/version, sizes,
  bounded counts, vertex/index/material ranges and skeleton signature. Encode
  fields explicitly; never dump GLM/std::string/pointer-containing structs.
  Reject overflow, truncated buffers, mismatched signatures and invalid indices.
- Rig/clip/controller resolution is transactional: validate all required data,
  then publish an immutable compiled bundle. Missing/incompatible resource keeps
  the last-good bundle on reload. A fresh unresolved component stays diagnosed
  in bind pose/disabled state without becoming an unrelated procedural biped.
- Keep the legacy `procedural://biped` explicit generator. Migrate the old empty
  path behavior and its default Idle/Walk controller intentionally. Legacy raw
  FBX/glTF scene references use the compatibility import adapter on loose files;
  pre-cook converts them to complete packaged data. Do not discard imported takes.
- Authored component fields: rig/controller refs, complete inline fallback or
  unique controller data, Playing, PlaybackSpeed and typed initial overrides.
  Do not serialize live timers, pending triggers, script pointers or GPU handles.
- Valid references resolve the current source. Missing references use the complete
  validated fallback where stored; otherwise show a diagnostic/bind pose. Make
  Unique creates a value copy and removes shared-reference authority. No hidden
  sparse overrides over arbitrary graph state.
- Update `SceneSerializer` AND direct assembly Serialize/Deserialize AND
  AssemblyNode LoadAssemblyData/SaveAssemblyData. An assembly editor save must
  not silently strip animator data. Instantiating twice creates two runtimes.
- Pre-cook preparation belongs at the Animation/Scene/Editor integration layer.
  If AssetCooker needs a hook, pass generic generated paths/bytes through a narrow
  extension. Do not make generic AssetCooker include Animation headers. Validate
  every referenced cooked dependency before packaging. Test reading the package
  after source media and authoring paths are made unavailable in a temp fixture.

## 6. Typed transitions and time (C1/C2)

Compile conditions to parameter indices once. Authoring uses stable IDs; live
script names are resolved through the immutable definition's validated name map.
All rules in a transition are AND; an empty rule list is unconditional, subject
to exit-time gating. Restrict numeric operators to numeric types, Bool to equality
or unary truth, Trigger to Triggered. Unknown/mistyped parameter writes produce
an actionable diagnostic and leave values unchanged.

Transition selection is deterministic: greater Priority first, then authored
transition order. Do not iterate an unordered_map to choose the next state.
Use separate **peek** and **consume** trigger operations:

```text
for candidate in deterministic order:
    if exit gate is not crossed/eligible: continue
    if any condition is false using a read-only parameter snapshot: continue
    select candidate
    consume exactly the trigger parameters used by the selected candidate
    commit transition
    break
```

A failed high-priority candidate cannot consume Jump before a later valid
candidate checks it. Repeated SetTrigger before an update coalesces to one pending
bit; a pending trigger remains until used or explicitly reset. Stop/restart clears
pending triggers. Do not serialize pending bits as parameter defaults.

Time contract:

- dt must be finite and nonnegative; playback/state speed finite and nonnegative.
  Runtime reverse is unsupported, explicitly rejected. dt=0 evaluates pose without
  time advance, transition side effects or event delivery; start/reset uses an
  explicit entry operation rather than abusing Update(0).
- Seconds are canonical. Clip FPS metadata never multiplies TimeStamp values.
- Maintain double-precision unwrapped time/phase for logic; sample wrapped/clamped
  float clip time. A nonloop clip clamps visibly at its last pose.
- ExitPhase is normalized relative to activation. For a looping state with a
  threshold below 1, eligibility is a crossing of that threshold in a cycle;
  threshold 1 means a cycle boundary. Validate thresholds in [0,1]; zero is
  eligible on the first positive advance after activation and subsequent loop
  boundaries. A nonloop threshold is crossed once and remains eligible afterward
  so a condition can become true later while the last pose is held.
  Store interval endpoints, split at crossing boundaries and evaluate the gate
  with the same parameter snapshot for that advance. An unconditional transition
  with no exit gate is eligible at the start of a positive update.
- Crossfade source and target have independent clocks, speeds and blend phases.
  Start the target at phase zero. Blend local TRS with alpha clamped from elapsed
  wall-playback seconds / CrossfadeSeconds; never blend palette matrices directly.
- Split a dt at exit/crossfade boundaries and advance the residual time in the new
  state. Noninterruptible fades are intentional. Immediate transitions use a
  bounded hop count (e.g. 16 per advance); diagnose zero-duration cycles without
  hanging. Carry elapsed time correctly rather than dropping the residual dt.
- Specify and test gate-at-update-end behavior: a crossing exactly at the end
  begins the target at t=0; the next positive update advances it. Do not retroactively
  emit target events from before activation. Test partitioned dt with fixed inputs.

Retain script-requested state changes as explicit runtime commands, validated
against state IDs. Reset of the current state must be explicit; calling an ordinary
SetCurrentState to the same state must not unexpectedly rearm event markers.

## 7. Blend spaces (D1/D2)

Return a pure `BlendWeightPlan` from parameter values and a validated point set.
It contains stable sample IDs, finite nonnegative weights summing to one, and an
effective cycle duration. Runtime sampling and UI visualization consume the same
plan. Do not compute different weights in the widget and runtime evaluator.

### 1D

Sort points by coordinate, use the bounding pair and linear weights, clamp outside
the endpoints. Reject coincident coordinates with conflicting samples; exact
sample positions select that sample. Handle 0 points as diagnosed bind-pose
fallback and 1 point as full weight. Keep the existing BlendSpace1D public adapter.

### 2D

Implement a bounded deterministic triangulation of unique sample positions
(e.g. Bowyer-Watson with sorted stable-ID insertion and a documented epsilon).
Topology is rebuilt on definition edits, not for every animated entity each frame.
Use double for orientation/circumcircle predicates; deterministic tie handling for
cocircular points is mandatory. Enforce a reasonable point cap (e.g. 128).

- Inside a triangle: barycentric weights for its three samples.
- On a shared edge: equivalent continuous weights, deterministic triangle choice.
- Outside the convex hull: project to the nearest hull segment and interpolate
  its endpoints. Resolve equidistant ties by stable IDs.
- One point: weight one. Two points: project onto their segment.
- Collinear point set: project onto a deterministic axis and use 1D interpolation.
- Duplicate/nonfinite points: reject edits transactionally with a diagnostic.
- Clamp only tiny numeric negatives and renormalize; a large negative/NaN signals
  invalid geometry, not permission to emit arbitrary weights.

### Pose and phase

For weights w_i and clip durations d_i, effectiveDuration = sum(w_i*d_i), with an
explicit safe case for zero-duration/static clips. Advance a persistent unwrapped
phase by dt * stateSpeed / effectiveDuration. Sample each looping leaf at
fractionalPhase * d_i; nonloop leaves use clamped phase. Changing Speed/direction
changes weights/duration without recomputing phase from absolute time/newDuration.

Blend translations/scales with weighted sums. For N quaternions choose a stable
reference (highest weight, stable ID tie), flip antipodal values into its hemisphere,
normalize the weighted quaternion sum; for exactly two samples use shortest-path
SLERP. Validate finite nonzero results and compare CPU reference poses. This is
single-layer pose blending, not foot locking/root motion or a guarantee of no slip.

## 8. Events and safe C++ delivery (E1/E2)

Event definition: stable ID, timestamp in seconds, name and typed payload
(Empty/Float/Int/Bool/String). Treat names as data, not arbitrary C++ source to
compile or reflection-based function invocation. A script implements the generic
hook and decides what PlayFootstep/SpawnSlashVFX/EnableHitbox means.

Proposed occurrence is a value: event ID/name/payload, controller/state/clip IDs,
activation ID, loop index and occurrence time. It owns strings, not clip pointers
or script callbacks.

### Required timing policy

- On explicit forward start/entry, a t=0 marker can fire once. dt=0 evaluation
  and paused pose inspection do not deliver it. Reset deliberately rearms it.
- For forward motion collect markers in `(previous,current]`. In a looping clip,
  marker time e occurs at k*duration+e. A duration marker belongs to the ending
  cycle; a zero marker belongs to the new cycle. Distinct authored markers at
  those positions may both fire in deterministic ID order. Do not duplicate an
  individual occurrence because intervals share an endpoint.
- Nonloop playback fires the final marker once when reaching duration; later
  clamped updates do not repeat it. Zero-duration/static clips may fire only
  explicit entry markers, not an infinite loop schedule.
- Seeking/scrubbing either direction and editing keys/events do not dispatch
  gameplay events. Clear/rebase cursors at the new sample position.
- Default crossfade policy: the source state is the sole event owner until the
  fade completes; target markers traversed during the fade are suppressed and
  never caught up later. After handoff, target traversal emits only newly crossed
  markers. Document this policy in UI/help rather than emitting duplicate footsteps.
- Inside a blend space, only the highest-weight leaf owns events, stable-ID tie.
  Changing owner rebases its cursor to the current phase; suppressed historical
  markers are not caught up. Pose blending still uses all leaves.
- Bound emitted occurrences (e.g. 256 per animator/update and a separate scene
  cap). Enumerate arithmetic loop ranges; do not iterate billions of missed loops.
  Advance/rebase to the actual final time after overflow, report dropped counts,
  and never postpone old events forever.

The runtime evaluator collects occurrences without calling gameplay. Scene finishes
the animator iteration, then dispatches from a bounded value queue. Each queued
recipient contains a scene-generation token and full versioned entity handle;
validate both, active runtime binding and current NativeScriptComponent instance
immediately before delivery. Do not cache a std::function/virtual-object pointer
from a hot-reloadable DLL. Hot reload rebuilds affected scripts for the new virtual
API and invalidates queued work for the old instance/generation.

Provide a safe scene-mutation boundary: callbacks requesting entity destruction,
scene changes or nested event delivery enqueue commands to be applied after this
dispatch batch, or use the existing safe equivalent if one exists. Never mutate a
live registry iteration from an animation callback. Stop/scene replacement clears
the pending queue. Test deletion/reload/stop between collection and delivery.

Private previews use the same occurrence collector with **log-only delivery**,
never the user's scene dispatcher, audio/VFX services or scripts. Default clip
scrub displays marker positions; it does not simulate gameplay history.

## 9. RHI skinning, materials and draw ownership (F1-F3)

### Mesh upload and deformed bounds

Use the existing 96-byte `RHISkinnedVertex3D` and layout helpers. Convert CPU
SkeletalVertex explicitly, generate tangents, preserve material submesh ranges and
normalize validated weights. Negative sentinel joint IDs with zero weight map to
safe zero indices; a positive-weight invalid joint is an asset error. Do not cast
-1 to an unsigned index and rely on out-of-bounds GPU reads.

A typed Scene-side mesh wrapper or a type-safe RHIMesh upload overload may reuse
buffer upload primitives. Do not force skinned data through the static 64-byte
RHIMesh API. Cache immutable geometry by asset identity/content generation;
per-entity palettes remain separate. Context-free CPU objects do not own GL VAOs.

Bind-pose bounds alone are insufficient for animated culling. Derive conservative
per-joint influence bounds and union their current transformed bounds, or provide
another proved conservative method. Normalized nonnegative LBS weights permit a
union AABB enclosing the influence contributions. Include entity/parent transforms
once. A moving limb outside the original bind AABB must remain drawable/pickable.

### Palette contract and standard shader variant

At the inspected baseline standard b0-b3/space0 are already occupied. A concrete
proposed skinned-only addition is **b4, space0** with a matching root CBV entry:

```cpp
struct alignas(256) SkinningConstants {
    glm::mat4 Bones[128];
    glm::uvec4 Meta; // x = validated bone count; yzw reserved zero
    glm::vec4 Reserved[15];
};
static_assert(sizeof(SkinningConstants) == 8448);
```

Recheck actual registers at implementation time. Define the matching HLSL layout
and descriptor/root index explicitly; neither b4 nor an API root index appears by
magic. Do not overwrite the material CB, object constants, terrain weights or
particle constants. Never use 8448 bytes as root constants; bind an aligned CBV.

- Build a skinned vertex variant using the standard ForwardPBR material pixel
  path and shared skinning helper. Static draws keep their own valid layout.
- Position uses the normalized weighted palette. Normals use a guarded normal
  transform for the resulting linear skin transform followed by the entity normal
  transform; tangents are transformed/orthogonalized and reflection handedness
  is preserved. Implement the required cofactor/inverse-transpose helper rather
  than assuming an unavailable HLSL inverse intrinsic. Define a finite fallback
  for a near-singular blended linear transform. Compare signed/nonuniform fixtures.
- Preserve the standard texture/color-space/cull/surface/UV/material behavior.
  Unsupported custom vertex interfaces receive a diagnostic and a built-in
  fallback; arbitrary custom PSO layouts are not automatically skinned-compatible.
- The entity-ID pass uses **the same skin positions and palette generation** as
  color. Preserve existing entity-ID encoding/version checks. CPU bone picking
  is separate and must not encode unbounded bone IDs into entity ID bits.
- Integrate with active color/depth formats, MSAA and HDR/tone mapping. Reuse the
  scene/private-preview render path, not an OpenGL-only preview implementation.
- Preserve legacy OpenGL skinned shadows. A new RHI shadow-map feature is excluded
  if that renderer still has no shadow pass; record it as a limitation honestly.

### Lifetime and state

Allocate palette/object/material data per recorded draw (or an immutable frame
arena slice with validated alignment and synchronization). Two entities cannot
reuse a host buffer that is overwritten before the recorded command list executes.
The same applies to descriptor sets and viewport generations. Reuse a palette
across submeshes only while its binding/coordinate basis actually matches.

Retain uploaded mesh/palette/resources until submission completes. Growth/resize,
material reload and preview close cannot free in-flight resources. Restore correct
static/sky/particle pipeline layouts after a skinned pass. Add tests with reversed
entity order; a test containing only one animated draw does not catch aliasing.
Do not add QueueWaitIdle per animated draw to conceal ownership bugs.

## 10. Documents, preview and dopesheet (G1-G3)

A document is the authoritative editable value model, separate from its compiled
preview resource and from the selected scene entity. Use monotonically unique
revision tokens and a saved revision token, not undo-stack depth as dirty identity.
A branch after Undo can have the same depth and different content. Save only marks
the revision persisted after successful file replacement.

Commands contain before/after edited data and stable selections, with bounded
history memory. One gesture begins a transaction, updates a working preview and
commits once on release; Escape restores the before state with no history entry.
No-op commands create neither history nor dirty changes. Open/New/close use
Save/Discard/Cancel against a concrete pending operation; a failed Save cancels
that operation and preserves the working document.

### Preview ownership

AnimationPreviewSession owns a separate animator and private scene/render target.
Immutable rig/clip assets may be shared. It never runs the user's Scene update,
scripts, character controller or physics, and it never changes scene entity TRS.
Scene selection changes only rebind a context key on actual change; do not reload
or reset the document every EditorLayer::OnImGuiRender frame. Detach scene bindings
before scene destruction; dirty standalone documents can remain open.

Clip preview can directly sample time; it does not need a simulation replay loop.
Controller preview uses fixed steps and explicit preview parameters for reproducible
Play/Pause/Step/Reset. Arbitrary state-machine history seeking is not advertised:
replaying a trigger/input history requires a separate recorded-input design.

Transport contract:

- Time in seconds, FPS selector 24/30/60 controls snapping/display and one Step.
- Speed range 0.1x-2.0x affects advancement, not clip key times.
- Pause retains pose; Stop resets to zero/bind start and rebases event cursors.
- Loop wraps positive time; nonloop stops at duration. Scroll/drag seeks clamp to
  clip bounds. Zero-duration clips and long dt remain finite.
- Closed/collapsed panels do not advance invisible preview unnecessarily; reopen
  does not recreate another owner or reset a preserved document unexpectedly.

### Timeline commands and interaction

Track tree groups each bone's Translation XYZ, Rotation quaternion, Scale XYZ.
Rotation is stored as one quaternion key; optional Euler display converts as a
complete normalized quaternion and must not split it into unrelated Euler tracks.
Selection identity is `(bonePath, componentKind, keyId)`, not vector index.

Implement pure commands, called by both widget and CPU tests:

- Add/replace key at snapped time; edit TRS value; delete keys; move selection;
  copy/paste compatible tracks preserving relative times and bone bindings.
- Move computes one bounded delta for the selection, preserving relative spacing.
  Clamp it so all selected keys remain within [0,duration]. Retime against existing
  unselected keys uses an explicit policy: reject the whole collision atomically.
  Paste uses the same rejection policy; deliberate Replace Key is a separate action.
- Enforce timestamp uniqueness after snapping. Preserve stable IDs on a move;
  pasted/duplicated keys get new IDs. Validate a complete edit before publishing.
- Inserting a component key evaluates that component at the cursor first, with
  bind fallback; it does not reset other components. Deleting the last rotation
  key restores the bind rotation rather than an arbitrary identity.
- Box selection uses visible track/time rectangles; clip to the timeline content
  area, honor zoom/scroll and exclude the toolbar. Selected count should match
  actual editable keys, not hidden arbitrary vector entries.
- Clipboard is value data owned by the editor with source skeleton signature;
  diagnose incompatible bone paths/types. Do not serialize raw memory/pointers.
- Event marker add/edit/move/copy/delete uses the same transaction/history flow.
  Values/name/payload survive saving and preview only logs forward occurrences.
- Keyboard actions require focused timeline and no active text edit/modal. Delete
  cannot also delete the scene entity; Space cannot also start Editor Play.

Dock using the existing bottom Project/Console node only when creating/resetting
the default layout, or apply an explicit first-open placement for the new panel.
Do not rebuild the user's entire saved ImGui layout every frame/version upgrade.
Use the existing icon loader/resources and labels/tooltips for unavailable actions.

## 11. Skeleton overlay and selection (H1/H2)

Compute joint positions from the same current pose globals used for skinning,
not bind matrices or inverse binds. Build octahedral bone wireframes along parent
-> child, with a stable perpendicular basis; zero-length bones show a joint marker.
Render joint spheres as bounded tessellated wire geometry. Respect viewport clip,
near plane, camera matrices and entity hierarchy transforms.

Keep a pure geometry/hit-test builder in a device-free file. Renderer adapter
submits the geometry through active RHI/legacy paths; Animation does not include
ImGui or physics/Jolt debug renderer types. An x-ray overlay is acceptable if
clearly labelled and its picking policy matches. Do not expose a fake depth-aware
option without testing its depth integration.

For picking use a viewport-derived ray against joint spheres/bone capsules (or
matching projected geometry), with deterministic nearest-hit/stable-path ties.
Ignore outside-viewport clicks and clipped/behind-camera primitives. Hovering a
gizmo or active camera gesture suppresses bone picking. Selecting a bone updates
one SkeletonSelectionModel shared by hierarchy/timeline/overlay; selecting a track
reveals/highlights the same bone without changing scene ownership or playback.

If bone TRS manipulation is added, make it an explicit animation-authoring command
at the current clip time with Undo and key policy. Ordinary bone selection must
never silently move the entity or write a permanent pose into the imported rig.

## 12. Animator graph and resource authoring (I1/I2/J1)

Use Dear ImGui drawing for a bounded node/edge canvas and pure graph commands.
Nodes hold stable state IDs; edges hold transition IDs; positions/pan/zoom are
editor state. Graph cycles are legal. Entry node is a marker to one existing state,
not another hidden state with a separately advancing clock.

- Add/duplicate/rename/remove states; when deleting, remove incident transitions
  in the same undoable operation and require a valid replacement entry state.
- Create/reconnect/select/delete edges; transition inspector edits crossfade,
  exit gate, priority and typed AND conditions. Reject invalid endpoints/types.
- Parameters: add/rename/type/default/remove, with explicit handling of all linked
  rules and blend coordinates. Rename preserves ID bindings. Removing a referenced
  parameter must either be rejected with usage diagnostics or explicitly remove
  dependents in the same reviewable Undo transaction.
- Motion inspector chooses compatible clip/1D/2D definition. 1D threshold rows and
  2D drag points edit the actual definition and rebuild validated topology once per
  transaction, not only the dots drawn on a canvas. Show computed weights/hull.
- Asset pickers search/filter current project rigs/clips/controllers, preserve
  validated drag/drop and clear/replace operations. Use compatibility signatures,
  not a matching bone count as proof of compatibility.
- Graph live telemetry (active/target state, times, fade, leaf weights/events) is
  read-only instance state. Preview Float/Int/Bool/Trigger controls write to the
  preview instance; authored defaults change only through a separate edit action.
- Explicit Assign/Apply/Make Unique operates on a validated bound entity. An asset
  document never auto-writes a scene component because the hierarchy selection
  changed. Entity Inspector can open the referenced controller or a unique copy.
- EditorLayer owns panels and lifetime. Register View menu, Content Browser open/
  create/import callbacks, scene detach/attach, per-frame preview update and RHI
  shutdown. Scope callbacks so a closed/destroyed panel cannot be called later.

## 13. CPU fixtures and always-active tests (A0/K1)

Introduce `AxleAnimationUnitTests` without a dependency on AxleEditor resource
staging or AxleTests window startup. Linking existing static libraries is acceptable
if the tested code never constructs graphics objects. Keep pure authoring sources
separate from panel render sources so tests can use them without a device. Every
expectation executes in Release. TestCommon.hpp already redefines assert to keep
legacy checks active; retain that behavior. A lightweight new harness can provide
throwing/returning checks without its broad engine includes. Do not introduce bare
standard assert expectations compiled away under NDEBUG.

| Suite | Minimum behavioral fixtures |
| --- | --- |
| Rig/sampling | Child stored before parent; forest/cycle/bad parent; nonzero-bind rotation-only channel; empty/one-key; antipodal/invalid quaternion; signed scale and unsupported shear; stale channel-map rebuild |
| Import | Tiny self-owned glTF/GLB with mesh transform, nonjoint parent, nonindexed primitive, selected skin and STEP/LINEAR/cubic data; repeat import; invalid accessors/influences/cap; basic ufbx/procedural regression |
| Instance | Shared immutable controller with two instances; Scene duplicate/copy; independent triggers/parameters/time; init/reset/stop; no source update during clone |
| Codec/VFS | Every field and disabled/default value, UTF-8/path quoting, typed Int, graph layout/events, unknown version, bad links, truncated binary; failed save preserves old bytes and dirty state; source-free package load |
| Controller | Rule type/operator validation, AND/priority/order, trigger peek/consume, exact exit crossing, zero fade, independent source/target speed, residual dt and bounded unconditional cycles |
| Blend | 1D endpoints/interior/duration change; 2D triangle centroid (1/3 each), edge (1/2,1/2,0), hull projection, cocircular tie, collinear/duplicates; normalized finite N-way pose; phase does not jump on parameter edit |
| Events | Entry zero, end/loop boundary, multiloop/large dt cap, pause/dt=0/seek, crossfade suppression, dominant leaf change, ordered typed payload, stop/delete/reload invalidation and preview log-only |
| Documents | Saved revision branch, failed save/open, no-op, transaction cancel/coalesce, stable selection after reorder/remove, copy isolation and unsaved operation state |
| Timeline/graph | Clamp whole selection delta; collision rejection atomicity; paste new IDs and skeleton validation; deleting last key bind fallback; entry/state/edge/parameter referential edits and undo restoration |
| Render planning/selection | 96-byte offsets/uint joints/weights/cap, CPU skin reference, animated bounds, overlay basis/zero-length/near-plane/hit ties, two palette allocation ranges not overlapping |
| Scene/assembly/package | Referenced/unique/missing fallback/legacy biped, all AssemblyNode paths, two instantiated assemblies, initial parameter restore, cooked deps and Runtime-facing loader without editor services |

Use isolated temporary directories, never assets/VFX, user characters or CarTest.
Tests exercise production codecs/loaders/edit commands and inspect data/poses;
a test that merely reimplements the same interpolation formula is insufficient.
For scene-script integration use a tiny in-process registered test script with
observable callback counts/payloads, no audio device or actual gameplay asset.

Register the actual Step 103 function in TestCommon, Milestone 10 test sources,
Editor/src/Test.cpp argument parser/dispatch and Editor CMake. Step 103 invokes
reusable acceptance APIs and propagates failures. Do not call another .exe through
system() and ignore its exit code; do not allow --step103 to fall through to all
unrelated tests or report success when the function was never called.

## 14. Physical GPU, UI and delivery evidence (K2/K3)

Implement a standalone `AxleAnimationGPUTests` similar in structure to the real
VFX GPU harness: requested backend D3D12/Vulkan, AllowFallback=false, verify actual
backend/physical device, offscreen render/readback and nonzero failure exit status.
Register CTest tests only with an explicit option such as
`AXLE_ENABLE_ANIMATION_GPU_TESTS=ON`; CPU environments must not silently substitute
NONE/WARP or skip assertions and label them PASS.

Required pixel/ID checks: known deformed calibration vertices/silhouette; two
entities with distinct palettes and reversed draw order; material/texture response;
nonidentity parent/mesh basis; signed/nonuniform normals; animated tip outside bind
bounds; ID picking aligned to deformed color; HDR/MSAA/resize; private preview vs
scene pose; shutdown/recreate; source-free cooked resource rendering. Retain actual
capture metadata and row-pitch-correct readback. Compiled bytes/draw counts only
establish structural coverage, not these rendered results.

The user/reviewer completes the plan's interactive and standalone checklists and
records OpenGL regression separately. Gemini supplies commands, fixtures and an
honest support matrix. A missing RHI shadow-map system or unsupported custom vertex
program must remain a documented limitation, not be disguised as animation parity.

Implementation status per package:

```text
Package / commit:
Behavior changed:
Production path and owned files:
CPU commands, exit codes, assertions:
Build/shader commands and warnings:
Physical DX12: PASS / FAIL / NOT RUN
Physical Vulkan: PASS / FAIL / NOT RUN
Interactive UI / standalone / OpenGL: PASS / FAIL / NOT RUN
Remaining dependency or acceptance gate:
```

Do not reuse historical material/VFX results as evidence for new animation paths.
Do not mark Phase 40 complete until the required CPU/build/GPU/UI/Runtime gates
actually pass. If tests expose a design mismatch, update the plan/status with the
reason and the final supported behavior rather than weakening tests to match a bug.

## 15. Copyable Gemini assignment

```text
Implement AxleEngine Phase 40 from the current checkout.

Read first:
1. docs/design/ANIMATION_PHASE_40_COMPLETION_PLAN.md
2. docs/design/ANIMATION_PHASE_40_GEMINI_IMPLEMENTATION_GUIDE.md
3. docs/guides/DEVELOPMENT.md and the actual source/CMake files linked by the guide.

Inspect git status/HEAD and preserve user changes. Start with A0, then A1/A2/A3
and follow the dependency table/waves. Reuse the existing Animation types and
RHISkinnedVertex3D. Deliver the first executable slice through G2 before expanding
all authoring controls. Use a separate local commit for each package (split a
package further when needed), include meaningful production-path CPU tests and
update ANIMATION_PHASE_40_IMPLEMENTATION_STATUS.md after each package. Do not push.

Your available environment can code and run CPU unit tests. Implement the real
renderer/UI/runtime and physical GPU harness, build what is available, and report
unavailable physical GPU/UI/standalone verification as NOT RUN. Do not replace
rendering with a mock, equate passing CPU tests with visual acceptance, modify user
scenes as fixtures, or mark Phase 38.3/39/40 complete without their own evidence.

Keep working through the scoped packages. At each wave report package IDs,
commit hashes, exact commands/results, deviations and remaining external checks.
Do not ask for approval for routine reversible implementation choices already
covered by these documents; follow the stated data/ownership/time contracts.
```
