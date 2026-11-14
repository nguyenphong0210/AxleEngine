# Milestone 15 implementation and acceptance recovery plan

**Status:** Proposed execution plan; no rendering implementation is released by this document.
**Prepared by:** Codex, 2026-11-13.
**Historical inspected source baseline:** `791c344`.
**M14 code boundary for future M15 work:** `19ac2a1`; no M15 implementation is released at this boundary.
**User objective:** Smooth, believable surfaces; reflective materials that show surrounding scene geometry; a complete, usable material system and an honestly verified Milestone 15.
**Authority:** The latest user request is detailed planning. Do not start implementation merely because this plan exists.

This is a correction and completion plan for the existing [PLAN.md](PLAN.md) and [ARCHITECTURE.md](ARCHITECTURE.md). Package IDs below are implementation checkpoints, not new roadmap phases. Preserve the canonical Phase 45 numbering and U0-U10 ownership. Shared measurement work must follow Milestone 19 contracts without treating the whole of Milestone 19 as released.

## 1. Scope, preservation, and acceptance boundaries

- Preserve working CSM, local shadows, environment baking/publication/cache, useful BRDF work, and RHI ownership infrastructure unless focused verification finds a defect.
- Do not hard-reset the shared checkout. `Samples/Sandbox/assets/scenes/Test.axlescene` has user changes at the inspected baseline. Do not overwrite, stage, or commit them.
- Save a recovery reference and a non-destructive backup of user changes before any future structural implementation. A separate worktree may be used for before/after comparisons; it is not permission for concurrent source writers.
- Keep implementation, self-verification, independent review, and acceptance as separate states. A compiled shader, allocated texture, dispatched pass, nonempty image, or working UI toggle is not evidence of algorithm correctness.
- Core readiness, full M15 contract completion, experimental DDGI readiness, and precise dynamic mirror rendering are distinct acceptance results. Do not silently remove failed items from the full milestone claim.
- The existing M15 reflection contract is SSR + local captures + sky IBL. It is approximate. Accurate off-screen dynamic reflection requires geometry tracing and is recorded separately in P6B; it cannot be promised by an SSR repair.
- Numerical limits below are proposed acceptance thresholds, to be frozen with fixtures before implementation. Record any justified adjustment before viewing the final candidate; never loosen a failing assertion just to obtain PASS.

## 2. Source findings that this plan must close

| Finding | Observed source behavior at baseline | Required closure |
|---|---|---|
| F01 Surface data | `ScreenNormal` is allocated/read but has no rendering producer. | Defined, material-consistent surface output; every consumer declared in the graph. |
| F02 Hi-Z | One pinned descriptor set is reused; later updates fail and are ignored. | Correct source/destination per level; checked operations; validated depth reduction. |
| F03 SSR integration | No roughness cone tracing; dielectric F0 for metals; additive integration over existing IBL. | Material-aware reflection selection, filtering, confidence, and energy-consistent resolve. |
| F04 Probes | First probe only; shader fallback is constant gray rather than cubemap sampling. | Actual scene capture/load, filtering, parallax correction, spatial selection/blending. |
| F05 Occlusion | GTAO/contact factor multiplies the full shaded image. | AO applied to appropriate indirect terms; contact visibility applied to its light. |
| F06 Exposure | Per-group histograms race on one texel; displayed exposure ignores compute output. | Global histogram/reduction and a defined adapted exposure consumed by tonemapping. |
| F07 Bloom | Repeated thresholds; finer levels overwritten; intensity/tint applied repeatedly. | Reconstructed multiscale bloom with controlled gain. |
| F08 Temporal | Camera reprojection only; insufficient history reset; TAA depth copy not self-contained. | Motion/depth contract, per-view history ownership and robust rejection/reset. |
| F09 Fog | Full-screen 2D raymarch rather than specified froxel system. | 3D froxels, shadowed integration and temporal history. |
| F10 DDGI | Current image resampling into an atlas; no spatial probes or shading feedback. | Actual radiance acquisition, irradiance/distance atlases, visibility and diffuse shading integration. |
| F11 Advanced lighting/materials | Representative-point rect specular; no disk LTC; wrap SSS rather than diffusion; incomplete authoring. | Required algorithms plus complete asset/scene/Editor/runtime contracts. |
| F12 Timing/evidence | Invalid probe buffer stride; direct view path does not read query results; smoke assertions. | Shared valid GPU measurements and tests that reject incorrect behavior. |

