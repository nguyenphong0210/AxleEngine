# Milestone 16 — Ray Tracing Backend Plan

**Status** — Execution plan. Implementation starts at the explicitly released package or batch boundary.
**Baseline** — The accepted M15 U11-E source and evidence, including raster lighting/material fidelity, planar and reflection capture, stateful `IRenderPass`, unified surface lighting, frame completion and transient resources.
**Supporting architecture** — [`ARCHITECTURE.md`](ARCHITECTURE.md).

M16 adds a capability-safe hardware ray-tracing backend. Every RT feature has a raster fallback and a separate evidence gate. The milestone does not claim support merely because a GPU reports an RT capability bit.

## 1. Entry gate and execution order

Record the accepted [M15 U11-E source, support matrix and post-refactor scorecard](../MILESTONE_15_LIGHTING_MATERIALS/PLAN.md#m16-handoff--ray-tracing-backend) before the first M16 implementation checkpoint. Reuse the M15 reference scenes and evidence tools where applicable. M16 adds hardware shadows and reflections to the raster renderer. M17 geometry/visibility work and optional `RT-09` have separate release boundaries.

Package IDs identify feature scope. Batch IDs identify buildable deliveries with a primary owner for each atomic requirement. The batch order brings shader compilation and history forward where another package needs them.

```text
M15 U11-E accepted
  -> RT-01A -> RT-01B -> RT-03A -> RT-01C
  -> RT-02A -> RT-02B -> RT-02C -> RT-02D
  -> RT-03B -> RT-03C
  -> RT-04A -> RT-04B -> RT-05A
  -> RT-06A -> RT-06B -> RT-04C -> RT-05B -> RT-05C -> RT-06C
  -> RT-07A -> RT-07B -> RT-07C
  -> RT-08A -> RT-08B -> RT-08C -> RT-08D (core acceptance)
  -> RT-09A -> RT-09B (optional, if released)
```

`RT-03A` compiles the minimal shader programs so `RT-01C` can prove native dispatch before scene integration. The triangle fixture exercises a minimal SBT through the RHI API. `RT-03B` owns production hit groups and scene bindings. `RT-06A/B` consume the raw shadow and reflection signals and provide the common history and filter contract to `RT-04C` and `RT-05B`.

## 2. Execution batches

The **26 core batches** and **2 optional batches** below map each atomic requirement to one primary checkpoint. In the tables, leaf `NN.n` means `M16-RT-NN.n`. The batch gate is required alongside each assigned leaf's PASS conditions.

### RT-01 to RT-03 — Backend and scene integration

| Batch | Atomic leaves | Deliverable and batch gate |
|---|---|---|
| **RT-01A** | `01.1`, `01.5` | Publish the backend capability and requested/effective tier matrix. Test native interface detection, unsupported capability combinations and RT Off on the normal raster path. Record diagnostics in Editor and Runtime for the applicable settings path. |
| **RT-01B** | `01.2`, `01.4` | Add opaque AS handles and typed graph accesses. Verify native resource creation and validate graph dependencies for build and trace without dispatch. Reject unbuilt reads, wrong access, stale generations and missing build dependencies. |
| **RT-01C** | `01.3`, `01.6` | Expose pipeline/SBT and completion-retained build, refit, bind and dispatch through Axle RHI. On physical D3D12 and Vulkan, read back exact hit/miss values from a BLAS/TLAS triangle fixture with validation enabled. Exercise native graph barriers and check AS and SBT lifetimes across submission. |
| **RT-02A** | `02.1`, `02.2` | Freeze the proxy contract and build a static BLAS from a retained in-budget mesh or actual cooked/runtime coarse proxy. Exercise both selection paths and verify the primitive-to-material mapping. |
| **RT-02B** | `02.5`, `02.6` | Build the production TLAS from extracted scene instances. Verify identity, masks and coverage status with the native RHI fixture. Cover transform, visibility, material and scene changes, including an eligible off-screen instance. |
| **RT-02C** | `02.3`, `02.4` | Exercise supported same-topology deformation, topology rebuild and explicit fallback classes. Verify compaction, transactional publication and deferred retirement with frames in flight. |
| **RT-02D** | `02.7` | Run native lifecycle and fault fixtures covering failed first/replacement build, allocation or budget failure, incompatible proxy, resize, scene replacement, and device reset/reinit. Verify that forced omission of an eligible instance signals incomplete coverage. |
| **RT-03A** | `03.1` | Compile the minimal raygen, miss and closest-hit programs for DXIL and SPIR-V through the engine compiler. Check exports, declared bindings and diagnostics for an invalid export or compile. Save compiler options and bytecode identity. |
| **RT-03B** | `03.2`, `03.3` | Bind production shader groups and SBT records to retained geometry/material generations. Verify which instance, primitive, and material were hit, including cutout any-hit behavior, on both backends. |
| **RT-03C** | `03.4`, `03.5` | Wire stateful RT service/pass entry points into the M15 scheduler and verify the receiver inputs in production views with SSR, planar and TAA disabled. Prove raster output survives missing shader or descriptor setup. Later batches supply the shadow, reflection and denoise pass bodies. |

### RT-04 to RT-07 — Effects, reconstruction and parity

| Batch | Atomic leaves | Deliverable and batch gate |
|---|---|---|
| **RT-04A** | `04.1`, `04.3` | Trace directional visibility and bind it to the matching direct light. Read back blocked/unblocked contacts and compare normal offset, light leaks and self-hits with the raster reference. |
| **RT-04B** | `04.2` | Add point and spot visibility with stable light selection, bounded ray length and masks. In mixed-light fixtures, verify correct per-light results and raster fallback for unselected lights or missing required casters. |
| **RT-04C** | `04.4`, `04.5` | Bind the shared reconstruction to the shadow path. Compare static and moving RT/raster fixtures on both backends, including temporal rejection and local-light identity changes. |
| **RT-05A** | `05.1`, `05.2`, `05.3` | Produce bounded, lit hit radiance and explicit hit/miss, distance, coverage and confidence signals. Establish the off-screen lighting fixture before applying final denoising and resolve. |
| **RT-05B** | `05.4` | Denoise reflections with per-view receiver and secondary-hit validation. Test disocclusion, changing roughness, hit/miss transitions and a moving reflection on a stationary receiver. |
| **RT-05C** | `05.5`, `05.6` | Wire shared indirect-specular resolve. Verify the full RT/fallback chain with HDR readbacks and final images, including independent RT Reflections with SSR and planar disabled, then with TAA also disabled. |
| **RT-06A** | `06.1`, `06.2` | Allocate retained view history and implement reset reasons and completion ordering. Verify cross-view isolation and every required invalidation on CPU and on native views. |
| **RT-06B** | `06.3` | Build the shared spatiotemporal variance-guided filtering (SVGF) framework with signal-specific reprojection and edge rules. Read back both shadow and reflection sequences and verify rejection at invalid edges. |
| **RT-06C** | `06.4` | Validate integrated shadow/reflection stability with repeatable camera and object motion, including off-screen hit motion with zero receiver velocity. Record convergence, ghosting/disocclusion and reset recovery on both backends. |
| **RT-07A** | `07.1` | Validate shared built-in material values, texture sampling and matched hit lighting against ForwardPBR via deterministic hit records. Cover dielectric and conductor cases, untextured fallbacks and roughness endpoints. |
| **RT-07B** | `07.2`, `07.3` | Deliver advanced material behavior and the custom Material Graph support matrix. Exercise supported and explicitly unsupported cases, including cooked graph assets and per-effect fallback. |
| **RT-07C** | `07.4` | Run the canonical material parity matrix with frozen tolerances. Verify equivalent material samples separately from the expected differences in RT and raster visibility. |

### RT-08 and RT-09 — Acceptance and optional GI

| Batch | Atomic leaves | Deliverable and batch gate |
|---|---|---|
| **RT-08A** | `08.1`, `08.2` | Freeze the core candidate and run startup, shutdown, fallback, product parity and lifecycle/soak checks. Cover physical D3D12/Vulkan RT, disabled RT, injected capability/fault cases, actual non-RT hardware and OpenGL compatibility. |
| **RT-08B** | `08.3` | Record Release CPU/GPU and peak-memory scorecards at 1080p and 1440p against accepted U11 and M16 RT Off baselines. Verify frozen budgets and identify any regression requiring repair. |
| **RT-08C** | `08.4`, `08.5` | Archive deterministic readbacks, verified artifact identities and inspected Debug/Release Editor captures. Include effective tier and fallback diagnostics along with the visual result. |
| **RT-08D** | `08.6` | Consolidated independent review of `RT-01` through `RT-08`, including corrections, separate-context recheck as required by the shared protocol and verification on final integrated source. |
| **RT-09A** | `09.1`, `09.2` | Add bounded irradiance and a shared AS/visibility consumer for DDGI. Verify the opt-in result and generation ownership independently of the core modes. |
| **RT-09B** | `09.3` | Verify bounce, light leaks, temporal behavior, denoise, budgets and GI-disabled behavior before promoting optional GI. |

## 3. Canonical atomic checklist

This is the single status-bearing checklist. Each item closes on its stated result and evidence from [Section 4](#4-verification-and-evidence). Batch handoffs reference the assigned IDs, the base/head commits and any open findings.

### M16-RT-01 — Capability policy and RHI foundation

- [ ] **M16-RT-01.1 — Freeze D3D12/Vulkan capability contract.** Define required AS, pipeline, shader-table, inline-ray and descriptor features, plus the backend-neutral feature matrix consumed by M17. Mesh-shader and indirect-draw support must not be inferred from an RT bit.
  - **PASS when** native interface availability, AS/pipeline/descriptor limits, SBT size/alignment, refit and inline ray capabilities are recorded and tested independently. Mesh-shader and indirect-draw results are independently derived. A pipeline-capable device can use the core path with inline rays unavailable.
- [ ] **M16-RT-01.2 — Add RHI acceleration-structure descriptors and handles.** No backend-specific handle leaks into Scene.
  - **PASS when** backend-neutral creation, validation and destruction work on both backend implementations. Native tests reject invalid inputs and cover partial initialization and device-epoch ownership.
- [ ] **M16-RT-01.3 — Add RT pipeline/SBT descriptors and lifetime leases.**
  - **PASS when** the same RHI API creates a valid pipeline/table on both backends, validates layout, record stride and range, and retains the exact versions until completion. Negative tests cover mismatched pipeline and table generations.
- [ ] **M16-RT-01.4 — Add RenderGraph RT access states and validation.**
  - **PASS when** typed edges represent build input/scratch, AS build/refit/read, SBT read and storage-output dependencies. Graph tests verify the build-to-trace and trace-to-consumer plans and reject an unbuilt or stale generation and missing producer. `RT-01C` verifies those transitions with native dispatch.
- [ ] **M16-RT-01.5 — Add explicit raster fallback diagnostics and quality-tier policy.**
  - **PASS when** Editor and Runtime resolve the same requested settings to an effective mode. Distinguish Off, unsupported, over-budget and initialization/resource failure with a reason, and select valid raster alternatives without rewriting authored settings.
- [ ] **M16-RT-01.6 — Add engine-owned RT command entry points.** Expose AS build/refit, `DispatchRays`, SBT binding and completion-retained leases through RHI without leaking NRI handles into Scene or RenderWorld.
  - **PASS when** native fixtures build and refit through RHI, bind the TLAS and table, dispatch hit/miss rays and read exact results. Release caller ownership before submission completes to prove transitive retention and deferred retirement.

### M16-RT-02 — BLAS/TLAS lifecycle

- [ ] **M16-RT-02.1 — Freeze the RT coarse-proxy contract.** Define proxy identity, source-mesh linkage, material/alpha/normal requirements, shadow/reflection masks, triangle-budget policy and static/deforming update classes. M16 must not depend on a future M17 autoLOD artifact.
  - **PASS when** the contract defines source UUID and revision, proxy generation, required vertex attributes and primitive/material mapping, classification, legal update classes, budget inputs and failure reasons. Tests cover valid and incompatible records without M17 assets.
- [ ] **M16-RT-02.2 — Build BLAS for static opaque geometry from a retained proxy generation.** A source mesh may be used only when it already satisfies the proxy budget and hit-shader contract; otherwise use the cooker/runtime coarse proxy.
  - **PASS when** native hit tests cover eligible source reuse and actual coarse-proxy generation/selection for an over-budget source. Verify mapping, silhouette/cutout fixture constraints and source/proxy lease retention.
- [ ] **M16-RT-02.3 — Refit or rebuild BLAS for supported deforming geometry.** Publish an explicit policy for skinned/deforming meshes; do not silently refit topology changes.
  - **PASS when** a supported moving-vertex fixture changes the native hit result through the declared update path, a topology change rebuilds, and unsupported deformation selects the documented fallback. Declare the supported skinned cases.
- [ ] **M16-RT-02.4 — Retire and compact BLAS generations after completion.** Keep the last-good generation when a replacement build fails.
  - **PASS when** native compaction preserves hit results, referenced generations survive concurrent frames, and completion releases retired resources. Failed replacements preserve a compatible last-good set, with deterministic fallback when no compatible set exists.
- [ ] **M16-RT-02.5 — Build TLAS with stable instance identity and masks.** Preserve opaque, transparent, shadow-only and reflection-only classification.
  - **PASS when** native readbacks distinguish effective shadow and reflection masks and stable instance IDs across rebuild and extraction order changes. Publish explicit cutout, two-sided and blended/transmissive participation policies.
  - Verify [coverage status](ARCHITECTURE.md#52-tlas-coverage) distinguishes intentionally excluded instances from required instances omitted by missing proxies, unsupported deformation or budget limits.
- [ ] **M16-RT-02.6 — Update TLAS on transform, visibility, scene and material generation changes.**
  - **PASS when** instance transforms, visibility and material/classification changes affect the correct hit, and scene replacement never consumes the prior scene's bindings. Include an off-screen caster or reflection target outside the main-view draw list.
- [ ] **M16-RT-02.7 — Test resize, device reset, scene replacement and failed rebuild retention.** Include proxy/source mismatch and budget overflow diagnostics.
  - **PASS when** both native backends complete resize, scene replacement and device reinitialization without validation errors or stale handles, and injected first-build/replacement, proxy mismatch and overflow cases produce the specified result and diagnostic.
  - Force omission of an eligible caster or reflection target. Verify the active coverage scope and reason differ from a geometric miss. Verify coverage is restored when the geometry is available again.

### M16-RT-03 — RT pipeline and shader table

- [ ] **M16-RT-03.1 — Compile ray-generation, miss and closest-hit libraries for D3D12/Vulkan.**
  - **PASS when** the production compiler emits valid DXIL and SPIR-V programs with the expected exports, bindings and specialization. Archive compiler/cache identity and failure diagnostics. Validate shader group compatibility in the native `RT-01C` fixture.
- [ ] **M16-RT-03.2 — Build hit groups and shader binding tables.**
  - **PASS when** both backends dispatch the intended raygen, miss, closest-hit and cutout any-hit groups for multiple instances and material slots. Verify alignment and index bounds through native results and rejection tests.
- [ ] **M16-RT-03.3 — Bind shared geometry/material records with generation checks.**
  - **PASS when** instance, primitive, material and descriptor identities match the extracted snapshot in a native fixture. Reject incompatible records and retain every referenced resource across frames in flight.
- [ ] **M16-RT-03.4 — Register `RayTracingASBuild` and RT scheduler passes.**
  - **PASS when** RT entry points are stateful `IRenderPass` instances with declared producers/consumers and enablement policy. Trace the production Editor/Runtime view callers and verify the capture/Preview policy, pass order and graph behavior with RT disabled.
  - Verify the [receiver inputs](ARCHITECTURE.md#21-rt-receiver-inputs) in a supported production RT view on both native backends with SSR, planar and TAA disabled. Assert the required depth, normal, surface/response and velocity targets are populated before their consumers. Verify the declared sample-count and unavailable-input policy. `RT-05C` verifies the final reflection result.
- [ ] **M16-RT-03.5 — Validate missing shader/descriptor fallback without corrupting the raster frame.**
  - **PASS when** failure injection exercises missing/invalid shaders and descriptors, selects a supported fallback for the affected effect and produces the expected raster output and diagnostic without publishing partial RT resources.

### M16-RT-04 — Ray-traced shadows

- [ ] **M16-RT-04.1 — Trace directional-light shadow visibility.**
  - **PASS when** native visibility readbacks agree with known blocked/unblocked rays, and toggling RT changes the intended directional light contribution within the production view.
- [ ] **M16-RT-04.2 — Add point/spot shadow rays and light masks.**
  - **PASS when** point/spot range, spot cone, light identity and masks produce correct direct lighting for multiple lights, with raster visibility for lights outside the selected RT set.
  - In an integrated shadow fixture, remove a caster required by the RT coverage contract while it still casts a raster shadow. Assert that affected visibility comes from the raster path and the receiver stays in shadow. Verify the same fallback for directional shadows.
- [ ] **M16-RT-04.3 — Match raster shadow bias/normal policy at contact boundaries.**
  - **PASS when** controlled comparisons cover thin contacts, grazing angles, geometric and shading normals, cutout and two-sided casters, self-hit and leak cases, using documented tolerances.
- [ ] **M16-RT-04.4 — Add spatial shadow denoise and temporal rejection.**
  - **PASS when** native shadow histories and filter outputs preserve light identity and contact boundaries, reject invalid motion/disocclusion and match the shared history reset rules.
- [ ] **M16-RT-04.5 — Compare RT/raster shadows on static and moving fixtures.**
  - **PASS when** directional and local-light fixtures with static and moving casters verify expected contact, off-screen occlusion, motion recovery and per-light raster fallback in readbacks and images on both backends.

### M16-RT-05 — Ray-traced reflections

- [ ] **M16-RT-05.1 — Trace primary reflection rays with bounded distance and recursion policy.**
  - **PASS when** native tests verify hit geometry inside the configured distance, miss outside that distance and a bounded one-bounce path. Capture and resolve policy prevents recursive sampling.
- [ ] **M16-RT-05.2 — Evaluate shared material Fresnel, roughness and metallic inputs.**
  - **PASS when** matched receiver/hit samples consume canonical surface values, verify metallic/roughness endpoints and conductor/dielectric response, and apply receiver Fresnel/color response once.
  - Verify [lighting at the hit](ARCHITECTURE.md#53-lighting-at-a-reflection-hit) in scene-linear HDR. Keep an off-screen hit, its material and camera fixed while toggling a light. Assert the expected change in outgoing radiance. Exercise lit and occluded hit cases, emissive without a direct light, and the documented environment and shadow-source fallback.
- [ ] **M16-RT-05.3 — Add reflection miss and confidence metadata.**
  - **PASS when** native readbacks distinguish valid hit, true miss, incomplete TLAS coverage, unsupported binding and unavailable effect. Record bounded confidence, hit identity/distance where available and a deterministic fallback signal.
- [ ] **M16-RT-05.4 — Add reflection denoise and history validation.**
  - **PASS when** temporal and spatial reconstruction reject invalid reprojection/hit distance and preserve geometry/material edges over motion, roughness changes and hit-to-miss sequences.
  - With the receiver and camera stationary, move only the reflected off-screen object. Verify secondary-hit validation rejects or reduces history for the changed reflection even with zero receiver velocity, and valid history survives on unaffected samples.
- [ ] **M16-RT-05.5 — Resolve RT, planar, SSR, local probe and sky IBL without duplicate energy.**
  - **PASS when** the shared indirect-specular path implements the precedence and fallback in [ARCHITECTURE.md](ARCHITECTURE.md#6-quality-tiers-and-fallback). HDR readbacks cover zero, partial and full confidence with overlapping sources, while diffuse, direct light and emission remain correct.
- [ ] **M16-RT-05.6 — Test off-screen, self-intersection, moving-object and roughness cases.**
  - **PASS when** controlled fixtures verify native raw signals and final output for off-screen targets, self-hit cases, movement, roughness sweep and fallback transitions on both backends.
  - On both native backends, require a positive off-screen RT reflection with SSR and planar disabled. Repeat with TAA disabled and verify the motion and denoise path stays valid. Force incomplete TLAS coverage and verify the applicable raster reflection fallback with its diagnostic.

### M16-RT-06 — Temporal and denoising system

- [ ] **M16-RT-06.1 — Add per-view RT history targets and generation identity.**
  - **PASS when** history is keyed by the view and compatible target/scene/AS/material/feature/device generations and tracks per-effect validity. Concurrent frames preserve exact history leases and two views keep independent histories.
- [ ] **M16-RT-06.2 — Reject history on camera cut, resize, view switch, AS/material/scene generation and device epoch.**
  - **PASS when** tests trigger every named reset, including re-enable after an incompatible tier change, and reject stale samples. Routine motion within the same compatible history generation exercises reprojection and per-pixel validation.
- [ ] **M16-RT-06.3 — Add variance/spatial reconstruction for shadows and reflections.**
  - **PASS when** both effects exercise shared temporal/moment and edge-aware reconstruction with their own signal validation. Verify finite output, reduced variance on stable input and preserved depth/normal/material edges.
- [ ] **M16-RT-06.4 — Validate stable output under camera/object motion.**
  - **PASS when** reproducible native sequences meet frozen convergence and ghosting limits, and invalidated views recover from clean history within the documented window.
  - Include the moving off-screen hit with a stationary reflective surface. Record receiver velocity, secondary-hit data, history rejection or weight and final HDR output to demonstrate recovery without a view-wide history reset.

### M16-RT-07 — Material and renderer parity

- [ ] **M16-RT-07.1 — Match ForwardPBR base color, metallic and roughness.**
  - **PASS when** RT hit samples match equivalent ForwardPBR values for textured/untextured dielectric and conductor cases, using the common extraction and surface/lighting contracts.
  - Compare reflected-hit radiance with a raster reference for the same surface point, outgoing direction and supported lighting inputs. Capture the hit radiance before applying the receiver's response.
- [ ] **M16-RT-07.2 — Match normal, clearcoat, sheen and transmission policy.**
  - **PASS when** fixtures verify normal map/two-sided behavior, clearcoat and sheen response, and the declared transmission participation or fallback. Compare canonical inputs and evaluated terms to the raster reference where applicable.
- [ ] **M16-RT-07.3 — Match custom Material Graph support or publish explicit unsupported fields.**
  - **PASS when** supported graph programs and bindings use the shared contract, unsupported graph fields or programs have specific diagnostics and correct per-effect fallback, and representative cooked assets produce the same effective decisions in Runtime.
- [ ] **M16-RT-07.4 — Validate deterministic RT/raster material comparisons within documented tolerance.**
  - **PASS when** the frozen fixture matrix covers every supported material class and the declared fallback cases on D3D12 and Vulkan. Readback and image tolerances, sample/seed policy and expected visibility differences are documented before scoring.

### M16-RT-08 — Fallback, performance and acceptance

- [ ] **M16-RT-08.1 — Validate RT-capable D3D12 and Vulkan startup and shutdown.**
  - **PASS when** strict native Debug/Release runs verify the requested backend, production Editor and Runtime startup/shutdown, tier transitions and the lifecycle matrix and soak in Section 4. Record validation and resource-retirement evidence.
- [ ] **M16-RT-08.2 — Validate non-RT hardware and disabled RT fallback to raster.**
  - **PASS when** disabled, unsupported, OpenGL and injected failure runs preserve the applicable M15 raster output and stored settings. Log hardware without RT separately from simulation or masking on an RT adapter.
- [ ] **M16-RT-08.3 — Record AS memory, SBT memory, dispatch cost and denoise cost.**
  - **PASS when** the reproducible 1080p/1440p scorecard meets the frozen workload budgets and records timing/peak-memory deltas versus accepted M15 U11 and M16 RT Off. Use the sampling and accounting rules in Section 4.
- [ ] **M16-RT-08.4 — Add deterministic image/readback and artifact identity manifests.**
  - **PASS when** artifacts are tied to current source, executable, backend/adapter/driver, fixture/settings, shader binaries and reference/tolerances. The verifier distinguishes stale or mismatched artifacts. Store raw signal readbacks as well as final images.
- [ ] **M16-RT-08.5 — Complete Editor visual evidence for shadows/reflections/fallback.**
  - **PASS when** Debug and Release captures from both strict native backends are inspected against the visible expectations in Section 4, including an Editor fallback diagnostic and restored raster result.
- [ ] **M16-RT-08.6 — Complete independent architecture and source review.**
  - **PASS when** the consolidated reviewer maps every core leaf to production callers and independent evidence, closes blocking findings and verifies corrected source. Significant corrections receive the separate-context recheck required by the shared protocol.

### M16-RT-09 — Optional RT GI

- [ ] **M16-RT-09.1 — Trace bounded indirect irradiance.**
  - **PASS when** the opt-in path produces the expected bounded indirect contribution in a known bounce fixture on declared native backends.
- [ ] **M16-RT-09.2 — Share geometry and visibility data with DDGI.**
  - **PASS when** DDGI uses the compatible retained scene/AS generations through the shared visibility contract, and toggling or failing the experimental path preserves the core rendering modes.
- [ ] **M16-RT-09.3 — Validate bounce, leak, denoise, temporal stability and budget.**
  - **PASS when** the optional fixture matrix meets documented irradiance, leak, convergence, timing and memory limits, including independent verification before promotion.

## 4. Verification and evidence

### Batch handoff

- Build the changed targets before running their tests. Run architecture checks, focused contract/unit tests and the applicable native fixture for each batch. Backend-neutral fixtures supplement the physical D3D12/Vulkan gates. Save exact commands, configuration, backend and adapter/driver, outputs and exit status. Commit each verified batch separately.
- Record base/head source, requirements, affected production callers, fixture and assertion, tolerance/reference and artifact location. Label delivery as self-verified and distinguish it from independent acceptance. Use `PASS`, `MISSING`, `FAIL`, `NOT RUN` and `BLOCKED` for mandatory dispositions. `DEFERRED` is available for explicitly optional scope. A missing required native or visual run remains open.
- Follow the [shared protocol](../../agents/WORKING_PROTOCOL.md) for release, separate-context correction recheck and consolidated acceptance. Preserve the source and evidence boundary when other milestone work is active.

### Core acceptance matrix

| Gate | Minimum evidence |
|---|---|
| Native path and product parity | Strict physical D3D12 and Vulkan in Debug and Release with validation. Cover Editor main/game views, Runtime loose/cooked and the declared Preview, planar and probe-capture policies. Cover Off, Shadows and Reflections plus combination if exposed. Where settings are persisted, prove roundtrip and shared effective settings across Editor and Runtime. |
| Fallback | Compare against the applicable M15 U11 raster reference for explicit Off, physical non-RT hardware, injected missing capability, OpenGL compatibility and shader/AS/descriptor/budget failure. Include incomplete TLAS coverage with an eligible caster or reflection target omitted. Confirm the intended diagnostic, valid supported remaining effects and recoverable re-enable without mutating the project or scene. |
| Lifecycle and soak | Exercise view/camera switches and cuts, same-size view isolation, rapid resize/minimize/restore, scene/asset replacement, frames in flight and device reset/reinit. Run at least 1,000 steady-state frames on each native backend with RT active. Record validation, generation retirement and bounded resource counts/memory. |
| Deterministic signals and images | Freeze the fixtures, sample/seed, inputs and per-signal tolerances before scoring. Save hit/miss and identity readbacks, shadow visibility, linear HDR radiance/confidence and final images with manifests. Compare material parity and RT Off against their references. Rerun the independent RT Reflections, off-screen hit lighting and zero-receiver-velocity fixtures on the integrated candidate. |
| Editor visual inspection | Use the [GPU capture procedure](../../agents/EDITOR_VISUAL_CAPTURE.md) with strict D3D12/Vulkan and validation for Debug/Release. Inspect directional and local contacts, cutout shadows, off-screen reflections on a roughness sweep, movement/disocclusion, and tier switching with fallback diagnostics. Archive viewport and UI captures where relevant and record expected/observed behavior. |
| Cost and budgets | On each declared RT backend, record Release 1080p/1440p cold startup/compile separately from at least 120 warm-up and 1,000 measured frames per workload. Measure CPU extraction/submission/wait, GPU AS build/update/compaction, dispatch, denoise/resolve and whole-frame `p50/p95/p99/max`. Report committed and peak transient VRAM including AS, scratch, SBT, descriptors, history and overlapping generations. Freeze tolerances and budgets for the declared adapter, fixture and mode after calibration and before scoring the acceptance run. |
| Consolidated audit | One row per `RT-01`–`RT-08` leaf with scope, implementation and caller paths, evidence, status and finding IDs. Audit ownership, graph dependencies, publication, deferred destruction, shared material evaluation, normal product entry points and failure behavior. Close findings and rerun affected plus required final integrated gates. |

## 5. Exit criteria

M16 core closes at `RT-08D` when every mandatory leaf in `RT-01`–`RT-08` is PASS on the final integrated source and the independent acceptance report links native, fallback, parity, lifecycle, Editor visual and performance evidence. The report records the supported modes/defaults and any verified limits. `RT-09` has its own release and acceptance record.
