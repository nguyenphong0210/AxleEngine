# M15 U3 batch 5 — Codex independent review

## Review scope

- **Implementation under review:** `739d0228` (`feat(renderer): implement M15 surface normal and material data contracts (U3-14..19)`)
- **Parent:** `5174c7fc`
- **Reported implementation scope:** M15-U3-14 through M15-U3-19.4
- **Review date:** 2026-11-16
- **Reviewer:** Codex

This review is independent of `docs/reviews/M15_U3_BATCH_5_REVIEW.md`. The Gemini report is treated as reported evidence; the commands and source checks below are Codex-verified evidence.

## Verdict

**HOLD / PARTIAL.** The batch is buildable and the CPU/native smoke gates pass, but it is not ready for full U3 acceptance. The Material Graph path in `MaterialSurface.hlsl` does not yet implement the same scene-lighting contract as `ForwardPBR.hlsl`, and custom graph objects are not evaluated by the SurfaceNormal pass. Those gaps conflict with the M15 remediation exit contract for built-in/graph agreement under the same lights, shadows, and IBL.

## Acceptance matrix

| Item | Codex verdict | Reason |
|---|---|---|
| U3-14 | **PASS (built-in path)** | Surface normal shader includes primary/detail normal decode, strength and RNM logic; shader compiles in native DX12/Vulkan runs. Graph visual parity is not proven. |
| U3-15.1 | **PASS** | Independent contract tests pass for inverse-transpose/cofactor normal handling. |
| U3-15.2 | **PASS** | Independent contract tests pass for mirrored-transform tangent handedness. |
| U3-15.3 | **PASS** | Independent contract tests pass for tangent-space normal transformation. |
| U3-16 | **PARTIAL** | Built-in cutout discard and validity output are present, but custom graph surfaces fall back to the default descriptor set in SurfaceNormal and therefore do not evaluate graph alpha/normal inputs. |
| U3-17.1–17.6 | **PARTIAL / NOT ACCEPTED for full parity** | Scalar BRDF tokens and clamps are present, but graph shading is single-sun plus hard-coded ambient; it omits ForwardPBR multi-light loops, shadow visibility, split-sum IBL, and the full advanced-lighting path. |
| U3-18 | **PASS** | sRGB/linear and packed ORM contract tests pass. |
| U3-19.1–19.4 | **PASS** | Asset/component/RHI/GPU/structured and flat serialization tests pass. |
| U3-20 | **OPEN** | Editor visual gate remains outstanding and still requires the user-provided evidence. |

## Findings

### F1 — High: Material Graph lighting is not equivalent to ForwardPBR

`Engine/Resources/Shaders/MaterialSurface.hlsl:71-110` evaluates one sun direction and uses a fixed ambient approximation:

- no `u_LightCounts` point/spot/rect-light loops;
- no sun or local-light shadow visibility;
- no irradiance map, prefiltered environment map, or BRDF LUT sampling;
- advanced sheen, subsurface, absorption and dispersion fields are declared but are not evaluated in this pixel path.

By comparison, `Engine/Resources/Shaders/ForwardPBR.hlsl:159-186` binds multi-light and IBL resources, and its lighting implementation at approximately `:516-887` evaluates local lights, shadows, split-sum IBL and advanced lobes.

This violates the M15 remediation exit stated in `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/REMEDIATION_PLAN.md:95-99`: built-in and graph materials must agree under the same lights, shadows and IBL. Token-level checks for `INV_PI`, clamps, clearcoat and `FresnelSchlickRoughness` do not establish that parity.

**Required action:** route both built-in and graph materials through a shared scene-lighting implementation (or explicitly narrow the supported graph scope and create a follow-up item). Add a native graph-vs-ForwardPBR render/readback fixture with the same sun, point/spot lights, shadows and IBL.

### F2 — Medium: SurfaceNormal does not evaluate custom graph alpha/normal inputs

In `Engine/Scene/src/RHISceneRendererPasses.cpp:916-929`, the SurfaceNormal pass binds the material constants and descriptor set only for non-custom materials. For `item.UseCustomShader`, it binds `m_DefaultDescriptorSet` instead. The pass then still draws the object through `SurfaceNormal.hlsl`.

Consequences:

- a graph-authored cutout alpha is not used by the normal/depth coverage pass;
- a graph-authored normal input is not represented in the normal buffer;
- the claimed graph coverage/normal agreement is therefore only true for the built-in material path.

**Required action:** resolve graph alpha/normal into the canonical SurfaceNormal inputs, or explicitly skip unsupported graph materials and document/test that fallback. The U3-16 evidence must cover at least one graph cutout and one graph normal-map case.

### F3 — Medium: Evidence proves contracts and compilation, not shader output parity

The focused unit test in `Tests/rendering/Test_M15_SurfaceAndMaterialContractUnit.cpp` checks shader source tokens and independently reimplements CPU reference math. That is useful contract coverage, but it does not execute `MaterialSurface.hlsl` and compare GPU pixels with ForwardPBR. The graph compiler/runtime/resource tests prove generated shader compilation and resource contracts; they do not prove rendered lighting equivalence.

**Required action:** add a deterministic native fixture that renders the same authored material through the built-in and graph paths and compares readback values under controlled lights/IBL. Keep source-token tests as fast guards, not as the parity gate.

### F4 — Low: Duplicate parser branches in Material.cpp

`Engine/Renderer/src/Material.cpp:827-883` already handles both uppercase and lowercase advanced-property keys. The following lowercase-only branches are unreachable duplicates. This did not cause a test failure, but it increases maintenance and parser-review noise.

### F5 — Low: Large line-ending churn in Material.cpp

The commit reports roughly 2,143 changed lines in `Material.cpp` for a small semantic serialization change. `git diff --check` is clean, and serialization tests pass, but normalizing line endings in the follow-up would make future reviews safer and easier.

## Codex-verified evidence

All commands below completed successfully unless noted otherwise:

1. `cmake --build build --config Debug --target AxleRenderUnitTests --parallel 4`
2. `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract`
3. `cmake --build build --config Release --target AxleRenderUnitTests --parallel 4`
4. `build/Tests/Release/AxleRenderUnitTests.exe --m15-surface-material-contract`
5. Full Debug and Release `AxleRenderUnitTests.exe` suites
6. Debug and Release `AxleEnvironmentRenderingTests` builds
7. Native environment runs: Debug/Release × DX12/Vulkan; all exit 0
8. `python scripts/check_architecture.py` → `AXLE_ARCH_CHECK PASS`
9. `git diff --check 5174c7fc..739d0228` → exit 0
10. Built and ran Debug Material Graph compiler/runtime/resource policy targets; all exit 0
11. `ctest --test-dir build -C Release -R "^RenderUnitTests$" --output-on-failure` → exit 0

The native logs contain expected negative-path messages from fixture cases (for example missing probe files and intentional descriptor/view failures); the test processes still exit 0. Release links emit the existing LNK4098 warning only.

## Recommended disposition

Accept U3-14/15/18/19 as independently verified contracts. Keep U3-16 and U3-17 partial, leave U3-20 open, and return the batch for F1–F3 remediation before marking U3/S3.2 complete. After the shared-lighting and graph SurfaceNormal fixes land, rerun the native graph parity fixture and the Editor visual gate.

