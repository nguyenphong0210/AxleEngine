# Phase 39: VFX Editor Coding Guide for Gemini

Date: 2026-11-02. Planning baseline: `45aeaee`.
Status: **implementation handoff; proposed interfaces and algorithms**.

Read the [completion plan](VFX_PHASE_39_COMPLETION_PLAN.md) for scope, package
IDs A1-J4, acceptance gates and commit subjects. This companion specifies how
to implement those contracts. It does not create production APIs or establish
any new runtime acceptance result. Reinspect HEAD and user changes before work.

Follow-up status: the implementation has received a corrective integration
review. See the [current review/acceptance report](VFX_PHASE_39_REVIEW_AND_ACCEPTANCE_REPORT.md)
for actual source names, fixes and verification. In particular, current code
uses `VFXEditorDocument`, `ParticleEmitterStack` and a private `RHISceneRenderer`
preview; suggested class names below are not a second set of production APIs.

## 1. Assignment and execution limits

Implement the scoped Phase 39 editor/runtime work and meaningful CPU unit tests,
including renderer code and real GPU test entry points. The assigned Gemini
environment can code and run CPU unit tests; physical GPU and interactive editor
acceptance must be performed elsewhere. Lack of GPU access is not a reason to
replace the renderer implementation with mocks or to omit its integration tests.

- Use C++20, existing module boundaries, AssetDSL and RHI/NRI helpers.
- Write code, comments, tests, docs and local commit messages in English.
- Preserve unrelated changes and existing material, vehicle and character VFX.
- Make one coherent behavior change with its tests per local commit. Do not push.
- Build what the available toolchain permits and record exact commands/results.
  Report unavailable checks as NOT RUN; do not invent PASS results.
- Keep Phase 39 open until actual backend and UI acceptance is recorded. CPU
  coverage, compiled shader bytes and a valid viewport handle are not equivalents.
- Do not start Phase 40, close Phase 38.3, or expand into a VFX node graph, GPU
  simulation, trails or arbitrary custom material programs.

All snippets below are blueprints. Define their supporting types, error paths,
ownership and tests before using them. Adapt signatures to the inspected source;
do not assume proposed helpers already exist or paste incomplete snippets as a
finished implementation.

## 2. Start with a small executable vertical slice

Execute in this order, retaining the completion plan's package IDs:

1. Add a device-free CPU test target and baseline fixtures.
2. A1: guard unchanged selection and scene lifetime; reproduce the reset defect.
3. Introduce B1's minimal value-only definition boundary as A2's prerequisite.
4. A2/A3: isolate preview instances and commit edits to the correct emitter.
5. B2/B3: full effect/scene persistence and legacy migration.
6. C1-C3: runtime playback, RNG and fixed-step seeking.
7. D1-D3: safe draw resources, real RHI preview and spatial/sorting behavior.
8. E/F/G: complete authoring, resources/shading, commands and curve/gradient edits.
9. H/I: authored sub-emitter routing, actual mesh emission and collisions.
10. J: reference assets, integrated checks, honest delivery/reporting.

Do not postpone tests until J. J combines and registers the coverage introduced
with each sub-step. Introducing minimal B1 data types early does not mean B2/B3
are already implemented. After every sub-step, update its implementation/test
status and list the external verification still required.

## 3. Source map and file ownership

| Existing file | Inspect/change responsibility |
| --- | --- |
| `Engine/UI/src/VFXEditorPanel.cpp` | Selection/reset, editable controls, transport, preview dispatch, scene Apply |
| `Engine/UI/include/Axle/VFXEditorPanel.hpp` | Panel lifetime, document/preview ownership; remove duplicate simulation authority |
| `Editor/src/EditorLayer.cpp` | Repeated selection call, edit-mode primary-only update, panel/scene lifecycle |
| `Engine/Scene/include/Axle/Components.hpp` | Legacy primary adapter, independent scene instances, clone/copy rules |
| `Engine/Scene/src/Scene.cpp` | Play/update/copy and particle render submission |
| `Engine/Scene/src/SceneSerializer.cpp` | Referenced and inline full-effect persistence |
| `Engine/VFX/src/ParticleEmitterSerializer.cpp` | Factor existing field encoding; retain legacy read support |
| `Engine/VFX/include/Axle/VFXCommon.hpp` | Burst state, RNG entry points, enums, gradient/curve data |
| `Engine/VFX/src/ParticleSystem.cpp` | Per-emitter simulation and queued lifecycle events |
| `Engine/VFX/src/RHIParticleRenderer.cpp` | Per-draw resources, packing, descriptors, pipeline state |
| `Engine/Resources/Shaders/Particle.hlsl` | Particle-compatible material parameters, cutout, depth fade |
| `Engine/UI/src/CurveEditorWidget.cpp` | Independent widget/key state and complete alpha-key editing |
| `Engine/UI/src/ContentBrowserPanel.cpp`, `AssetInspector.cpp`, `EntityInspector.cpp` | Asset opening, resource assignment and entity/document routing |
| `Editor/src/Tests/Test_Milestone10.cpp` | Historical Step 102; retain valid assertions, correct incomplete coverage claims |

Suggested new files, not existing APIs:

