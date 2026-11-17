# M15 U5 Batch D — Reflection Probe Invalidation and Scheduling Self-Verification Review (`M15-U5-21.1` -> `M15-U5-26.4`)

Self-verification delivery report for Milestone 15 Unit 5 Batch D (`U5-D`, SubUnit `S5.4`: `M15-U5-21.1` through `M15-U5-26.4`), implemented on top of accepted `M15-U5-C` commit `f4f4e94a` (`fix(m15): repair U5-C mip views and cubemap verification`). Independent Codex review and acceptance remain pending per [WORKING_PROTOCOL.md](../agents/WORKING_PROTOCOL.md). Interactive Editor visual checks (`M15-U5-21.3`, `21.4`, `26.1`) remain open (`NOT RUN`) and deferred per user direction to the post-U5 consolidated visual pass.

---

## 1. Requirement Coverage Summary

| Requirement | Status | Summary of Implementation & Verification |
| :--- | :--- | :--- |
| **M15-U5-21.1 — Track probe transform** | **PASS (self-verification)** | `ReflectionCaptureDomainStamps::TransformStamp` hashes probe `EntityID`, `StableUUID`, `Position`, `BoxOffset`, `BoxExtents`, `Radius`, `Shape`, `WorldTransform`, and `ParallaxCorrected` cleanly in `RHISceneRenderer::ComputeDynamicCaptureDomainStamps`. Moving/editing probe transform isolates `TransformChanged` on both CPU contract (`Test_M15_ReflectionCaptureContractUnit.cpp`, `Test_LightingServicesUnit.cpp`) and native GPU renderer (`Test_M15_ReflectionCaptureRendering.cpp`). |
| **M15-U5-21.2 — Track scene revision** | **PASS (self-verification)** | `RenderWorld` extracts `SceneRevision`, `SceneInstanceID`, and `SceneLifetimeEpoch` from `SceneData` (`SceneExtraction.cpp`). `ReflectionService::EvaluateInvalidation` tracks revision, instance ID, and lifetime epoch transitions under `SceneRevisionChanged`. Verified on CPU & GPU (`Debug`/`Release` DX12 & Vulkan). |
| **M15-U5-21.3 — Track geometry/material** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `GeometryStamp` hashes mesh transforms, entities/UUIDs, URIs, shadow flags, builtins, and skeletal `Skins` transforms/bone matrices; `MaterialStamp` hashes `MaterialResolver` resolved surface generation, material properties, custom shaders, and custom shader property scalars. Verified via CPU unit tests (`skin.BoneMatrices`, `Roughness`) and GPU native dynamic capture tests (emissive radiance replacement + mesh transform delta). Interactive Editor visual check deferred. |
| **M15-U5-21.4 — Track light/shadow** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | `LightingStamp` hashes directional/additional directional lights, point/spot/rect lights, shadow flags/biases/resolutions, skybox settings, and IBL enablement. Editing light intensity isolates `LightingChanged` in CPU unit & GPU renderer domain stamp checks. Interactive Editor visual check deferred. |
| **M15-U5-21.5 — Track asset generation** | **PASS (self-verification)** | `AssetStamp` hashes `DeviceEpoch`, manual IBL `RHIResourceHandle` identities, `sky.HDRPath` & material/custom-property texture file timestamps, custom shader file timestamps/fingerprints, and mesh `NativeView` vertex/index `RHIResourceHandle` generations. Verified on GPU (`RHIMesh::CreateCube` buffer swap isolating `AssetChanged`) and CPU contract tests. |
| **M15-U5-22 — Support manual and automatic capture** | **PASS (self-verification)** | Added `ReflectionProbeCaptureMode` (`Automatic` / `Manual`) across `Types.hpp`, `Components.hpp`, `RenderWorld.hpp`, `SceneExtraction.cpp`, and YAML serialization (`SceneSerializer.cpp`). `Manual` probes ignore edits without auto-capturing, flag `Stale` / `ManualRefreshRequired`, and recapture only via explicit `CaptureDynamicReflectionProbe` or `RequestDynamicReflectionCapture`. Verified across YAML round-trip, CPU contracts, and native D3D12/Vulkan runs. |
| **M15-U5-23.1 — Bound faces/probes per frame** | **PASS (self-verification)** | Added `ReflectionCaptureMaxFacesPerFrame` (default `6`), `ReflectionCaptureMaxProbesPerFrame` (default `1`), pure-CPU `ComputeFrameCaptureSchedule`, and `RHISceneRenderer::PendingCaptureJob` incremental state machine (`ScheduleDynamicReflectionCaptures`). Verified `MaxFacesPerFrame=2` slicing a 6-face recapture across 3 frames (`2/6 -> 4/6 -> Ready`) and `MaxProbesPerFrame=1` deferring lower-priority probes on Debug/Release DX12 & Vulkan. |
| **M15-U5-23.2 — Expose pending diagnostics** | **PASS (self-verification)** | `DynamicReflectionProbeStatus` and `ReflectionFrame` (`CapturePending`, `CaptureCompletedFaces`, `CaptureTotalFaces`, `ActiveGenerationID`) report exact face progress (`DYNAMIC_PROBE_PENDING: captured X/6 faces`) during multi-frame jobs without publishing incomplete cubemaps. |
| **M15-U5-23.3 — Expose stale diagnostics** | **PASS (self-verification)** | `DynamicReflectionProbeStatus` and `ReflectionFrame::CaptureStale` flag edited manual probes, forcibly invalidated probes (`InvalidateDynamicReflectionProbe`), and last-good generations held during background/incremental or failed rebuilds (`DYNAMIC_PROBE_STALE`). |
| **M15-U5-23.4 — Expose failed diagnostics** | **PASS (self-verification)** | `RecordDynamicCaptureFailure` updates `DynamicCaptureLifecycleState::Failed`, `FailureCount`, `LastDiagnostic`, and `ReflectionFrame::CaptureFailed` while suppressing infinite auto-retry on deterministic settings/budget failures (`DYNAMIC_PROBE_SETTINGS` / `DYNAMIC_PROBE_BUDGET`). |
| **M15-U5-24 — Retain last-good generation on rebuild failure** | **PASS (self-verification)** | Multi-frame `PendingCaptureJob` renders exclusively into candidate cubemaps. Mid-capture `ScopedRHIFault` on face `4/6` (and settings/budget failures) abort cleanly, discard partial candidate state, preserve the prior `DynamicReflectionProbeGeneration` & intact HDR radiance texels, keep main-view `ReflectionFrame::PrefilterReady` bound to last-good handle, and recover cleanly on subsequent captures. |
| **M15-U5-25 — Add capture cache key, memory budget and eviction** | **PASS (self-verification)** | Added stable `ResolveDynamicProbeKey` (`uuid:` / `entity:` / `anon:`), exact mip-chain memory accounting (`EstimateDynamicProbeCacheBytes`), configurable `ReflectionCaptureCacheBudgetBytes` & `SetDynamicReflectionCaptureCacheBudget`, `DynamicReflectionCaptureCacheStats`, and LRU eviction (`ComputeLRUCacheEvictionPlan` via `LastUsedSequence`). Verified oldest-used probe eviction across 3 probes in a 2-probe budget and deterministic rejection when a single capture exceeds budget. |
| **M15-U5-26.1 — Test repeated capture** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Automated native DX12/Vulkan runs execute repeated captures across material/clip/setting mutations, sliced recovery cycles, and unchanged re-renders (`GenerationID` reuse vs monotonic increment). Interactive Editor visual check deferred. |
| **M15-U5-26.2 — Test resize** | **PASS (self-verification)** | Main viewport resolution is excluded from `ComputeDynamicCaptureDomainStamps`: resizing main viewport targets (`64x64 -> 96x48`) leaves cached probe `GenerationID` untouched (`0` recaptures). Updating `CaptureResolution` (`32 -> 64`) reallocates candidate cubemaps cleanly and updates `CachedBytes` (`130,944 -> 524,160` bytes for 5 mips `RGBA16_FLOAT`). |
| **M15-U5-26.3 — Test device reset** | **PASS (self-verification)** | `DynamicReflectionProbeGeneration::IsValidForDevice(deviceEpoch)` checks both `DeviceEpoch` and live `Cubemap->GetHandle().Epoch`. Real `device.Shutdown()` + `device.Init(...)` test (`RunM15ReflectionDeviceResetTests`) on DX12 & Vulkan (`Debug`/`Release`) confirms stale generations invalidate immediately across epochs and recapture cleanly on the new device epoch with zero RHI validation errors. |
| **M15-U5-26.4 — Test scene replacement** | **PASS (self-verification)** | `RenderWorld` tracks `SceneInstanceID` + `SceneLifetimeEpoch`, and extracted probes key by `StableUUID`. Replacing `Scene A` with `Scene B` (even reusing identical ECS `entt::entity` indices) and invoking `Scene::Clear()` (advancing `SceneLifetimeEpoch`) evict retired captures without entity-ID aliasing or stale radiance leakage. |

