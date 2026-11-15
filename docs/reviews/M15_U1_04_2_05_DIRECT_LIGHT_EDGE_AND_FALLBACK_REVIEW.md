# M15 U1-04.2 / U1-05 Direct-Light Edge and Fallback Verification

## Scope

This review closes the two remaining M15 U1 leaves:

- U1-04.2: finite direct-light behavior for zero, tangent and behind-surface vectors.
- U1-05: explicit fallback behavior for unsupported local-light shadow capability tiers.

The CPU reference is independent of production shader helpers and renderer execution. The runtime fallback check uses the existing OpenGL RenderService fixture, which exercises the actual compatibility renderer and diagnostic path.

## Implementation

Implementation commit: `034f07e7` (`feat(m15): verify direct-light edge and fallback policies`).

- `Tests/rendering/Test_M15_DirectLightEdgeAndFallbackReferenceUnit.cpp`
  - Evaluates an independent Cook-Torrance sample with safe normalization and bounded zero return for invalid/behind inputs.
  - Covers roughness `0.04`, `0.25`, `0.80`, `1.0`, metallic `0` and `1`, zero vectors, tangent vectors and back-facing vectors.
  - Checks the shader normal reorientation, NdotV floor, local-light rejection and denominator floor tokens.
  - Checks the capability matrix and the `LOCAL_SHADOW_UNSUPPORTED` RenderService gate.
- `Tests/rendering/RenderUnitTestMain.cpp` and `Tests/CMakeLists.txt`
  - Add two focused command lines and CTest entries: `M15DirectLightEdgeReference` and `M15DirectLightFallbackReference`.

## Verification evidence

All commands were run from `D:\WorkSpace\AxleEngine` against the final source in this worktree.

| Configuration | Build | Focused tests | Full RenderUnitTests | Runtime GL fallback |
|---|---|---|---|---|
| Debug | `cmake --build build --config Debug --target AxleRenderUnitTests --parallel 4` — exit 0 | `ctest --test-dir build -C Debug -R "M15DirectLight(Edge|Fallback)Reference" --output-on-failure` — 2/2 pass, exit 0 | `ctest --test-dir build -C Debug -R "^RenderUnitTests$" --output-on-failure` — pass on rerun, exit 0 | `build\\Tests\\Debug\\AxleRenderServiceTests.exe gl` — exit 0, `OPT05 RenderService PASS` |
| Release | `cmake --build build --config Release --target AxleRenderUnitTests --parallel 4` — exit 0 | `ctest --test-dir build -C Release -R "M15DirectLight(Edge|Fallback)Reference" --output-on-failure` — 2/2 pass, exit 0 | `ctest --test-dir build -C Release -R "^RenderUnitTests$" --output-on-failure` — pass, exit 0 | `build\\Tests\\Release\\AxleRenderServiceTests.exe gl` — exit 0, `OPT05 RenderService PASS` |

The first Debug full-suite invocation hit an unrelated pre-existing assembly/material persistence check at `Tests/rendering/Test_RenderServiceUnit.cpp:665`; the immediate rerun passed without source changes. This is recorded as an environment-sensitive first-run result, not a failure of either new M15 fixture.

The GL runtime reports the physical adapter `NVIDIA GeForce RTX 3070`, creates the compatibility window, exercises requested local shadows, verifies the `LOCAL_SHADOW_UNSUPPORTED` diagnostic, verifies warning suppression when local shadows or the global shadow toggle are disabled, and shuts down with a successful `OPT05 RenderService PASS`. Runtime artifacts were written under:

- `build/agent-runs/OPT-05/consumer-fixtures/gl-208472141004900/thumbnail.axle` (Debug)
- `build/agent-runs/OPT-05/consumer-fixtures/gl-208490817980400/thumbnail.axle` (Release)

The optional `LocalShadowGLPolicy` CTest remains opt-in behind `AXLE_ENABLE_LOCAL_SHADOW_GPU_TESTS`; the exact executable command above was run directly in both configurations.

## Decision

**PASS for M15-U1-04.2 and M15-U1-05.** No Editor screenshot is required for these leaves: the acceptance evidence is the independent CPU reference plus the actual OpenGL fallback runtime fixture and its explicit diagnostic.
