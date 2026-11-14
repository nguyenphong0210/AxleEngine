# M14-U7 — Backend and Fallback Parity

Status: **Accepted**

## Delivered contracts

- `RenderBackendPolicy` (`Engine/Renderer/include/Axle/RenderBackendPolicy.hpp`) converts raw RHI capability facts into a backend quality tier, pass capability map and explicit feature states.
- `RHIDeviceCapabilities` now publishes independent facts for compute shaders, storage textures, timestamp queries, descriptor sets, HDR formats and MSAA targets. Policy code no longer infers support from unrelated limits.
- Unsupported settings are resolved on a copy. Compatibility mode disables compute-dependent passes, HDR-only post effects, FXAA/TAA and multisampling while preserving authored settings and reporting fallback reasons.
- `RHISceneRenderer` uses the policy to configure `RenderPassCapabilities` and records each requested-but-disabled feature in `RenderDiagnostics`.
- Existing strict graph, descriptor/pipeline failure, target-generation and device-epoch tests remain the failure authority; no failure is converted into a silent success.

## Backend matrix

| Backend | Quality | Native path | Explicit fallback |
|---|---|---|---|
| D3D12 | Standard | RHI compute/UAV, timestamp, descriptor, HDR/MSAA, ForwardPBR | M14 keeps RT disabled even when hardware exists; SSR/IBL is authoritative |
| Vulkan 1.3 | Standard | RHI compute/UAV, timestamp, descriptor, HDR/MSAA, ForwardPBR | M14 keeps RT disabled even when hardware exists; SSR/IBL is authoritative |
| OpenGL 4.5 compatibility | Compatibility | Analytical forward/standard GL renderer | no compute/UAV, HDR post, FXAA/TAA, or MSAA view targets; direct/IBL image remains |

## Verification evidence

- `cmake --build build --config Debug --target AxleRenderUnitTests --parallel 6` — passed.
- `build/Tests/Debug/AxleRenderUnitTests.exe` — passed, including synthetic native/degraded/OpenGL policy cases, pipeline/graph failure contracts, and device-epoch reset coverage.
- `cmake --build build --config Release --target AxleRenderUnitTests --parallel 6` — passed.
- `build/Tests/Release/AxleRenderUnitTests.exe` — passed.
- `build/Tests/Debug/AxleRHIFoundationTests.exe policy` — passed strict/fallback selection and rollback probes.
- `build/Tests/Debug/AxleRHIFoundationTests.exe dx12` — passed native D3D12 initialization, injected interface failures and recovery.
- `build/Tests/Debug/AxleRHIFoundationTests.exe vulkan` — passed native Vulkan validation, injected interface failures and recovery.
- `build/Tests/Debug/AxleRHIFoundationTests.exe gl` — passed OpenGL compatibility and D3D12/Vulkan-to-GL fallback equivalence probe.
- `build/Tests/Debug/AxleRenderAcceptanceTests.exe policy` — passed strict/fallback product policy.
- `build/Tests/Debug/AxleRenderAcceptanceTests.exe dx12 build/agent-runs/m14-u7-dx12.json` — passed native serial/worker acceptance.
- `build/Tests/Debug/AxleRenderAcceptanceTests.exe vulkan build/agent-runs/m14-u7-vulkan.json` — passed native serial/worker acceptance.
- `python scripts/check_architecture.py` — PASS.
- `git diff --check` — PASS.

The OpenGL path is intentionally a reduced compatibility subset; its lack of modern compute/HDR/post-process parity is surfaced as policy state and is not counted as native feature parity.
