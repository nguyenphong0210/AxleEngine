# M14-U9 - Final Acceptance and M15 Handoff

Status: **ACCEPTED** at source `19ac2a1`.

This report closes the M14 architecture boundary. It excludes M15 lighting and
material fidelity implementation. The historical hold report remains preserved
in `docs/reviews/M14_RENDER_ARCHITECTURE_CODEX_REVIEW.md`; this report and the
consolidated U9 review are the current acceptance records.

## Acceptance scorecard

| Unit / contract | Result | Source and evidence |
|---|---|---|
| U0 baseline and migration contract | PASS | `U0_BASELINE_EVIDENCE.md`; historical P0 audit is pinned to `769a9f2` and reports history only |
| U1 View/frame ownership | PASS | `ViewContext.hpp:57-120`, `ViewContext.cpp:77-191`; `RHIFrameContext.cpp:54-110` |
| U2 pass orchestration | PASS at released boundary | `RenderPass.cpp:73-129`; live scheduler groups in `RHISceneRendererPasses.cpp:503-1768`; callback/persistent-cache adapter is explicit and quarantined |
| U3 canonical surface | PASS | `ResolvedSurface.hpp:34-112`; native built-in/custom/graph and shadow callers in `RHISceneRendererPasses.cpp:391-789` |
| U4 graph/resource lifetime | PASS | `RenderGraph.cpp:55-238`; `RHIResource.cpp:271-383`; `RHIViewTargets.cpp:57-175`; strict graph and failure/lifetime fixtures |
| U5 service ownership | PASS | service calls in `RHISceneRendererPasses.cpp:555-568` and renderer publication at `RHISceneRenderer.cpp:2611-2640`; U5 correction evidence |
| U6 temporal/post/diagnostics | PASS | `TemporalService.cpp:17-66`, `PostProcessService.cpp:30-83`, `RenderDiagnostics.cpp:44-112`, target reset in `ViewContext.cpp:77-191` |
| U7 backend/fallback policy | PASS | `RenderBackendPolicy.cpp:26-143`; native D3D12/Vulkan and reduced OpenGL/fallback runs |
| U8 migration/tooling | PASS | `RenderService.cpp:204-330`; product callers; generated owner map with zero architecture/caller violations |
| U9 final gates and handoff | PASS | This report, [consolidated review](../../reviews/M14_U9_FINAL_ACCEPTANCE_REVIEW.md), and current evidence manifest below |

## Corrections closed during U9

### U9-R1 - target generation reset through a reused wrapper (`27e6811`)

`ViewContext::Observe` previously compared only the `RHIViewTargets` pointer. A
successful in-place `Configure` changed generation and textures while keeping
the pointer, so TAA history could remain valid across a new target generation.
Implicit targets also reached observation before the final target was adopted.
The fix adds a numeric generation snapshot, adopts final targets after frame
acquisition and before jitter/history imports, and records the generation in
`RenderDiagnostics`. Native regression coverage proves unchanged wrappers keep
history, failed Configure keeps the last-good generation, successful same-wrapper
replacement resets history, and implicit same-size color replacement resets it.

### U9-R2 - retired target wrapper held after submit (`56931769`)

The native resource-view fixture exposed a real ownership leak: `ViewContext`
kept a strong target-generation wrapper in its input snapshot after submission.
The fix keeps weak target identity plus numeric generation for reset comparison,
then releases the strong wrapper after submit or abandon. RHI command/resource
leases remain completion-owned. The corrected fixture checks exact enabled target
slots, atomic failed replacement, generation preservation, pixel retention while
an explicit lease is held, and release after that lease is reset. D3D12 and Vulkan
both pass.

## Current verification evidence

The runner `build/run_m14_u9_final.py` creates immutable per-phase command logs,
source identity and binary hash manifests under `build/Tests/Work/M14-u9-final`.
All five current phases below use source fingerprint
`924cdeaffff272837890a8205fd1d3724c20aba36db4c684ea775ecb00c8824c` and HEAD
`19ac2a1a1b7fb3b1684b5e6ad4c618b0e23f50da`:

| Phase | Result | Evidence |
|---|---|---|
| Tools | PASS (6/6) | `tools-tools-20261114T145824/results.json` |
| Debug build | PASS | `Debug-build-20261114T145922/results.json` |
| Debug tests | PASS (72/72) | `Debug-test-20261114T150258/results.json` |
| Release build | PASS | `Release-build-20261114T150639/results.json` |
| Release tests | PASS (72/72) | `Release-test-20261114T151131/results.json` |

The 72-case matrices include CPU/policy contracts; native D3D12 and Vulkan
foundation, graph, frame, presentation, resource-view, service, environment,
shadow, material-graph, acceptance, preview and Runtime loose/cooked fixtures;
OpenGL compatibility/fallback foundation, service, canvas, environment, preview
and Runtime; and Editor empty-startup verification for `dx12`, `vulkan` and
`opengl` with strict RHI. Required proof strings were matched for Runtime and
Editor. The build manifests include hashes for Editor, Runtime and every
focused test executable.

The generated tracked map is
`U8_M14_DEPENDENCY_MAP.{json,md}`. It lists 18 contract owners, product caller
counts, **0 architecture violations**, **0 product caller violations**, and
617 filtered dependency edges at this source.

## M15 entry contracts

M15 work enters through these accepted interfaces:

1. `ViewContext` for view-local state, target-generation reset, temporal state and completion ownership.
2. `IRenderPass` and `RenderPassScheduler` for Configure/Declare/Prepare/Compile/Record/Finalize ordering and structured fallback results.
3. `RenderGraph`, `RHIViewTargets` and `RHIResourceService` for declared resources, physical generations, descriptor roles, leases and completion lifetime.
4. `ResolvedSurface` and `ShaderSurface` for all built-in, custom and Material Graph material inputs consumed by lighting/visibility.
5. `RenderBackendPolicy` for capability, quality tier and fallback decisions.
6. `LightingService`, `ReflectionService` and `GIService` for bounded lighting/reflection/GI policy and publication metadata.
7. `TemporalService`, `PostProcessService` and `RenderDiagnostics` for history, output policy, exposure and telemetry.
8. `RenderService` for Editor, Runtime and preview product ownership; product callers do not reach through to concrete renderers.

## Explicit boundaries and deferred scope

- The current U2 boundary intentionally retains the callback adapter and shared
  pipeline/cache ownership in `RHISceneRenderer`. It is a documented migration
  seam; adding a new M15 feature still requires scheduler ownership, declarations,
  capability policy and tests. Full per-technique class extraction is M15 scope.
- OpenGL is a supported compatibility subset with explicit reduced policy. M14
  does not claim native OpenGL Material Graph/ResolvedSurface parity with the RHI
  backends.
- M14 does not claim final SSR quality, dynamic six-face reflection capture or
  prefilter, multi-probe blending, DDGI irradiance/visibility feedback,
  ray-traced reflections, advanced material fidelity, or a validated GPU budget.
  These remain M15 or later and must retain explicit fallback/diagnostic states.

M15 remains **HOLD / unreleased** until a concrete user assignment activates its
feature scope and its own Unit evidence passes. M14 acceptance does not promote
any M15 checkbox.
