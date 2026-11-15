# M15 U2-05 - Environment backend readback

## Scope and fixture

This report verifies the existing `AxleEnvironmentRenderingTests` fixture on
each backend, with new output directories under `build/agent-runs/M15-U2-05`.
The source tree baseline is `22f41ad8`; this checkpoint changes no rendering
source. The fixture reads native HDR cubemap faces/mips and the rendered color
target on D3D12/Vulkan. It checks irradiance, prefilter, BRDF finiteness,
sky/metallic pixels, roughness response, environment rotation, missing/corrupt
source fallback and retry after injected upload failure. The OpenGL branch uses
`glReadPixels` and checks the background-only versus explicit-IBL pixels and
the ECS bridge toggle. Readback artifacts are emitted as 128x128 P6 PPM files.

The fixture has a required output-directory argument. Build and direct-run
commands, exit codes and artifact paths are recorded per backend below.

## U2-05.1 - D3D12

Builds (both exit 0):

```text
cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 4
cmake --build build --config Release --target AxleEnvironmentRenderingTests --parallel 4
```

Build logs: `build/agent-runs/M15-U2-05/build-debug.log` and
`build/agent-runs/M15-U2-05/build-release.log`.

| Configuration | Direct command | Exit | Log | Artifact directory |
|---|---|---:|---|---|
| Debug | `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/M15-U2-05/debug-dx12` | 0 | `build/agent-runs/M15-U2-05/debug-dx12.log` | `build/agent-runs/M15-U2-05/debug-dx12/` |
| Release | `build/Tests/Release/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/M15-U2-05/release-dx12` | 0 | `build/agent-runs/M15-U2-05/release-dx12.log` | `build/agent-runs/M15-U2-05/release-dx12/` |

Both logs end in `PASS HDR native subresources, sky/specular/diffuse, cache,
exposure, reload, fallback and upload retry`. Each output directory contains
`red.ppm`, `blue.ppm`, `sharp.ppm` and `rough.ppm`, all valid 128x128 P6 images
with 49,152 RGB bytes. The four files have distinct SHA-256 values, and each
file has the same hash across Debug and Release. `red.ppm` is bound to the
baseline/configuration/backend in `red.identity.json`; both manifests pass
`python scripts/validate_artifact_identity.py` (exit 0), with validation output
in `red.identity.report.json`.

The images and manifests are generated artifacts under `build` and are not
committed. No Editor screenshot is required for this readback checkpoint.
