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
$env:AXLE_MATERIAL_CAPTURE_DIR = "$PWD/build/material-captures/dx12"
./build/Editor/Release/AxleTests.exe --step106
$env:AXLE_MATERIAL_TEST_BACKEND = 'vulkan'
$env:AXLE_MATERIAL_CAPTURE_DIR = "$PWD/build/material-captures/vulkan"
./build/Editor/Release/AxleTests.exe --step106
Remove-Item Env:AXLE_MATERIAL_TEST_BACKEND
Remove-Item Env:AXLE_MATERIAL_CAPTURE_DIR
```

Images use portable PPM format. Timings report 24 warmed-up frames at 1024x640,
including CPU recording, GPU submission and queue idle. They are not isolated
shader timings and have no pass/fail performance threshold.
