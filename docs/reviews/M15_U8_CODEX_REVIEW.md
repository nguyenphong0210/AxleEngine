# M15 U8 independent HDR post and temporal review

Gemini delivered `a13155b4..6f22017a` and reported visual recovery in `CG-20261121T023647247766Z-GEMINI-198b3b`. Codex reviewed the integration from `73ce4ecf` and corrected it in `7b1bf41c`. The native tests and capture-plan replays below are independent evidence. The separate source recheck covered the new history, motion, exposure, and bloom paths.

## Findings and corrections

| Finding | Correction |
|---|---|
| U8-R1 · P1 · Similar-colored disocclusions retain the wrong surface | TAA now compares current-surface previous depth with the previous frame's saved depth at the reprojected coordinate. It validates a neighboring motion vector before using it for the center. A missing previous model or an occupied pixel with only sky velocity falls back to current color. The motion pass only writes over matching opaque depth. |
| U8-R2 · P1 · Exposure can sample overlays and reads exposure declared write-only | Histogram bin accumulation and steady-state exposure declare storage reads and writes. A reset seeds exposure. Exposure and bloom use clean scene color, with the physics overlay composed after post even when TAA is off. Vulkan uses the actual FP16 storage format. |
| U8-R3 · P2 · Bloom can overflow or quantize early, and post passes overwrite alpha | All six bloom levels use FP16 linear radiance. Normalizing the first octave before additive blending bounds intermediate values. Output pipelines match the actual color format. Bloom and TAA retain viewport alpha, TAA preserves HDR peaks, and post-only views skip the unused screen-storage texture. |
| U8-R4 · P2 · Orthographic jitter shears the projection | Scene and sky projection apply a constant screen-space translation using clip `w`. The two camera projection modes use the same jitter convention. The capture tool can move the camera continuously to test adaptation while preserving history. |

## Requirement-to-evidence matrix

| M15 U8 requirement | Production path and independent check | Status |
|---|---|---|
| 01.1–01.2 · Histogram and reduction | `AutoExposure.hlsl` and `PostProcessPass` clear, accumulate and reduce 256 bins. Native mixed-tile fixtures verify the whole-viewport result. | PASS |
| 02.1–02.3 · Adaptation, elapsed time and view ownership | `ViewContext` and `PostProcessPass` keep independent exposure allocations across interleaved views. The native 30, 60 and 120 Hz fixtures check the adaptation curve. Editor retains exposure history during a bright-room step. | PASS |
| 03.1–03.4 · Tonemap, first frame, black frame and camera cut | `ToneMapping.hlsl` consumes the computed exposure. Native tests verify the displayed image, first-frame target, black fallback, and camera-cut reset. | PASS |
| 04 · Clean metering | Auto-exposure precedes bloom. UI is composed after post. Native fixtures check physics-overlay invariance with TAA both on and off. | PASS |
| 05.1–05.4 · Mixed tiles and 30/60/120 Hz | Native mixed-distribution cases and adaptation outputs exercise the production shaders. | PASS |
| 06, 07.1–07.2 · Extraction and six mip sizes | `Bloom.hlsl` thresholds level 0 once. `RHIViewTargets` validates all levels, including `3×5` and `127×63`. | PASS |
| 08.1–08.2, 09 · Reconstruction, gain and tint | Additive HDR reconstruction preserves earlier levels. Native impulse, gain and tint fixtures verify the composite. | PASS |
| 10.1–10.4 · Bloom fixtures | Native halo, linear gain, tiny/odd sizes, and HDR/RGBA8/BGRA8 outputs. Editor compares bloom with a fixed exposure and camera. | PASS |
| 11.1–11.3 · Motion vectors and jitter | `Velocity.hlsl`, `ViewContext` and `ApplyProjectionJitter` cover camera/object motion, stable identity and first spawn. CPU and native checks cover projection and no static jitter leak. | PASS |
| 12.1–12.2 · Disocclusion and invalid history | `TAA.hlsl` validates previous depth, screen bounds and geometry motion. The similar-color edge fixture compares the newly revealed sample with a freshly rendered reference. | PASS |
| 13.1–13.5 · Reset triggers | Native and CPU fixtures reset on resize, view switch, camera cut, settings/format change and scene generation. | PASS |
| 14.1–14.3 · Transparent, reactive and overlay policies | Transparent geometry and particles leave opaque motion intact. `ReactiveMask` suppresses history weight. Native presentation tests verify the overlay is unjittered and excluded from metering. | PASS |
| 15.1–15.5 · Image quality and temporal regressions | Native static diagonal, moving object, similar-color disocclusion, resize/view switch, transparent wake and additive snap-off fixtures, plus the timestamp/VRAM scorecard. | PASS |

