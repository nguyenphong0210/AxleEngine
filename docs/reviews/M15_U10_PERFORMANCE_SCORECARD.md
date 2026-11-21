# Milestone 15 Unit 10 (`M15-U10`) — Performance, Memory, Lifecycle & Soak Scorecard (`U10-D2`)

- **Scope:** `M15-U10-12.1` → `12.3`, `M15-U10-14.1` → `16.2` (`U10-D2`; independent consolidated review `U10-E` / `M15-U10-17.1..19.2` left open for Codex).
- **Status:** Self-verified input to `U10-E` (`Release` unvalidated timings + `Debug`/`Release` `--validate` lifecycle & soak + `EDITOR VISUAL` corroboration).

---

## 1. Environment, Adapter, Driver & Traceability (`M15-U10-16.1`, `16.2`)

| Field | Measured Value |
|---|---|
| **GPU Adapter** | `NVIDIA GeForce RTX 3070` (`8,232 MiB` Dedicated VRAM, Discrete GPU, vBIOS `94.04.25.80.6a`) |
| **GPU Driver** | `610.88` (Windows WDDM `32.0.16.1088`, DriverDate `2026-07-22`) |
| **GPU Timestamp Frequency** | `1,000,000,000 Hz` (`1 ns` tick resolution on both D3D12 & Vulkan 1.3) |
| **OS** | `Microsoft Windows 11 Pro` `10.0.26200` (`Build 26200`, 64-bit) |
| **CPU** | `13th Gen Intel(R) Core(TM) i5-13400` |
| **Backends & Fallback Policy** | `Direct3D 12` & `Vulkan 1.3` with `--no-fallback` (`AllowFallback = false`, strictly physical discrete adapter) |
| **API Validation Policy** | Validation **ON** (`EnableNRIValidation = EnableValidation = true`) for `U10-D2` lifecycle suites, validated 1,000-frame soak, lifetime stress tests, and Editor visual captures; validation **OFF** during Release performance benchmarks & unvalidated soak so validation layers do not skew GPU/CPU timings |
| **Frozen Engine / Editor / Runtime / Shaders Commit** | `4abbc663` (`4abbc66382d4bcaed9f52e4cfe81d2c936b5e896`, `git diff --stat 4abbc663 -- Engine Editor Runtime` is empty) |
| **`U10-D1` Test Fixes / Parity Manifest Commits** | `b10da533` (`Tests/`), `104798df` (`docs/reviews/M15_U10_PARITY_MANIFEST.md` & `PLAN.md`) |
| **`U10-D2` Test Harness Commit** | `34e216e9` (`34e216e990bba19a9d3428332f50a9f777f2d8cf` — `Tests/`-only: `M15U10BaselineScorecardMain.cpp`, `RenderWorkerStress.cpp`, `Tests/CMakeLists.txt`) |
| **Fixtures / Scene Specs** | Programmatic reproducible `U10D2_Workload` + exported `build/agent-runs/u10d2/workload/U10D2_Workload.axlescene` & `U10D2.axleproj` |

### Executable Hashes (`SHA-256`)

| Executable | Config | SHA-256 |
|---|---|---|
| `build/Tests/Release/AxleM15U10BaselineScorecard.exe` | Release | `899EA3E6501FD41117A2C498BB6ECE8DF41D2F8A5C3CAEE35007F76B172E9E04` |
| `build/Tests/Debug/AxleM15U10BaselineScorecard.exe` | Debug | `BFCF4EFA794544C22BDE73B61CBD1616D1AB82F7B1B2C0781B8FED74C19F5FF0` |
| `build/Tests/Release/AxleRenderLifetimeStressTests.exe` | Release | `41764B98E728A6BA639ECAE2733377E081BC02B981A4D40DA9E6D91E54B28D38` |
| `build/Tests/Debug/AxleRenderLifetimeStressTests.exe` | Debug | `DEE62A7081181D34DAEDBF109AC73E8E9564C2E8BA2CF986C9F05570D9C5B6B8` |
| `build/Editor/Debug/AxleEditor.exe` | Debug (`4abbc663`) | `77096447B0B787EF19F75579D6CB2C7DBB48E6E4433B2B381CC94693` |
| `build/Editor/Release/AxleEditor.exe` | Release (`4abbc663`) | `7CCBB6F5EE243CD650C0C946001ABD8B499A9A2BE016CF32392A579D9B1A8B64` |