Detailed source locations are in the historical `docs/reviews/M14_CODEX_REVIEW.md`. Its FIXED labels and driver explanation are historical evidence, not current M15 status; P0 must re-audit each finding against code boundary `19ac2a1` without rewriting historical messages.

## 3. Target architecture and invariants

### Surface and lighting contract

Retain a forward renderer. Do not introduce a full deferred renderer merely to obtain screen-space inputs. Add a material-consistent depth/surface preparation pass that writes depth, shading normal, roughness and material reflection parameters; motion data is enabled when temporal features need it. Select packing/formats after validating precision and memory cost. Geometric normals and shading normals have different uses and must not be substituted silently.

Built-in and generated graph materials must share surface evaluation conventions and lighting integration, not only similarly named GGX helper functions. The graph shader currently has its own reduced direct/ambient path; material parity requires integrating it with scene lights, shadow visibility, IBL and the reflection resolve. Prepass coverage, alpha clipping, deformation, UVs and normal maps must match the color pass.

AO and per-light visibility are evaluated before the corresponding lighting contributions. Keep indirect specular identifiable until reflection resolve, so SSR can replace/blend the fallback rather than add a second full reflection. Preserve emissive and direct lighting when applying AO. Advanced diffuse scattering needs an identifiable diffuse contribution as well.

### Resource and temporal ownership

- Each graph pass declares every actual texture/buffer read and write, including all Hi-Z levels, surface parameters, histories and exposure resources.
- A recorded descriptor binding is immutable for its submission lifetime. Use distinct per-pass/per-frame sets or the existing supported generation mechanism; never ignore rejected updates.
- A view context owns target generations, previous matrices/transforms, jitter, TAA/fog/reflection histories, exposure state and completion tokens. Reset on resize, camera cut, incompatible settings/format changes, scene replacement or device generation change.
- Publish new targets/resources transactionally. On failure, abandon the incomplete recording and retain a valid prior generation or use an explicit supported fallback; do not present partial work as success.
- Render feature requirements are resolved once and used by both viewport allocation and renderer recording. MSAA combinations that lack compatible surface/depth resolve must be rejected or explicitly fall back with a diagnostic.

### Logical frame dependencies

The intended dependency chain is surface/depth preparation -> AO/contact visibility and Hi-Z -> opaque forward lighting -> SSR tracing and specular resolve -> supported SSS diffusion -> transparent/refraction -> fog composition -> temporal resolve -> HDR post-processing -> tonemapping/output -> Editor/UI overlay. Shadow atlases and probe updates provide lighting inputs. DDGI updates use prior/independent probe radiance data with a bounded feedback policy, not a recursive copy of the final shaded image.

Bloom/exposure sourcing, transparency/fog interaction and motion/jitter conventions must be fixed in P0/P8 before recording the final pass order. Luminance metering should exclude Editor/UI and bloom amplification; bloom must be composed in the same defined linear/pre-exposed HDR space. Transparent motion and unsupported material cases require an explicit policy.

## 4. Ordered implementation packages

### P0 - Freeze the contract and reproducible baseline (U0; feeds U10 acceptance)

**Work:**

