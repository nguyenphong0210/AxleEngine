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

## U2-05.2 - Vulkan 1.3

The Debug and Release builds above also produced the Vulkan-capable fixture
(both exit 0).

| Configuration | Direct command | Exit | Log | Artifact directory |
|---|---|---:|---|---|
| Debug | `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/M15-U2-05/debug-vulkan` | 0 | `build/agent-runs/M15-U2-05/debug-vulkan.log` | `build/agent-runs/M15-U2-05/debug-vulkan/` |
| Release | `build/Tests/Release/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/M15-U2-05/release-vulkan` | 0 | `build/agent-runs/M15-U2-05/release-vulkan.log` | `build/agent-runs/M15-U2-05/release-vulkan/` |

Both logs end in the native `PASS HDR subresources...` result quoted above.
The four PPM outputs in each directory are valid 128x128 P6 images with distinct
scene hashes. Each corresponding image has the same SHA-256 across Vulkan
Debug/Release and D3D12 Debug/Release. Vulkan `red.identity.json` manifests
pass `python scripts/validate_artifact_identity.py` (exit 0); validation
reports are `red.identity.report.json` in each Vulkan artifact directory.

## U2-05.3 - OpenGL subset

The same Debug and Release builds also produced the OpenGL fixture (both exit
0). This branch verifies the OpenGL background and explicit IBL consumer using
`glReadPixels`; it does not claim the native HDR cubemap bake/subresource tests.

| Configuration | Direct command | Exit | Log | Artifact directory |
|---|---|---:|---|---|
| Debug | `build/Tests/Debug/AxleEnvironmentRenderingTests.exe gl build/agent-runs/M15-U2-05/debug-gl` | 0 | `build/agent-runs/M15-U2-05/debug-gl.log` | `build/agent-runs/M15-U2-05/debug-gl/` |
| Release | `build/Tests/Release/AxleEnvironmentRenderingTests.exe gl build/agent-runs/M15-U2-05/release-gl` | 0 | `build/agent-runs/M15-U2-05/release-gl.log` | `build/agent-runs/M15-U2-05/release-gl/` |

Both logs end in `PASS OpenGL background-only and explicit IBL pixels, snapshot
and ECS bridge gates`. `gl-background.ppm` and `gl-environment.ppm` are valid
128x128 P6 images with 49,152 RGB bytes and distinct hashes; corresponding
Debug/Release hashes match. The `gl-environment.identity.json` manifests pass
`python scripts/validate_artifact_identity.py` (exit 0), producing
`gl-environment.identity.report.json` in each output directory. The fixture
also checks `glGetError() == GL_NO_ERROR`. A shader warning says the unused
`u_SkyGroundColor` uniform was stripped; it did not fail the readback gate.

## Acceptance

**U2-05.1, U2-05.2 and U2-05.3 PASS.** Six direct runs on the current source
tree exited 0. D3D12/Vulkan validated native cubemap subresources and rendered
environment pixels; OpenGL validated the declared background/IBL subset.
Artifact manifests bind each captured image to its backend, configuration,
fixture, resolution, source baseline and SHA-256. This closes the U2-05
readback checkpoints without claiming U5 dynamic capture or probe blending.