---

## 2. Reproducible Reference Workload (`M15-U10-14.3`)

Constructed identically across C++ `M15U10BaselineScorecard` and serialized `U10D2_Workload.axlescene`:
- **Scene Geometry:** `48` entities (`49` with optional `DDGIProbeGrid` in `UltraDDGI`), `36` mesh instances (`16` PBR metallic/roughness sweep spheres, `4` SSS spheres with `SubsurfaceEnabled=true`, `2` emissive bloom beacons, `12` shadow-casting cube/cylinder pillars, `1` hero centerpiece sphere, `1` large planar reflector floor `builtin://plane` scaled `16x1x16`).
- **Mesh Counts:** `176,677` vertices, `339,170` triangles; `drawCalls = 219` (`Low`/`Medium`, planar disabled on `Low`, `Planar` enabled `Medium` only if scene triggers or preset enables — note `Low`/`Medium` have `EnablePlanarReflections = false` in `RenderQualityPreset.cpp`, `758` draws in `High`/`Ultra`, `2,534` draws in `UltraDDGI` including DDGI probe geometry capture passes).
- **Lighting & Environment:**
  - `1` `DirectionalLightComponent` (`CastShadows=true`, `ShadowQuality=1` / 2048 4-cascade CSM, `MaxShadowDistance=120 m`)
  - `4` `PointLightComponent` (`2` with `Shadows.CastShadows=true`, `2` fill)
  - `2` `SpotLightComponent` (both `Shadows.CastShadows=true`)
  - `1` `RectLightComponent` (analytic LTC area light)
  - `1` `SkyboxComponent` (`ProceduralAtmospheric`, `EnvironmentLighting=true`)
  - `1` `VolumetricFogComponent` (`Density=0.015`, `MaxDistance=90 m`)
  - `1` `ReflectionProbeComponent` (`DynamicCapture=true`, `128x128` cubemap, `ParallaxCorrected=true`)
  - `1` `PlanarReflectionComponent` (`HalfExtents={14,14}`, `ResolutionScale=0.50`, `MaxRoughness=0.45`)
  - `UltraDDGI` only: `1` `DDGIProbeGridComponent` (`8x4x8 = 256` probes, `96` rays/probe, `MaxProbesPerFrame=8`)
- **Camera State & Update Behavior:**
  - `PerspectiveCamera(50.0f, aspect, 0.1f, 250.0f)` at `(0.0, 4.5, 13.5)` looking at `(0.0, 1.2, 0.0)`.
  - In **Offscreen Benchmarks** (`14.1..15.3`), the scene camera/transforms are steady after the `120`-frame warm-up: `PlanarCache` (`hits=1120+`), `DynamicProbeCache` (`cached=1`, `2.00 MiB`), and `RHIShadowDepthAtlas` (`NeedsRender() == false` when `m_PendingKey == m_LastKey`) hit their warm steady cache paths while `CascadedShadows`, `SurfaceNormalPrepass`, `GTAO`, `HiZ`, `SSR`, `PlanarResolve`, `FroxelFog`, `SSS`, `OpaqueAndSky`, and `PostProcess` execute every frame (`1,000/1,000` valid GPU timestamp frames per config).
  - In **Windowed Soaks** (`12.3`), `HeroCenterpiece` and `SpotLight_0` animate continuously every frame (`ExtractRenderWorld` + `RenderService::RenderView` + `RHIPresentationContext::RenderFrame` swapchain blit/present), forcing continuous shadow, planar, and dynamic probe invalidation/re-capture across all `1,120` frames (`120` warm-up + `1,000` measured).

