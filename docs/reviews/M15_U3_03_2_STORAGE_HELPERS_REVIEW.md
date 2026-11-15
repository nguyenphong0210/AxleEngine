# M15 U3-03.2 - Storage descriptor update helpers

## Scope and result

**PASS (self-review).** Source commit `5210156f` covers the storage-texture
descriptor helper and its scene pass callers. `UpdateStorageTexture` already
delegated to the validated, transactional `UpdateTextureRange` path from
U3-03.1. The missing behavior was checking the bool result in GTAO, SSR and
Fog; those passes now abort through their existing `fail()` path before bind
and dispatch if a storage update fails. DDGI, Hi-Z, Bloom and Auto-Exposure
storage updates were already checked.

## Dedicated fixture

`AxleRenderGraphTests` allocates a real `STORAGE_TEXTURE` descriptor set and
binds a real UAV. It rejects SRV role and out-of-range updates, injects an
`UpdateDescriptorRanges` rejection before the native call, and verifies that
the old texture and `SHADER_RESOURCE_STORAGE` access/layout remain published.
It then retries with a replacement UAV and restores the original. This is an
injected pre-native failure: NRI's update entry point returns `void`, so the
helper cannot observe a driver-returned error after that call.

## Verification

Builds (exit 0):

```text
cmake --build build --config Debug --target AxleRenderGraphTests AxleEnvironmentRenderingTests --parallel 4
cmake --build build --config Release --target AxleRenderGraphTests AxleEnvironmentRenderingTests --parallel 4
```

Build logs: `build/agent-runs/M15-U3-03.2/build-debug.log` and
`build/agent-runs/M15-U3-03.2/build-release.log`. After the final access-layout
assertion was added, `AxleRenderGraphTests` was rebuilt in both configurations
with exit 0; logs are `build-rendergraph-debug.log` and
`build-rendergraph-release.log` in the same directory.

| Config | Backend | Commands | Result / logs |
|---|---|---|---|
| Debug | D3D12 | `build/Tests/Debug/AxleRenderGraphTests.exe dx12`; `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/M15-U3-03.2/env-debug-dx12` | Both exit 0: `debug-rendergraph-dx12.log`, `debug-env-dx12.log` |
| Debug | Vulkan | `build/Tests/Debug/AxleRenderGraphTests.exe vulkan`; `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/M15-U3-03.2/env-debug-vulkan` | Both exit 0: `debug-rendergraph-vulkan.log`, `debug-env-vulkan.log` |
| Release | D3D12 | `build/Tests/Release/AxleRenderGraphTests.exe dx12`; `build/Tests/Release/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/M15-U3-03.2/env-release-dx12` | Both exit 0: `release-rendergraph-dx12.log`, `release-env-dx12.log` |
| Release | Vulkan | `build/Tests/Release/AxleRenderGraphTests.exe vulkan`; `build/Tests/Release/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/M15-U3-03.2/env-release-vulkan` | Both exit 0: `release-rendergraph-vulkan.log`, `release-env-vulkan.log` |

All RenderGraph logs end `RenderGraph authority PASS`. All environment logs
end `PASS HDR native subresources, sky/specular/diffuse, cache, exposure,
reload, fallback and upload retry`. The environment fixture exercises the
changed scene renderer on submitted views and saves images in each `env-*`
artifact directory. `git diff --check` passed. The Release environment link
emitted the existing LNK4098 runtime-library warning.

U3-03.3 constant updates and U3-04 descriptor immutability remain open.
No Editor screenshot is required for this code/fixture checkpoint.