---

## 2. Modified Files

- **Core / Renderer Contracts & Reflection Policy:**
  - [Types.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Core/include/Axle/Types.hpp)
  - [RenderWorld.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/RenderWorld.hpp)
  - [SceneRenderSettings.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/SceneRenderSettings.hpp)
  - [ReflectionService.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/ReflectionService.hpp)
  - [ReflectionService.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/src/ReflectionService.cpp)
  - [RenderContracts.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/RenderContracts.hpp)
  - [RenderContracts.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/src/RenderContracts.cpp)
- **Scene Extraction, Serialization & Renderer Scheduler/Cache:**
  - [Components.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/Components.hpp)
  - [SceneExtraction.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/SceneExtraction.cpp)
  - [SceneSerializer.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/SceneSerializer.cpp)
  - [RHISceneRenderer.hpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/RHISceneRenderer.hpp)
  - [RHISceneRenderer.cpp](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/RHISceneRenderer.cpp)
- **Automated Tests:**
  - [Test_LightingServicesUnit.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_LightingServicesUnit.cpp)
  - [Test_M15_ReflectionCaptureContractUnit.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_M15_ReflectionCaptureContractUnit.cpp)
  - [Test_M15_ReflectionCaptureRendering.cpp](file:///d:/WorkSpace/AxleEngine/Tests/rendering/Test_M15_ReflectionCaptureRendering.cpp)
  - [Test_EnvironmentRendering.cpp](file:///d:/WorkSpace/AxleEngine/Tests/src/Test_EnvironmentRendering.cpp)
- **Documentation & Coordination:**
  - [PLAN.md](file:///d:/WorkSpace/AxleEngine/docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md)
  - [M15_U5_D_REVIEW.md](file:///d:/WorkSpace/AxleEngine/docs/reviews/M15_U5_D_REVIEW.md)
  - [CODEX_GEMINI_EXCHANGE.json](file:///d:/WorkSpace/AxleEngine/docs/agents/CODEX_GEMINI_EXCHANGE.json)

---

## 3. Verification Matrix (Debug & Release after Final Code Edit)

All 24 automated verification runs executed cleanly after the final code/test edit (`exit 0`, zero asserted RHI validation errors on NVIDIA GeForce RTX 3070 with D3D12 & Vulkan validation layers active):

| Check | Debug Result | Release Result | Notes |
| :--- | :--- | :--- | :--- |
| `AxleRenderUnitTests.exe --m15-reflection-capture-contract` | PASS (`exit 0`) | PASS (`exit 0`) | Contract, YAML round-trip, stamps, LRU plan, scheduler plan |
| `AxleRenderUnitTests.exe --lighting-services` | PASS (`exit 0`) | PASS (`exit 0`) | Domain invalidation, retained Pending/Stale/Failed diagnostics |
| `AxleRenderUnitTests.exe` (full suite) | PASS (`exit 0`) | PASS (`exit 0`) | All CPU render unit suites pass |
| `AxleEnvironmentBakeTests.exe` | PASS (`exit 0`) | PASS (`exit 0`) | Bake orientation & transactional validation |
| `ctest -C {Debug,Release} -R "M15ReflectionCaptureContract\|M15EnvironmentContract"` | 2/2 PASS (`exit 0`) | 2/2 PASS (`exit 0`) | Registered CTest contracts |
| `AxleRHIResourceViewTests.exe dx12 storage` | PASS (`exit 0`) | PASS (`exit 0`) | Per-mip storage views on D3D12 |
| `AxleRHIResourceViewTests.exe vulkan storage` | PASS (`exit 0`) | PASS (`exit 0`) | Per-mip storage views on Vulkan |
| `AxleRHIResourceViewTests.exe vulkan targets` | PASS (`exit 0`) | PASS (`exit 0`) | Swapchain presentation targets on Vulkan |
| `AxleEnvironmentRenderingTests.exe dx12` | PASS (`exit 0`) | PASS (`exit 0`) | Native D3D12 U5-B/C/D + device reset, 0 validation errors |
| `AxleEnvironmentRenderingTests.exe vulkan` | PASS (`exit 0`) | PASS (`exit 0`) | Native Vulkan U5-B/C/D + device reset, 0 validation errors |
| `AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback` | PASS (`exit 0`) | PASS (`exit 0`) | Strict Vulkan startup (`5 UI frames`, `fallback=none`) |
| `python scripts/check_architecture.py` | PASS (`exit 0`) | — | 613 files, 25 CMake files (`AXLE_ARCH_CHECK PASS`) |
| `git diff --check` | PASS (`exit 0`) | — | Clean whitespace/EOF check |

### Explicit Scope & Verification Limits
- **DX12 Presentation Target Limit:** As established in U5-C (`M15_U5_C_CODEX_REVIEW.md`), `SwapChainInterface` initialization for `AxleRHIResourceViewTests dx12 targets` and strict `--rhi=dx12 --no-fallback` Editor startup is unsupported on this local adapter/driver setup and was not claimed as D3D12 presentation evidence; native D3D12 compute/rendering/storage views (`dx12 storage`, `AxleEnvironmentRenderingTests dx12`) passed in both Debug and Release.
- **Deferred Interactive Visual Checks:** Interactive `EDITOR VISUAL` items (`M15-U5-21.3`, `21.4`, `26.1`) remain `NOT RUN` and unchecked in [PLAN.md](file:///d:/WorkSpace/AxleEngine/docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md), deferred per user direction to the consolidated post-U5 visual verification pass.


## Codex review outcome (2026-11-17)

Automated U5-D scope was accepted after independent repairs to cache admission
and recovery, removed-probe retirement, selected-probe eviction protection,
override receipts and synchronization with preceding main-view submissions.
The original implementation and self-verification claims above remain historical
reported evidence. See [the independent findings, final executed matrix and
acceptance limits](M15_U5_D_CODEX_REVIEW.md). Final Debug/Release strict DX12 and
Vulkan presentation/startup checks now pass; interactive fixture visuals remain
user-deferred and NOT RUN. Partial captures still require a stable input window.
