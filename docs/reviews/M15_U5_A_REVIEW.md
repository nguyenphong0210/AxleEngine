# M15 U5 Batch A — Hi-Z Depth Pyramid Self-Verification Review (`M15-U5-05.1` → `M15-U5-07.2`)

Self-verification delivery report for Milestone 15 Unit 5 Batch A (`U5-A`, SubUnit `S5.1`: `M15-U5-05.1` through `M15-U5-07.2`), built against baseline commit `a746397b` (delivered in a single coherent local commit). Independent Codex review and acceptance remain pending per [WORKING_PROTOCOL.md](../agents/WORKING_PROTOCOL.md). Interactive Editor visual capture (`M15-U5-07.1`) is explicitly deferred per user instruction to a consolidated visual pass after automated U5 implementation.

---

## 1. Requirement Coverage Summary

| Requirement | Status | Summary of Implementation & Verification |
| :--- | :--- | :--- |
| **M15-U5-05.1 — Validate min reduction reference** | **PASS (self-verification)** | Canonical `4x4` standalone compute readback and full `RHISceneRenderer` `HiZ0..HiZ(N-1)` readback match `Axle::HiZContract::BuildMinReductionPyramid` and `scripts/test_m15_hiz_reference.py` with `max_error = 0` on CPU and Debug/Release D3D12/Vulkan. |
| **M15-U5-05.2 — Validate 1x1** | **PASS (self-verification)** | Verified `1x1` standalone and full-renderer Hi-Z (`useful = 1`, `allocated = 8` for SSR static binding compatibility, exact `[0, 1]` min depth preserved, `max_error = 0`, 0 validation errors). |
| **M15-U5-05.3 — Validate 3x5** | **PASS (self-verification)** | Verified `3x5` (`3x5 -> 1x2 -> 1x1`, `useful = 3`, `allocated = 8`), CPU trailing odd-column (`x=2 -> 0.03f`) and odd-row (`y=4 -> 0.02f`) propagation, standalone corner blocker (`0.015f`), and full-renderer `3x5` readback (`max_error = 0`). |
| **M15-U5-05.4 — Validate 1919x1079** | **PASS (self-verification)** | Verified `1919x1079` (`11` levels down to `1x1`: `1919x1079 -> ... -> 1x1`), standalone corner blocker propagation, `127x63` odd extent, and full-renderer `1919x1079` HiZ0..HiZ10 texel-by-texel readback vs CPU reference (`max_error = 0`, 0 validation errors). |
| **M15-U5-06.1 — Freeze depth convention** | **PASS (self-verification)** | Frozen `[0.0, 1.0]` zero-to-one nearest-blocker `min()` convention in `Axle::HiZContract` (`NearDepth = 0.0f`, `FarDepth = 1.0f`, `GetDepthConventionSummary()`) and `HiZ.hlsl`, with negative checks rejecting zero extents, invalid level counts, buffer size mismatches, and `<0`/`>1`/`NaN`/`Inf` depths. |
| **M15-U5-06.2 — Define edge policy** | **PASS (self-verification)** | Implemented conservative odd-edge border folding (`includeExtraX`, `includeExtraY`) in `HiZ.hlsl`, `Axle::HiZContract::BuildMinReductionPyramid`, and `scripts/test_m15_hiz_reference.py` so trailing odd column/row (`2*dst + 2`) folds into the last destination texel whenever `src > 1` is odd. |
| **M15-U5-06.3 — Define useful level count** | **PASS (self-verification)** | Defined `ComputeUsefulHiZLevelCount(w, h)` (`floor(log2(max(w, h))) + 1` clamped to `[1, MaxHiZLevels(16)]`, `0` on `0` extent) and `ComputeAllocatedHiZLevelCount(w, h)` (`max(MinSSRHiZLevels(8), useful)`). Expanded `RHIViewTargets` (`m_ExtraHiZTextures`) and `RHISceneRenderer` (`m_HiZTextureSets`/`m_HiZStorageSets`, 36-set pool) up to 16 `R32_FLOAT` levels while keeping SSR's 8 static `u_HiZ0..u_HiZ7` bindings. |
| **M15-U5-07.1 — Add per-level visualization** | **Automated PASS / EDITOR VISUAL: NOT RUN (deferred)** | Implemented `VisualizeVSMain`/`VisualizePSMain` in `HiZ.hlsl`, `HiZGenerationPass::DeclareVisualization`/`RecordVisualization`, `SceneRenderSettings::HiZVisualizationLevel` (`-1` off, hashed into `HashViewFeaturePolicy`), target-builder parity (`RHISceneRenderer`, `RenderService`, `RHIViewportDescriptor`), `EditorLayer` `Hi-Z Debug Level (-1=Off)` slider (`-1..11`), and explicit fallback diagnostics (`hiz_visualization_unavailable`, `hiz_visualization_clamped_or_tail`). Native Debug/Release D3D12/Vulkan checks verify SDR contrast (`level0 left=0.360784` vs `sky=0`), coarse propagation (`vis0 != vis3 != vis6`), uniform 1x1 level 6 (`0.372549`), clamped level 99 matching level 6 with fallback diagnostic, finite HDR visualization, and exact scene recovery when disabled. Interactive Editor capture deferred per user direction. |
| **M15-U5-07.2 — Validate barriers** | **PASS (self-verification)** | CPU `RenderGraph` contract compiles the 11-level `SSRCopy -> HiZ0..HiZ10 -> SSRCompute + HiZVisualize` RAW barrier chain and rejects uninitialized Hi-Z reads before generation; native Debug/Release D3D12/Vulkan runs execute all transitions with `0` RHI validation errors. |