| Location | Proposed helper |
| --- | --- |
| `Engine/VFX/include/Axle/ParticleEffectDefinition.hpp` and `src/ParticleEffectDefinition.cpp` | Authored definitions, IDs, validation and comparison |
| `Engine/VFX/.../ParticleEffectSerializer.*` | Versioned effect serialization using AssetDSL |
| `Engine/VFX/.../ParticleEffectInstance.*` | Instance ownership, playback clock, emitter lookup, RNG and event queue |
| `Engine/VFX/.../ParticleMeshSampler.*` | Device-free triangle/CDF sampling |
| `Engine/UI/.../VFXEditorDocument.*` | Device-free working definition, selection, revision and history |
| `Engine/UI/.../VFXPreviewController.*` | Preview instance, fixed-step transport and seek progress |
| `Engine/UI/.../VFXPreviewRenderer.*` | Offscreen RHI target rendering; no file/undo ownership |
| `Editor/src/Tests/VFXUnitTestMain.cpp`, `VFXTestHarness.hpp`, `Test_VFX*Unit.cpp` | Always-active CPU assertions and independent fixtures |
| `Editor/src/Tests/Test_VFXRendering.cpp` | Physical backend integration/pixel tests, separate from CPU main |

New Engine/VFX sources belong in its CMake target; authoring/render-panel sources
belong in the appropriate editor/UI target. Do not add an Engine/VFX dependency
on Engine/UI or Engine/Scene to obtain an entity, mesh picker or ImGui function.
Pass plain data and narrow callbacks across those boundaries.

## 4. Data model: authored values, runtime state, editor state

Use one authoritative authored emitter vector. Do not add another mutable
configuration copy to each layer and keep them coherent through frame polling.

Illustrative value types:

```cpp
using EmitterId = std::string; // Opaque stable ID, not a display name.

struct EmitterTransform {
    glm::vec3 Translation{0.0f};
    glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 Scale{1.0f};
};

struct EmitterResources {
    std::string TexturePath;
    std::string NormalMapPath;
    std::string MaterialPath;
    bool UseTexture = false;
    bool UseNormalMap = false;
};

struct EmitterDefinition {
    EmitterId Id;
    std::string Name;
    bool Enabled = true;
    bool TriggerOnly = false;
    EmitterTransform LocalTransform;
    ParticleEmitterConfig Config;
    EmitterResources Resources;
    // Add explicit module-enable state and typed target-ID links.
};

struct ParticleEffectDefinition {
    uint32_t SchemaVersion = 2;
    std::string Name;
    uint32_t Seed = 1;
    std::vector<EmitterDefinition> Emitters;
};
```

This is a transition design: current `ParticleEmitterConfig` still contains a
MaterialPath, named SubEmitters and runtime burst counters. Factor/adapt these
fields rather than creating conflicting authorities. In the final definition:

- Resource paths/enable states have one canonical location; compatibility getters
  derive legacy fields. Do not serialize two disagreeing MaterialPath fields.
- Sub-emitter links store target IDs. Target names exist only in v1 migration/UI.
- Bursts store schedule/count/probability values; counters live in the instance.
- `MaterialAsset` handles, texture objects, callbacks, pools and child-system
  pointers are transient. They never appear in serialized authored state.
- Newly exposed module enables are real authored fields. Collapsing a header
  is not disabling a module. Disabled values remain saved for later re-enable.
- Equality/change detection compares authored data including paths, enable
  states, HDR color/alpha and graph links, never shared-pointer addresses.

Runtime ownership should be explicit and noncopyable:

```cpp
class ParticleEffectInstance {
public:
    explicit ParticleEffectInstance(ParticleEffectDefinition definition);
    ParticleEffectInstance(const ParticleEffectInstance&) = delete;
    ParticleEffectInstance& operator=(const ParticleEffectInstance&) = delete;
    ParticleEffectInstance(ParticleEffectInstance&&) noexcept = default;
    ParticleEffectInstance& operator=(ParticleEffectInstance&&) noexcept = default;

    // Proposed: validate/create transactionally, then implement these operations.
    void Restart();
    void Play();
    void Pause();
    void StopAndClear();
    void StepFixed(double seconds);
    ParticleSystem* FindSystem(const EmitterId& id);

private:
    ParticleEffectDefinition m_Definition;
    std::vector<EmitterRuntime> m_Emitters; // Supporting type to implement.
    // Per-instance playback, lookup, RNG streams and bounded value-event queue.
};
```

Each `EmitterRuntime` owns a distinct system, burst state and RNG stream. A
shared pointer may alias the primary system inside the same instance for a
legacy API, but must not be copied into another scene/preview instance. Never
construct a non-owning shared pointer with an empty deleter as a lifetime fix.

Scene/component copy operations copy authored definition and instantiate new
runtime state. Audit entity duplication, `Scene::Copy`, scene Play snapshots and
existing `pecCopy` paths. Do not rely on default member copying of shared systems.
Immutable asset definitions may be shared; scene runtime, preview runtime and
two scene entities remain independent.

## 5. Selection, editing and scene lifecycle (A1-A3)

Create a selection key from a scene binding generation and the full versioned
entity handle. Pointer address alone is not a scene generation: allocators can
reuse an address. Advance/clear the binding when a scene/project changes.

Proposed selection flow:

```text
On selection notification:
    resolve scene + entity generation; validate registry membership
    if identity and bound authored revision are unchanged: return
    if current document is dirty: queue a switch request for the UI to resolve
    export the selected component's authored definition into a candidate
    validate candidate
    install candidate document and create a new independent preview instance
    select a surviving emitter ID, otherwise the first ID

On scene unload/entity deletion:
    detach the scene binding before destroying its owner
    preserve an unsaved standalone working document
    invalidate pending Apply operations; do not dereference the old entity
```

