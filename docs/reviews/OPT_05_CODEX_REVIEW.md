# OPT-05 Codex review, repairs and acceptance

Date: 2026-11-07. Review candidate: `c597cc8d3ce4dd8462d599c4ddc0fe59177092d1`.
Gemini baseline: `2baf3de7223b5d3c8c77395048cb6faad8e515d9`.
Scope: P41-12..20 and P41-25; stop before OPT-06. User authorizes Codex to
replace unsuitable design/code, including structural file changes. No push.

Status: **ACCEPTED** for P41-12..20 and P41-25. Final production/test source:
`de97d9b`, following corrective commits `5f3f7bd`, `2e3a77c`, `c9a6572`,
`83f28e1`, `3171d7d`, `ed1ec07`, `e2f6ee2` and `2653a46`.
Gemini's original delivery claims were treated as reported evidence. Codex
verified the corrected implementation through the actual final gates below.

## Original findings (closed)

- **OPT05-R1 / P1:** Extraction mutates through const_cast, invokes GPU resolution
  during registry iteration and retains mutable material objects. CPU extraction
  and immutable render snapshots do not satisfy P41-12.
- **OPT05-R2 / P1:** RenderService reports success after void render early exits and
  fabricates tokens. Default target replacement and service/device epoch lifecycle
  are unsafe.
- **OPT05-R3 / P1:** Editor main view bypasses service; duplicate RenderWorld paths
  drop existing custom shader/VFX and GL texture/animation/environment behavior.
- **OPT05-R4 / P1:** Runtime ignores render failure, blindly copies texture to a
  swapchain without matching format/extent, retains singleton state across teardown,
  and Canvas counts draws without actual submission.
- **OPT05-R5 / P1:** GL thumbnails return a local FBO attachment ID after its owner
  is destroyed. Preview/thumbnail target/error/invalidation paths need real tests.
- **OPT05-R6 / P2:** Capability flags claim modern shadows/skinning/async compute
  without implemented service support. New layout contracts must match actual
  bindings rather than parallel declarations.
- **OPT05-R7 / P1:** Sequential declared-usage graph and physical consumer/output
  gates are absent; default-value CPU tests do not establish these contracts.

## Required evidence

Full Debug/Release builds, CTest, default integration, jobs/ECS and simulation
products; focused physical D3D12/Vulkan API validation/readback for service and
consumers; GL/fallback baseline and preview behavior; Runtime startup/present,
resize and actual Canvas pixels/input; architecture/baseline tool checks.
Significant Codex corrections receive a separate-context final recheck.
P41-26 Editor-disabled build closure is explicitly a later package; no claim of
that closure is made by P41-19 behavior checks. Missing required checks stay NOT RUN.

Current final-source verification: **PASS in Debug and Release**; final evidence below.
The checkpoint sections preserve intermediate progress and failed attempts; the
final acceptance ledger records the current conclusion.

## Correction checkpoint 1

Owned CPU snapshot material/pose/particle values replace const mutation and live
simulation pointers. GPU mesh resolution happens after extraction on local
records. Shared view settings no longer include Scene/Physics implementation
headers from Renderer. The mature custom-material/VFX modern path now consumes
RenderWorld instead of a second reduced implementation. RenderService uses one
snapshot adapter, checked RHI frame submissions and actual completion tokens,
transactional default targets, owner/context/epoch checks and pre-device-close
retirement. No GL renderer is constructed without a current GL context.

Debug AxleRenderUnitTests, AxleRHIResourceViewTests and new AxleRenderServiceTests
builds passed (opt05-*-build-debug.log under ignored OPT-04 runner folder).
The CPU suite passed, including owned material/purity regressions. New physical
D3D12/Vulkan service tests passed with actual API validation: intended red/green
pixels, immutable prior snapshots, OOM replacement retention, zero-size/private
view rejection, real completion identity, ambiguous submit failure stopping work,
and same-service device epoch restart. Exact logs: OPT-05/service2-*.log and
service2-results.json. An earlier test incorrectly expected ambiguous queue-submit
failure to be recoverable; production correctly stops the device until restart.
The regression now checks that contract explicitly.

This is intermediate self-verification only. Release, consumer migrations, graph,
Runtime/Canvas, remaining GL baseline details and final independent correction
recheck are still pending. No phase acceptance is claimed.

## Correction checkpoint 2

