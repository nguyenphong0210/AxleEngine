# M15 Unit 7 — Experimental DDGI Radiance Acquisition Policy & Budgets

**Scope:** `M15-U7-01.1`, `M15-U7-01.2`, `M15-U7-02`, `M15-U7-07.3` (Experimental / Optional GI, `ARCHITECTURE.md` Sections 8, 12, 13; `REMEDIATION_PLAN.md` P11).

## 1. Real Scene-Radiance Acquisition Decision (`M15-U7-01.1`)

- **Chosen production GPU path:** **Amortized Multi-Face Tile-Atlas Raster Probe Capture (`DDGICapture`)** coupled with **Compute Octahedral Integration & Visibility Moment Filtering (`DDGIUpdate`)** on native D3D12 / Vulkan (`SM 6.0`).
- **Rejected non-compliant path:** Reusing the main camera's screen-space `OpaqueColor`/`OpaqueDepth` buffer (`u_Scene` copy in legacy `DDGI.hlsl`) is explicitly retired. Screen pixels represent camera-facing surfaces and cannot measure world-space enclosed bounce or behind-wall occlusion independently of camera movement.
- **Hardware tracing scope.** M16 owns hardware ray tracing and BLAS/TLAS acceleration structures.
- **Raster inputs.** Each probe renders the opaque world draw list in six 90-degree views, including mesh depth, built-in material shading and emission, sky, local lights and shadows, and the previous DDGI atlas for multi-bounce. Canonical MaterialSurface shaders can receive DDGI in the main view. Capture uses the resolved built-in material properties as the proxy for custom shading.
- **Shadow coverage.** Directional shadow detail is limited to the main view's CSM coverage. Probe faces select cascades using that view's depth. Scene hits and their radial distances come from the world-space probe faces. Local shadow lookup uses world positions.

## 2. World-Space Probe Lattice & Octahedral Atlas Packing (`M15-U7-02`)

- **Probe Indexing:** Linear index `p = (iy * dims.z + iz) * dims.x + ix` maps bijectively to grid coordinate `(ix, iy, iz)` and world center `GridOrigin + vec3(ix, iy, iz) * GridSpacing`.
- **2D Tile Layout:** Probes pack into a near-square 2D tile grid with `atlasCols = ceil(sqrt(probeCount))`, `atlasRows = ceil(probeCount / atlasCols)` (`col = p % atlasCols`, `row = p / atlasCols`). Keeping tile columns bounded prevents huge horizontal textures for wide `32x16x32` grids (`128x128` tiles fits well under `MaxDDGIAtlasDimension = 8192`).
- **Irradiance Atlas (`RGBA16_FLOAT`, 8 B/texel):** `6x6` octahedral interior + `1 px` border gutter = **`8x8` texels/probe** (`512 B/probe`). Stores cosine-weighted diffuse irradiance (`.rgb`) and current probe validity (`.a`).
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

- **Experimental opt-in.** A scene with an enabled `DDGIProbeGridComponent` opts in on Runtime and by default in Editor. Editor Render FX can override the viewport and can return to the scene setting. The low-level setting defaults to disabled.
- **Capability and diffuse fallback.** Missing or invalid grids, disabled settings, unsupported compute/storage or OpenGL, MSAA, and sub-captures fall back to environment/ambient diffuse. A view without a valid, active grid allocates no DDGI atlas. A fragment with no valid neighboring probes uses the same fallback directly in the shader.
- **Backface rejection and bias.** The validity weight starts falling above 15% backface hits and reaches zero at 27.5%. The latest backface measurement updates immediately when the probe is recaptured. Backfaces contribute zero radiance with their full angular weight. `ViewBias` defaults to `0.15` and `NormalBias` to `0.08`.
- **Temporal policy.** Scene identity and revision, target/device generations, lattice and capture policy changes, and replaced mesh/material generations invalidate history. Camera motion, light animation and moving geometry continue the staggered cursor. `Hysteresis` controls the next radiance and distance-moment blend without reseeding. `EnergyMultiplier` scales the receiver contribution once. The capture feedback uses unit gain.
- **Raster sample count.** Each capture uses the fixed 16x16 face size. Existing serialized `RaysPerProbe` data does not change the raster sample count.
