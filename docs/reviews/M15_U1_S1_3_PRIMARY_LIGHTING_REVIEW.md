# M15 U1 S1.3 primary-light units and shadows

Status: **implementation self-verified; independent recheck pending**. The derived S1.3 parent and its three children remain unchecked in the PLAN until the separate-context review is complete.

## Scope and source

- Baseline: `d92ce53a` (accepted S1.2 task record).
- Implementation: `c6245e2e` (`test(m15): add primary lighting and shadow reference`).
- Released leaves: M15-U1-03.1, M15-U1-03.2 and M15-U1-03.3 only. U1-04 and later leaves are outside this release.

## Implementation

`Tests/rendering/Test_M15_PrimaryLightingReferenceUnit.cpp` is an independent CPU fixture that exercises the production `LightingService`, `CascadedShadowMap` and `BuildLocalShadowFrame` contracts. It checks:

- directional `1.0` -> `100,000` illuminance, point `2.0` -> `2,000`, spot `2.0` -> `1,000`;
- shadow publication fallback keeps direct light inputs and emits `shadow_publication_unavailable`;
- four CSM cascades have finite matrices, ordered splits, positive texel/depth ranges and exact max-distance coverage;
- invalid directional input clears the CSM frame with an error;
- point six-face cubemap views and spot cone projection use finite zero-to-one depth and stay within the atlas;
- invalid local shadow resolution clears views and reports `LOCAL_SHADOW_SETTINGS`;
- ForwardPBR and LocalShadows receivers call the matching directional, point and spot visibility paths with comparison sampling, clamped PCF and explicit unsupported fallback.

The existing native shadow fixtures provide the GPU evidence rather than relying only on source-token checks:

- `AxleCascadedShadowRenderingTests` checks receiver darkening, real depth in all four cascades, cutout alpha behavior, flags, resize/failure recovery and camera policy.
- `AxleLocalShadowRenderingTests` checks spot and point darkening, all six point faces, cube-face seam coverage, cache publication, cutout updates, opt-out and allocation retry.

## Verification evidence

All commands ran from `D:\WorkSpace\AxleEngine`.

| Gate | Debug | Release | Artifact/readback |
| --- | --- | --- | --- |
| `cmake --build build --config <config> --target AxleRenderUnitTests AxleCascadedShadowMapTests AxleCascadedShadowRenderingTests AxleLocalShadowTests AxleLocalShadowRenderingTests --parallel 6` | exit 0 | exit 0 | `build/Tests/<config>/` |
| `ctest --test-dir build -C <config> -R '^(M15PrimaryLightingReference\|M15BRDFReference\|RenderUnitTests)$' --output-on-failure` | exit 0, 3/3 | exit 0, 3/3 | `build/Testing/Temporary/LastTest.log` |
| `AxleCascadedShadowMapTests.exe` | exit 0 | exit 0 | CSM splits, ZO coverage, stabilization and invalid-input policy |
| `AxleLocalShadowTests.exe` | exit 0 | exit 0 | local projection, six faces, culling, indexing and budget |
| `AxleCascadedShadowRenderingTests.exe dx12` | exit 0, PASS | exit 0, PASS | `build/agent-runs/m15-s13/<config>-dx12-csm.log`; 54 darkened pixels, all cascades depth-written |
| `AxleCascadedShadowRenderingTests.exe vulkan` | exit 0, PASS | exit 0, PASS | `build/agent-runs/m15-s13/<config>-vulkan-csm.log`; 54 darkened pixels, all cascades depth-written |
| `AxleLocalShadowRenderingTests.exe dx12` | exit 0, PASS | exit 0, PASS | `build/agent-runs/m15-s13/<config>-dx12-local.log`; spot 196 and point 179 darkened pixels; six faces 484 depth pixels each |
| `AxleLocalShadowRenderingTests.exe vulkan` | exit 0, PASS | exit 0, PASS | `build/agent-runs/m15-s13/<config>-vulkan-local.log`; spot 196 and point 179 darkened pixels; six faces 484 depth pixels each |
| `cmake --build build --config <config> --target AxleEditor --parallel 6` | exit 0 | exit 0 | `build/Editor/<config>/AxleEditor.exe` |
| `git diff --check` | exit 0 | same source | clean diff |

No Editor visual screenshot is claimed yet. The PLAN's S1.3 leaves declare an Editor visual gate, so the user must still inspect the applicable fixture and provide the required Editor evidence before final acceptance. Native GPU evidence is complete on D3D12/Vulkan; OpenGL local-shadow fallback remains outside this subunit.

## Open gate

Separate-context review is required before acceptance. The implementation and all automated/native verification above are complete, but S1.3 remains **IMPLEMENTED / PENDING REVIEW AND EDITOR VISUAL EVIDENCE**.
