# M14-U2 - Pass Interface and Renderer Orchestration Evidence

**Status:** `ACCEPTED`  
**Unit:** M14-U2  
**Scope:** pass lifecycle contract, deterministic scheduler, named pass groups, failure propagation and renderer integration.

## 1. Implementation

The pass contract is defined in:

- `Engine/Renderer/include/Axle/RenderPass.hpp`
- `Engine/Renderer/src/RenderPass.cpp`

The new API provides:

```text
IRenderPass
  Configure(ViewContext, capabilities)
  Declare(RenderGraph)
  Prepare(RenderPassContext)
  Record(RenderPassContext)
  Finalize(RenderPassContext, result)
```

`RenderPassResult` distinguishes `Supported`, `Disabled`, `Fallback` and `Failed`. A failed pass stops the scheduler and preserves a pass-qualified diagnostic. `RenderPassContext` carries immutable `RenderWorld`, camera/settings snapshots, `ViewContext`, graph/command handles and capability policy.

`RenderPassScheduler` owns deterministic ordering and phase dispatch. `CallbackRenderPass` is the migration adapter for existing renderer code while the pass internals move out of `RHISceneRenderer` incrementally.

## 2. Current renderer integration

`RHISceneRenderer` now owns one scheduler with these named groups:

```text
OpaqueAndSky
LightingAndShadows
ScreenSpace
ReflectionAndGI
PostAndTemporal
```

Every native view executes scheduler `Configure` and `Prepare` before recording, declares the scheduler groups at the graph boundary, executes `Record` after graph compilation, and calls `Finalize` after submission or failure. The public renderer remains a facade; the order and result handling are no longer implicit in scattered pass code.

The existing shader/pipeline bodies are deliberately kept behind `CallbackRenderPass` for this checkpoint. This preserves the current image and lifetime behavior while making each group independently replaceable. Moving the concrete pipeline layouts, shaders, descriptor pools and samplers into the group objects is the next implementation step in the migration plan; no new feature may bypass the scheduler.

## 3. Verification

### CPU pass contract fixture

Command:

```text
build/Tests/Debug/AxleRenderUnitTests.exe
```

Result: **exit code 0**. The `[RenderPass]` suite verified deterministic group order, Configure/Prepare/Declare/Record/Finalize lifecycle, graph declaration boundary and failure diagnostics. Existing RenderWorld, ViewContext, worker, parallel-recording and transient-graph suites also passed.

### Native renderer integration fixture

Command:

```text
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/Work/M14-U2-baseline
```

Result: **exit code 0**. Native D3D12 ForwardPBR/environment rendering passed with the unchanged 128x128 baseline, two draw calls and one rendered entity.

### Build and architecture checks

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests --parallel 4  => exit 0
python scripts/check_architecture.py                                                                         => PASS
git diff --check                                                                                              => PASS
```

## 4. Ownership boundary after U2

- `RenderPassScheduler` owns ordering and lifecycle dispatch.
- `IRenderPass` owns the future location of pass-local state and capability policy.
- `RenderGraph` remains the resource declaration/transition authority.
- `ViewContext` owns frame recording and completion.
- `RHIDevice` and shared RHI caches remain below pass modules.
- `RHISceneRenderer` remains a facade plus legacy implementation adapter; it is forbidden to add new feature pass logic directly to the monolith.

## 5. U2 decision

**U2 ACCEPTED.** The pass lifecycle and orchestration seam is implemented and integrated without changing the reference render. Concrete shader/pipeline relocation proceeds through the established adapter boundary in later migration work.
