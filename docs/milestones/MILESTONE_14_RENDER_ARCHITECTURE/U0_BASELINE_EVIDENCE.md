# M14-U0 - Baseline and Migration Contract Evidence

**Status:** `ACCEPTED`  
**Unit:** M14-U0  
**Source baseline:** `ac4941c`  
**Recorded:** 2026-11-14 (Asia/Saigon)  
**Scope:** source inventory, ownership/contract freeze and reference build/runtime evidence. No M15 feature behavior is claimed.

## 1. Source inventory

| Responsibility | Current source owner | Observed callers / evidence | M14 migration owner |
|---|---|---|---|
| Scene extraction | `Engine/Scene/src/SceneExtraction.cpp` | Builds immutable `RenderWorld`; material sync at lines 310-311 | `ViewContext` input remains `RenderWorld` |
| RHI-primary renderer facade | `Engine/Scene/include/Axle/RHISceneRenderer.hpp`, `Engine/Scene/src/RHISceneRenderer.cpp` | Editor, RenderService and rendering tests call `RenderScene`/`RenderWorldView` | `RHISceneRenderer` remains public facade; delegates to `RenderScheduler` |
| Legacy/GL renderer | `Engine/Scene/include/Axle/SceneRenderer.hpp`, `Engine/Scene/src/SceneRenderer.cpp` | OpenGL fallback and legacy scene path | Explicit backend adapter; no direct ECS access from new passes |
| Snapshot data | `Engine/Renderer/include/Axle/RenderWorld.hpp` | `RenderMaterialData`, mesh instances, lights, probes, fog and DDGI data | Immutable `RenderWorld` contract |
| Frame recording | `Engine/RHI/include/Axle/RHI/RHIFrameContext.hpp` | Per-view command buffer, upload/uniform arenas and completion token | Owned by `ViewContext` frame execution |
| Native device/queue | `Engine/RHI/include/Axle/RHI/RHIDevice.hpp` | NRI backend selection, submission, epoch and resource service | Remains below renderer pass layer |
| Target generation | `Engine/RHI/include/Axle/RHI/RHIViewTargets.hpp` | Color/depth/MSAA/normal/storage/history/AO/bloom/exposure/DDGI/Hi-Z slots | `ViewResourceRegistry` publishes generations |
| Texture lifetime | `Engine/RHI/include/Axle/RHI/RHITexture.hpp` | Native views plus `RHIResourceLease` and generation retention | Registry and graph retain exact generations through completion |
| Logical graph | `Engine/Renderer/include/Axle/RenderGraph.hpp`, `Engine/Renderer/src/RenderGraph.cpp` | `RHISceneRenderer.cpp` declares the current pass sequence; tests cover ordering and invalid uses | Graph becomes the only dependency/transition authority |
| Authoring material | `Engine/Renderer/include/Axle/Material.hpp` | Serialized PBR, texture, surface and advanced material fields | `MaterialResolver` input |
| Component material mirror | `Engine/Scene/include/Axle/Components.hpp` | `SyncFromAsset()` and `SyncToRHI()` copy authoring state | Generation-tracked instance state |
| GPU material | `Engine/RHI/include/Axle/RHI/RHIMaterial.hpp`, `Engine/RHI/src/RHIMaterial.cpp` | Texture slots, descriptors and `BuildConstants()` | Output representation of `ResolvedSurface`, not a second authoring model |
| Material Graph | `Engine/Asset/src/MaterialGraphHLSL.cpp`, `Engine/Renderer/src/MaterialSurfaceShader.cpp` | Graph surface generation and custom program path | Must emit the same `ResolvedSurface` fields or explicit unsupported status |
| Preview | `Engine/UI/src/AssetPreviewPanel.cpp` | Separate `SceneRenderSettings`, RHI viewport and material sync path | Uses same material resolver/defaults as runtime |
| Feature policy | `Engine/Renderer/include/Axle/SceneRenderSettings.hpp`, `Engine/UI/src/RHIViewportDescriptor.cpp`, `Engine/Scene/src/RenderService.cpp` | SSR/GTAO/contact/fog/DDGI/bloom/exposure/TAA/FXAA target mapping | Capability-aware `ViewContext` policy |

### Current pass ownership observed in `RHISceneRenderer.cpp`

The current renderer owns shader compilation, pipeline layouts, descriptor pools, samplers and mutable state for ForwardPBR, skybox, tone mapping, FXAA, GTAO, SSR, Hi-Z, fog, bloom, auto-exposure, DDGI, TAA, particles, physics debug and picking. The graph declarations at approximately lines 2976-3098 describe the current order from shadows through export, but command recording and descriptor updates still happen in the same monolithic class. This is the primary migration target for M14-U2.

## 2. Contract freeze

The following rules are now frozen for M14 migration:

