# M15 U1 Direct BRDF — Sequential Checkpoint

## M15-U1-01.1 — Freeze direct-light units

Result: **PASS**.

- Frozen constants in `Engine/Resources/Shaders/ForwardPBR.hlsl`:
  - `DIRECT_LUX_TO_RADIANCE = 0.000035`: directional sun illuminance in Lux (e.g. 100,000 lx direct sunlight) to scene-linear radiance scale.
  - `LOCAL_INTENSITY_SCALE = 0.05`: local light (point/spot) intensity scale applied after windowed inverse-square attenuation.
- `Tests/rendering/Test_LightingServicesUnit.cpp` verifies:
  - Artist directional intensity scales to Lux against 100,000 lx direct sunlight.
  - Additional directional lights are pre-scaled once on the CPU by `100000.0f * FrozenDirectLuxToRadiance`.
  - Artist point/spot intensities use the frozen 1000/500 local scales.
  - Physical directional illuminance > 100 lx passes through unchanged.
  - `ForwardPBR.hlsl` declares `DIRECT_LUX_TO_RADIANCE` and `LOCAL_INTENSITY_SCALE`, and does not bypass them with literal factors.
- Verification:
  - `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0.
  - `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0.
  - `ctest --test-dir build -C Debug -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
  - `ctest --test-dir build -C Release -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
- Source commit: `ab776a4`.

Next sequential leaf: `M15-U1-01.2 — Freeze sun angular-radius policy`.

## M15-U1-01.2 — Freeze sun angular-radius policy

Result: **PASS**.

- Frozen constants in `Engine/Resources/Shaders/ForwardPBR.hlsl`:
  - `SUN_DEFAULT_ANGULAR_RADIUS = 0.00462512`: physical angular radius of solar disk (0.53° diameter in radians).
  - `SUN_MAX_ANGULAR_RADIUS = 0.25`: upper bound for authored solar angular radius.
  - Convolution: `sunConvolvedRoughness = sqrt(saturate(roughness^2 + sunAngularRadius^2))`.
- `Tests/rendering/Test_LightingServicesUnit.cpp` verifies:
  - Exact match of `0.00462512 rad` with $0.53^\circ / 2 \times \pi / 180^\circ$ within $10^{-6}\text{ rad}$.
  - Default `RenderDirectionalLight::SolarAngularRadius` is `0.00462512f`.
  - Negative values clamp to `0.0f` (point directional collapse).
  - Authored values exceeding $0.25\text{ rad}$ clamp to $0.25\text{ rad}$.
  - Roughness 0.0 convolved with physical sun yields finite floor $0.00462512$, preventing specular microfacet singularity.
  - Roughness convolution is monotonic across $[0.0, 1.0]$ and bounded by $1.0$.
  - Canonical shader token check for `SUN_DEFAULT_ANGULAR_RADIUS`, `SUN_MAX_ANGULAR_RADIUS`, clamping, fallback, and convolution.
- Verification:
  - `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0.
  - `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0.
  - `ctest --test-dir build -C Debug -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
  - `ctest --test-dir build -C Release -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
- Source commit: `7ff495a`.

Next sequential leaf: `M15-U1-01.3 — Freeze BRDF normalization`.