---

## 3. Lifecycle & Stability Stress Results (`M15-U10-12.1` → `12.3`)

### 3.1 Lifetime & Presentation Tests (Post-Edit Re-Verification)

*Note on `Tests/rendering/RenderWorkerStress.cpp`:* Cold Debug/Validated Vulkan shader & pipeline compilation across 63 pipelines took `15.1–15.9s`, triggering a pre-existing `15s` `Until()` test timeout at line 55. Raising that test wait limit to `45s` inside `RenderWorkerStress.cpp` (preserving Engine/Editor/Runtime freeze at `4abbc663`) eliminated the flaky timeout across every backend and configuration:

- `AxleRenderLifetimeStressTests`: Debug & Release **PASS** across `{dx12, vulkan}` x `{native, submit, lost, worker, exception, sync}` (post-edit runs logged in `build/agent-runs/u10d2/logs/`).
- `AxleRHIPresentationTests {dx12, vulkan} stress`: Debug & Release **PASS** (`U10-D2` & `U10-D1` verified).
- `python scripts/check_architecture.py`: **PASS** (`Files scanned: 629; CMake files: 25`).
- `ctest --test-dir build -C Release --output-on-failure`: **107/107 PASS** (`254.74s`, post-edit).

### 3.2 `M15-U10-12.1` & `12.2` Validated Lifecycle Suite (`--mode=lifecycle --validate`)

All 4 runs (`Debug DX12`, `Debug Vulkan`, `Release DX12`, `Release Vulkan`) **PASSED** with `GetRHIValidationErrorCount() == 0` and finite non-black pixel readback verification:
- **`12.1` Resize / Minimize / Restore:** 4 full cycles across `1280x720 -> 1920x1080 (vsync) -> 2560x1440 (vsync) -> 320x240 -> 1281x721 -> 0x0 (3 minimized Skipped frames) -> 1920x1080 (restored)` through `RenderService` + `RHIPresentationContext`. Resident VRAM stayed flat between Cycle 1 and Cycle 4 (`DX12: 788.875 -> 788.938 MiB`; `Vulkan: 1108.95 -> 1111.30 MiB` Release / `1106.02 -> 1108.37 MiB` Debug).
- **`12.2` View Switch, Camera Cut, Preset Transitions & Epoch-2 Device Reinit:** 15 interleaved `Main (1280x720) / AssetPreview (640x480) / Thumbnail (256x256)` switches confirmed isolated `RHIViewTargets` generations without thrashing; `CameraCut=true` teleports reset temporal history cleanly; rapid `Low -> Medium -> High -> Ultra -> High -> Low` transitions succeeded on the active native backend (noting `Low` correctly surfaces a `CollectMaterialFeatureDiagnostics` downgrade diagnostic for SSS materials while executing without RHI fallback); full `Shutdown() -> Init()` bumped `Epoch 1 -> 2`, rejected Epoch-1 completion tokens, and resumed clean swapchain presentation.

### 3.3 `M15-U10-12.3` Continuous 1,000-Frame Windowed Soak (`120` warm-up + `1,000` measured frames, `High` Preset)