Do not reset in `SetSelectedEntity()` for an unchanged selection, and avoid
calling it as an unconditional document reload from every ImGui frame. External
asset changes are explicit revision/reload events; they do not silently replace
dirty working data. Do not prompt from runtime code; the panel owns pending
switch/reload decisions, while CPU model methods expose the decision state.

Every control routes through one edit path:

```text
widget temporary value
  -> resolve selected stable ID
  -> build/validate authored change
  -> apply or coalesce edit command in document
  -> increment/change document state token
  -> notify preview controller of changed definition + change impact
```

Do not keep unsaved edits only in `m_Config`. Either remove that copy or treat it
as a short-lived control buffer that always commits before selection changes.
Structural/resource changes may rebuild/resimulate preview; numeric/module edits
must have a documented live-update policy. Preserve target time where supported,
coalesce drag changes, and use the bounded seek mechanism for expensive rebuilds.

`ApplyToEntity` exports all authored emitters, validates the still-current scene
binding and installs a fresh scene runtime under a documented restart policy.
It does not copy preview pools. The primary compatibility config comes from the
designated primary/first emitter, not the currently selected emitter. Preserve
entity transform, unrelated components and authored disabled flags.

Both edit-mode and Play update paths call the component/effect-instance update
once per scene tick. Remove the edit-mode primary-only `pec.System->Update()`
path after routing it through the multi-emitter component. Do not update the
primary again through a legacy alias. A preview controller advances once in
`OnUpdate`, never a second time while drawing ImGui.

## 6. AssetDSL schema and migration (B1-B3)

Keep `AssetKind::ParticleEmitter` and the root spelling `ParticleEmitter` so
Content Browser recognition continues to work. The following is a proposed v2
example using the repository's colon/block syntax, not JSON/YAML:

```text
ParticleEmitter {
    schemaVersion: 2
    name: "Campfire"
    seed: 12345
    Emitter {
        id: "flame-0001"
        name: "Flame"
        enabled: true
        triggerOnly: false
        translation: [0, 0, 0]
        rotation: [1, 0, 0, 0]
        scale: [1, 1, 1]
        texture: "vfx/reference/textures/flame.png"
        useTexture: true
        Config {
            emissionRate: 40
            duration: 3
            looping: true
            maxParticles: 512
        }
    }
    Emitter {
        id: "sparks-0002"
        name: "Sparks"
        enabled: true
        triggerOnly: true
        Config {
            emissionRate: 0
            lifetime: [0.5, 1]
        }
    }
}
```

Freeze key names and link-block placement with schema tests before filling in
all modules. Use quaternion order `[w, x, y, z]` for the example schema, matching
the proposed GLM constructor; reject zero/nonfinite rotations and define the
normalization policy. Do not memcpy quaternion memory as an assumed file layout.
Example IDs are fixtures; generate fresh opaque
IDs for user-created/duplicated emitters. Legacy single-emitter migration must
derive stable IDs from normalized content/identity rather than creating random
IDs on every load. Duplicate IDs are invalid; duplicate names may be allowed
because links use IDs. Reject ambiguous legacy name-to-ID conversion.

Factor the existing serializer into reusable block helpers, for example:

```cpp
void WriteEmitterConfig(AssetBlock& block, const ParticleEmitterConfig& config);
bool ReadEmitterConfig(const AssetBlock& block,
                       ParticleEmitterConfig& candidate,
                       std::vector<VFXDiagnostic>& diagnostics);

bool ParseEffect(std::string_view source,
                 ParticleEffectDefinition& destination,
                 std::vector<VFXDiagnostic>& diagnostics);
```

These helpers are proposed. Implement `VFXDiagnostic` as a small plain structure
with severity, code, emitter ID/property path and message. Avoid a generic error
framework. `ParseEffect` parses with `AssetDSL::ParseSource`, checks kind/version,
builds a fresh candidate, validates all emitters/links, and only then assigns the
destination. Reset omitted fields to versioned defaults and clear candidate
collections before appending; never append gradients/bursts into a previously
loaded config. Use `SetVec4` for HDR colors rather than clamping hex conversion.

Persist all supported authoring fields. In addition to current emitter scalars,
cover module enables, resources, transforms, size/speed/alpha curves and ranges,
color/alpha keys, flipbook settings, burst schedules, force fields, collision
planes/boxes and ID-based sub-emitter links/inheritance. Never serialize world
raycast callbacks, completed burst cycles, particle arrays or cache handles.

Missing assets retain their authored paths and produce resource diagnostics;
they are different from malformed effect data. Reject nonfinite/invalid ranges,
unknown enum values in v2 and unsupported future schema versions. Define limits
for input sizes, counts, empty effects and graph edges before allocating pools.
The accepted authoring policy is an empty effect: New and Content Browser Create
contain zero emitters, and the user adds a neutral emitter explicitly. Empty
assets and inline scenes are valid; runtime initialization must not synthesize
a primary emitter for an explicitly empty definition. See VFX_EDITOR_AUTHORING_GUIDE.md.

Legacy APIs cannot silently truncate a v2 effect to its first emitter. Keep v1
helpers for v1 callers, introduce full-effect APIs, migrate actual consumers and
return a clear unsupported/multi-emitter result when a legacy-only path is used.

Save algorithm:

1. Validate a value snapshot and serialize it in memory.
2. Write to a uniquely named sibling temporary file; flush/close and check errors.
3. Replace the destination using a platform-appropriate atomic replacement.
   Inspect existing helpers first; do not delete the valid destination before a
   potentially failing rename. Return a diagnostic if replacement is unavailable.
