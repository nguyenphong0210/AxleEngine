# M15 U1 S1.2 GGX and F0 reference verification

Status: **implementation self-verified; independent recheck NOT RUN**. The derived S1.2 parent and its two children remain unchecked in the PLAN until that review gate is satisfied.

## Scope and source

- Baseline: `551fd4b7` (accepted S1.1).
- Implementation: `5c65cd84` (`fix(m15): align GGX DFG visibility with forward shading`).
- Released leaves: M15-U1-02.1 and M15-U1-02.2 only. U1-03 and later leaves are outside this release.

## M15-U1-02.1: GGX visibility and energy compensation

The previous environment DFG bake used a Schlick-GGX product with `k = roughness² / 2`; the forward shader used height-correlated Smith visibility. The DFG result therefore measured a different single-scatter lobe from the one multiplied by the shader's energy compensation. The newly added independent midpoint quadrature fixture failed against the baseline bake (Debug `M15BRDFReference`, exit 1) before the correction.

`IntegrateEnvironmentBRDF` now evaluates the same isotropic correlated Smith visibility as the shader's `MaterialGeometry`. The reference fixture integrates the BRDF over light directions, independently of the production half-vector/Hammersley sampler. It also integrates anisotropic GGX NDF over the hemisphere and checks normalization within `0.002`. At 32,768 production samples, the corrected scale/bias values were:

| N·V | Roughness | Independent A/B | Bake A/B |
| ---: | ---: | ---: | ---: |
| 0.35 | 0.80 | 0.655643 / 0.0145264 | 0.655632 / 0.0145262 |
| 0.65 | 0.55 | 0.829851 / 0.00656778 | 0.829859 / 0.00656776 |
| 0.85 | 0.25 | 0.994555 / 0.000212588 | 0.994538 / 0.000212549 |

The fixture accepts absolute error below `0.002` for both channels. It checks that compensation is finite, F0 ordered, neutral for `r = 1`, capped at 8, and neutral for zero/NaN LUT energy. The shader explicitly replaces nonfinite or nonpositive LUT energy with `1`, then clamps positive energy to `[0.05, 1]`. The resident fallback BRDF LUT is `(1, 0)`, so it selects neutral compensation. Shader source checks bind the reference to the actual NDF, Smith visibility, compensation formula, and sun/point/spot/rect/ambient usage.

## M15-U1-02.2: dielectric and metal F0

The independent CPU reference uses dielectric F0 `0.04` per channel and metal F0 equal to linear RGB base color, with a linear metallic interpolation. For gold RGB `(0.85, 0.52, 0.13)`, the fixture checks:

| Metallic | Expected F0 RGB |
| ---: | --- |
| 0 | `(0.04, 0.04, 0.04)` |
| 0.5 | `(0.445, 0.28, 0.085)` |
| 1 | `(0.85, 0.52, 0.13)` |

It also checks endpoint clamping, `ResolvedSurface` packing of base color/metallic into shader constants, and the shader's shared F0 expression and Fresnel usage in sun, point, spot, rect and ambient paths. No F0 production change was necessary.

## Executed verification

All commands below were run from `D:\WorkSpace\AxleEngine` against the source recorded in implementation commit `5c65cd84`. The last CMake edit before commit changed indentation only.

| Command or gate | Debug | Release | Evidence |
| --- | --- | --- | --- |
| `cmake --build build --config <config> --target AxleRenderUnitTests --parallel 6` | exit 0 | exit 0 | `build/Tests/<config>/AxleRenderUnitTests.exe` |
| `ctest --test-dir build -C <config> -R '^(M15BRDFReference\|RenderUnitTests)$' --output-on-failure` | exit 0, 2/2 | exit 0, 2/2 | `build/Testing/Temporary/LastTest.log`; dedicated fixture can also run with `--m15-brdf-reference` |
| `cmake --build build --config <config> --target AxlePreviewRenderTests --parallel 6` | exit 0 | exit 0 | `build/Tests/<config>/AxlePreviewRenderTests.exe` |
| `AxlePreviewRenderTests.exe dx12 surfaces` | exit 0, PASS | exit 0, PASS | `build/agent-runs/m15-s12/<config>-dx12-surfaces.log` |
| `AxlePreviewRenderTests.exe vulkan surfaces` | exit 0, PASS | exit 0, PASS | `build/agent-runs/m15-s12/<config>-vulkan-surfaces.log` |
| DXC `-T ps_6_0 -E PSMain` and `-spirv -T ps_6_0 -E PSMain` on `ForwardPBR.hlsl` | exit 0 | same source | `build/agent-runs/m15-s12-forward.dxil` and `.spv` |
| `git diff --cached --check` before source commit | exit 0 | same source | clean diff |

The native surface fixture exercises shader compilation, valid material pixels, light and environment behavior, and GPU validation on D3D12/Vulkan. It is a pipeline smoke/readback gate, not a per-pixel quantitative BRDF sweep. That broader image gate belongs to M15-U1-04.1. Neither U1-02 leaf declares an Editor visual gate, so no Editor screenshot is requested for S1.2. OpenGL fallback behavior is reserved for U1-05.

## Open review gate

The repository protocol requests a separate-context recheck for significant Codex corrections before acceptance. A reviewer was invoked read-only on `5c65cd84`, but its session failed before inspection because its refresh token was revoked. No reviewer result is claimed. The implementation and verification above are complete; independent acceptance remains **NOT RUN**.