| Run | Mode | Resolution | Warmup + Measured | Presents | Validation Errors | `TotalFrameWall` p50 / p95 / p99 / max (ms) | `RenderService` View p50 / p95 / p99 / max (ms) | VRAM Warm / Mid / End / Peak (MiB) |
|---|---|---|---:|---:|---:|---:|---:|---|
| `soak_dx12` | Release Unvalidated | `1920x1080` | `120 + 1000` | `1120` | `0` | `161.392 / 191.704 / 216.194 / 292.057` | `142.944 / 164.862 / 182.636 / 215.052` | `787.73 / 787.73 / 787.73 / 787.73` |
| `soak_dx12` | Release Unvalidated | `2560x1440` | `120 + 1000` | `1120` | `0` | `148.487 / 202.078 / 214.602 / 307.579` | `126.116 / 145.796 / 153.917 / 232.796` | `1058.85 / 1058.85 / 1058.85 / 1058.85` |
| `soak_val_dx12` | Release `--validate` | `1920x1080` | `120 + 1000` | `1120` | `0` | `199.323 / 262.246 / 285.984 / 402.492` | `166.299 / 210.995 / 259.571 / 390.272` | `788.74 / 788.74 / 788.74 / 788.74` |
| `soak_vulkan` | Release Unvalidated | `1920x1080` | `120 + 1000` | `1120` | `0` | `225.221 / 266.032 / 327.468 / 433.557` | `179.998 / 197.670 / 214.308 / 329.861` | `1106.79 / 1105.66 / 1105.66 / 1106.79` |
| `soak_vulkan` | Release Unvalidated | `2560x1440` | `120 + 1000` | `1120` | `0` | `208.769 / 278.558 / 318.802 / 504.426` | `178.781 / 200.066 / 235.169 / 413.046` | `1416.85 / 1416.85 / 1416.85 / 1417.98` |
| `soak_val_vulkan` | Release `--validate` | `1920x1080` | `120 + 1000` | `1120` | `0` | `325.014 / 374.040 / 400.135 / 548.528` | `292.498 / 334.034 / 358.872 / 492.358` | `1115.02 / 1113.90 / 1113.90 / 1115.02` |

- **Memory Churn Verdict (`12.3` + `14.2`):** Over 1,120 continuous animated windowed frames per backend/resolution, resident VRAM showed **0.00 MiB monotonic growth** (`0.00 MiB` net delta on D3D12 `1080p`/`1440p` and Vulkan `1440p`; `-1.13 MiB` settle on Vulkan `1080p`), zero descriptor/fence/swapchain leaks, and `0` validation errors.

---

## 4. Release Timings & Memory vs Budgets (`M15-U10-14.1`, `14.2`, `15.1` → `15.3`)

### 4.1 Scope Separation (`M15-U10-15.3`)

Measurements strictly separate three distinct layers:
1. **Total Frame Time (`Windowed Soak` in Section 3.3):** Full CPU `ExtractRenderWorld` + `RenderService::RenderView` (including continuous animated planar & dynamic probe recapture) + `RHIPresentationContext::RenderFrame` swapchain blit/present (`148.5–225.2 ms` p50 unvalidated; exposes high CPU snapshot/sync overhead targeted in `U11`).
2. **Whole-View GPU Time (`RHISceneRenderer` Offscreen Steady State):** Pure GPU timeline duration across all passes in `GPUTimingFrame` (`5.630–10.897 ms` p50 across `Low..Ultra`; `18.647–20.497 ms` on `UltraDDGI`).
3. **Named Lighting Subset GPU (`3.5–5.0 ms` Reference Budget Scope):** Sum per frame of `{Shadows (CascadedShadows), AOAndNormals (SurfaceNormalPrepass, GTAOCompute, GTAOUpsample), Reflections (HiZ0..N, SSRCopy/Color/Compute/Apply, PlanarResolve/Release), FroxelFog (FogCopy/Inject/Reproject/Integrate/Apply), SSS (SSSCopy/BlurH/BlurV), GI (DDGICapture/DDGIUpdate)}` — **excluding** `OpaqueAndSky` geometry/lighting pixel shading and `PostProcessPass` (`AutoExposure, Bloom, TAA, ToneMapping, FXAA`).
   - Note on percentile math: per-frame `p50(sum)` can differ slightly from `sum(pass p50)` because each pass median is sampled independently across the 1,000 measured frames.

### 4.2 Full Matrix — Cold Compile vs Warm Steady-State (`120` warm-up + `1,000` measured frames, `1000/1000` valid GPU timestamps)

#### Direct3D 12 (`NVIDIA GeForce RTX 3070`)