4. Mark the document clean only after replacement succeeds. Failure preserves
   the old file, current working data and dirty state; clean up only owned temps.

For scenes, implement one full-effect serializer/helper boundary and reuse it
for inline definitions. Do not hand-maintain another partial field list in
`SceneSerializer`. Referenced assets store a project-relative path and stable
asset identity when available; Make Unique clears the shared binding and owns
an independent authored copy. Initially do not add per-emitter sparse overrides.
Save/reopen preserves shared-versus-inline semantics and resource path bases.

## 7. Playback, RNG, scheduling and seek (C1-C3)

Separate authored enable state, transport state and emission state. Pause freezes
age, movement, events and emission. Stop clears all pools, events and time. Play
resumes without enabling disabled emitters. Restart resets seed, schedules and
clock; PlayOnAwake is evaluated on instance activation, not every config edit.
Manual Burst while paused may create visible particles without aging them; make
its disabled/trigger-only behavior explicit and test it.

Choose one owner for duration/loop scheduling (prefer the effect runtime over a
second UI clock). A non-looping emitter emits during its configured interval and
then drains living particles; it does not freeze them at Duration. Loop boundaries
restart emission schedules while existing particles continue aging. Define the
effect preview range for different emitter durations and trigger-only emitters.

Use double precision for the controller clock and explicit integer fixed ticks
for deterministic preview stepping. An illustrative runtime clock:

```cpp
struct BurstRuntime {
    uint32_t CompletedCycles = 0;
    double NextTriggerTime = 0.0;
};

struct PreviewClock {
    uint64_t Tick = 0;
    double Accumulator = 0.0;
    double StepSeconds = 1.0 / 60.0;
    double PlaybackSpeed = 1.0;
};
```

Burst runtime starts from the authored Time after construction, config rebuild
and Restart. Editing Time must not leave the previous `NextTriggerTime`. Consume
each due scheduled event once, including t=0; select and document a boundary
convention. Partition loop crossings and catch up all due events within a bounded
budget. A zero interval with infinite cycles must never spin forever; define it
as a single trigger per loop or reject it with a diagnostic. Do not evaluate one
burst cycle per display frame and silently lose events for larger dt.

Inject an RNG reference into actual spawn/shape/range/burst sampling. Preserve
old static RNG wrappers only as compatibility entry points. Each runtime derives
its stream from effect seed + stable emitter ID using a specified stable hash,
not `std::hash` or vector index. Route all actual random choices through that
stream, including count/probability and `std::rand()` paths. Restart/seek restores
the same seed; another preview never reseeds or consumes a scene's stream.

Seek algorithm (proposed operations):

```text
RequestSeek(target):
    validate finite/nonnegative target and documented maximum range
    cancel any stale seek request; capture definition revision + seed
    reset the preview instance to tick zero
    compute target fixed tick and any documented fractional-step policy
    enter a seeking state without advancing the scene

PumpSeek(maximumTicksThisUpdate):
    advance at most that many fixed ticks through the normal runtime Step path
    abort/restart if a newer definition/seek replaces this request
    publish progress; commit preview time only as simulation advances
    on completion, retain prior Play/Pause intent
```

A later optimized forward seek can reuse compatible checkpoints. Do not require
checkpointing for the first correct version. Advancing a slider variable alone
is not seeking. Single Step uses exactly the same fixed Step method. Never claim
arbitrary-dt floating-point integration is subdivision invariant; compare the
same fixed tick sequence and test event scheduling separately from integration.

## 8. Per-draw GPU resources and packet preparation (D1/D3)

Fix resource ownership before connecting the panel to it. The existing renderer
updates one mapped instance buffer, one constant buffer and one descriptor set
for multiple recorded draws. A CPU test can verify allocation plans, but only
physical GPU tests can establish the rendered result.

A straightforward first implementation uses separately owned draw resources:

```cpp
struct ParticleDrawResources {
    RHI::RHIBuffer Instances;
    RHI::RHIBuffer Constants;
    nri::DescriptorSet* Set = nullptr;
    std::shared_ptr<RHI::RHITexture> ColorTexture;
    std::shared_ptr<RHI::RHITexture> NormalTexture;
    // Retain any depth snapshot and resources actually referenced by this draw.
};

struct ParticleFrameResources {
    std::vector<std::unique_ptr<ParticleDrawResources>> Draws;
    size_t DrawCursor = 0;
    // Descriptor-pool/fence ownership must match the implemented frame strategy.
};
```

An arena/ring with disjoint ranges is also valid, but do not build a generalized
allocator if per-draw resources solve the current defect. Heap-allocated draw
objects avoid invalidating addresses when a vector grows. Own descriptor range
storage for as long as native layout descriptions reference it.

Frame/draw sequence:

1. Confirm the frame resources are no longer in flight, using existing completion
   guarantees/fences. Only then reset descriptor pools and draw cursors.
2. Build CPU packets with emitter ID, effective material/config, transformed
   particle instances, texture references and target-compatible pipeline key.
3. Allocate/reuse a distinct draw resource or arena range for each packet.
4. Resize buffers as necessary; check Init/UpdateData results. Align CBV storage
   to the RHI requirement and copy only the logical constant bytes.
5. Populate that draw's descriptors, including initialized fallback resources.
6. Record bindings and draw. Do not overwrite any referenced bytes/descriptors
   until GPU completion, including draws from an earlier Begin/End pass.
7. Retain resources through submission; recycle only the completed frame.

