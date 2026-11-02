# Automotive material reference scene

Open `Editor/assets/scenes/MaterialReference.axlescene`. The primary camera is
at (0, 0, 10), facing -Z. The top row contains car paint, brushed metal and carbon
fiber; the bottom row contains rubber, leather and glass. A colored checkerboard
behind the samples makes transmitted distortion visible. Lighting is fixed.

Materials and small deterministic textures live in `materials/reference/`.
`python scripts/generate_material_references.py` regenerates only these examples
and the reference scene. It does not change the project's existing materials.
The textures are diagnostic patterns rather than production vehicle textures.

- Car paint: texture-driven coat strength/glossiness.
- Brushed metal: tangent anisotropy with an RG flow direction.
- Carbon fiber: albedo weave, coat, anisotropy and detail normal together.
- Rubber: strong detail normals with high independent UV repetition.
- Leather: detail normal masked by the R channel.
- Glass: IOR 1.5, transmission tint and authored thickness over opaque geometry.

Step 106 loads, saves and reopens this scene, compares the rendered pixels,
renders HDR, and compares asset-backed and inline copies of each material under
the same camera/light settings. This verifies the shared material path; the
editor preview intentionally uses its own studio lights and camera framing.

To export reference images and end-to-end timings in PowerShell:

```powershell
$env:AXLE_MATERIAL_TEST_BACKEND = 'dx12'
$env:AXLE_MATERIAL_CAPTURE_DIR = "$PWD/build/material-captures/dx12"
./build/Editor/Release/AxleTests.exe --step106
$env:AXLE_MATERIAL_TEST_BACKEND = 'vulkan'
$env:AXLE_MATERIAL_CAPTURE_DIR = "$PWD/build/material-captures/vulkan"
./build/Editor/Release/AxleTests.exe --step106
Remove-Item Env:AXLE_MATERIAL_TEST_BACKEND
Remove-Item Env:AXLE_MATERIAL_CAPTURE_DIR
```

### Environment Variables & Options

The acceptance test harness in `Editor/src/Tests/Test_MaterialAcceptance.cpp` implements and enforces these configuration options:

- `AXLE_MATERIAL_TEST_BACKEND`:
  - Accepted values: `dx12` (or `d3d12`), `vulkan` (or `vk`).
  - Unset defaults to Direct3D 12 for backwards compatibility.
  - Specifying an unknown or unsupported backend strictly fails the test (no silent fallback).
- `AXLE_MATERIAL_CAPTURE_DIR`:
  - Explicit output path for captured offscreen framebuffers.
  - Automatically created if it does not already exist.
  - Must not be inside the project source tree or assets directory (`Editor/assets`, `Engine/Resources`). Capturing into protected asset trees fails immediately with a validation error.

Images use portable PPM (P6 binary) format. HDR inspection images are tone mapped
by the renderer and decoded from eight-byte RGBA16F pixels before conversion to
8-bit PPM; they do not preserve the original HDR radiance. Readback rows are
aligned to 256 bytes. Step 106 compares original, reopened and detached inline
materials using identical camera/light settings.

Each timing configuration uses three warm-up frames followed by 24 measured
frames at 1024x640, 1x MSAA: baseline (detail/refraction disabled), detail normal,
and detail normal with refraction. Results include CPU recording, GPU submission
and queue idle. They are not isolated shader timings and have no pass/fail
performance threshold. Run one `--stepNNN` option per AxleTests process.

See the [2026-11-02 acceptance report](MATERIAL_PHASE_38_3_ACCEPTANCE_REPORT.md)
for verified DX12/Vulkan results, measurements and remaining interactive checks.

### Acceptance Report Template

When executing physical acceptance runs on target hardware, document the results using the following template:

```text
Revision:
Platform / toolchain:
Unit-test command and exit code:
Editor / integration build: RUN or NOT RUN
DXC DXIL / SPIR-V compilation: RUN or NOT RUN
DX12 GPU acceptance: RUN or NOT RUN
Vulkan GPU acceptance: RUN or NOT RUN
UI acceptance: RUN or NOT RUN
Capture paths:
Performance method and measurements, if actually run:
Known failures / unsupported cases:
```