| Preset | Res | ColdInit (ms) | Frame0 CPU/GPU (ms) | CPU p50 / p95 / max (ms) | Whole-View GPU p50 / p95 / p99 / max (ms) | Lighting Subset p50 / p95 / max (ms) | Geom p50 (ms) | Post p50 (ms) | Steady VRAM (MiB) | ViewTargets Resident / Budget (MiB) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `Low` | `1920x1080` | `12429.5` | `1199.04 / 35.88` | `15.870 / 19.697 / 27.916` | `5.630 / 6.986 / 7.476 / 10.062` | **1.065** / `2.375 / 3.688` | `4.324` | `0.090` | `532.82` | `15 tex, 126.12 / 142.12` |
| `Medium` | `1920x1080` | `12429.5` | `132.19 / 8.46` | `16.969 / 19.360 / 43.242` | `6.421 / 7.811 / 8.343 / 8.771` | **1.911** / `2.913 / 4.524` | `3.983` | `0.378` | `673.73` | `43 tex, 252.10 / 284.10` |
| `High` | `1920x1080` | `12429.5` | `147.23 / 10.45` | `19.104 / 20.673 / 35.502` | `7.515 / 9.804 / 10.096 / 11.180` | **2.545** / `3.165 / 4.324` | `4.154` | `0.856` | `770.62` | `50 tex, 322.10 / 386.10` |
| `Ultra` | `1920x1080` | `12429.5` | `128.41 / 13.34` | `16.644 / 19.440 / 37.321` | `10.897 / 11.343 / 12.167 / 12.450` | **4.584** / `4.693 / 5.691` | `5.072` | `1.228` | `860.62` | `50 tex, 396.31 / 524.31` |
| `UltraDDGI` | `1920x1080` | `12429.5` | `182.96 / 19.48` | `63.043 / 76.104 / 109.500` | `18.647 / 25.652 / 49.858 / 109.869` | **12.828** / `19.063 / 88.432` | `4.703` | `0.934` | `862.68` | `52 tex, 396.93 / 524.93` |
| `Low` | `2560x1440` | `12429.5` | `47.45 / 7.33` | `14.748 / 17.632 / 39.989` | `6.487 / 7.840 / 8.308 / 10.017` | **1.253** / `2.023 / 3.599` | `5.010` | `0.155` | `994.55` | `15 tex, 221.48 / 237.48` |
| `Medium` | `2560x1440` | `12429.5` | `96.62 / 12.66` | `14.283 / 16.388 / 28.763` | `9.277 / 9.773 / 10.649 / 12.201` | **2.910** / `3.047 / 3.845` | `5.514` | `0.849` | `1035.99` | `44 tex, 437.68 / 469.68` |
| `High` | `2560x1440` | `12429.5` | `130.86 / 13.01` | `18.655 / 21.717 / 37.750` | `10.777 / 11.551 / 12.688 / 14.596` | **3.368** / `3.885 / 4.962` | `5.603` | `1.808` | `1030.04` | `51 tex, 550.74 / 614.74` |
| `Ultra` | `2560x1440` | `12429.5` | `147.21 / 10.57` | `19.853 / 24.322 / 41.386` | `9.004 / 10.122 / 10.562 / 11.517` | **3.511** / `4.715 / 5.834` | `4.154` | `1.106` | `1127.10` | `51 tex, 630.72 / 758.72` |
| `UltraDDGI` | `2560x1440` | `12429.5` | `205.96 / 20.37` | `60.804 / 65.189 / 87.017` | `20.497 / 27.165 / 51.167 / 88.371` | **12.957** / `16.720 / 64.673` | `5.595` | `1.551` | `1127.16` | `53 tex, 631.35 / 759.35` |

#### Vulkan 1.3 (`NVIDIA GeForce RTX 3070`)