Do not fix this by waiting/submitting after every emitter or by using one static
resource set with a different logical name. Do not retain only raw SRV pointers
if the texture cache can release or replace their owners before completion.

Extract a CPU helper for packing `ParticlePool` + effective render settings into
instances. For local simulation, apply emitter/parent transforms to positions
and velocities during packing or through an explicitly designed shader transform;
for world simulation, do not apply the current emitter transform a second time.
Preserve camera-facing orientation and define uniform/nonuniform scale effects on
size, velocity and collision radius. Implement/document approximations explicitly.

If adding GPU fields, update the C++ struct, HLSL cbuffer/input declarations,
offset/size assertions, pipeline layout and upload sizes together. Do not alias
unrelated values into spare fields or assume the existing 512-byte constants can
hold an extra matrix without a layout change. Validate instance stride/offsets
against the actual `ParticleInstanceVertex`, not guessed GLM alignment.

Sort after world-transform resolution. Specify alpha depth order and stable ties;
only batch contiguous compatible runs if regrouping would alter transparency.
Changing emitter order should not change independent random streams. Split pools
larger than `MAX_PARTICLES_PER_BATCH` into all required draws, with correct source
offsets. Return diagnostics for allocation/capacity limits rather than clipping
the effect silently.

Pipeline keys include blend/cutout mode, color/depth formats, sample count and
relevant state. Fallback must match the active target configuration. A default
RGBA8 single-sample PSO is not a safe fallback for an HDR/MSAA render target.

## 9. Actual offscreen RHI preview (D2)

Reuse existing `RHIViewportDescriptor` target accessors and ImGui registration;
do not mistake its allocation for drawing. `RHIParticleRenderer::Begin` configures
particle rendering; the caller still owns command recording and render passes.

Implement a small preview-render service taking target textures, camera and the
preview instance. It must not read a scene entity or serialize assets. Proposed
flow, to implement using inspected RHI/NRI calls:

```text
OnUpdate:
    advance/pump the independent preview controller once

Panel drawing / preview render request:
    measure usable size; skip zero/minimized sizes
    resize targets safely before recording into them
    compute camera view/projection using current orbit target and aspect ratio
    begin command recording; transition color/depth to attachment layouts
    clear; render RHI grid/axes/optional preview geometry
    prepare any readable opaque-depth input needed by soft particles
    begin particle pass, submit packets for visible preview emitters, end pass
    end rendering; transition color to ShaderResource for ImGui
    end/submit recording with valid resource lifetime/completion handling
    show resulting image with RHIViewportDescriptor's backend-specific UVs
```

If grid/preview geometry and soft particles require separate passes, construct
that sequence explicitly rather than sampling a depth attachment in an invalid
layout. Reuse established RHI grid/debug drawing where suitable; do not call
OpenGL grid functions from the RHI path. Keep the legacy GL preview path separate
and verified by its own available checks.

Orbit/zoom/pan act only when the preview owns the input; module editing and
scrubbing do not move the camera. Resize refreshes the ImGui descriptor and any
depth snapshots. Teardown waits for/retires referenced resources, unregisters
the image and releases native resources before device/ImGui destruction. Handle
init/record/submit failure visibly and leave the document usable.

Prepare a real integration test rendering into an offscreen target without an
editor window. Assert non-clear pixels and the distinct-emitter fixtures in
section 16. Gemini records this test as prepared/compiled, GPU execution NOT RUN.

## 10. Document lifecycle and authoring operations (E1-E3)

Use a device-free `VFXEditorDocument` model. Its operations should accept IDs and
authored values and return structured success/change/diagnostic information.
The panel converts ImGui input into calls; it does not implement file ownership
or manipulate shared scene simulation directly.

Recommended model operations:

```text
NewEffect / OpenEffect / Save / SaveAs
SelectEmitter / AddEmitter / DuplicateEmitter / RemoveEmitter
RenameEmitter / ReorderEmitter / ReplaceEmitterConfig / SetResource
ApplyCommand / Undo / Redo / Validate / ExportDefinition
```

New/Open construct candidates before replacing the current document. Save As
updates the document path only on success. Model dirty state must handle undo
back to the saved state and redo/branching correctly. Treat entity bindings,
active selection, solo/visibility and camera as separate session state.

Duplicate copies authored values and assigns a fresh ID; it does not share a
runtime system. For duplication of several emitters, build old-ID -> new-ID
mapping first, then rewrite links internal to that duplicated set. Links outside
it retain their existing targets. Reorder changes authored vector order but not
IDs/links. Removal of referenced emitters produces an explicit repair/confirmation
flow; a dangling target cannot remain silently executable.

Content Browser creation should write a valid v2 effect through the same
serializer. Double-click/open routes to the document by path; existing entity
Open in VFX Editor routes by scene binding and exported definition. Add an
asset-open callback; do not synthesize a temporary scene entity just to open an
asset. Distinguish Save Asset, Apply to Entity and Make Unique in the UI.

Audit every ImGui control and its change return value. For example, a gravity
drag changes the selected definition, dirty state and effective preview config
through the same operation as an emission-rate drag. Module reset/copy operates
on authored module data. New module switches retain values while disabled.

Implement a pending action for switch/close when dirty: Save, Discard or Cancel.
Failed Save leaves the pending action/document intact. Scene destruction may
detach an invalid binding immediately while preserving the standalone document;
it must not block runtime teardown waiting for an ImGui modal.

## 11. Texture/material binding and soft particles (F1-F3)

