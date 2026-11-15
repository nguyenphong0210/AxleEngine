# M15 U1 / S1.1 independent review

**Result: HOLD.** Reviewed source at `dc6e2ccf` (2026-11-15). S1.1 comprises
M15-U1-01.1 through M15-U1-01.4. The sun-radius default/clamp and the direct
BRDF normalization contracts are acceptable at their stated CPU/shader-source
boundary. The direct-light unit and exposure contracts are not accepted.

## Findings

### S1.1-R1 — GPU auto exposure does not affect the displayed image (high)

`Engine/Resources/Shaders/AutoExposure.hlsl` writes an adapted value to a 1x1
`u_ExposureOutput`, but `Engine/Scene/src/RHISceneRendererPasses.cpp` does not
bind/read that texture in its tone-mapping pass. At lines 1615–1623, the CPU
derives a synthetic luminance from `settings.Exposure` and
`settings.AutoExposureKeyValue`, then sends that exposure to `ToneMapping.hlsl`.
Changing only scene luminance therefore cannot drive the displayed exposure.
Furthermore, every compute group writes the same 1x1 output from a separate
group-local histogram, so the recorded value is not a deterministic whole-image
histogram. `Test_LightingServicesUnit.cpp` calls `PostProcessService` with a
supplied luminance and scans shader text; it does not exercise the runtime
producer-to-consumer path. This blocks M15-U1-01.4, regardless of a visual
screenshot of the current Editor.

**Required correction:** publish one deterministic frame exposure from the HDR
scene and consume it in the active tone-mapping path, with a test or readback
whose output changes when scene luminance changes while authored exposure stays
fixed. Then capture the PLAN's Editor visual evidence for Debug and Release.

### S1.1-R2 — intensity-to-unit mapping has destructive thresholds (high)

`Engine/Renderer/src/LightingService.cpp` interprets directional intensity
`<= 100` as a multiplier of 100,000 lux, but `> 100` as lux. Point and spot
intensities similarly change interpretation at 10. The current mapping yields:

| Input transition | Value at threshold | Value immediately above |
|---|---:|---:|
| Directional `100 -> 100.001` | 10,000,000 | 100.001 |
| Point `10 -> 10.001` | 10,000 | 10.001 |
| Spot `10 -> 10.001` | 5,000 | 10.001 |

These are changes of units inside one authored field, with no mode flag or
diagnostic. The point/spot Editor controls allow values across 10. The shader
constants `DIRECT_LUX_TO_RADIANCE` and `LOCAL_INTENSITY_SCALE` preserve a
historical scene-linear calibration; they do not resolve this ambiguity or
constitute a photometric conversion. The existing unit test checks values away
from the boundaries and therefore passes. This blocks M15-U1-01.1.

**Required correction:** specify one stable meaning for each authored intensity
field, migrate existing defaults/presets/serialized values as needed, and test
the boundary, zero, negative/non-finite inputs, and CPU-to-GPU packing. Preserve
the documented scene-linear calibration separately from SI unit claims.

### S1.1-R3 — source checks are narrower than rendered-light validation (medium)

The tests for M15-U1-01.2 and M15-U1-01.3 evaluate standalone CPU equations
and search exact strings in `ForwardPBR.hlsl`; no shader output is compared
against an independent numeric image fixture. The angular radius is currently
fixed at the `RenderDirectionalLight` default for authored scenes because
`DirectionalLightComponent` has no radius field and extraction does not populate
one. This does not invalidate the accepted default/clamp and normalization
contracts, but their current PASS labels must not be used as evidence of authored
radius controls, GPU energy conservation, backend parity, or the full U1 exit.
Those checks belong to the later U1 validation leaves.

## Independently rerun verification

| Command | Result |
|---|---|
| `cmake --build build --config Debug --target AxleRenderUnitTests --parallel 6` | exit 0 |
| `ctest --test-dir build -C Debug -R '^RenderUnitTests$' --output-on-failure` | exit 0, 1/1 |
| `cmake --build build --config Release --target AxleRenderUnitTests --parallel 6` | exit 0 |
| `ctest --test-dir build -C Release -R '^RenderUnitTests$' --output-on-failure` | exit 0, 1/1 |
| Independent threshold reproduction using the `LightingService.cpp` expressions | directional, point and spot discontinuities reproduced |

The two CTest passes verify that the existing test executable passes at this
source revision; they do not close R1 or R2. No Editor image, GPU readback, or
native backend comparison was run in this review. No S1.1 acceptance is recorded.