1. `RenderWorld` is the only scene input allowed while a view records GPU work. Passes do not read ECS or mutate `Scene`.
2. `RHIFrameContext` owns per-view command recording, upload/uniform arenas and submission completion. Passes do not submit queues.
3. `RHIDevice` owns native device state, queue submission, device epoch and resource service. Renderer code does not free native resources directly.
4. `RHIViewTargets` publishes complete generations. A failed candidate leaves the last-good generation live.
5. `RHITexture`, buffers and descriptors are retained by lease/completion lifetime, not by raw pointer convention.
6. A descriptor set is immutable after binding in a submission.
7. `RenderGraph` declarations must include every pass read/write, layout, load/store policy and ordering dependency. Direct command code may not create undeclared dependencies.
8. `Material` is authoring data, `RenderMaterialData` is a snapshot, and `RHIMaterial` is runtime/GPU data. M14 introduces `MaterialResolver` and `ResolvedSurface` to remove duplicated resolution logic.
9. M14 does not claim final SSR, dynamic reflection capture/prefilter/scheduling, DDGI feedback, advanced-material parity or ray tracing. Those remain M15 or later scope.
10. Unsupported capability returns a named fallback/diagnostic state; an unavailable timestamp is `unknown`, never a valid zero budget.

## 3. Reference build and runtime evidence

### Build

Command:

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests --parallel 4
```

Result: **exit code 0**. Both executables linked successfully. MSVC emitted the existing `LNK4098 defaultlib 'LIBCMT' conflicts` warning for the test targets; no compile or link failure occurred.

### CPU/RHI contract fixture

Command:

```text
build/Tests/Debug/AxleRenderUnitTests.exe
```

Result: **exit code 0**. Passed RenderContracts, RenderPolicy, RenderPrimitives, RenderWorld extraction, RenderService/ViewRequest, snapshot queue, worker lifecycle, parallel-recording probe/fallback and transient-graph planner suites.

### Native rendering fixture

Command:

```text
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/Work/M14-U0-baseline
```

Result: **exit code 0**. The fixture initialized native D3D12 on an NVIDIA GeForce RTX 3070, compiled the ForwardPBR and auxiliary pipelines, rendered the HDR/environment cases and reported:

```text
reflection center sharp=255 rough=197
PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry
```

The fixture also recorded `baseline_width=128`, `baseline_height=128`, `color_format=RGBA8_UNORM`, `depth_format=D32_FLOAT`, `baseline_draw_calls=2` and `baseline_entities=1` for the final native view.

The fixture produced deterministic HDR/PPM outputs and a timestamp report under `build/Tests/Work/M14-U0-baseline/`. The output directory is build evidence and is intentionally not committed as authored content.

### Capability snapshot

```text
backend=D3D12
gpu=NVIDIA GeForce RTX 3070
dedicated_vram_mb=8232
hardware_ray_tracing=Tier 3
hardware_mesh_shaders=YES
bindless_resources=Tier 2
enhanced_resource_barriers=YES
render_worker_ownership=UNSUPPORTED
parallel_command_recording=PROBE REQUIRED
placed_resource_primitives=AVAILABLE
transient_aliasing=PROBE REQUIRED
```

The NRI `CheckFeatureSupport(options22)` warning is recorded as a baseline driver/API diagnostic. It did not prevent device creation or invalidate this Unit's reference fixture. The worker-ownership and parallel-recording probe results remain explicit constraints for later M14 Units.

### Timestamp evidence

The native fixture's report recorded a valid scoped timestamp on this machine:

```text
timestamp_frequency_hz=1000000000
empty_gpu_scope_ms=0.100352
gpu_scope_valid=1
probe_submission_ok=1
renderer_gpu_scope_ms=0.017408
renderer_gpu_scope_valid=1
```

This is a baseline measurement only; it is not the M19 multi-resolution performance scorecard.

### Output hashes

The generated baseline outputs were hashed after the successful run. The authoritative files remain in the build workspace at `build/Tests/Work/M14-U0-baseline/`; future U0 re-runs must record new hashes rather than overwrite this report silently.

```text
red.ppm=E1893B80A5F07CFE837D7213DF1B1F92925B97279F372996A6A20D000962FD97
blue.ppm=CD5F80ECFA7B0D59A2DD457A18D922442D3682EF7F0D812DE7777F6980FEA919
sharp.ppm=E51830D90FA321A80394311F619844BED5B117D5BC87E83BF10A011174CA1B13
rough.ppm=39D4CCA469749F71302C9CCD9FC246F6C5BCB25E8B882922240E0C43D66E0C91
ldr.ppm=8DEE30E2CAA26A23F15A2E903788B34B4BD247A15CCB4FEED7EC8B3DB1BC71EE
```

## 4. Known baseline limitations

- The current implementation still has a monolithic `RHISceneRenderer`; this is the reason M14 exists.
- The graph is declared in the renderer but pass recording and descriptor mutation remain colocated there.
- Material data is duplicated across asset, component, snapshot and runtime representations until U3 introduces `ResolvedSurface`.
- The native fixture intentionally logs missing-HDR fallback attempts; the fallback path is part of the test and passed.
- The no-argument invocation of `AxleEnvironmentRenderingTests.exe` returns exit code 1 because its contract requires `<backend> <output-directory>`; the correctly parameterized invocation passed.
- OpenGL/Vulkan evidence is not required to close U0's baseline fixture, but is required by U7/U9 before the architecture milestone is accepted.

## 5. U0 decision

**U0 ACCEPTED.** The source map, ownership rules, M15 boundary and native reference evidence are recorded. The next released Unit is **M14-U1 - ViewContext and frame ownership**.