| Preset | Res | ColdInit (ms) | Frame0 CPU/GPU (ms) | CPU p50 / p95 / max (ms) | Whole-View GPU p50 / p95 / p99 / max (ms) | Lighting Subset p50 / p95 / max (ms) | Geom p50 (ms) | Post p50 (ms) | Steady VRAM (MiB) | ViewTargets Resident / Budget (MiB) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `Low` | `1920x1080` | `21058.3` | `1068.11 / 63.81` | `19.532 / 21.440 / 24.983` | `7.596 / 9.734 / 10.581 / 11.395` | **1.080** / `1.817 / 3.247` | `6.016` | `0.158` | `790.25` | `15 tex, 126.12 / 142.12` |
| `Medium` | `1920x1080` | `21058.3` | `2181.67 / 33.34` | `21.797 / 25.282 / 57.489` | `7.613 / 10.100 / 11.426 / 14.210` | **1.846** / `2.913 / 4.285` | `4.905` | `0.512` | `927.18` | `43 tex, 252.10 / 284.10` |
| `High` | `1920x1080` | `21058.3` | `179.66 / 8.14` | `23.913 / 27.030 / 38.776` | `8.474 / 10.501 / 11.350 / 12.624` | **2.208** / `3.152 / 4.495` | `5.038` | `0.981` | `1030.43` | `50 tex, 322.10 / 386.10` |
| `Ultra` | `1920x1080` | `21058.3` | `194.47 / 6.25` | `24.565 / 26.601 / 43.527` | `8.399 / 10.204 / 10.829 / 13.756` | **3.123** / `4.014 / 5.380` | `4.285` | `0.825` | `1102.81` | `50 tex, 396.31 / 524.31` |
| `UltraDDGI` | `1920x1080` | `21058.3` | `248.67 / 14.61` | `68.568 / 73.337 / 84.899` | `19.157 / 23.973 / 26.838 / 56.471` | **11.718** / `15.019 / 29.171` | `5.909` | `1.079` | `1104.00` | `52 tex, 396.93 / 524.93` |
| `Low` | `2560x1440` | `21058.3` | `68.96 / 9.11` | `20.744 / 22.882 / 29.318` | `8.129 / 10.061 / 10.604 / 13.176` | **0.997** / `1.940 / 3.696` | `6.569` | `0.201` | `1239.18` | `15 tex, 221.48 / 237.48` |
| `Medium` | `2560x1440` | `21058.3` | `172.45 / 10.97` | `23.256 / 25.272 / 30.457` | `8.868 / 9.940 / 10.608 / 11.072` | **2.152** / `3.132 / 3.900` | `5.766` | `0.650` | `1289.93` | `44 tex, 437.68 / 469.68` |
| `High` | `2560x1440` | `21058.3` | `196.25 / 11.09` | `25.054 / 27.064 / 34.988` | `9.633 / 10.733 / 11.247 / 12.019` | **2.106** / `3.357 / 4.093` | `5.538` | `1.260` | `1289.18` | `51 tex, 550.74 / 614.74` |
| `Ultra` | `2560x1440` | `21058.3` | `193.53 / 10.16` | `25.056 / 27.716 / 75.808` | `9.796 / 10.986 / 11.548 / 12.963` | **3.087** / `4.146 / 4.742` | `5.077` | `1.174` | `1369.00` | `51 tex, 630.72 / 758.72` |
| `UltraDDGI` | `2560x1440` | `21058.3` | `227.14 / 15.73` | `67.638 / 70.662 / 75.411` | `20.203 / 26.388 / 28.734 / 30.114` | **10.811** / `14.872 / 17.120` | `7.392` | `1.805` | `1369.00` | `53 tex, 631.35 / 759.35` |

### 4.3 Named Lighting Feature Breakdown (`M15-U10-15.1` → `15.3`)

#### `1920x1080` (`p50` ms)

