# M15 U1-04.1 roughness/view/light-angle sweep

Status: **accepted**. The independent CPU reference and Debug/Release CTest gates pass, and the user supplied Editor screenshot shows the required roughness progression with Console `Error 0`.

## Scope and source

- Source commit: `cc0623d9`
- Base: `fde09d9b` (accepted S1.3)
- Scope: M15-U1-04.1 only. U1-04.2 and later leaves remain outside this release.

## Implementation

`Tests/rendering/Test_M15_RoughnessSweepReferenceUnit.cpp` is an independent direct-light reference. It does not call the production shader helpers or the environment LUT baker. It checks:

- roughness input clamping in the direct, textured and packed ORM shader paths;
- the five fixture values `0.04`, `0.12`, `0.25`, `0.50` and `0.80`;
- finite non-zero mirror responses;
- monotonic peak reduction and half-power lobe broadening;
- view/light-angle samples around the mirror direction, with finite non-negative values and the mirror direction remaining the local maximum;
- a denser valid-hemisphere light-angle sweep with no finite-range discontinuity.

The runner exposes `--m15-roughness-sweep-reference` and CTest registers `M15RoughnessSweepReference`.

## Verification

All commands ran from `D:\WorkSpace\AxleEngine`.

| Check | Debug | Release |
|---|---:|---:|
| `cmake --build build --config <config> --target AxleRenderUnitTests --parallel 6` | exit 0 | exit 0 |
| `ctest --test-dir build -C <config> -R '^M15RoughnessSweepReference$' --output-on-failure` | 1/1 PASS | 1/1 PASS |
| `ctest --test-dir build -C <config> -R '^(M15RoughnessSweepReference|M15BRDFReference|M15PrimaryLightingReference|RenderUnitTests)$' --output-on-failure` | 4/4 PASS | 4/4 PASS |
| `AxleRenderUnitTests.exe --m15-roughness-sweep-reference` | exit 0 | exit 0 |

Reference output is stable in both configurations:

| Roughness | Peak response | Half-power width |
|---:|---:|---:|
| 0.04 | 1524.96 | 0.25° |
| 0.12 | 18.8257 | 1.25° |
| 0.25 | 0.99844 | 4.75° |
| 0.50 | 0.0615266 | 19.25° |
| 0.80 | 0.00869757 | 65.5° |

## Editor gate

The user supplied an Editor screenshot of `M15_RoughnessSweep` with all five spheres visible, the Directional Light selected, `Cast Real-Time Shadows` enabled, and Console `Error 0`. The highlight progression is continuous from the narrow highlight on `roughness_004` through the broad highlight on `roughness_050`; `roughness_080` is the broadest/softest response. The local shadow banding observed while testing point/spot lights is a separate quality finding and is not claimed as fixed by this sweep.

U1-04.1 is accepted at source `cc0623d9`. U1-04.2 and later leaves remain outside this release.