---

## 2. Modified and Added Files

- **RHI & Renderer Contracts:**
  - `Engine/RHI/include/Axle/RHI/RHIViewTargets.hpp`, `Engine/RHI/src/RHIViewTargets.cpp`
  - `Engine/Renderer/include/Axle/RenderContracts.hpp`, `Engine/Renderer/src/RenderContracts.cpp`
  - `Engine/Renderer/include/Axle/SceneRenderSettings.hpp`, `Engine/Renderer/src/ViewContext.cpp`
- **Shader, Scene Passes & UI/Editor Wiring:**
  - `Engine/Resources/Shaders/HiZ.hlsl`
  - `Engine/Scene/include/Axle/RHISceneRenderer.hpp`, `Engine/Scene/src/RHISceneRenderer.cpp`, `Engine/Scene/src/RHISceneRendererPasses.cpp`
  - `Engine/Scene/src/Passes/HiZGenerationPass.hpp`, `Engine/Scene/src/Passes/HiZGenerationPass.cpp`
  - `Engine/Scene/src/Passes/ScreenSpaceReflectionPass.hpp`, `Engine/Scene/src/Passes/ScreenSpaceReflectionPass.cpp`
  - `Engine/Scene/src/RenderService.cpp`, `Engine/UI/src/RHIViewportDescriptor.cpp`
  - `Editor/src/EditorLayer.hpp`, `Editor/src/EditorLayer.cpp`
- **Reference Script & Automated Tests:**
  - `scripts/test_m15_hiz_reference.py`
  - `Tests/CMakeLists.txt`, `Tests/rendering/RenderUnitTestMain.cpp`, `Tests/src/Test_EnvironmentRendering.cpp`
  - `Tests/rendering/Test_M15_HiZContractUnit.cpp`, `Tests/rendering/Test_M15_HiZRendering.cpp`
- **Documentation & Tracking:**
  - `docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/PLAN.md`
  - `docs/reviews/M15_U5_A_REVIEW.md`
  - `docs/agents/CODEX_GEMINI_EXCHANGE.json`

---

## 3. Native Readback Evidence Metrics

All four native GPU configurations (`Debug-DX12`, `Debug-Vulkan`, `Release-DX12`, `Release-Vulkan`) produced bit-identical `M15_U5A_hiz.txt` metrics and visualization PPM outputs (`M15_U5A_vis-level0.ppm`, `M15_U5A_vis-level3.ppm`, `M15_U5A_vis-level6.ppm` under `build/Tests/EnvironmentRendering/U5A-*/`):

- `standalone=canonical-4x4 useful=3 last_extent=1x1 max_error=0`
- `standalone=minimal-1x1 useful=1 last_extent=1x1 max_error=0`
- `standalone=odd-3x5 useful=3 last_extent=1x1 max_error=0`
- `standalone=odd-3x5-border useful=3 last_extent=1x1 max_error=0`
- `standalone=odd-127x63-border useful=7 last_extent=1x1 max_error=0`
- `standalone=odd-1919x1079 useful=11 last_extent=1x1 max_error=0`
- `standalone=odd-1919x1079-border useful=11 last_extent=1x1 max_error=0`
- `rendered=1x1 useful=1 allocated=8 max_error=0`
- `rendered=3x5 useful=3 allocated=8 max_error=0`
- `rendered=127x63 useful=7 allocated=8 max_error=0`
- `rendered=1919x1079 useful=11 allocated=11 max_error=0`
- `visualization_level0_left=0.360784 visualization_level0_sky=0 visualization_level6_1x1=0.372549`

---

## 4. Verification Matrix

