# M15 U4 Batch 1 — Independent Codex Review

## Scope

Reviewed Gemini delivery `83ef0286` plus exchange metadata `5afdce70` for
`M15-U4-03.1` and `M15-U4-03.2`, from base `672d5728`. The requested contract
requires production GTAO dataflow to run before forward shading and to modulate
ambient/indirect radiance only, while preserving direct diffuse, direct
specular, and emissive radiance.

## Findings

### U4A-R1 — BLOCKER: the production GTAO path is still post-lighting

- **Source:** `Engine/Scene/src/RHISceneRendererPasses.cpp:1210-1231`;
  `Engine/Resources/Shaders/GTAO.hlsl:275-285`.
- **Trigger:** enable GTAO or contact shadows in the native renderer.
- **Expected:** depth/normal visibility is generated before forward shading and
  is consumed by the forward shader so AO affects only the ambient/indirect
  term.
- **Observed:** `GTAOPass` is registered after `OpaqueAndSky`, `ScreenSpace`,
  `Transparent`, `ParticlesAndResolve`, and `DDGIUpdate`. Its apply shader
  still returns `sceneColor.rgb * ambientFactor * contactFactor`. The delivery
  does not modify `GTAO.hlsl`, `ForwardPBR.hlsl`, the forward descriptor
  bindings, or the renderer pass ordering.
- **Impact:** direct diffuse, direct specular, and emissive output remain
  subject to the post-lighting composite. `GTAOPass::IsPreLightingDataflowSupported()`
  returns `true`, but the production path does not implement that mode.
- **Required correction:** wire a real pre-lighting path (depth/normal
  prepass, GTAO compute before forward shading, and forward-shader visibility
  consumption) or report the mode as unsupported and retain an explicit
  fallback. Do not mark U4-03.1 PASS until a runtime fixture proves direct,
  specular, and emissive invariance under AO variation.

### U4A-R2 — HIGH: the focused test proves a helper, not renderer wiring

- **Source:** `Tests/rendering/Test_M15_OcclusionContractUnit.cpp:132-149` and
  `:160-200`.
- **Observed:** the test validates `OcclusionContract::EvaluateShading`, shader
  string tokens, and a hardcoded VRAM calculation. It does not assert pass
  ordering, a production descriptor binding, a GTAO resource read in
  `ForwardPBR`, or native radiance invariance for AO changes.
- **Impact:** all reported CPU/native checks can pass while the actual renderer
  still executes the old whole-scene post-composite path.
- **Required correction:** add a production-facing graph/binding assertion and
  a deterministic native fixture that toggles AO while checking direct,
  specular, and emissive readbacks independently.

## Verification performed

- `cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests -j 6` — PASS.
- `cmake --build build --config Release --target AxleRenderUnitTests AxleEnvironmentRenderingTests -j 6` — PASS.
- Debug `AxleRenderUnitTests.exe --m15-occlusion-contract` — PASS, exit 0.
- Release `AxleRenderUnitTests.exe --m15-occlusion-contract` — PASS, exit 0.
- Debug native D3D12 and Vulkan environment fixtures — PASS, exit 0, baseline parity checks pass.
- Release native D3D12 and Vulkan environment fixtures — PASS, exit 0, baseline parity checks pass.
- `python scripts/check_architecture.py` — PASS (`611` files, `25` CMake files).
- `git diff --check 672d5728..HEAD` — PASS.

The native and unit gates are reported as passing, but they do not cover the
missing production pre-lighting dataflow identified above.

## Verdict

**NOT ACCEPTED.** U4-03.2's architectural rationale is reasonable as a target
allocation decision, but U4-03.1 is not implemented in the production renderer.
The checkpoint remains open until U4A-R1 and U4A-R2 are corrected and rerun
under independent Codex verification.