Build an effective CPU render-settings helper before assigning GPU descriptors:

```text
legacy emitter config + authored resources
    -> optional material lookup/adapter
    -> effective supported particle settings + resource requests + diagnostics
    -> resource cache/loaders
    -> per-draw GPU packet
```

Keep material/texture loading out of the simulation. Reuse existing texture cache
and descriptor-aware image resolution where appropriate, without duplicating
color-space rules. Color maps use the configured sRGB/color interpretation;
normal/data maps use linear uploads. Retain assigned paths when disabled or
missing so the user can repair them. Clear removes the effective binding and
uses initialized white/flat-normal fallbacks; stale SRVs cannot survive clearing.

Define the supported material adapter explicitly: base color/alpha, albedo map
enable/path/UV, emission and supported surface/cutout settings plus the existing
particle lighting controls. Assigned materials drive their supported fields;
unassigned emitters retain legacy config behavior. Preserve particle lifetime
color/alpha modulation rather than replacing it wholesale with material tint.
Unsupported custom shader/PBR features produce clear diagnostics and a documented
fallback; accepting an arbitrary MaterialPath with no rendering effect is invalid.

Preserve enum compatibility. Existing `ParticleBlendMode` is Additive=0,
AlphaBlend=1, Multiply=2. The current third UI label must not claim premultiplied
alpha for Multiply. Append an explicit Cutout value and version it as needed;
do not reinterpret serialized 2. Keep C++ mapping, DSL strings, UI labels, PSO
selection and shader behavior consistent. Explicitly test alpha threshold zero.

Depth fading requires a real readable scene-depth resource with matching camera,
resolution, sample configuration and projection convention. Add the descriptor
and HLSL declarations/layout together, bind it per draw and set a validity flag.
Compare linearized particle/scene depths using the actual engine projection;
test the formula with known camera/depth fixtures. Do not confuse depth fading
with Jolt collision. Missing depth/MSAA cases use a documented non-soft fallback
instead of reading unbound textures. Blend/depth-write behavior must match each
surface mode; avoid transparent particles blocking later particles unexpectedly.

## 12. Undo, curves and color/alpha editing (G1-G2)

Keep history in the authoring model. A small value-snapshot command stack is an
acceptable first version because it avoids pointers into reallocating emitter
vectors; optimize later only if measured. Runtime/pool/GPU snapshots are forbidden.

```cpp
struct EffectEditCommand {
    std::string Label;
    ParticleEffectDefinition Before;
    ParticleEffectDefinition After;
    // Stable selection IDs may be included for sensible selection restoration.
};
```

Use stable state/node tokens to represent the clean revision. Comparing only
history cursor positions is incorrect after undo followed by a divergent edit.
Save records the current state token. Undo/redo restores authored state, validates
selection and sends one preview invalidation notification.

Numeric/curve drags begin a transaction on activation, update live authored
values during dragging and commit one command on deactivation-after-edit. Do not
enqueue a command per frame. Cancel restores the initial authored snapshot.

Give each curve/gradient widget an instance state keyed by document + emitter
ID + property ID. Keep separate color/alpha selection; do not retain a static
global selected-key index. Stable key IDs or an explicit selection-remap policy
must survive sorting/deletion. Sort keys after time changes, define coincident
key behavior and preserve HDR color/alpha in the serializer. Add the alpha
track's add/select/drag/value/delete controls, not merely preset AlphaKeys data.
Use the same document command pathway for curves, presets and numeric key entry.

## 13. Sub-emitter graph and queued events (H1-H2)

Resolve target IDs once a candidate definition is validated and all runtime
emitters exist. The effect instance owns each emitter once. Do not also register
the same target as a recursively updated owning child of several parent systems.
The existing `AddChildSystem()` may remain for legacy callers under a documented
adapter, but the new effect path has one update/render traversal.

Use value events so swap-with-last `ParticlePool::Kill()` cannot invalidate them:

```cpp
struct ParticleEvent {
    EmitterId Source;
    EmitterId Target;
    SubEmitterTrigger Trigger;
    glm::vec3 WorldPosition;
    glm::vec3 WorldVelocity;
    glm::vec4 Color;
    uint32_t Count = 0;
    // Timestamp/sequence needed for the chosen deterministic processing order.
};
```

Flow: simulate an emitter -> copy a lifecycle event -> enqueue -> enforce graph
and budget rules -> resolve target -> convert world/local coordinates -> spawn
into the target -> enqueue resulting Birth events if allowed. Drain in a defined
stable order without recursive calls to `EmitBurst()` growing the C++ stack.

Reject cycles/self-links for this phase and enforce maximum events/spawns per
tick even for a valid DAG. Define the tick at which children spawned from events
start aging; an arbitrary vector order must not change this. Missing/deleted
targets are diagnosed. TriggerOnly prevents unintended continuous emission but
does not prevent routed events from creating particles in an enabled target.

Inheritance is per-particle state. If inherited color is a multiplier, store it
and apply it during later lifetime-color evaluation; setting StartColor once is
not sufficient if Update overwrites it. Convert inherited velocity into the
target simulation space before adding it. Dispatch Birth only after a successful
pool spawn; a full pool must not create phantom Birth chains. Collect Death data
before removing the source particle. Render target pools once through the normal
effect list rather than inferring drawing from the parent's aggregate count.

## 14. Real mesh emission and collision binding (I1-I2)

Implement a pure mesh sampler taking positions, indices, optional normals and an
RNG. An asset adapter loads mesh data and constructs that sampler; a UI picker
only assigns an authored mesh reference. Engine/VFX must not depend on Scene to
obtain a selected entity's geometry.