1. Record base commit, dirty paths, active source writer, feature/backend support and current test provenance. Preserve the user's scene separately from fixtures.
2. Correct stale current acceptance summaries: reopen U3/U4 dataflow gates; distinguish fixed ping-pong from full TAA readiness; withdraw unsupported exposure/bloom/Hi-Z and driver claims. Preserve historical delivery messages.
3. Create deterministic reference fixtures: smooth sphere without textures; dielectric/metal roughness sweep; chrome sphere with numbered/color-coded boxes; two overlapping probes; emissive impulse; bright/dark metering scenes; moving objects/camera; fog blocker; SSS sample; enclosed DDGI room.
4. Add diagnostic captures for depth, shading normal, roughness/F0, each Hi-Z level, AO/contact masks, SSR hit/confidence, selected probe, motion, exposure histogram and histories. Debug presentation must not become a production dependency.
5. Freeze depth convention, units, roughness mapping, color space, material schema, proposed numeric thresholds, scope of reflection accuracy and baseline performance workload.

**Expected owners:** M15 docs/reviews, `Tests/src/Test_EnvironmentRendering.cpp`, focused new rendering fixtures/tests, `Tests/CMakeLists.txt`, shared benchmark definitions.

**Exit:** A known bad result can be reproduced and rejected by the relevant fixture. No all-green acceptance based on image size. This documentation/fixture checkpoint does not claim a rendering fix.

### P1 - Repair resource recording and establish valid GPU timing (U3, shared U10/M19)

**Depends on:** P0.

**Work:** Audit and check descriptor/constant updates; make bindings immutable for each recorded pass; synchronize graph declarations with actual use. Add per-view generation/completion ownership and transactional target reconfiguration. Fix the invalid timestamp probe stride and route query collection through every supported view submission path. Use a bounded asynchronous query/readback ring, completion-aware reuse and an explicit invalid-measurement state. Separate pass timing from whole-view timing; exclude CPU waits/readbacks from GPU scope interpretation.

**Expected owners:** `RHISceneRenderer.{hpp,cpp}`, `RHIViewTargets.{hpp,cpp}`, `RHIViewportDescriptor.{hpp,cpp}`, RHI command/resource/descriptor/query infrastructure, shared metrics and focused tests. Extract cohesive helpers rather than growing the monolithic renderer indefinitely.

**Exit:** Failed updates fail the view predictably; resize/toggle/allocation-failure scenarios retain valid ownership; native validation is clean. A sufficiently large known GPU workload has successful submission and valid, nonzero elapsed measurements on D3D12/Vulkan. An unsupported query is reported invalid, never zero-cost PASS. Tiny scopes need not always produce nonzero ticks.

### P2 - Complete shared material/surface data (U3, U8 authoring foundation)

**Depends on:** P1.

**Work:** Implement surface/depth preparation using the same material evaluation as color shading. Verify inverse-transpose normal handling, tangent handedness, normal-map decoding and smooth vertex normals. Add reflection parameters needed by SSR/resolve. Make built-in and Material Graph lighting paths use the common scene-lighting contract. Complete basic material asset/scene/assembly/runtime conversion and Editor controls; fix current material fields that are copied into snapshots but never reach GPU state. Enforce correct sRGB versus linear texture usage and one documented roughness mapping.

**Expected owners:** `ForwardPBR.hlsl`, `MaterialSurface.hlsl`, shared shader includes/templates, `MaterialSurfaceShader.cpp`, graph lowering/runtime/binding, `RHIMaterial`, `Material.cpp`, `MaterialComponentCodec.cpp`, `SceneExtraction.cpp`, surface targets/PSOs and material Inspector/preview code.

**Exit:** Every covered opaque pixel has defined surface data; background has a defined validity marker. Normal-map/nonuniform-scale/mirrored-transform fixtures agree with expected orientation. Equivalent built-in and graph materials agree under the same lights, shadows and IBL. Save/reload changes no supported material values. A high-resolution sphere has a smooth shading-normal field without triangle bands before post-processing.

### P3 - Correct GTAO and contact-light integration (U4)

**Depends on:** P2.

**Work:** Use the valid shading-normal/depth inputs; verify half-resolution sampling and edge-preserving reconstruction. Remove whole-scene color multiplication. Feed ambient visibility into indirect diffuse and a justified specular-occlusion model; apply contact visibility to the matching sun contribution only. Project actual contact-ray positions through the camera projection rather than using an unrelated UV offset. Retain CSM/local shadow behavior and avoid double-counting contact attenuation.

