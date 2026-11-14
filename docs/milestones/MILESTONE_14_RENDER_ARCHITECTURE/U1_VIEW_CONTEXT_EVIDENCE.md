# M14-U1 - ViewContext and Frame Ownership Evidence

**Status:** `ACCEPTED`  
**Unit:** M14-U1  
**Scope:** per-view identity, reset reasons, frame recording ownership and completion lifetime.

## 1. Implementation

The new `Axle::ViewContext` lives in the Renderer layer:

- `Engine/Renderer/include/Axle/ViewContext.hpp`
- `Engine/Renderer/src/ViewContext.cpp`

It owns:

- camera, viewport size and `SceneRenderSettings` snapshot;
- render-view kind and target generation identity;
- scene/material/feature generation identifiers;
- jitter state;
- device epoch and latched reset reasons;
- one `RHIFrameContext` and its active/completion state;
- monotonically advancing frame index, reset on view identity changes.

`RHISceneRenderer` now owns a `ViewContext` instead of a bare `RHIFrameContext`. Its command path calls `ViewContext::BeginFrame`, `Commands`, `AbandonFrame` and `EndFrameAndSubmit`. The renderer no longer increments a separate frame counter or submits the frame outside the view context.

## 2. Reset contract

`ViewContext::Observe` detects and latches these independent reasons:

```text
Initial
Resize
ViewSwitch
CameraCut
SceneRevision
MaterialGeneration
FeaturePolicy
TargetGeneration
DeviceEpoch
```

The renderer derives a stable feature-policy fingerprint from the target-affecting `SceneRenderSettings` fields. Target pointer changes and RHI device epoch changes are detected directly. Resize, view switch, camera cut, scene/material generation and feature-policy changes reset the context frame index so temporal consumers cannot silently continue with an incompatible view identity.

## 3. Frame ownership contract

```text
ViewContext::BeginFrame
    -> RHIFrameContext::Begin
    -> pass code borrows Commands()/Frame()
ViewContext::EndFrameAndSubmit
    -> RHIFrameContext::End
    -> RHIFrameContext::Submit
    -> completion token is published
ViewContext::AbandonFrame
    -> RHIFrameContext::Abandon
    -> active state is cleared without advertising completion
```

Pass code does not submit a queue, recycle an upload arena or reuse a descriptor pool. `RHIFrameContext` continues to wait on the previous completion before reuse, and the existing RHI resource lease chain remains responsible for native lifetime.

## 4. Verification

### CPU contract suite

Command:

```text
build/Tests/Debug/AxleRenderUnitTests.exe
```

Result: **exit code 0**. The new `[ViewContext]` suite passed initial observation, unchanged-view stability, resize, view switch, scene/material/feature generation, camera cut, target-generation, device-epoch and inactive-frame abandon checks. Existing RenderService, RenderWorld, worker, parallel-recording and transient-graph suites also passed.

### Native frame lifetime suite

Command:

```text
build/Tests/Debug/AxleRHIFrameTests.exe dx12
```

Result: **exit code 0**. Existing RHI frame tests passed completion-gated reuse, failed queue-submit handling, partial slot construction, abandon/rebegin and validation teardown.

### Renderer integration fixture

Command:

```text
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/Work/M14-U1-baseline-final
```

Result: **exit code 0**. Native ForwardPBR/environment rendering still passed after `RHISceneRenderer` switched to `ViewContext` ownership. The fixture recorded the same 128x128 RGBA8/D32 baseline, 2 draw calls and 1 rendered entity.

### Build and architecture checks

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests --parallel 4  => exit 0
cmake --build build --config Debug --target AxleRHIFrameTests --parallel 4                              => exit 0
python scripts/check_architecture.py                                                                         => PASS
git diff --check                                                                                              => PASS
```

The existing MSVC `LNK4098 defaultlib 'LIBCMT' conflicts` warning remains non-fatal and is unchanged from the U0 baseline.

## 5. Known boundary

`ViewContext` is now the frame ownership seam, but pass extraction has not started. `RHISceneRenderer` still schedules and records all passes; that is the explicit scope of M14-U2. Temporal history policy is represented by reset reasons and frame identity here; TAA/fog/exposure behavior remains owned by later Units.

## 6. U1 decision

**U1 ACCEPTED.** The view identity and frame ownership contract is implemented and verified. The next released Unit is **M14-U2 - Pass interface and renderer orchestration**.
