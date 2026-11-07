# OPT-05 Codex review, repairs and acceptance

Date: 2026-11-07. Review candidate: `c597cc8d3ce4dd8462d599c4ddc0fe59177092d1`.
Gemini baseline: `2baf3de7223b5d3c8c77395048cb6faad8e515d9`.
Scope: P41-12..20 and P41-25; stop before OPT-06. User authorizes Codex to
replace unsuitable design/code, including structural file changes. No push.

Status: **IN PROGRESS; NOT ACCEPTED**. Gemini delivery evidence is reported,
not independently verified by this report. Source defects below are confirmed
by inspection; native reproductions and final-source evidence will be added.

## Findings

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

Current final-source verification: **NOT RUN** (repairs in progress).

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
