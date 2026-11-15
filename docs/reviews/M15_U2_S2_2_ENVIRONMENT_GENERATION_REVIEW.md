# M15 U2 S2.2 — Environment generation publication, cache and recovery

## Scope

This review covers U2-02.1 through U2-02.4 only:

- publish a complete native environment generation atomically;
- identify cache entries from source/request, interpretation and bake settings;
- remove stale native generations after device loss and rebuild them;
- retain a same-source last-good generation, or use a diagnosed neutral fallback.

S2.3 consumer policy remains unreleased and is outside this review.

## Implementation review

`RHIEnvironmentGeneration::IsValid()` requires Radiance, Irradiance, Prefilter and BRDF textures and checks each native wrapper. `RHIEnvironment::Prepare()` constructs a candidate off to the side and publishes it only after the four-texture upload succeeds. The cache key includes the request/source identity, source revision, image interpretation and bake settings. The cache is an LRU with an explicit limit of four generations.

`Fallback()` never replaces a valid candidate with a failed one. It searches only the same request domain, retains the newest valid generation and appends a diagnostic. `Prepare()` removes generations whose native wrappers are no longer valid, which makes device shutdown/reinitialization transactional from the caller's perspective.

The native fixture in `Tests/src/Test_EnvironmentRendering.cpp` exercises these paths with real GPU resources: four-map publication, intensity/exposure reuse, source revision replacement, corrupted source recovery, redirected missing source neutral fallback, injected upload failure/retry, LRU eviction and device shutdown/rebuild.

## Source changes

- `Engine/Scene/include/Axle/RHIEnvironment.hpp`: documented the immutable generation/publication and same-source fallback contract.
- `Engine/Scene/src/RHIEnvironment.cpp`: named the four-generation cache limit and documented failure retention.

## Verification

Builds (exit 0):

```text
cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 4
cmake --build build --config Release --target AxleEnvironmentRenderingTests --parallel 4
```

Native direct runs (all exit 0):

| Configuration | Backend | Command | Log |
|---|---|---|---|
| Debug | D3D12 | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe dx12 build\Tests\Environment\Debug\dx12` | `build/agent-runs/M15-U2-S2.2/debug-dx12.log` |
| Debug | Vulkan 1.3 | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe vulkan build\Tests\Environment\Debug\vulkan` | `build/agent-runs/M15-U2-S2.2/debug-vulkan.log` |
| Debug | OpenGL 4.5 fallback | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe gl build\Tests\Environment\Debug\gl` | `build/agent-runs/M15-U2-S2.2/debug-gl.log` |
| Release | D3D12 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build\Tests\Environment\Release\dx12` | `build/agent-runs/M15-U2-S2.2/release-dx12.log` |
| Release | Vulkan 1.3 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build\Tests\Environment\Release\vulkan` | `build/agent-runs/M15-U2-S2.2/release-vulkan.log` |
| Release | OpenGL 4.5 fallback | `build\Tests\Release\AxleEnvironmentRenderingTests.exe gl build\Tests\Environment\Release\gl` | `build/agent-runs/M15-U2-S2.2/release-gl.log` |

DX12/Vulkan logs end with:

```text
PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry
```

OpenGL logs end with the expected fallback fixture result:

```text
PASS OpenGL background-only and explicit IBL pixels, snapshot and ECS bridge gates
```

The direct runs use the existing native fixture and do not require an Editor screenshot. Generated logs and output assets are under `build/agent-runs/M15-U2-S2.2` and are not committed.

## Acceptance

**ACCEPTED.** U2-02.1, U2-02.2, U2-02.3 and U2-02.4 are verified in Debug and Release on D3D12, Vulkan and the OpenGL fallback path. S2.3 remains open.
