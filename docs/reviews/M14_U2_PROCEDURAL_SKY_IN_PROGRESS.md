# M14-U2 — Dynamic Procedural Skybox IBL

## Status

Implemented and independently verified. Procedural skies now generate a floating-point equirectangular source, reuse the existing environment baker for radiance/irradiance/prefilter/BRDF products, publish generations transactionally, and feed the native PBR IBL path when `EnvironmentLighting` is enabled.

## Checklist

- [x] Add a CPU-only procedural sky generator matching the existing atmospheric shader's gradient, sun disk, halo, turbidity, and ground model.
- [x] Reuse `BakeEnvironment` for radiance, irradiance, GGX prefilter, and BRDF LUT products.
- [x] Add procedural parameter identity and bake-settings cache keys.
- [x] Preserve transactional generation publication and bounded four-generation cache.
- [x] Allow modern PBR IBL selection for procedural sky when `EnvironmentLighting` is explicitly enabled.
- [x] Preserve background-only semantics when environment lighting is disabled.
- [x] Compile changed production sources and test sources with MSVC directly.
- [x] Rebuild `AxleEnvironmentBakeTests` and run the new procedural CPU assertions.
- [x] Rebuild `AxleEnvironmentRenderingTests` Debug/Release and run native D3D12/Vulkan plus OpenGL policy fixtures.
- [x] Record native readback/parity evidence and tick Phase 44.5 in the M14 checklist.

## Changed Sources

- `Engine/Renderer/include/Axle/EnvironmentBake.hpp`
- `Engine/Renderer/src/EnvironmentBake.cpp`
- `Engine/Scene/src/RHIEnvironment.cpp`
- `Engine/Scene/src/RHISceneRenderer.cpp`
- `Tests/src/Test_EnvironmentBake.cpp`
- `Tests/src/Test_EnvironmentRendering.cpp`

## Verification Evidence

- Direct MSVC compile of `EnvironmentBake.cpp`: PASS.
- Direct MSVC compile of `RHIEnvironment.cpp`: PASS.
- Direct MSVC compile of `RHISceneRenderer.cpp`: PASS.
- Direct MSVC compile of `Test_EnvironmentBake.cpp`: PASS.
- Direct MSVC compile of `Test_EnvironmentRendering.cpp`: PASS.
- Standalone linked procedural bake probe: PASS; generated 32x16 HDR source, baked 16px radiance/irradiance/prefilter products, and verified zenith/horizon color separation.
- `cmake --build build --config Debug --target AxleEnvironmentBakeTests AxleEnvironmentRenderingTests --parallel 6`: PASS.
- Debug `AxleEnvironmentBakeTests.exe`: PASS; procedural generation and transactional validation assertions pass.
- Debug `AxleEnvironmentRenderingTests.exe` on D3D12, Vulkan, and OpenGL: PASS; HDR background-only behavior, procedural IBL response, cache reuse, and readback policy fixtures pass.
- `cmake --build build --config Release --target AxleEnvironmentBakeTests AxleEnvironmentRenderingTests --parallel 6`: PASS.
- Release `AxleEnvironmentBakeTests.exe`: PASS.
- Release `AxleEnvironmentRenderingTests.exe` on D3D12, Vulkan, and OpenGL: PASS.