**Expected owners:** `GTAO.hlsl`, forward/shared lighting code, renderer graph/bindings, settings, diagnostic masks and occlusion fixtures.

**Exit:** Enabling AO does not darken an isolated sphere in an unoccluded reference environment beyond a frozen tolerance; emissive is unchanged. Contact shadows follow the sun and disappear when the light is absent. Grounding works at several camera angles/resolutions without introducing tessellation bands, edge halos or NaNs.

### P4 - Build and verify a real Hi-Z pyramid (U5 foundation)

**Depends on:** P1/P2; scheduled after P3 to close the smooth-surface goal first.

**Work:** Distinct immutable bindings per level; checked updates; complete graph read/write declarations. Reduce according to the frozen depth convention. Handle odd dimensions and boundaries conservatively; choose the pyramid extent/level policy from actual view size rather than assuming eight levels are sufficient for every target. Validate dimensions such as 1x1, 3x5 and 1919x1079 as well as 1080p/1440p.

**Expected owners:** `HiZ.hlsl`, `RHIViewTargets`, renderer Hi-Z recording, focused native readback tests.

**Exit:** Every level matches a CPU reduction reference within the chosen format's tolerance, including edge pixels. The test must fail if all dispatches accidentally write only level zero. All levels transition correctly into SSR reads.

### P5 - Implement local reflection captures and stable fallback (U5)

**Depends on:** P2/P4 infrastructure.

**Work:** Implement actual six-face scene capture and authored cubemap loading; exclude the receiving/self geometry where appropriate and disable recursive SSR/capture feedback during capture. Bake specular roughness mips using the established environment filtering path. Implement box/sphere influence, box projection, normalized overlap weights, sky fallback and defined capture invalidation. Support manual capture plus bounded scheduled updates; describe staleness for moving geometry. Add create/edit/capture/preview/diagnostic UI and complete serialization/runtime extraction. Capture resources follow the same generation/lifetime rules as environment assets; no per-asset metadata sidecars.

**Expected owners:** reflection capture/cache subsystem in the appropriate renderer/scene layer, `Components.hpp`, `SceneExtraction.cpp`, serializers/codecs, Editor Inspector/hierarchy, `RHIEnvironment`/bake reuse and reflection-resolve shaders.

**Exit:** A uniquely colored nearby box is identifiable in a reflection with SSR disabled. Capture orientation is correct on all faces; two probes blend without a hard seam in the reference fixture. Updating the capture changes the reflected object. Missing/failed captures fall back to sky IBL with a diagnostic, never constant gray labeled as a probe.

### P6 - Finish material-aware SSR and reflection resolve (U5)

**Depends on:** P3/P4/P5.

**Work:** Hierarchical depth traversal with cell advancement, thickness handling and hit refinement. Integrate roughness-dependent cone/footprint filtering using suitable HDR color mips, material F0 and rejection thresholds. Add robust edge/miss confidence and, if required by measured stability, per-view temporal reflection filtering with motion/depth rejection. Resolve one indirect-specular contribution from SSR and local/sky fallback; do not add an extra dielectric reflection over existing metal IBL. Define opaque/transparent coverage and diagnostic unsupported cases.

**Expected owners:** `SSR.hlsl`, surface/reflection buffers, HDR color pyramid, forward/shared lighting separation, renderer graph and view histories.

**Exit:** On-screen numbered/color-coded geometry is recognizable at the expected reflected region; moving it changes that region. Roughness progressively broadens the reflection without arbitrary brightness jumps. Gold/chrome/dielectric fixtures use their own Fresnel response. Known SSR misses fall back smoothly. Camera movement does not create persistent ghosts or abrupt black regions. Tests compare hit location/color/confidence, not only image existence.