Build validated nondegenerate triangles and a cumulative area distribution.
Choose triangles proportionally to area and sample barycentric coordinates using
the square-root method. Interpolate/normalize valid vertex normals, otherwise
use the face normal. Handle transforms consistently, including whether sampling
area is local-space or transformed-space for nonuniform scale; document/test the
choice. Missing/empty/degenerate meshes use a stated fallback plus diagnostic,
never a silent sphere labeled MeshSurface. Persist the mesh resource reference.

Persist collision planes/boxes, radii, bounce/friction/kill settings and forces.
Runtime physics raycast functions are supplied as narrow services with lifetime
validation on scene changes. Preview uses its own explicit collider setup;
reading live scene physics is an optional bound service, not ownership of the
scene. Add a floor plane when the user enables a floor-collision mode, not merely
a decorative grid and a Collision.Enabled flag with no geometry.

For unit tests, provide analytic planes/boxes or a deterministic raycast stub
through the real callback interface. This tests simulation independently of Jolt
availability; it does not establish scene-physics or GPU acceptance. Verify
per-collider KillOnCollision semantics in addition to the global switch.

## 15. CPU test target and CMake integration (J1 throughout)

Create a proposed `AxleVFXUnitTests` executable registered as `VFXUnitTests` in
CTest. It does not exist at this baseline. Add it before claiming the commands
below work. Model the main after the material CPU target's failure reporting;
use VFX-specific names and always-active checks rather than Release-disabled
assertions. Return nonzero on a failed suite or uncaught exception.

Suggested suite files:

| Proposed suite | Production entry points it must exercise |
| --- | --- |
| `Test_VFXDefinitionUnit.cpp` | Definition validation, IDs, duplicate/reorder and version constraints |
| `Test_VFXPersistenceUnit.cpp` | Actual effect/scene serializers and migration, full module/resource round-trip |
| `Test_VFXDocumentUnit.cpp` | Real document selection, commands, dirty/save and failed-open/save behavior |
| `Test_VFXPreviewIsolationUnit.cpp` | Scene and preview instance operations, copy/duplicate independence |
| `Test_VFXPlaybackUnit.cpp` | Fixed-step controller, actual pools, duration, bursts, RNG and seek |
| `Test_VFXRenderPlanUnit.cpp` | Instance packing, batching/sorting, resource/material resolution and format decisions |
| `Test_VFXEventsAndMeshUnit.cpp` | Loaded graph routing, budgets/inheritance, real sampler and collision services |

Compile device-free authoring/controller model sources into the CPU target or a
small model library. Do not link the whole editor executable or call ImGui from
these models. If the same model source belongs to AxleUI, compile it only once
per final executable; do not introduce duplicate definitions by also linking it
through a UI library. Engine/VFX's transitive renderer/RHI libraries may be linked,
but tests must never initialize RHIDevice, GLFW, a window or an OpenGL context.
Scene persistence fixtures can link Axle::Scene as the material CPU target does.

Do not make the CPU target depend on AxleEditor/AxleTests, which would force the
full GPU/editor build path to run unit tests. Do not use a generic GPU-owning
TestCommon fixture as the new CPU main. Temporary directories and deterministic
fixtures are allowed; clean up only test-owned paths. Include every new source
in its actual CMake source list and register all suites in main and CTest.

Proposed commands, available only after that target/registration is implemented:

```powershell
cmake --build build --config Release --target AxleVFXUnitTests --parallel 12
ctest --test-dir build -C Release -R '^VFXUnitTests$' --output-on-failure
```

If no configured build directory exists, follow the repository's current CMake
toolchain/setup instructions first. Record compiler/configuration and real exit
codes. A pure-model PASS does not make uncompiled panel/renderer changes valid;
compile the affected production targets when possible and report any limit.

## 16. Explicit test fixtures and expected outcomes

Use these as the minimum regression inputs, adapting only names to the final API.
Exercise production helpers; do not recode an algorithm in the test and compare
the two copies. Assert failures preserve the previous destination/state.

| Fixture | Required assertion |
| --- | --- |
| Same valid selection bound repeatedly | Preview time/selection/counts and unsaved edits are unchanged |
| Scene with 2 live particles, independent preview with 5 | Reset/stop/edit preview leaves scene count and particle values unchanged |
| Three emitters, edit only Smoke | Flame/Sparks values remain unchanged after commit, Apply and reload |
| Duplicate scene entity / effect instance | Changing/resetting one does not affect another pool/RNG stream |
| Effect with three IDs, reorder and rename target | IDs, resources and ID links survive actual asset/scene round-trip |
| Load valid A, then truncated/duplicate-ID/future-version B | Operation fails with diagnostic and destination still equals A |
| Dirty document, failed save | Old file bytes unchanged; dirty state/path and working data retained |
| Edit -> Save -> Edit -> Undo | Saved state is clean; redo is dirty; divergent branch cannot reuse a clean cursor index |
| Disabled second emitter, Play/Pause/Restart | It remains authored-disabled and does not emit |
| Nonlooping duration with long particle lifetime | Emission stops at duration; existing particles continue to drain |
| Finite bursts at t=0 and later loop boundaries | Each scheduled occurrence fires once; exact boundary policy is tested |
| Same seeded effect, ordinary playback vs seek to same fixed tick | Compare real particle counts/positions/colors and runtime counters |
| Preview interleaved with scene updates | Scene seeded result matches scene-only execution |
| Pool capacity 2, burst count 5 with Birth links | Only successful births dispatch; counts and budgets stay bounded |
| Cycle/self-link, deleted target, duplicate display names | Validation/ID resolution behaves as specified; no recursion/ambiguous name binding |
| Loaded collision/death graph | Children appear via normal effect APIs without test-only AddChildSystem registration |
| Color/velocity inheritance | Child values stay correct after later lifetime updates and local/world conversion |
| Vertex/instance batches for 20,001 particles | Both required batches cover each source instance exactly once |
| Two unequal-area non-spherical triangles | Samples are on supplied triangles with expected area-weighted distribution |
| Two gradient widgets/emitters | Selection/editing one cannot modify another's color/alpha key |

