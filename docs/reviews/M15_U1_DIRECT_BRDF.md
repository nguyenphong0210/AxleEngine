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