### P6B - Precise dynamic mirror extension (beyond the original SSR/capture contract)

**Depends on:** P2/P6; share geometry-query design with P11.

**Decision checkpoint:** Inspect actual NRI/RHI tracing capabilities and scene geometry access. Select and document hardware tracing support and/or a GPU software-BVH baseline, geometry update rules, supported deformation, transparency limits and budget. Do not require an RTX-only implementation without an explicit backend policy. This checkpoint must result in a concrete implementation design and cost assessment, not a fake capability flag.

**If this stronger mirror target is activated:** Implement scene acceleration ownership/build/refit, closest-hit material/radiance evaluation, dynamic invalidation, tracing/denoising and fallback integration. Define bounce limits and capture/tracing self-intersection rules. Share tracing infrastructure with DDGI rather than creating incompatible scene copies. Planar reflections may be an additional optimization for flat mirrors, not a solution for curved spheres.

**Exit:** An off-screen moving box remains correctly reflected by the sphere within documented trace/bounce/geometry support. If this package is not implemented, explicitly label P5/P6 as approximate raster reflections; do not claim exact dynamic mirrors or count P6B as delivered.

### P7 - Implement actual LTC rectangular/disk lighting (U5)

**Depends on:** P2; execute after the primary sphere/reflection correction.

**Work:** Integrate validated LTC transform/amplitude tables and horizon-clipped emitter integration. Implement rectangle and disk emitters, orientation, sidedness, units and range policy. Keep area-light reflection/lighting energy consistent; avoid adding a visible captured emitter plus an unaccounted duplicate analytical contribution. Complete components, extraction, asset/scene authoring and Inspector controls. Shadow support must be described separately from area-light BRDF integration.

**Expected owners:** shared PBR/LTC shaders, GPU light layout, emitter components/extraction/codecs, Editor authoring, LUT loading or reproducible generation, reference tests.

**Exit:** Compare a grid of roughness/view/emitter angles with a high-sample numerical emitter-integration reference. Wider emitters broaden highlights coherently; disk/rectangle shapes differ correctly; grazing/behind-surface/zero-intensity cases remain finite. Record area-shadow limitations rather than implying LTC provides soft shadows automatically.

### P8 - Correct HDR post-processing and TAA (U7)

**Depends on:** P1/P2/P6; split into separate completed commits/checkpoints.

1. **Exposure:** Clear/build one global histogram, reduce it, adapt a defined previous exposure using actual delta time and feed the result into tonemapping. Define first-frame/cut behavior, clamping, black-frame handling, metering source and per-view state. Avoid using UI/bloom for metering.
2. **Bloom:** Threshold extraction once, build six levels, reconstruct using finer-level contributions, and apply tint/intensity once at the documented stage. Use source/destination dimensions correctly and define odd-size/small-view behavior.
3. **TAA:** Produce camera/object motion vectors with consistent jitter, current/previous transforms and deformation policy. Bind current depth/color explicitly without relying on GTAO/SSR side effects. Reject occluded/disoccluded history and reset on incompatible view changes. Define transparent/reactive treatment and unjittered UI/gizmo composition. Do not silently stack FXAA on TAA.

**Expected owners:** `AutoExposure.hlsl`, `Bloom.hlsl`, `TAA.hlsl`, tone-mapping shader, surface/motion passes, view context/targets, settings and post-process controls.

**Exit:** Uniform known luminance and mixed bright/dark tiles produce the global expected exposure; adaptation is approximately frame-rate invariant at 30/60/120 Hz. Bloom impulse captures show the expected spatial halo and proportional intensity without repeated tint/gain. TAA reduces measured temporal variance on a controlled static/subpixel-motion fixture while a moving object and disocclusion meet separately frozen ghost/error limits. Toggle/resize/view-switch tests never reuse an incompatible history.

### P9 - Replace fog prototype with shadowed froxel fog (U6)

**Depends on:** P1/P2/P8 temporal ownership.

