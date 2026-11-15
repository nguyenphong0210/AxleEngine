# M15 U2 S2.1 Environment Input Contract Review

## Scope

S2.1 freezes the three environment input contracts without starting S2.2 generation cache/publication or S2.3 consumer behavior:

- U2-01.1 procedural-sky convention.
- U2-01.2 environment-bake convention.
- U2-01.3 authored-cubemap color/orientation and explicit environment-lighting opt-in.

## Implementation

Implementation commit: `6c98b0e0` (`feat(m15): freeze environment input contract`).

- `Engine/Renderer/include/Axle/EnvironmentBake.hpp` records the linear HDR, equirectangular and fixed cubemap-face conventions.
- `Engine/Scene/include/Axle/Components.hpp` records the independent background and authored IBL policy while preserving authored HDR identity.
- `Tests/rendering/Test_M15_EnvironmentContractReferenceUnit.cpp` adds an independent CPU fixture covering deterministic procedural output, parameter rejection, bake dimensions, constant radiance, BRDF finiteness, face order/orientation, authored extraction and non-mutating invalid input.
- `Tests/rendering/RenderUnitTestMain.cpp` and `Tests/CMakeLists.txt` add `--m15-environment-contract-reference` and `M15EnvironmentContractReference`.

## Verification

All commands ran from `D:\WorkSpace\AxleEngine` against the final source.

| Configuration | Command | Result |
|---|---|---|
| Debug | `cmake --build build --config Debug --target AxleRenderUnitTests --parallel 4` | exit 0 |
| Debug | `ctest --test-dir build -C Debug -R "^M15EnvironmentContractReference$" --output-on-failure` | 1/1 pass, exit 0 |
| Debug | `cmake --build build --config Debug --target AxleEnvironmentBakeTests --parallel 4` | exit 0 |
| Debug | `ctest --test-dir build -C Debug -R "^EnvironmentBakeTests$" --output-on-failure` | 1/1 pass, exit 0 |
| Debug | `ctest --test-dir build -C Debug -R "^RenderUnitTests$" --output-on-failure` | 1/1 pass, exit 0 |
| Release | `cmake --build build --config Release --target AxleRenderUnitTests --parallel 4` | exit 0 |
| Release | `ctest --test-dir build -C Release -R "^M15EnvironmentContractReference$" --output-on-failure` | 1/1 pass, exit 0 |
| Release | `cmake --build build --config Release --target AxleEnvironmentBakeTests --parallel 4` | exit 0 |
| Release | `ctest --test-dir build -C Release -R "^EnvironmentBakeTests$" --output-on-failure` | 1/1 pass, exit 0 |
| Release | `ctest --test-dir build -C Release -R "^RenderUnitTests$" --output-on-failure` | 1/1 pass, exit 0 |

## Decision

**PASS for M15-U2-S2.1.** The parent and children U2-01.1, U2-01.2 and U2-01.3 are complete. S2.2 cache/publication and S2.3 consumer behavior remain unreleased.
