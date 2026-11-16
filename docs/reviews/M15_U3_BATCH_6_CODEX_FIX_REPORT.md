# M15 U3-20..U3-23.4 Codex correction report

## Scope

This report records the direct repairs applied after the independent review of
Gemini delivery `1046b695`. The source review identified that the graph shader
compiled, but its lighting interface was narrower than ForwardPBR and the
fallback bit was diagnostic only.

## Corrections

- `MaterialSurface.hlsl` now consumes renderer-owned multi-light, cascade shadow
  and split-sum IBL resources. It evaluates the uploaded directional, point and
  spot light data, uses the cascade receiver, and keeps the SDR/HDR output policy
  in the same template.
- `MaterialGraphLayout` treats the lighting descriptors as reserved engine
  bindings. They are excluded from editable graph properties and packed graph
  buffers, while the native descriptor layout retains them.
- `RHISceneRendererPasses` binds reserved light, cascade, local shadow, IBL and
  shadow resources for graph descriptor sets. A graph surface with an explicit
  unsupported feature mask now restores the canonical material and selects the
  complete ForwardPBR path.
- `SurfaceFeature::Emission` no longer reports a false positive because graph
  emission is part of `MaterialSurface`. Non-zero dispersion has its own mask.
- Contract tests now require the shared light, shadow and IBL declarations, and
  the native parity fixture compares both SDR and HDR output branches.

## Verification

- `cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests -j 6` — PASS.
- `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract` — PASS.
- `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/CodexFix3-DX12` — PASS; native graph shader compiled and the SDR/HDR center-pixel diffs were zero.
- Earlier corrected Vulkan run: `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/CodexFix-Vulkan` — PASS; active backend was Vulkan 1.3 and parity diff was zero.
- `python scripts/check_architecture.py` — PASS.

## Remaining gate

The U3-20 Editor visual gate still requires a user-provided Editor capture. This
is a native UI evidence requirement and cannot be marked complete from the
headless test runs above.