**Work:** Allocate bounded 3D froxel scattering/extinction resources, with the documented 160x90x64 reference quality and scalable dimensions. Inject density/media and supported light/shadow visibility, use physically consistent extinction/scattering integration, then integrate/composite into the scene. Add temporal reprojection/rejection with camera-cut and volume-change invalidation. Define interaction with transparent geometry and depth. Zero density/intensity and no-light cases must have sensible, documented results; no invented sun.

**Expected owners:** fog shaders/passes/3D targets, light-shadow sampling, fog component/settings/Inspector, temporal context, memory and reference fixtures.

**Exit:** A wall occludes direct sun scattering in the reference scene; a slit creates correctly bounded shafts. Zero density preserves the image; no sun produces no sun shafts. Moving camera/blocker tests control trails; memory usage and frame cost are reported for each quality preset.

### P10 - Complete advanced materials and their authoring (U8)

**Depends on:** P2/P7/P8; preserve the basic PBR material path.

1. Charlie sheen with consistent direct/environment response and layering energy checks.
2. Beer-Lambert absorption using documented path thickness, plus wavelength-dependent IOR/dispersion where supported; distinguish approximate screen refraction from traced thick-glass behavior.
3. Screen-space SSS using separated diffuse lighting, material/profile masks, depth/normal-aware separable diffusion and an undiffused specular contribution. Do not call wrap lighting diffusion SSS.
4. Complete parameters/defaults/clamps across native assets, inline components, extraction, GPU packing, Inspector, Preview and runtime. Extend the shared MaterialSurface/graph output for the supported advanced inputs, with matching lowering, resource bindings and current-format serialization; do not leave shader-only support labeled fully authored.

**Expected owners:** shared material shaders/layouts, SSS passes/targets, `Material.cpp`, `MaterialComponentCodec.cpp`, graph nodes/lowering/templates/runtime, Editor material controls and asset preview.

**Exit:** Absorption agrees with exp(-sigma*d) for known thickness samples; sheen responds at grazing angles without unbounded energy. SSS spreads diffuse response inside the surface and stops across silhouettes/material boundaries while preserving specular detail. Built-in/graph equivalents agree in the supported scope; save/reopen and Editor/Runtime render preserve authored values. Explicitly document unsupported transparent/volume cases.

### P11 - Implement experimental DDGI rather than screen-image copying (U6)

**Depends on:** P1/P2/P8; geometry-query design coordinated with P6B.

**Design gate:** Before implementation, choose a real scene-radiance acquisition method with an executable native prototype and measured cost: shared geometry tracing or amortized raster probe captures. Reusing the final camera image is not acceptable. A CPU reference may validate results but must not be disguised as the production GPU update path. If the geometry/query prerequisite cannot be supplied, leave DDGI explicitly incomplete.

**Work:** Spatial probe indexing/origin/spacing, bounded grid/atlas sizing, octahedral irradiance and distance first/second moments, Chebyshev visibility with bias/weight policy, temporal accumulation and staggered update scheduling. Feed interpolated irradiance into the diffuse lighting term and honor per-grid/per-view settings. Define scene-change invalidation, no-valid-probe fallback, inside-geometry handling, ray/capture budget and multibounce feedback stability. The 32x16x32 reference grid and 25% update target must be validated against measured memory/work, not assumed affordable.

**Expected owners:** dedicated probe-grid/update resources and radiance acquisition, `DDGI.hlsl` replacement, ForwardPBR/shared lighting bindings, component/grid authoring and diagnostics, scheduling/history and native GI fixtures.

**Exit:** A colored indirect bounce illuminates a region without direct light; a separating wall suppresses leakage within frozen tolerance. Results follow world probes rather than the current camera image. Grid origin/spacing/energy/update settings have tested effects. Probe memory/update count stay bounded; disabled or unavailable DDGI returns to valid IBL/ambient behavior. Record experimental status independently of core acceptance, without claiming full M15 completion while promised GI work remains absent.

### P12 - Integrate Editor quality controls and perform final acceptance (U10/S10.5, Phase 45.18)

