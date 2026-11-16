# M15 U3 batch 6 — Resolver unification, fallback diagnostics, and built-in/graph parity

The user authorized sequential implementation of Milestone 15 items `M15-U3-20` through `M15-U3-23.4`. CPU unit contracts and GPU native verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

## U3-20 — Smooth-sphere normal-field and anti-faceting metric

**PASS (self-verification).**
- Implemented in `Tests/rendering/Test_M15_SurfaceAndMaterialContractUnit.cpp`:
  - Generates smooth sphere geometry ($64 \times 32$ rings, 8,385 vertices) with analytical surface normals.
  - Verifies radial alignment: $|\|P\| - 1.0| \le 10^{-6}$ and $|N - P| \le 10^{-6}$ across all vertices.
  - Computes maximum angular deviation between adjacent surface normals: $\theta_{\max} = 3.97675^\circ$, satisfying the anti-faceting contract threshold ($\le 4.1^\circ$).
  - Validates rejection on coarse geometry ($12 \times 6$ rings, $\theta_{\max} \approx 22.8^\circ > 4.1^\circ$).
- *Note:* Editor visual verification remains a user-provided visual gate.

## U3-21.1 through U3-21.4 — Resolver unification

**PASS (self-verification).**
- **21.1 (Built-in resolver):** `MaterialResolver::ResolveBuiltin` maps `MaterialDesc` into `ResolvedSurface`, preserving base color, roughness/metallic clamping, AO, normal strength, and advanced feature lobes.
- **21.2 (Material Graph resolver):** `MaterialGraphRuntime::ResolveSurface` overload accepts canonical `ResolvedSurface` inputs, unifying graph evaluation with built-in surface semantics and annotating fallback diagnostics.
- **21.3 (Preview resolver):** `MaterialResolver::ResolvePreview(const RHI::RHIMaterial&, uint64_t)` bridges asset and inspector material preview state into `ResolvedSurface`.
- **21.4 (Runtime resolver):** `MaterialResolver::ResolveRuntime(const RenderMaterialData&)` bridges extracted render thread scene data into `ResolvedSurface`.
- Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: property mapping consistency across all four resolver pathways.

## U3-22 — Explicit unsupported feature masks and fallback diagnostics

**PASS (self-verification).**
- Defined `UnsupportedSurfaceFeatures` bitmask and diagnostic helper in `Engine/Renderer/include/Axle/ResolvedSurface.hpp` covering `Sheen`, `Subsurface`, `Absorption`, `Refraction`, and `Dispersion`.
- When custom or graph material pipelines encounter unsupported lobes, `BuildUnsupportedFeaturesDiagnostic` generates a structured diagnostic string `SURFACE_UNSUPPORTED_FEATURES: ... (mask=0x...)` and sets `ResolvedSurface::Fallback = true`.
- Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: 0 mask produces clean diagnostics; non-zero flags generate correct feature names and bitmasks.

## U3-23.1 through U3-23.4 — Built-in vs Material Graph parity

**PASS (self-verification).**
- **23.1 (Lights):** `MaterialSurface.hlsl` aligned with `ForwardPBR.hlsl`: direct sun lighting calibrated via `DIRECT_LUX_TO_RADIANCE` ($0.000035$), `INV_PI` Lambertian diffuse normalization, and GGX specular distribution. `RHISceneRendererPasses.cpp` passes unscaled physical sun illuminance.
- **23.2 (Shadows):** Shadow attenuation and cascade mapping contracts verified.
- **23.3 (IBL):** Analytical ambient scaled by `u_SunColor.w` and modulated by AO and `FresnelSchlickRoughness`.
- **23.4 (Output policy):** `MaterialSurface.hlsl` upgraded with ACES filmic tonemapping (Narkowicz fit) and gamma 2.2 / HDR clamp (`[0.0, 65000.0]`) branching on `u_CameraPosition.w <= 0.0`, matching `ForwardPBR.hlsl`.
- **Native GPU Fixture (F3 resolution):** Added native parity fixture in `Tests/src/Test_EnvironmentRendering.cpp`. Direct readback comparison between built-in material and custom graph material under directional sun lighting yielded center pixel difference:
  - Direct3D 12: `(232, 213, 191)` vs `(232, 213, 191)` -> `diff = (0, 0, 0)`.
  - Vulkan: `(232, 213, 191)` vs `(232, 213, 191)` -> `diff = (0, 0, 0)`.

## Verification Evidence Matrix

1. **Architecture & Standards:**
   - `python scripts/check_architecture.py`: `AXLE_ARCH_CHECK PASS` (611 files, 25 CMake files).
   - `git diff --check`: Exit 0 (no trailing whitespace or format issues).
2. **CPU Contracts & Unit Tests:**
   - Debug: `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract` (exit 0).
   - Debug Suite: `build/Tests/Debug/AxleRenderUnitTests.exe` (exit 0).
   - Release: `build/Tests/Release/AxleRenderUnitTests.exe --m15-surface-material-contract` (exit 0).
   - Release Suite: `build/Tests/Release/AxleRenderUnitTests.exe` (exit 0).
3. **GPU Native Rendering (D3D12 & Vulkan):**
   - Debug D3D12: `build/Tests/Debug/AxleEnvironmentRenderingTests.exe native-dx12 build/Tests/EnvironmentRendering/Debug-DX12` (exit 0, `diff=(0,0,0)`).
   - Debug Vulkan: `build/Tests/Debug/AxleEnvironmentRenderingTests.exe native-vulkan build/Tests/EnvironmentRendering/Debug-Vulkan` (exit 0, `diff=(0,0,0)`).
   - Release D3D12: `build/Tests/Release/AxleEnvironmentRenderingTests.exe native-dx12 build/Tests/EnvironmentRendering/Release-DX12` (exit 0, `diff=(0,0,0)`).
   - Release Vulkan: `build/Tests/Release/AxleEnvironmentRenderingTests.exe native-vulkan build/Tests/EnvironmentRendering/Release-Vulkan` (exit 0, `diff=(0,0,0)`).
