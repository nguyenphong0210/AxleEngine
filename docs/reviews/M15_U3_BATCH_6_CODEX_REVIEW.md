# M15 U3 batch 6 — Codex independent review

## Result

**Recommendation: HOLD / PARTIAL.** Commit `1046b695` builds and its narrow CPU/native
fixtures pass, but it does not satisfy the M15 P2 exit condition that built-in and
Material Graph materials use the same scene-lighting contract under identical lights,
shadows, and IBL. The delivery report also contains an invalid Vulkan command name.

## Findings

### F1 — P1: Material Graph lighting parity is still incomplete

`Engine/Resources/Shaders/MaterialSurface.hlsl:2-10` declares only `EngineObject` and
the reduced `EngineScene` block. It has no `MultiLightBuffer` (`b2`), cascaded/local
shadow receivers (`b4`/`b5`), or IBL resources (`u_IrradianceMap`, `u_PrefilterMap`,
`u_BRDFLUT`). Its pixel path is therefore primary-sun plus analytical ambient only.

The built-in path binds the additional scene resources in
`Engine/Scene/src/RHISceneRendererPasses.cpp:728-734, 861-868, 997-1002` and evaluates
point/spot lights, CSM/local shadows, and split-sum IBL in `ForwardPBR.hlsl`. A graph
object in a scene with those features therefore cannot match the built-in result;
the graph shader simply has no inputs with which to evaluate them.

The native parity fixture deliberately disables environment lighting and ambient light
and uses one primary sun (`Tests/src/Test_EnvironmentRendering.cpp:833-840`). Its
`diff=(0,0,0)` proves only this reduced single-sun case. It is not evidence for
U3-23.1 through U3-23.3 in the required scene contract.

### F2 — P1: Unsupported-feature diagnostics do not match shader capability

`Engine/Renderer/src/ShaderSurface.cpp:7-24` marks nonzero canonical emission as
`SurfaceFeature::Emission`, although `MaterialSurface.hlsl:121` directly evaluates
`surface.Emission`. This produces a false fallback diagnostic for a supported graph
output. The mask has no `Dispersion` bit and does not flag nonzero dispersion when
absorption is disabled, despite the delivery report claiming dispersion coverage.

Also, `ResolvedSurface::Fallback` is recorded and reported, but it does not switch the
draw back to the built-in pipeline. The custom path remains selected at
`Engine/Scene/src/RHISceneRendererPasses.cpp:820-826`; the fallback branch at
`428-435` is only reached after custom pipeline/resource preparation fails. An
unsupported graph lobe can therefore be omitted while the diagnostic says fallback.

### F3 — P2: Output-policy parity is not exercised for the SDR branch

The native fixture runs after `settings.EnableToneMapping = true`, so
`u_CameraPosition.w > 0` and the ACES/gamma branch is bypassed. The unit test checks
source tokens, not the rendered SDR result. U3-23.4 has implementation evidence but
still needs a numeric fixture with tone mapping disabled (and the same scene readback)
before full acceptance.

### F4 — P2: U3-20 visual gate remains open

The smooth-sphere metric is a valid CPU geometry check, but the plan also requires an
Editor visual capture showing the actual smooth shaded sphere without bands or invalid
pixels. That evidence was not part of `1046b695` and remains user-provided.

### F5 — Evidence command typo

`Tests/src/Test_EnvironmentRendering.cpp:950-953` accepts `vulkan`, `dx12` (or any
non-`vulkan` token as D3D12) and `gl`. The delivery report records
`native-vulkan`; those runs actually initialized Direct3D 12. This invalidates the
reported Vulkan evidence, although the correct independent run below did pass.

## Independent verification

| Check | Result |
|---|---|
| `cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests -j 6` | PASS, exit 0 |
| `python scripts/check_architecture.py` | PASS, exit 0 |
| `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract` | PASS, exit 0 |
| `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/CodexReview-DX12-rerun` | PASS, exit 0; graph parity diff `(0,0,0)` in reduced sun-only fixture |
| `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/CodexReview-Vulkan-correct` | PASS, exit 0; log confirms `RHI Active Backend: Vulkan 1.3`, graph parity diff `(0,0,0)` in reduced sun-only fixture |

The native runs also exercise the existing environment/reflection and lifecycle suite;
no RHI validation error caused the process to fail. They do not close F1–F4.

## Acceptance decision

- **Accepted at narrow contract level:** the smooth-sphere CPU metric, resolver API
  plumbing, and the reduced single-sun graph readback fixture are implemented and
  reproducible.
- **Not accepted for full U3:** graph multi-light integration, shadow visibility,
  split-sum IBL parity, accurate unsupported-feature fallback behavior, SDR output
  readback, and the U3-20 Editor visual gate remain open.

## Required follow-up

1. Add a shared graph scene-lighting interface and bind the same light, shadow, and IBL
   resources used by `ForwardPBR`.
2. Correct the unsupported-feature table: remove false-positive emission, add explicit
   dispersion, and define whether a diagnostic means real built-in fallback or an
   intentional graph feature omission.
3. Extend the native parity fixture to cover local light, CSM/local shadow, IBL, and
   tone-mapping-disabled cases with per-case readback thresholds.
4. Re-run the Editor visual gate for U3-20 (and the graph/shadow visual checks) before
   changing these checklist items to accepted.

