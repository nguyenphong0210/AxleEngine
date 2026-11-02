# Material Phase 38.3 acceptance report

Date: 2026-11-02. Platform: Windows, MSVC 2022, CMake Release, DXC.
GPU: NVIDIA GeForce RTX 3070. Backend requests are strict: no silent fallback.
Verified code revision: `ce71ca0` (fixes starting at `8373b2e`).

The scoped implementation and core automated rendering checks now pass.
The full interactive editor checklist is still pending;
the phase remains open in ROADMAP.md for that reason. This is not full Godot
material or texture-importer feature parity.

## Validation

Run from the repository root. AxleTests selects one step per invocation.

```powershell
cmake --build build --config Release --target AxleTests AxleMaterialUnitTests --parallel 12
ctest --test-dir build -C Release -R '^MaterialUnitTests$' --output-on-failure
foreach ($step in @(101,104,105,106)) {
    ./build/Editor/Release/AxleTests.exe "--step$step"
    if ($LASTEXITCODE -ne 0) { throw "Material step $step failed" }
}
$env:AXLE_MATERIAL_TEST_BACKEND = 'vulkan'
./build/Editor/Release/AxleTests.exe --step106
Remove-Item Env:AXLE_MATERIAL_TEST_BACKEND
```

| Check | Result |
| --- | --- |
| Editor, runtime, integration executable and standalone CPU target build | PASS |
| MaterialUnitTests: five CPU suites, no device/window initialization | PASS |
| Steps 101, 104, 105, each in its own process | PASS |
| Actual ForwardPBR VS/PS compilation to DXIL and SPIR-V | PASS |
| Step 106, requested DX12 and requested Vulkan | PASS |
| Custom Tint/uint Enabled, two distinct per-draw values, texture replacement/clearing | PASS |
| Legacy shader, failed reload retention, deleted main fallback and recovery | PASS |
| Buffer growth beyond 256 bytes, macro include edit/delete/restore | PASS |
| Sky pass between built-in opaque and custom transparent draws | PASS |
| Shared asset schema/defaults/diagnostics refreshed without a new shader generation | PASS |
| SDR/HDR reference readback, save/reopen and asset/inline pixel equivalence | PASS |
| Unknown explicit backend | Correctly fails |

Expected compiler errors in reload/error tests are deliberate fixtures. Existing
LNK4098 default-library conflicts remain in Release linking; they are not new
material compiler failures. No new compiler warning was observed in these fixes.

## Captures and timings

See [reference scene instructions](MATERIAL_REFERENCE_SCENE.md) for capture commands.
Outputs are ignored build artifacts, not project assets:

- `build/material-captures/dx12/capture_reference_{sdr,hdr}_direct3d12_1024x640.ppm`
- `build/material-captures/vulkan/capture_reference_{sdr,hdr}_vulkan_1024x640.ppm`
- `timings_direct3d12.txt` / `timings_vulkan.txt` in those directories.

The SDR capture was visually inspected: all six samples and the checkerboard
are visible, including detail patterns and transparent glass. HDR captures are
tone mapped through the renderer, decoded from RGBA16F and exported as 8-bit PPM;
they are inspection images, not lossless HDR exports.

Each configuration uses three warm-up frames, then 24 measured frames at
1024x640, 1x MSAA, fixed camera/lights, sky/shadows/particles disabled:

| Configuration | DX12 mean | Vulkan mean |
| --- | ---: | ---: |
| Baseline: detail normal and refraction disabled | 0.470 ms | 0.514 ms |
| Detail normal enabled, refraction disabled | 0.502 ms | 0.532 ms |
| Detail normal and refraction enabled | 0.644 ms | 0.583 ms |

These are one short diagnostic run and include CPU recording, submission and
queue-idle time. They are not isolated GPU shader costs, stable benchmarks or
performance thresholds. Re-run on target hardware before performance decisions.

## Interactive UI status

The editor started on DX12, and the reference Rubber asset opened in the live
Inspector. Its material preview, larger checkerboard and fixed-height viewport
were visually inspected at a roughly 560-pixel Inspector width. Automated
headless coverage verifies the common material render path.

Further input stopped because Computer Use returned `failed to activate captured
window`, including its recovery attempt. The test editor process was closed to
allow relinking. The following interactive checks are therefore NOT fully run:

- Narrow/wide asset and entity Inspector layout; texture search, assign, replace,
  disable and clear in a live window.
- Shader drag/drop, named creation, custom numeric/texture editing and preview
  changes; error diagnostics and last-good behavior in the live editor.
- UI save/reopen/shared-user refresh and mouse rotation with fixed camera/lights.

Glass distortion, tint, IOR=1, MSAA fallback and resizing pass automated GPU
checks. Dedicated image assertions isolating foreground-depth rejection and
viewport-edge clamping are still pending; keep that detailed roadmap item open.

Use the dedicated reference assets or disposable copies for these checks, not
the user's default material or vehicle scene. Automated persistence, picker,
binding and GPU checks cover their implementation but do not substitute for UI
input acceptance. No user material or scene was edited during UI inspection.

## Supported custom-shader contract

The canonical template is a complete VS/PS example with unlit material output;
custom shaders own their shading. It is not a hook into ForwardPBR's BRDF.
EngineObject/EngineScene/MaterialParameters use b0/b1/b2 in space0, textures and
samplers use space1. Legacy ObjectConstants/SceneConstants templates are adapted
only in the compiled view. Matrix storage is column major.

Supported editable values are float, float2/3/4, HDR Color, int/uint (32-bit
integer authoring), bool and individual Texture2D. The standard u_BaseMap uses
the Albedo assignment and sRGB upload. Normal/data custom maps use linear
uploads and role-correct initialized fallbacks; ordinary repeat samplers are
renderer-provided. Vertex inputs use the existing 64-byte stream: POSITION0,
NORMAL0, TANGENT0, TEXCOORD0 and COLOR0. Other indices/semantics are rejected.

Arrays, cubes, storage/UAV/structured resources and editable user matrices are
outside this interface. Unsupported or conflicting layouts produce diagnostics
and retain the last usable generation or select the built-in material. Refraction
uses visible opaque screen color/depth; offscreen and transparent-layer tracing
and MSAA refraction remain outside the approximation. Mip/filter/wrap importer
settings and other Godot features remain separate work.