Main Editor uses the shared RenderService. GL snapshot execution restores sampled
material maps, authored lighting/shadow/environment values and skin palettes.
Thumbnail requests now queue bounded work, own color/depth generations or a GL
framebuffer, reacquire native draw-list pins on cache hits, and render material
assets through the actual service instead of a separate CPU sphere approximation.
Cache/queue entries are bounded; invalidation retires GL owners through the UI
frame boundary. Builtin assembly hierarchies use canonical identities and bounds.

Native consumer pixel checks exposed omitted MaterialComponent output in
SaveAssemblyData; the existing material serializer block is now used for node
saves as well. They also reproduced GL debug renderer initialization before Jolt's
allocator; SceneRenderer initializes PhysicsEngine before constructing that bridge.
Debug consumer3-results.json records actual D3D12/Vulkan/GL thumbnail pixels and
invalidate-before-submit lifetime checks, all exit 0. Debug AxleEditor and focused
target builds pass. These are intermediate checks, not phase acceptance.

## Correction checkpoint 3

Canvas now has a real GL and NRI adapter with valid 80-byte vertices/indices,
owned per-quad image descriptors, clip/scissor, alpha, sprite images and a CPU
coverage font atlas rasterized from the installed Windows monospace font. Existing
explicit external MSDF indices keep their geometry contract. UI shaders are
embedded from the canonical source during configure, so the player does not need
to find source files at draw time. Canvas CPU collection reports zero draws until
a real submission; old CPU-only fixtures now assert that honest count. Actual
pixel regressions independently check glyph strokes, sprite color, partial clips
and alpha on GL/D3D12/Vulkan (canvas-stride-results.json, Debug all exit 0).

Presentation samples the completed image through a fullscreen pass, supporting
RGBA/BGRA format conversion and different/odd extents instead of invalid raw
CopyTexture. Physical D3D12/Vulkan probes present seven frames each including
resize, with actual API validation and no ImGui. Runtime now rejects renderer
startup/render/Canvas failure and shuts service ownership down with the layer.
Required final-source/full-product Debug/Release gates remain pending.

## Correction checkpoint 4 (in progress)

The production modern path compiles a sequential, whole-texture resource-use
graph and records its declared passes in order. It validates dependencies,
first-use/read-before-write, attachment LOAD/store/discard, duplicate/aliased
uses and layout/access mismatches. Recording pins imported generations and
obtains native transitions from the existing transactional state journal.
Actual opaque/sky, refraction copy, transparent, particle depth copy, particle/
physics, MSAA resolve, tone mapping, FXAA and export passes use this path.

Debug service readbacks pass all eight MSAA/tone/FXAA combinations, repeated and
empty views, then return to the original image without optional-pass leakage.
Physical D3D12/Vulkan and GL focused tests pass (reflection-focused-debug-results
and reflection-stage-debug-*.log under OPT-05). Capability flags no longer claim
modern shadows, skeletal skinning or async compute, or nonexistent GL MSAA views.
The forward pipeline consumes the canonical vertex description and actual frame/
light/material register constants; decoded LDR slot formats match uploads.

Existing material regressions exposed lost default native sky behavior after
extraction; snapshots now distinguish authored sky settings from renderer fallback.
Reflected material schema/status is published on the scene owner after rendering,
checked against snapshot UUIDs, rather than mutating authoring during extraction.
Enabling API validation for existing material tests exposed NRI 190's zero-valued
StageBits::ALL in reflected pipeline layouts. Reflection now retains concrete
shader and shared-resource stage unions, including resource-free shaders.
Custom programs consume this concrete visibility. Built-in draw and post-process
preparation failures reject the view instead of reporting a successful blank draw.

Debug material step 106 and VFX GPU regressions pass on both physical backends
with actual API validation and Vulkan synchronization validation. Full integration
completed successfully (reflection-stage-debug-results.json, all five cases exit 0). Architecture checking identifies direct GL/GDI links
from Canvas in UI2D; the pending correction moves GPU execution to Renderer and
native CPU font rasterization to Platform. Release, Runtime product fixtures,
remaining consumer gates and the independent correction recheck are still pending.
This checkpoint is not acceptance.

## Correction checkpoint 5

Canvas GPU execution and embedded shaders now belong to Renderer. Its input is
an owned caller batch of canonical vertices, image sources and pixel clips;
Renderer has no dependency on UI layout or document types. UI2D keeps layout,
input, text geometry and submission statistics. Platform owns the Win32 CPU font
rasterizer and caches the immutable atlas once; GDI is a declared Platform system
dependency. The architecture gate passes (416 files, 18 CMake files, AF-04 closed).