## Execution evidence

Evidence root is `build/agent-runs/codex-m15-u8`. The [replay fixture](../../Tests/Fixtures/M15/U8_HDRBloomTAA/) contains the scene and UTF-8 plan. Copy the directory under `build/agent-runs/` before launching Editor so startup metadata stays out of the fixture. See the [capture tool guide](../agents/EDITOR_VISUAL_CAPTURE.md).

| Run | Result and artifact |
|---|---|
| `cmake --build build --config <config> --target AxleEnvironmentRenderingTests AxleRenderUnitTests AxleEditor AxleRuntimeRenderTests` | Debug and Release exit 0. See `verify-build-*.log`. |
| `ctest --test-dir build -C <config> -R '^(M15U8PostTemporalContract\|M15DDGIContract\|M15FroxelFogContract\|M15OcclusionContract\|M15ReflectionCaptureContract)$' --output-on-failure` | 5/5 each in Debug and Release. See `contract-*.log`. |
| `AxleEnvironmentRenderingTests.exe <backend> <output> --m15-u8` | Four passes across Debug and Release on D3D12 and Vulkan. Each similarly colored reveal matched the fresh reference with `max_rgb_delta=0`. Adaptation ended at `2.6582`, `2.6484`, and `2.6367` at 30, 60 and 120 Hz. See `u8-*.log`. |
| `AxleEditor.exe --project=<project> --rhi=<backend> --no-fallback --rhi-validation --capture-plan=<plan>` | Debug D3D12, Release D3D12 and Release Vulkan all exit 0. Every replay renders 179 successful views with zero failed views, zero Console errors and zero nonfinite pixels. Vulkan reports 10 unused vertex input warnings. See `editor-*.log` and the captures in `<config>_<backend>/`. |
| `AxleRuntimeRenderTests.exe <backend>` | Release product regression passes for D3D12, Vulkan and OpenGL WGL. See `runtime-*.log`. |
| `python scripts/check_architecture.py` and `git diff --check` | Both pass. The architecture scan covers 625 source files and 25 CMake files. |

The locked-camera ON/OFF captures show a bounded warm halo around the emitter on all three configurations. A continuous camera move from the dark setup to the bright setup preserves exposure history. The early bright-room frame has a mean of about 249/255 in the central crop. After 100 frames the same region measures 195.3 on Release D3D12, 161.7 on Release Vulkan and 143.1 on Debug D3D12. Those captures use the measured elapsed time for each configuration. The native tests hold elapsed time constant for the rate comparison. Capture metrics are in `image-metrics.txt`.

The 1280×720 GPU scorecard measures view-target allocations of `29.8789 MiB` on D3D12 and `29.8960 MiB` on Vulkan, including the two new depth textures. Both are within the fixture's `64 MiB` budget. Captured GPU duration includes the named post passes and reflects this fixture workload.

## Coverage limits

Verification uses a single GeForce RTX 3070 on D3D12 and Vulkan, and OpenGL for Runtime compatibility. The depth comparison uses previous rigid transforms. Unsupported custom vertex/depth changes can forgo temporal history where exact motion coverage is unavailable. The built-in Runtime retains its existing post-effect defaults. Explicit render requests and the Editor Render FX controls exercise the optional HDR post features.