| Backend | Config | Shadows | Normals+GTAO | HiZ+SSR+Planar | FroxelFog | SSS | DDGI | Lighting Subset (`p50`) | Whole-View GPU (`p50`) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct3D 12 | `High` | `0.561` | `0.241` | `0.523` | `0.667` | `0.154` | `0.000` | **2.545** | `7.515` |
| Direct3D 12 | `Ultra` | `0.640` | `0.357` | `0.787` | `2.567` | `0.235` | `0.000` | **4.584** | `10.897` |
| Direct3D 12 | `UltraDDGI` | `0.578` | `0.273` | `0.593` | `1.971` | `0.175` | `8.092` | **12.828** | `18.647` |
| Vulkan 1.3 | `High` | `0.255` | `0.234` | `0.623` | `0.784` | `0.178` | `0.000` | **2.208** | `8.474` |
| Vulkan 1.3 | `Ultra` | `0.215` | `0.205` | `0.519` | `1.671` | `0.146` | `0.000` | **3.123** | `8.399` |
| Vulkan 1.3 | `UltraDDGI` | `0.287` | `0.277` | `0.681` | `2.341` | `0.197` | `7.047` | **11.718** | `19.157` |

#### `2560x1440` (`p50` ms)

| Backend | Config | Shadows | Normals+GTAO | HiZ+SSR+Planar | FroxelFog | SSS | DDGI | Lighting Subset (`p50`) | Whole-View GPU (`p50`) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Direct3D 12 | `High` | `0.592` | `0.371` | `1.057` | `0.968` | `0.345` | `0.000` | **3.368** | `10.777` |
| Direct3D 12 | `Ultra` | `0.534` | `0.256` | `0.655` | `1.442` | `0.209` | `0.000` | **3.511** | `9.004` |
| Direct3D 12 | `UltraDDGI` | `0.574` | `0.334` | `0.896` | `2.185` | `0.286` | `7.557` | **12.957** | `20.497` |
| Vulkan 1.3 | `High` | `0.203` | `0.237` | `0.675` | `0.681` | `0.233` | `0.000` | **2.106** | `9.633` |
| Vulkan 1.3 | `Ultra` | `0.187` | `0.228` | `0.634` | `1.482` | `0.218` | `0.000` | **3.087** | `9.796` |
| Vulkan 1.3 | `UltraDDGI` | `0.249` | `0.301` | `0.851` | `2.337` | `0.294` | `6.147` | **10.811** | `20.203` |

### 4.4 Key Architectural Observations (Honest Baseline Findings for `U11` & Vulkan/D3D12)

1. **`3.5–5.0 ms` Lighting Subset Budget (`15.3`):** On **`NVIDIA GeForce RTX 3070`** with declared raster presets (`High` & `Ultra` without optional DDGI), the named lighting subset sits squarely inside/below target: **`2.21–2.55 ms` (`High` 1080p), `2.11–3.37 ms` (`High` 1440p), and `3.09–4.58 ms` (`Ultra`)**.
2. **`UltraDDGI` Budget Excess:** Enabling optional software/raster `DDGI` (`8x4x8`, `8` probes/frame) adds **`6.15–8.09 ms` GPU** (`DDGICapture` `5.88–7.86 ms` + `DDGIUpdate` `0.22–0.30 ms`) and pushes `CPU Record/Submit` from `~19 ms` to **`60.8–68.6 ms`** (`2,534` draws/frame). Correctly gated behind experimental opt-in until hardware/compute optimization (`U11`/`M16`).
3. **Cold Pipeline Compilation vs Warm Execution:** Cold `RHISceneRenderer::Init()` compiles 63 base pipelines in `12.43s` (D3D12) and `21.06s` (Vulkan); Frame 0 first-use on new preset permutations takes `47–2,182 ms` CPU before converging to steady-state `14–25 ms`.
4. **CPU Bottleneck Exposed by Soak vs Offscreen (`U11` Target):** While GPU `Whole-View` takes `7.5–10.8 ms` (`High`), single-frame CPU record/submit sits at `15–25 ms`, and continuous animated `RenderService` + `ExtractRenderWorld` + per-frame dynamic probe/planar invalidation reaches **`148–225 ms`** p50 total frame wall time — directly motivating `U11`'s `RenderMaterialData` revision caching, snapshot reuse, and descriptor churn reduction.
5. **GPU Boost / D3D12 vs Vulkan Variance:** Under steady-state `High` @ `1080p`, Vulkan 1.3 is slightly slower in `OpaqueAndSky` (`4.834 ms` vs `3.952 ms`) and faster in `CascadedShadows` (`0.255 ms` vs `0.561 ms`). Notice `D3D12 Ultra @ 1440p` clocks higher sustained GPU boost (`OpaqueAndSky` `3.961 ms` vs `5.383 ms` on `High`) than lighter passes.
6. **Memory / Target Budget Compliance (`14.2`):** Across every preset (`Low..UltraDDGI`, `1080p..1440p`), `RHIViewTargets` resident bytes strictly remain below `ComputeRenderQualityBudgetReport::TotalEstimatedBudgetBytes` (`126.12 / 142.12 MiB` Low 1080p through `631.35 / 759.35 MiB` UltraDDGI 1440p; headroom corresponds directly to `ReflectionCacheBudgetBytes` vs active `2.00 MiB` dynamic probe).