| Gate / Configuration | Command | Exit Code | Result Summary |
| :--- | :--- | :--- | :--- |
| **Hi-Z CPU Reference Script** | `python scripts/test_m15_hiz_reference.py` | `0` | **PASS**: `4x4 canonical; 1x1, 3x5 and 1919x1079 edge cases` |
| **Debug Build** | `cmake --build build --config Debug --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentRenderingTests AxleEditor -j 8` | `0` | **PASS** |
| **Debug Focused CPU Unit** | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-hiz-contract` | `0` | **PASS**: `M15-U5-05.1` through `U5-07.2` contract tests passed |
| **Debug Full CPU Suite** | `build\Tests\Debug\AxleRenderUnitTests.exe` | `0` | **PASS**: all CPU contract suites passed from repo root |
| **Debug Focused CTest** | `ctest --test-dir build -C Debug -R "^(M15HiZContract\|M15HiZReference)$" --output-on-failure` | `0` | **PASS**: `2/2` (`M15HiZContract`, `M15HiZReference`) |
| **Debug RHI Resource View** | `build\Tests\Debug\AxleRHIResourceViewTests.exe` | `0` | **PASS**: `RHIResourceView: PASS` |
| **Debug D3D12 Native GPU** | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U5A-Debug-DX12` | `0` | **PASS**: `PASS M15-U5-A`, `max_error=0`, `0` validation errors |
| **Debug Vulkan Native GPU** | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U5A-Debug-Vulkan` | `0` | **PASS**: `PASS M15-U5-A`, `max_error=0`, `0` validation errors |
| **Debug Editor Startup (Vulkan / DX12)** | `AxleEditor.exe --rhi=vulkan --verify-empty-startup` & `--rhi=dx12 --verify-empty-startup` (in `build\Editor\Debug`) | `0` / `0` | **PASS**: `5 UI frames`, clean shutdown |
| **Release Build** | `cmake --build build --config Release --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentRenderingTests AxleEditor -j 8` | `0` | **PASS** |
| **Release Focused CPU Unit** | `build\Tests\Release\AxleRenderUnitTests.exe --m15-hiz-contract` | `0` | **PASS**: `M15-U5-05.1` through `U5-07.2` contract tests passed |
| **Release Full CPU Suite** | `build\Tests\Release\AxleRenderUnitTests.exe` | `0` | **PASS**: all CPU contract suites passed from repo root |
| **Release Focused CTest** | `ctest --test-dir build -C Release -R "^(M15HiZContract\|M15HiZReference)$" --output-on-failure` | `0` | **PASS**: `2/2` (`M15HiZContract`, `M15HiZReference`) |
| **Release RHI Resource View** | `build\Tests\Release\AxleRHIResourceViewTests.exe` | `0` | **PASS**: `RHIResourceView: PASS` |
| **Release D3D12 Native GPU** | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U5A-Release-DX12` | `0` | **PASS**: `PASS M15-U5-A`, `max_error=0` |
| **Release Vulkan Native GPU** | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U5A-Release-Vulkan` | `0` | **PASS**: `PASS M15-U5-A`, `max_error=0` |
| **Release Editor Startup (Vulkan / DX12)** | `AxleEditor.exe --rhi=vulkan --verify-empty-startup` & `--rhi=dx12 --verify-empty-startup` (in `build\Editor\Release`) | `0` / `0` | **PASS**: `5 UI frames`, clean shutdown |
| **Architecture Gate** | `python scripts/check_architecture.py` | `0` | **PASS**: `611` source files, `25` CMake files (`AXLE_ARCH_CHECK PASS`) |
| **Exchange Schema Gate** | `python scripts/agent_exchange.py validate` | `0` | **PASS**: `DELIVERY` message `CG-20261117T070850437254Z-GEMINI-4e70b7` valid |
| **Diff / Style Gate** | `git diff --check` | `0` | **PASS**: clean whitespace/CRLF check |
| **Interactive Editor Capture** | — | — | **NOT RUN (deferred)**: consolidated visual pass deferred per user direction |

Note on `ctest` full `RenderUnitTests`: existing pre-U5 test `Test_M15_SurfaceAndMaterialContractUnit.cpp:554` opens relative `"Engine/Resources/Shaders/ForwardPBR.hlsl"`, so `ctest --test-dir build -C Debug -R "^RenderUnitTests$"` fails when CTest runs in `build/Tests`, whereas invoking `build\Tests\{Debug,Release}\AxleRenderUnitTests.exe` from the repository root passes 100% of all suites and focused CTest targets `M15HiZContract` and `M15HiZReference` pass regardless of working directory.

---

## 5. Open Gates

- **M15-U5-07.1 Interactive Editor Visual Capture:** Remains open (`[ ]` in `PLAN.md`) and deferred to the post-U5 visual verification pass per user direction (alongside prior deferred `M15-U4-06.5`, `U4-E`, and `M15-U5-03` visual items).
- **Codex Independent Review:** Pending at the end of `U5-A` (or consolidated Cluster 1 review once `U5-B..D` follow).

## Independent Codex review

See [the independent findings and final executed checks](M15_U5_A_CODEX_REVIEW.md). Codex corrected visualization attachment-format selection, missing capability fallback and Editor level bounds, and strengthened native depth-source and one-axis edge assertions. Automated scope is accepted after corrections; interactive captures remain user-deferred. Original self-verification is retained above as historical reported evidence.
