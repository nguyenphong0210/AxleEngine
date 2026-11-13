# Milestone 14 Codex Review

## Decision

**Full Milestone 14 acceptance: HOLD.**

The released M14-U3/U4 scope is conditionally accepted for the exercised paths. The full U0-U9 claim is not accepted because several later-unit requirements are represented by shaders, components, toggles, or smoke checks without the production path and evidence required by the milestone architecture.

## Verified evidence

- Base reviewed: `6e8e9a3`; reviewed head: `f5f677f`.
- `python scripts/check_architecture.py`: PASS, 576 source files and 25 CMake files.
- Debug build: `AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`, `AxleEditor`, and `AxleRuntime`: exit 0.
- Debug CTest: `EnvironmentBakeTests`, `RenderAcceptancePolicy`, and `RenderUnitTests`: 3/3 PASS.
- Release build: `AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`, `AxleEnvironmentBakeTests`, and `AxleRenderAcceptanceTests`: exit 0.
- Release CTest: the same three focused tests: 3/3 PASS.
- `AxleEnvironmentRenderingTests`: exit 0 on native D3D12, native Vulkan 1.3, and OpenGL 4.5 fallback in Debug and Release. The fixtures record zero RHI validation errors and exercise shader compilation, storage views, ScreenNormal/ScreenStorage targets, GTAO/contact toggles, and fallback rendering.

These checks are real reruns in this review. The test fixture mostly asserts successful submission, non-empty readback, and selected pixels; it is not proof of all advertised algorithms or the U9 performance budget.

## Findings

### M14-R1 — P1 — SSR has no Hi-Z depth pyramid

- **Source:** `Engine/Resources/Shaders/SSR.hlsl:11-21, 37-42`; `Engine/Scene/src/RHISceneRenderer.cpp:3221-3263`.
- **Expected:** Phase 44.9 requires a min-depth mip chain and hierarchical raymarching with roughness cone tracing.
- **Observed:** SSR binds one `Texture2D<float>` depth SRV and samples mip 0. The renderer copies one depth texture and dispatches the raymarcher; no Hi-Z texture, mip-generation pass, or mip traversal exists.
- **Impact:** The Phase 44.9 checkbox and the U5 Hi-Z contract overstate the delivered implementation.

### M14-R2 — P1 — DDGI is data-only and never rendered

- **Source:** `Engine/Scene/src/SceneExtraction.cpp:230-243`; `Engine/Renderer/include/Axle/SceneRenderSettings.hpp:22`; `Engine/Scene/src/RHISceneRenderer.cpp:3326-3348`; `Engine/Resources/Shaders/VolumetricFog.hlsl:11-22`.
- **Expected:** A $32\times16\times32$ probe grid, irradiance/distance atlases, visibility updates, temporal scheduling, and an IBL/ambient fallback.
- **Observed:** ECS extraction and serialization populate a `RenderDDGIProbeGrid`, but the renderer has no DDGI resource, update pass, atlas binding, probe sampling, or `EnableDDGI` use. `u_DDGIParams` is only a constant in the fog pass.
- **Impact:** Phase 44.13 is not implemented; the experimental feature has no runtime behavior to verify.

### M14-R3 — P1 — Auto-exposure histogram shader is not wired

- **Source:** `Engine/Resources/Shaders/AutoExposure.hlsl`; `Engine/Scene/src/RHISceneRenderer.cpp:3503-3515`.
- **Expected:** A dispatched log-luminance histogram with smooth adaptation and the authored key-value/limits.
- **Observed:** `AutoExposure.hlsl` is present but no compile, pipeline, descriptor, dispatch, or exposure-output resource is created. The renderer uses a fixed `dt` and blends toward `settings.Exposure`; `AutoExposureKeyValue` is not consumed.
- **Impact:** Phase 44.14's histogram requirement is not delivered. The current toggle is a CPU scalar smoothing path, not histogram auto-exposure.

### M14-R4 — P1 — TAA history never ping-pongs

- **Source:** `Engine/Scene/src/RHISceneRenderer.cpp:3447-3499`.
- **Expected:** The resolved current frame becomes the next frame's history and the read/write history indices swap after each frame.
- **Observed:** Every frame samples `history0`, renders into `history1`, then copies `sceneTargetTexture` to `history1` and immediately copies `history0` over `history1`. `history0` is never updated or swapped.
- **Impact:** After the first frame, TAA continues reading stale or initially undefined history. The current two-frame smoke test only checks non-empty readback and does not detect this.

### M14-R5 — P1 — U9 performance acceptance is unverified

- **Source:** `docs/milestones/MILESTONE_14_LIGHTING_MATERIALS/PLAN.md:91, 109`; no M14 U9 scorecard/report exists under `docs/reviews`.
- **Expected:** The shared Milestone 18 benchmark/timestamp/parity scorecard and the stated 3.5–5.0 ms GPU lighting budget.
- **Observed:** The rerun fixtures use 128x128 images and blocking readback. No GPU timestamp measurements, 1080p/1440p matrix, frame-time report, or scorecard is present.
- **Impact:** The final acceptance gate cannot be marked complete from the available evidence.

### M14-R6 — P2 — Bloom is one full-resolution pass, not a six-tier pyramid

- **Source:** `Engine/Resources/Shaders/Bloom.hlsl`; `Engine/Scene/src/RHISceneRenderer.cpp:3382-3445`.
- **Expected:** Six-tier downsample/upsample pyramid with separate intermediate levels.
- **Observed:** One full-size `ScreenStorage` target is written by one dispatch with `BloomParams.w` fixed at mode 0, followed by one fullscreen composite. No mip/level resources or upsample passes exist.

### M14-R7 — P2 — GTAO is full-resolution despite the declared half-resolution contract

- **Source:** `docs/milestones/MILESTONE_14_LIGHTING_MATERIALS/PLAN.md:81`; `Engine/Scene/src/RHISceneRenderer.cpp:3144-3184`.
- **Expected:** Half-resolution GTAO at the declared 960x540 footprint with reconstruction.
- **Observed:** The storage target is configured at the view width/height and dispatch uses `(width + 7) / 8` and `(height + 7) / 8`. The shader applies a 5x5 bilateral filter at full resolution.
- **Impact:** This may be functionally valid for a small fixture, but it does not meet the stated performance/scale contract.

## Acceptance boundary

M14-U3/U4: **conditional pass for the rerun paths** (native D3D12/Vulkan/OpenGL shader/resource/fallback smoke and zero recorded RHI validation errors). The tests do not close the broader U3 transient/history fixture contract or the declared half-resolution budget.

M14-U5/U6/U7/U9: **not accepted** pending R1–R5. M14-U8 and the editor controls can remain opt-in/reference code, but the milestone cannot be called complete while the dependent core and final acceptance gates are open.

No source repair was made in this review because R1–R3 and R5 require feature implementation and measurement work beyond the released U3/U4 boundary; R4 is a correctness repair that must be made together with the TAA history contract and its regression fixture.