---

## 5. `EDITOR VISUAL` Corroboration (`M15-U10-15.1`)

Verified via `AxleEditor.exe --rhi={dx12,vulkan} --no-fallback --rhi-validation` on `build/agent-runs/u10d2/workload/U10D2.axleproj` (`U10D2_Workload.axlescene`, camera `(0.0, 4.5, 13.5)`, `telemetry 1`):
- `EditorVisual_Dbg_dx12` (`exit=0`, `61.8s`): `VISUAL_CONSOLE infos=176 warnings=0 errors=0 okViews=47 failedViews=0`; viewport captures `high_viewport.png` (`1472x891`, `nonFinite=0`, `nonBlack=1311552`, `meanLuma=0.588877`) and `ultra_viewport.png` (`meanLuma=0.588744`) + `high_editor.png` (`2560x1369`) visually inspected.
- `EditorVisual_Rel_vulkan` (`exit=0`, `36.0s`): `VISUAL_CONSOLE infos=176 warnings=10 errors=0 okViews=47 failedViews=0` (`10` startup `vkCreateGraphicsPipelines` unconsumed vertex attribute warnings only); `high_viewport.png` (`nonFinite=0`, `meanLuma=0.58875`) and `ultra_viewport.png` (`meanLuma=0.588744`) + `high_editor.png` visually inspected.
- **DX12 vs Vulkan Viewport Parity (`scripts/compare_images.py` tolerances: `PSNR >= 40 dB`, `SSIM >= 0.985`, `maxChErr <= 4/255 = 0.015686`):**
  - `High` (`1472x891`): `PSNR = 61.10 dB`, `SSIM = 0.999991`, `max_channel_error = 0.014855` -> **PASS**
  - `Ultra` (`1472x891`): `PSNR = 66.74 dB`, `SSIM = 0.999998`, `max_channel_error = 0.013895` -> **PASS**
- **Visual Inspection Summary:** Planar floor reflections accurately mirror spheres, pillars, and bloom beacons; metallic PBR sweep spheres resolve SSR + dynamic probe cubemap reflections; emissive beacons emit volumetric froxel halos; front SSS spheres display warm subsurface transmission; Editor Performance & Debug telemetry HUD displays active camera pose and `Error 0`.

---

## 6. Open Hand-Off to `U10-E` (`M15-U10-17.1..19.2`)

Per the `M15-U10` execution plan and `CODEX_GEMINI_EXCHANGE.json`, `U10-D2` concludes Gemini's pre-acceptance verification scope. All `M15-U10-17.1..19.2` checkboxes remain strictly `[ ]` for Codex's independent consolidated architecture/feature acceptance audit and promoted defaults manifest.
