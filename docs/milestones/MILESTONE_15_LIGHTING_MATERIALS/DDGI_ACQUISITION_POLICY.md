# M15 Unit 7 — Experimental DDGI Radiance Acquisition Policy & Budgets

**Scope:** `M15-U7-01.1`, `M15-U7-01.2`, `M15-U7-02`, `M15-U7-07.3` (Experimental / Optional GI, `ARCHITECTURE.md` Sections 8, 12, 13; `REMEDIATION_PLAN.md` P11).

## 1. Real Scene-Radiance Acquisition Decision (`M15-U7-01.1`)

- **Chosen production GPU path:** **Amortized Multi-Face Tile-Atlas Raster Probe Capture (`DDGICapture`)** coupled with **Compute Octahedral Integration & Visibility Moment Filtering (`DDGIUpdate`)** on native D3D12 / Vulkan (`SM 6.0`).
- **Rejected non-compliant path:** Reusing the main camera's screen-space `OpaqueColor`/`OpaqueDepth` buffer (`u_Scene` copy in legacy `DDGI.hlsl`) is explicitly retired. Screen pixels represent camera-facing surfaces and cannot measure world-space enclosed bounce or behind-wall occlusion independently of camera movement.
- **Why raster probe captures fit M15:** M15 explicitly excludes hardware ray-tracing pipelines and BLAS/TLAS acceleration ownership (reserved for **M16-RT**). Amortized 6-face 90-degree probe captures execute directly against the immutable `RenderWorld` draw lists, Material Graphs, Forward+ lights, CSM/Local shadow atlases, Skybox, and prior-frame DDGI irradiance (multi-bounce feedback) with exact depth hit distances from `D32_FLOAT` inside a single `RenderGraph`-managed frame.

## 2. World-Space Probe Lattice & Octahedral Atlas Packing (`M15-U7-02`)

- **Probe Indexing:** Linear index `p = (iy * dims.z + iz) * dims.x + ix` maps bijectively to grid coordinate `(ix, iy, iz)` and world center `GridOrigin + vec3(ix, iy, iz) * GridSpacing`.
- **2D Tile Layout:** Probes pack into a near-square 2D tile grid with `atlasCols = ceil(sqrt(probeCount))`, `atlasRows = ceil(probeCount / atlasCols)` (`col = p % atlasCols`, `row = p / atlasCols`). Keeping tile columns bounded prevents huge horizontal textures for wide `32x16x32` grids (`128x128` tiles fits well under `MaxDDGIAtlasDimension = 8192`).
- **Irradiance Atlas (`RGBA16_FLOAT`, 8 B/texel):** `6x6` octahedral interior + `1 px` border gutter = **`8x8` texels/probe** (`512 B/probe`). Stores cosine-weighted diffuse irradiance (`.rgb`) and accumulated valid hit weight (`.a`).
- **Distance & Visibility Moments Atlas (`RGBA16_FLOAT`, 8 B/texel):** `14x14` octahedral interior + `1 px` border gutter = **`16x16` texels/probe** (`2048 B/probe`). Stores `(E[r], E[r^2], validWeight, backfaceRatio)` to drive Chebyshev visibility upper bounds and detect probes trapped inside closed geometry.

## 3. Memory & Staggered Update Budgets (`M15-U7-01.2`, `M15-U7-07.3`)

### Reconciled `32x16x32` Reference Grid vs Practical Work Budgets

The reference `32x16x32` lattice (`16,384` probes) requires bounded atlas memory and an explicit capture budget:

| Grid Preset | Probe Count | Atlas Tiles | Irradiance Atlas (`RGBA16F`) | Distance Moments (`RGBA16F`) | Total Logical Atlas VRAM |
|---|---:|---|---:|---:|---:|
| Small Room (`4x2x4`) | 32 | `6 x 6` | `48 x 48` (~0.02 MiB) | `96 x 96` (~0.07 MiB) | **~0.09 MiB** |
| Local Room (`8x4x8`) | 256 | `16 x 16` | `128 x 128` (0.13 MiB) | `256 x 256` (0.50 MiB) | **0.63 MiB** |
| Authored Default (`16x8x16`) | 2,048 | `46 x 45` | `368 x 360` (1.01 MiB) | `736 x 720` (4.03 MiB) | **5.04 MiB** |
| Reference Max (`32x16x32`) | 16,384 | `128 x 128` | `1024 x 1024` (8.00 MiB) | `2048 x 2048` (32.00 MiB) | **40.00 MiB** |

- **Capture Scratch Atlas Budget (renderer-owned shared target):** `16x16` pixels per cube face × 6 faces per active probe (`1,536` angular samples/probe). With `MaxProbesPerFrame = 32` (`192` face viewports in a `24x8` tile layout = `384x128` pixels), `RGBA16F` color + `D32F` depth occupy only **`576 KiB`** logical (`384 KiB` + `192 KiB`).
- **Validation of the `25%` update target:** Updating `25%` of a small/medium grid (e.g., `8` of `32` probes, or `8` staggered probes/frame by default) stays within budget because `48` tiny `16x16` raster tiles render in one shared pass. By contrast, naively rasterizing `25%` of `32x16x32` (`4,096` probes × 6 = `24,576` scene draws/frame) exceeds interactive CPU command-recording and GPU frame budgets. Therefore, `DDGIContract` hard-caps staggered capture work at `MaxProbesPerFrame = 32` (default steady-state `8` probes/frame; round-robin cursor across frames).

## 4. Fallback, Capability & Tier Policy (`M15-U7-07.2`, `08.4`)

- **Opt-in Experimental Status:** `SceneRenderSettings::EnableDDGI` defaults to `false`; `GIService::Prepare` reports `GIFrameStatus::Experimental`.
- **No-Compute / OpenGL / MSAA / Sub-Capture / Invalid Grid / No-Valid-Probe:** Explicitly falls back to IBL/ambient diffuse (`GIFallback::IBL`) with structured diagnostic (`ddgi_disabled_or_no_grid`, `ddgi_compute_unavailable`, `ddgi_invalid_grid`, `ddgi_disabled:compute_or_storage_unavailable`) and zero authored-scene mutation.
- **Inside-Geometry & Bias Controls:** Configurable `ViewBias` (default `0.15`), `NormalBias` (default `0.08`), `Hysteresis` (`0.90`), `UpdateFraction` (`0.25`), and `MaxProbesPerFrame` (`8`). Probes hitting backfaces (`> 25%`) are weighted down so adjacent valid air probes or IBL fallback cleanly suppress light leakage.