The GL Canvas adapter now restores caller texture/buffer/program/VAO bindings,
sampler, blend equations/factors, color mask, viewport/scissor and depth/cull/
stencil/sRGB enables, including lazy image initialization and early returns.
A real GL regression seeds conflicting state, verifies actual intended pixels,
then checks restoration. Debug Editor, Runtime and focused targets build; all
nine focused CPU/D3D12/Vulkan/GL cases pass with actual native API validation
(canvas-boundary-focused-debug-results.json). Full final-source gates and the
Runtime loose/cooked product fixture remain pending. No phase acceptance yet.

## Correction checkpoint 6

Runtime project/package/backend resolution now completes on CPU before backend
candidates are constructed, including absolute paths to virtual project files in
exports. Explicit CLI selection overrides the project's backend. The reusable
player core owns startup, layer and presentation; the executable uses the same
implementations as its physical product probe. Application reports presentation
or UI failure and supports a bounded loop for automation. Runtime rejects a
configured startup scene that cannot be loaded, closes the device on failure,
and registers its existing pause-menu actions.

The product fixture exposed a UI round-trip defect: Image serialization wrote
`texture`, while UIImageNode only accepted `source`/`texturePath`. The canonical
serialized property is now read. Rejected render requests also no longer publish
stale reflected material feedback from an earlier successful view.

Debug Runtime product probes pass all eight loose/cooked x D3D12/Vulkan/GL/forced
GL fallback cases. They run the production Application/layer/presenter, read real
mesh, sprite, font and particle pixels, resize the actual window, click the loaded
UI before/after DPI change, pause/resume and check presentation failure/teardown.
Native probes require installed API validation and zero NRI/API errors; Vulkan
confirms synchronization validation. Cooked fixtures contain no loose authored
payload. GL fallback matches explicit GL byte-for-byte before and after resize.

The actual AxleRuntime.exe also passes six bounded loose/cooked startup/present
cases; Vulkan inherits its backend from the project without an RHI CLI override.
Six missing-startup-scene executable cases fail with exit 1 as required, without
silently substituting an empty scene or producing API errors. Evidence is under
OPT-05/runtime-fixture-debug-results.json, runtime-exe-debug-results.json and
runtime-negative-debug-results.json. The focused service regression passes after
the stale-feedback correction. Final Debug/Release matrices, remaining consumer
checks and independent correction recheck still block phase acceptance. P41-26
Editor-disabled build closure remains a separate later package.

## Correction checkpoint 7

Further source inspection found that AXLE_BUILD_RHI is never defined by the
current build. Its stale guards compiled out Assembly's native viewport and the
actual Runtime executable's presentation context. The earlier Runtime library
fixture proved the shared presenter, but the executable smoke alone did not
prove presentation. Those guards are removed (RHI is an unconditional dependency).
The rebuilt executable now logs its real swapchain/native sampled context and
checked present count. The replacement runtime-present-final Debug smoke passed;
the complete final matrix will rerun the executable with present-count assertions.

Assembly Editor initializes and registers its native viewport with the host GUI
registrar. Asset Preview preserves actual hierarchy transforms/materials, retains
CPU imported part sources and no longer substitutes cubes for missing parts.
Model packages are distinguished from Assembly files; native Mesh assets are
supported in preview/thumbnail paths. Shared resolution retains normals/UVs and
caches immutable source revisions; file mesh caches observe source/project changes
and allow mounted VFS meshes. RHIMeshView captures strong buffer generations, so
a saved world keeps its geometry even after its mutable wrapper is replaced or
shut down, without GPU work during extraction.

GL panel images retain framebuffer generations at the UI frame boundary and
resize by publishing a replacement, rather than destroying queued attachments.
Thumbnail limits apply to raw images and 3D jobs (256 cached/pending, six renders
per update). Root and used mesh/material/texture/include revisions invalidate
results, including source deletion. GL uses the actual key/fill light rig,
opaque/cutout/transparent/additive states and sorted transparent queues, with
sky rendered before transparency/particles. Physics GL debug lines preserve the
requested depth bias/alpha; the native path consumes the requested active scene.

Debug physical probes pass actual Assembly/Asset Preview material/hierarchy and
imported multi-part pixels, queued close/resize, main-scene isolation, thumbnail
budgets/revisions/deletion, three-backend surface/second-light pixels and active
PhysicsScene edit/play/pause/stop/close/alpha output. Evidence: preview-imported,
preview-surfaces, frozen-geometry-previews and preview-lastcheck result JSON/logs.
CPU/D3D12/Vulkan/GL focused service/Canvas/thumbnail probes also pass, including
the new saved-geometry generation test (frozen-geometry-focused-debug-results).

