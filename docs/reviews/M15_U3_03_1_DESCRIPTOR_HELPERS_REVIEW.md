# M15 U3-03.1 - Descriptor update helpers

## Scope and result

**PASS (self-review).** Source commit `1cb3a8b7` checks texture and sampler
descriptor updates at the RHI helper and their material, scene-renderer and VFX
callers. Storage updates, constant updates and the broader descriptor-set
immutability rule remain U3-03.2, U3-03.3 and U3-04 respectively.

## Implementation

`RHIDescriptorPool::UpdateTextureRange` now asks `RHIResourceService` to validate
and prepare the replacement dependencies, invoke the native update, then publish
the new dependency map. Validation or injected update rejection leaves the old
published dependencies intact. The single texture, sampler range and single
sampler helpers use this path. NRI's `UpdateDescriptorRanges` returns `void`, so
the helper cannot observe a driver error after that call; the fault hook rejects
before native mutation and exposes a testable failure path.

Callers now check the returned bool. Material/VFX creation and renderer
initialization fail when a required update fails. View passes abort through their
existing failure path. `SetIBLMaps` has a `void` API; on update failure it logs an
error and disables IBL instead of silently claiming the new maps are bound.

## Verification

Build commands (each exit 0):

```text
cmake --build build --config Debug --target AxleRenderGraphTests AxleEditor --parallel 4
cmake --build build --config Debug --target AxleRenderGraphTests AxleEnvironmentRenderingTests --parallel 4
cmake --build build --config Release --target AxleRenderGraphTests AxleEnvironmentRenderingTests --parallel 4
```

Each test command below exited 0. The RenderGraph fixture tests real texture and
sampler descriptors, wrong types/ranges, injected update rejection, preserved
texture dependencies and successful retry. The environment fixture exercises a
submitted renderer view and pixel/readback checks through the changed callers.

| Config | Backend | Commands | Logs |
|---|---|---|---|
| Debug | D3D12 | `build/Tests/Debug/AxleRenderGraphTests.exe dx12`; `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/M15-U3-03.1/env-debug-dx12` | `build/agent-runs/M15-U3-03.1/debug-dx12.log`; `debug-env-dx12.log` |
| Debug | Vulkan | `build/Tests/Debug/AxleRenderGraphTests.exe vulkan`; `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/M15-U3-03.1/env-debug-vulkan` | `debug-vulkan.log`; `debug-env-vulkan.log` |
| Release | D3D12 | `build/Tests/Release/AxleRenderGraphTests.exe dx12`; `build/Tests/Release/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/M15-U3-03.1/env-release-dx12` | `release-dx12.log`; `release-env-dx12.log` |
| Release | Vulkan | `build/Tests/Release/AxleRenderGraphTests.exe vulkan`; `build/Tests/Release/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/M15-U3-03.1/env-release-vulkan` | `release-vulkan.log`; `release-env-vulkan.log` |

All RenderGraph logs end `RenderGraph authority PASS`. All environment logs end
`PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload,
fallback and upload retry`. The first environment invocation omitted its required
output-directory argument and exited 2 without running the fixture; the table
records the corrected invocations and their generated artifacts.

`git diff --check` and exchange schema validation passed. The Release environment
link emitted the existing LNK4098 runtime-library warning.
