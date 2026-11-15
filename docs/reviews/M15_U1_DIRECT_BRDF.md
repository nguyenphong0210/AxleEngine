# M15 U1 Direct BRDF — Sequential Checkpoint

> Independent S1.1 review supersedes the earlier U1-01.1 and U1-01.4
> self-verification claims below. See `M15_U1_S1_1_INDEPENDENT_REVIEW.md`:
> intensity mapping was repaired in `4af042b2`; GPU auto exposure now feeds
> tone mapping in `c5b9f82f`. S1.1 remains open for Editor visual evidence.

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

## M15-U1-01.3 — Freeze BRDF normalization

Result: **PASS**.

- Frozen constants in `Engine/Resources/Shaders/ForwardPBR.hlsl` and `Engine/Resources/Shaders/PBRCommon.hlsli`:
  - `BRDF_DIFFUSE_NORMALIZATION = 0.31830988618379067154` (exact $1/\pi$ diffuse normalization).
  - `BRDF_SPECULAR_DENOMINATOR_SCALE = 4.0` (Cook-Torrance $4 (N\cdot V)(N\cdot L)$ specular normalization).
  - Spot light specular denominator aligned to `BRDF_SPECULAR_DENOMINATOR_SCALE * NdotV * max(NdotL, 0.001) + 0.0001` matching Sun and Point lights.
- `Tests/rendering/Test_LightingServicesUnit.cpp` verifies:
  - Exact match of `InvPi` with $1/\pi$ within $10^{-7}$.
  - GGX NDF numerical hemispherical integration $\int_0^{\pi/2} D(\theta) \cos\theta \sin\theta 2\pi d\theta = 1.000 \pm 0.002$ across roughnesses $[0.05, 1.0]$.
  - Lambertian diffuse hemispherical integration $\int_0^{\pi/2} \frac{1}{\pi} \cos\theta \sin\theta 2\pi d\theta = 1.0000 \pm 10^{-4}$ for unit albedo.
  - Direct energy partition: $k_D = (1 - k_S)(1 - \text{metallic})$; pure metals have $k_D = 0$ strictly; pure dielectrics conserve $k_D + k_S = 1.0$; total partition never exceeds 1.0.
  - Canonical shader token check across all direct light paths (Sun, Point, Spot).
- Verification:
  - `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0.
  - `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0.
  - `ctest --test-dir build -C Debug -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
  - `ctest --test-dir build -C Release -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
- Source commit: `35905c6`.

Next sequential leaf: `M15-U1-01.4 — Freeze exposure interaction`.

## M15-U1-01.4 — Freeze exposure interaction

Result: **PARTIAL / NOT RUN for Editor visual gate; automated test PASS**.

- Implementation & Contracts:
  - Verified direct sunlight reference radiance $L_o \approx 1.1140846\text{ cd/m}^2$ under frozen `DIRECT_LUX_TO_RADIANCE` and diffuse $1/\pi$.
  - Linear exposure scaling: exposure 1.0 preserves radiance 1:1; exposure 2.0 doubles radiance linearly.
  - Safe fallback: non-positive or non-finite exposure falls back to 1.0 safely without invalid black/negative output.
  - Auto-exposure contract: `PostProcessService` adapts exposure dynamically to direct scene luminance, clamping strictly to $[\text{AutoExposureMin}, \text{AutoExposureMax}] = [0.1, 10.0]$.
  - `ToneMapping.hlsl` verifies positive exposure fallback `(u_Params.x > 0.0) ? u_Params.x : 1.0` and pre-tonemapping radiance scaling `hdrColor * exposure`.
- Verification:
  - `cmake --build build --config Debug --target AxleRenderUnitTests`: exit 0.
  - `cmake --build build --config Release --target AxleRenderUnitTests`: exit 0.
  - `ctest --test-dir build -C Debug -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
  - `ctest --test-dir build -C Release -R '^RenderUnitTests$' --output-on-failure`: exit 0 (1/1 passed).
  - `cmake --build build --config Debug --target AxleEditor`: exit 0.
  - `cmake --build build --config Release --target AxleEditor`: exit 0.
  - `git diff --check`: exit 0.
- Source commit: `6615896`.

Editor visual gate remains open: build and launch AxleEditor, open test fixture, sweep exposure, and verify smooth highlight/shadow scaling without black pixels or clamping artifacts.

Next sequential SubUnit: `S1.2 — GGX visibility and F0 reference (M15-U1-02.1)`.