Physical GPU fixtures, prepared by Gemini and executed elsewhere:

1. Draw a red emitter on the left and green on the right in one command
   submission. Read back and assert both colors in their expected regions.
2. Reverse submission order and repeat across frames with different counts,
   positions, configs and textures. This catches shared-buffer/descriptor reuse.
3. Replace then clear one emitter's texture/normal/material; another emitter must
   remain unchanged. Include missing-file fallback and map enable toggles.
4. Move a parent after emission to distinguish local/world simulation; exercise
   billboard modes, depth ordering and particles beyond one batch.
5. Verify Additive/Alpha Blend/Multiply/Cutout with explicit background pixels,
   including alpha-cutoff zero. Check soft-depth intersection and missing-depth
   fallback under the supported camera/MSAA conventions.
6. Render real preview output, not just a scene path; resize, minimize, close,
   reopen and test supported SDR/HDR/MSAA targets without stale descriptors.
7. Load/save/reopen deterministic Campfire and compare it with matched inline
   copies at the same seed/time/camera. Check visible triggered child particles.

Add a real registered integration entry point. Define explicit backend parsing
with fallback disabled and active-backend verification. If using proposed options
such as `AXLE_VFX_TEST_BACKEND` or capture directories, implement, register and
test them before documenting runnable commands. Bad backend/capture requests
must fail visibly. Reuse format-aware readback helpers: RGBA16F is 8 bytes/pixel,
with aligned rows, correct half decoding and finite-value checks. No fake image
provider substitutes for actual particle pixel tests.

## 17. Change audit and verification checklist

Before each commit:

1. Inspect the diff and map the behavior to one completion-plan sub-step.
2. Compile affected helpers and run the relevant CPU suites.
3. Ensure the helper is called by the real panel/component/renderer path; a
   tested but unused helper does not fix an integration bug.
4. Register new tests and production sources, and preserve existing test intent.
5. Record compile/test limitations and any pre-existing warning separately from
   new warnings. Do not claim all regressions passed based on one CLI step.
6. Create a local atomic commit with a concrete subject; do not push.

When the toolchain permits, build AxleTests/AxleEditor to validate integration
compilation. A CPU-only operator must not run tests that require unavailable GPU
or UI facilities and then substitute mocks while retaining the same PASS label.
Existing regressions 47/99/102 and shared material regressions 101/104/105/106
remain operator checks where their setup requires those facilities. Run one
`--stepNNN` per process; the runner is not a reliable multi-option batch command.

Manual editor matrix for the external operator:

- Open/create an asset directly; edit all three emitters and save/reopen.
- Select the same entity repeatedly; preview continues and scene stays unchanged.
- Use narrow/wide panes; assign/replace/disable/clear texture/material/mesh slots.
- Add/delete/duplicate/rename/reorder; resolve target references and undo/redo.
- Edit curves, HDR colors and alpha keys across emitters; verify live output.
- Play/Pause/Stop/Restart/Step/scrub/speed; compare deterministic replay.
- Switch scene/project, delete a bound entity, close/reopen panel and enter/exit Play.
- Confirm shared effect reload and Make Unique behavior and no silent data loss.

These checks are pending until an operator supplies actual evidence. Keep the
phase parent unchecked even when every coding package is implemented/CPU-tested.

## 18. Required delivery and copyable assignment prompt

Deliver:

1. Atomic commit list with the mapped A1-J4 behavior and changed files.
2. Implemented APIs/schema, migration behavior and supported material/module limits.
3. Exact CPU test/build commands, exit codes, toolchain and warnings/limitations.
4. GPU/UI checks prepared, actually executed and NOT RUN, listed separately.
5. Remaining defects or unimplemented requirements, without percentage guesses.
6. Updated completion plan/Roadmap status with implementation versus external
   acceptance clearly distinguished. Do not replace pending checks with x based
   only on a source edit or the existing Step 102 message.

Suggested assignment to paste into Gemini:

> Implement the remaining AxleEngine Phase 39 work using this coding guide and
> VFX_PHASE_39_COMPLETION_PLAN.md. Inspect the current repository and user changes
> first. Start with the CPU test target, A1, the minimal B1 value-definition
> boundary needed for A2, then A2/A3, and continue in the documented dependency
> order. Adapt the proposed APIs to existing code without changing their ownership,
> persistence or verification contracts. You can implement code, run CPU unit
> tests and create local atomic commits. Implement the actual renderer/UI wiring
> and prepare registered physical GPU integration tests, but record GPU execution
> and interactive UI acceptance as NOT RUN in this environment. Preserve material,
> vehicle and character VFX behavior. Verify each available sub-step, report actual
> failures/limits and keep Phase 39 open for external acceptance. Do not push or
> begin Phase 40. Produce the required delivery list from section 18.