An initial panel probe incorrectly read an RGBA16 target using an RGBA8 readback
helper; API validation rejected that probe. Assembly now explicitly declares its
SDR GUI output. The GL probe also initially treated numeric texture-name reuse
after a completed frame as a defect; the corrected test checks non-reuse while
the prior image is queued in the same frame, and explicitly retains its manually
queued GL image just as production panels do. Failed logs are retained.

These are corrective self-checks. Final Debug/Release builds and complete gates,
followed by the separate-context correction recheck, still block acceptance.

## Separate-context correction findings (checkpoint 8)

An independent read-only correction review of the candidate through e2f6ee2
found five remaining defects. These are actual source findings, not acceptance:

| ID | Severity | Trigger and correction |
| --- | --- | --- |
| OPT05-FR-01 | P2 | Canvas full-circle/partial arcs used inconsistent modulo and active endpoint conventions. Normalize CPU/GL/HLSL spans and compare relative active spans; physical probes cover 0/50/100 percent full and partial arcs. |
| OPT05-FR-02 | P1 | GL VFX snapshots lost path-only textures/materials and local transforms. Freeze effective material values with metadata-only loading, strip live physics callbacks, resolve images outside extraction and share CPU packet transforms with native rendering. The recheck additionally identified attached-depth feedback; GL now snapshots opaque depth into a detached generation and disables soft sampling for zero softness. |
| OPT05-FR-03 | P1 | Both Assembly writers/parser dropped advanced inline material properties. Scene and Assembly now share one complete authored MaterialComponent codec, including typed shader values and excluding transient feedback. |
| OPT05-FR-04 | P2 | Fully clipped Canvas batches produced zero draws and were incorrectly treated as submission failure. Separate successful submission from draw count; keep GPU failures unsuccessful. |
| OPT05-FR-05 | P2 | A no-light GL view inherited a preceding private view's directional light. Resolve the fallback direction per request. |

The first arc-only Debug focused matrix passed all nine CPU/native/GL cases
(fr01-focused-debug-results.json). The complete repairs and new regressions are
being built and remain unaccepted pending actual results and reviewer recheck.

Checkpoint 8 corrected-source self-checks: full Debug AxleAllTests/AxleEditor/
AxleRuntime build passed (opt05-review-repairs2-debug.log); final fixture-only
rebuild passed (opt05-particle-owner-debug.log). Nine focused CPU/service/Canvas
cases passed (review-repairs-debug-results.json). Nine panel/physics/surface
cases passed; the three new particle cases passed after fixture corrections
(particles-owner3-results.json), with native API/synchronization validation and
zero detected API errors. They verify textured/material multi-emitter pixels,
local transforms, texture toggles, zero texture allocation during GL extraction,
opaque-depth intersection fading, actual VFX panel output and source isolation.
CPU regressions cover all authored material fields and all eight shader property
types across Scene, both Assembly writers/reload and entity instantiation, plus
snapshot callback removal. Fully offscreen clipped Canvas succeeds with zero
actual draws and unchanged destination pixels on all three backends.

The separate-context reviewer inspected the five corrections and affected callers,
identified and rechecked metadata-load side effects, depth feedback and texture
completeness, and reported no remaining P1/P2 finding. Its evidence is source
inspection and diff checking only; the physical results above were run by the
implementing Codex context. Full Debug/Release gates remain pending.

Preserved failed intermediate logs include the test's initial raw-pointer versus
shared-target compile error, a material fixture authored outside its active asset
root, and a probe constructing two GL render services/Jolt debug renderers. The
final fixture uses the production shared owner and a scoped real asset root;
production assertions were not suppressed.

## Full-gate correction checkpoint 9

The first full Debug gate at 2653a46 passed 50/50 CTest, focused 9/9, previews
12/12, Runtime fixtures/executable/negative cases, jobs/ECS, full default
integration, architecture and baseline tools. It did **not** pass acceptance:

- **OPT05-FR-06 / P1:** The older physical canonical-primitive gate on D3D12 and
  Vulkan exposed a completely unassigned MeshInstance3D aborting resolution of
  the entire view. Extraction now excludes only components with no URI, CPU
  source or GL/native geometry. Nonempty missing asset references still fail.