**Depends on:** All packages in the declared M15 delivery; report P6B separately.

**Work:** Finish component creation/Inspector/debug views for lights/probes/fog/GI. Add reproducible Low/Medium/High/Ultra settings with explicit resolution/update/sample budgets and capability diagnostics. Add a Preview mode matching the scene's lights/environment/exposure/features. Keep the isolated studio preview as an alternative. Promote only verified features to defaults. Reuse shared benchmark/image-difference tooling and report all remaining limitations.

**Exit matrix:**

| Gate | Required evidence |
|---|---|
| Builds | Affected Editor, Runtime and test products rebuilt in Debug and Release from the final source. |
| Native behavior | D3D12 and Vulkan on a physical supported GPU; feature-specific assertions and actual windowed Editor image inspection. |
| Compatibility | OpenGL and unsupported capability profiles exercise their documented fallback; no false parity claim against unavailable effects. |
| Material quality | Smooth sphere; roughness/metallic sweep; graph/built-in matching; local reflection hit/miss/fallback; authored save/reopen. |
| Temporal/reliability | Camera/object motion, cuts, toggle combinations, resize/minimize/restore, separate views, resource failure/retry, shutdown/reinit and a >=1,000-frame soak. |
| Parity | For deterministic equivalent outputs, M19 rollout thresholds: PSNR >=40 dB, SSIM >=0.985 and channel error <=4/255; specify the comparison space and separately justify temporal/statistical cases. |
| Performance | Release 1080p/1440p, fixed scene/camera/settings, proposed 120 warm-up +1,000 measured frames; valid per-pass/aggregate GPU timing, CPU/GPU p50/p95/p99/max, memory and update workloads. |
| Budget | Report which named lighting passes satisfy the original 3.5-5.0 ms target on the named GPU/preset; report total frame time separately. An FPS goal cannot be inferred from lighting time alone. |
| Portability | Record GPU/driver/CPU/OS/commit. One available vendor cannot substantiate M19's multi-vendor production-promotion gate. Mark missing hardware runs NOT RUN. |
| Independent review | One consolidated final source review and focused separate-context recheck of significant Codex corrections under the shared protocol. |

Do not make a blanket performance claim for maximum DDGI/fog/reflection settings if only a lower preset meets budget. Tune quality/cost transparently; do not hide regressions by disabling an effect in the benchmark.

## 5. Delivery checkpoints and definition of done

The implementation order is P0 -> P1 -> P2 -> P3 -> P4 -> P5 -> P6 -> P7 -> P8 -> P9 -> P10 -> P11 -> P12. P6B is a separately identified mirror extension, coordinated with P11 before selecting shared tracing infrastructure. Dependencies permit some disjoint work, but the shared checkout has one source writer unless the user explicitly authorizes otherwise.

User-visible checkpoints:

1. **Surface quality:** P1-P3 produce defined surface data and a smooth sphere with correct occlusion; this is not reflection completion.
2. **Raster reflections:** P4-P6 show nearby geometry and correct material response, with documented SSR/capture limitations.
3. **Stable material rendering:** P7/P8/P10 complete area lighting, temporal/post and authored material behavior.
4. **Remaining M15 scope:** P9/P11 complete fog and experimental GI to their declared contract.
5. **Acceptance:** P12 records supported quality tiers, measured costs, actual review and all open gates. P6B has its own exact-mirror acceptance result.

For each completed checkpoint, record the problem, final behavior, affected callers, exact builds/tests/configurations/exit codes and artifact paths. Keep one coherent behavior and meaningful verification per local commit; do not push. Package self-checks precede the consolidated final review. Never mark another agent's review as performed, invent a result or replace a required runtime check with an architecture script.

Schedule estimates should be assigned after P0/P1 and the P6B/P11 feasibility gates. The largest uncertainty is real scene-radiance acquisition, followed by graph/material integration and temporal correctness. This is substantial renderer completion work, not a small sphere tessellation patch.