- **OPT05-FR-07 / P1:** The older VFX GPU gate on both native APIs exposed unsaved
  inspector material edits being lost to disk-only metadata loading. Extraction
  now reads already loaded authored material values through a read-only cache
  lookup, checks current path/UUID identity and freezes effective values. Cache
  misses still use metadata-only parsing. CPU tests reject another project's
  same relative alias and replacement UUIDs; physical snapshots retain old values
  while a new snapshot reflects unsaved edits.
- **OPT05-FR-08 / P1:** The actual Editor GL simulation probe asserted Jolt's
  singleton because Editor retained a legacy PhysicsDebugRenderer beside the
  service-owned adapter. The obsolete Editor owner is removed. Rendering and
  current-view line statistics are owned by the shared adapter; disabled/empty
  debug passes report zero.

First-attempt gate/API/regression logs are preserved under
build/agent-runs/OPT-05/attempt-2653a46. The default integration itself passed;
its wrapper failed because the two explicit VFX GPU cases failed. These failures
are not suppressed or treated as unavailable checks. Corrections are being built;
final gates and separate-context recheck remain required.

Checkpoint 9 Debug self-verification now passes full AllTests/Editor/Runtime
build (opt05-full-gate-fixes-debug.log), nine critical cases
(critical9-results.json: CPU, old primitive/VFX gates on both native APIs, three
particle consumers and actual Editor/Runtime simulation), and all nine focused
CPU/service/Canvas cases (final9-focused-debug-results.json). The cache identity
fixture initializes a real asset root and uses a fresh workspace per invocation.
A separate-context reviewer rechecked the source repairs, including current
path/UUID guards and per-view zero line counts, and found no remaining P1/P2
issue. That reviewer performed source inspection only. The full final-source
Debug/Release matrix is still required before acceptance.

## Final acceptance

All OPT05-R1..R7 and OPT05-FR-01..08 findings are closed by the correction
commits and verified behavior above. The consolidated independent source review
and focused separate-context correction rechecks found no remaining P1/P2 issue.
Reviewer evidence is source/caller inspection and diff checks. Build, integration,
physical pixel/API and product results below were executed by the implementing
Codex context; they are not represented as independently executed by that reviewer.

Final commands (all exit 0), run sequentially per configuration:
`python build/agent-runs/OPT-05/build.py <Debug|Release> opt05-final9-source AxleAllTests AxleEditor AxleRuntime`
and `python build/agent-runs/OPT-05/final_gates.py <Debug|Release>`.
The complete command lists, durations and logs are recorded in
`build/agent-runs/OPT-05/final/{debug,release}/results.json`.

| Final gate | Debug | Release |
| --- | --- | --- |
| Full AxleAllTests / AxleEditor / AxleRuntime build | PASS | PASS |
| Full CTest | 50/50 | 50/50 |
| CPU/shared service/Canvas focused cases | 9/9 | 9/9 |
| Actual panel/physics/surface/VFX consumer cases | 12/12 | 12/12 |
| Runtime loose/cooked product fixtures, four backend choices | 8/8 | 8/8 |
| Runtime executable starts/presents plus GL fallback pixel comparisons | 10/10 | 10/10 |
| Missing startup-scene executable failures | 6/6 expected failures verified | 6/6 expected failures verified |
| Physical D3D12/Vulkan API gates, including teardown | 35/35 | 35/35 |
| Explicit material/VFX native gates plus full default integration | 5/5 | 5/5 |
| Jobs/ECS harness | 16 PASS, 0 defects, 3 controls | 16 PASS, 0 defects, 3 controls |
| Actual Editor/Runtime simulation product probe | PASS | PASS |
| AF-04 architecture boundary check | PASS | PASS |
| Baseline tools | 18/18 | 18/18 |

Native runs use the RTX 3070 with installed graphics API validation and NRI
validation; Vulkan confirms synchronization validation. Physical result records
contain no detected API-validation errors, including shutdown. Native Runtime
executable probes assert eight actual presented frames. Explicit GL and forced
fallback have byte-identical mesh/Canvas outputs before and after resize.
The earlier failed full attempt remains under `attempt-2653a46`; no failed
required gate was waived. NRI stays clean at
`6998e3fbbaed1b132f386a9d49743a2481003487` (API 190).

Accepted limits: the render graph is sequential with whole-texture usages;
advanced modern scene features remain explicitly unsupported in capability
reporting and retain their supported GL path. P41-26 Editor-disabled build
closure and other later packages remain outside this acceptance. No OPT-06 work
is released or implemented, and no push was performed.
