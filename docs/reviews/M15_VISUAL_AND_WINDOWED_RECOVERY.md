# Milestone 15 — Windowed & Editor Visual Recovery Report (`U4..U9`)

- **Date:** 2026-11-21
- **Baseline:** `0eeef6b6` (`M15-U9` docs) / `7568cd5e` (`M15-U9-E`)
- **Commits added in this recovery pass:**
  - `93234b4e` `fix(rendering): use unclipped projection for CSM during planar reflection capture`
  - `82e82d82` `feat(editor): add deterministic swapchain and viewport capture plan runner`
- **Physical Display State:** `SM_CMONITORS = 2`, desktop `2560 x 1440`, D3D12 / Vulkan SwapChain `2560 x 1369`, NVIDIA GeForce RTX 3070 (`OpenGL 4.5.0 NVIDIA 610.88` for WGL).

---

## 1. Windowed SwapChain & OpenGL WGL Backlog Checks (`U6-E` / `U5-H` Parity)

Executed cleanly across **Debug** and **Release** (`exit 0`, zero fallback):

| Check | Configs / Backends | Result |
|---|---|---|
| `AxleEnvironmentRenderingTests.exe gl` | Debug & Release (`OpenGL 4.5.0`) | **PASS** (`OpenGL background-only and explicit IBL pixels, snapshot and ECS bridge gates`) |
| `AxleRenderServiceTests.exe gl` | Debug & Release | **PASS** (`OPT05 RenderService PASS`) |
| `AxleRHIResourceViewTests.exe <api> targets` | Debug & Release (`dx12`, `vulkan`) | **PASS** (`RHIResourceView: PASS`) |
| `AxleRHIStateTests.exe <api>` | Debug & Release (`dx12`, `vulkan`) | **PASS** (`Resource states: PASS`) |
| `AxleRHIPresentationTests.exe <api>` | Debug & Release (`dx12`, `vulkan`) | **PASS** (`Shared presentation: PASS`) |
| `AxleRuntimeRenderTests.exe <api> [cooked]` | Debug & Release (`dx12` loose/cooked, `vulkan` loose/cooked, `gl`) | **PASS** (`OPT05 Runtime PASS`) |
| `AxleEditor.exe --verify-empty-startup --rhi=<api> --no-fallback --rhi-validation` | Debug & Release (`dx12`, `vulkan`) | **PASS** (`0 entities, 0 assets; 5 UI frames`) |
| `AxleEditor.exe --project=<copy of U4E> --verify-startup` | Debug & Release (`dx12`, `vulkan`) | **PASS** (`3 entities; 5 UI frames`, prior to visual capture runner) |
| `AxleEditor.exe --project=<copy of FroxelSlit> --verify-startup` | Debug & Release (`dx12`, `vulkan`) | **PASS** (`5 entities; 5 UI frames`, prior to visual capture runner) |

---

## 2. Post-Final-Edit Regression Suite (`82e82d82`, RTX 3070)

All checks below ran after the final C++ edit (`RHICascadedShadows.cpp`, `MaterialInspectorControls.cpp`, `Test_M15_ReflectionCaptureRendering.cpp`, `VisualCapture.{hpp,cpp}`):

- `python scripts/check_architecture.py` -> **PASS** (`Files scanned: 625; CMake files: 25`)
- `cmake --build build --config {Debug,Release}` (`10` targets including `AxleEditor`, `AxleEnvironmentRenderingTests`) -> **OK**
- `AxleRenderUnitTests.exe` (`Debug` & `Release`) -> **PASS** (all suites)
- `AxleRenderUnitTests.exe --m15-surface-material-contract` from repo root (`Debug` & `Release`) -> **PASS** *(Note: running `ctest --test-dir build` without cwd override fails `#45 M15SurfaceMaterialContract` on its pre-existing relative path `Engine/Resources/Shaders/ForwardPBR.hlsl`)*
- `AxleMaterialUnitTests.exe` (`Debug` & `Release`) -> **5 / 5 PASS**
- `ctest --test-dir build -C {Debug,Release} -R "^(MaterialGraph|M15BRDF|M15Primary|M15Roughness|M15Direct|M15Environment|M15Occlusion|M15HiZ|M15Reflection|M15Froxel|M15DDGI|M15U8|M15U9|M15Image|M15Artifact)"` -> **24 / 24 PASS** (both configs)
- `AxleEnvironmentRenderingTests.exe {dx12,vulkan,gl}` (`Debug` & `Release`) -> **6 / 6 PASS** (includes new `M15-U5-46.8` Planar + CSM Directional Shadow regression test in `Test_M15_ReflectionCaptureRendering.cpp`)
- `AxlePreviewRenderTests.exe {dx12,vulkan}` & `{dx12,vulkan} surfaces` (`Debug` & `Release`) -> **8 / 8 PASS**
- `AxleRHIResourceViewTests.exe {dx12,vulkan} {storage,targets}` (`Debug` & `Release`) -> **8 / 8 PASS**
- `AxleRHIStateTests.exe {dx12,vulkan}`, `AxleRHIPresentationTests.exe {dx12,vulkan}` (`Debug` & `Release`) -> **8 / 8 PASS**
- `AxleRenderServiceTests.exe {dx12,vulkan,gl}`, `AxleRuntimeRenderTests.exe {dx12,vulkan,dx12 cooked,vulkan cooked,gl}` (`Debug` & `Release`) -> **16 / 16 PASS**
- `AxleEditor.exe --verify-empty-startup --rhi={dx12,vulkan} --no-fallback --rhi-validation` (`Debug` & `Release`) -> **4 / 4 PASS**

---

## 3. Editor Visual Capture & Inspection Summary (`91` PNGs, Console `errors=0`, `failedViews=0`)

Because commands in this environment run in a non-active Win32 desktop (`exebox-*`) where OS `PrintWindow` receives uncomposited white client surfaces, `AxleEditor --capture-plan` (`RHIPresentationContext::RequestNextFrameCapture` + `RHITexture` viewport readback) captures the **live RTX 3070 GPU SwapChain backbuffer** (`2560x1369` composited ImGui panels + 3D viewport) and raw `RGBA16_FLOAT` `EditorMainViewport` color buffer (`1472x891`, `nonFinite=0`) under `--no-fallback --rhi-validation`.

All `91` PNGs and logs are archived under `build/agent-runs/m15_visual_recovery/{Debug/dx12, Release/dx12, Release/vulkan}`:

| Suite / Fixture | Configs & Backends | Key Visual & Readback Verification | Covers `PLAN.md` Items |
|---|---|---|---|
| **U4D (`UnoccludedSphere.axlescene`)** | Debug DX12, Release DX12, Release Vulkan | Smooth mirror sphere curvature over HDR sky; GTAO ON vs OFF matches analytically (`meanAbsDiff=0.00`, `nonFinite=0`, `Error 0`). | `M15-U4-06.5` |
| **U5 Probes, SSR, Hi-Z & Play/Eject (`U5_ProbesSSR`)** | Debug DX12, Release DX12, Release Vulkan | Mirror & satin spheres reflect glowing Red/Blue reference walls & checkered floor; dual Sphere/Box parallax probes blend cleanly with `EnableEnvironmentReflections=false` box matte; Play mode + Free-Fly Eject works cleanly; Hi-Z Level `0` vs Level `3` coarsens properly (`meanAbsDiff=1.59, maxAbsDiff=186, changedPct=1.57%`), restores clean on `-1`. | `M15-U5-07.1, 08.1, 08.2, 10.1..10.4, 11.2, 13.1, 13.2, 14, 21.3, 21.4, 26.1, 27.2, 28, 29, 30.1, 31.5, 35.3, 37.2, 38.1` |
| **U5 Roughness Sweep (`M15_RoughnessSweep.axlescene`)** | Debug DX12, Release DX12 | Five gold metallic spheres (`roughness 0.04 -> 0.80`) broaden highlights & floor/env reflections monotonically; SSR ON vs OFF changes `20.57%` (`meanAbsDiff=2.31, maxAbsDiff=65`), `Error 0`. | `M15-U5-17.1, 18.2, 19.2, 34.1, 35.1, 39.4` |
| **U5 Area Light (LTC) & Planar Mirror (`U5_AreaPlanar`)** | Debug DX12, Release DX12, Release Vulkan | Caught & fixed real CSM oblique-projection failure (`93234b4e`). Crisp mirror reflection of spheres, crimson tower & gold overhead bar with `[x] Planar Reflections` checked in open `RenderFXPopup`; smooth SSR+Probe fallback when unchecked (`changedPct=40.15%, meanAbsDiff=27.46..27.47, maxAbsDiff=222`); `Rect Light Warm` shows `Area Light (LTC)` & `Shadow Policy: Unshadowed Analytic LTC` in Inspector, `Error 0`. | `M15-U5-43, 44.4, 45.1, 46.5, 46.8` |
| **U6C (`FroxelSlit.axlescene`)** | Debug DX12, Release DX12 | Volumetric light shaft strictly bounded inside slit blockers in Play mode (`Error 0`). | `M15-U6-05.1/05.2/07.5/07.6` *(windowed + visual note; Codex recheck still open)* |
| **U7 (`U7_DDGI.axlescene`)** | Debug DX12, Release DX12, Release Vulkan | Warm orange & cool teal indirect bounce illuminates matte white sphere smoothly (`changedPct=68.49%..71.44%, meanAbsDiff=64.42..65.06`), `DDGI Probe Grid (Experimental)` visible in Inspector, `Error 0`. | `M15-U7-06` *(self-verified visual)* |
| **U8 (`U8_HDRBloomTAA.axlescene`)** | Debug DX12, Release DX12, Release Vulkan | 16-frame temporal Auto-Exposure + Bloom + TAA adaptation (`changedPct=92.52%, meanAbsDiff=33.65`), bounded bloom halo around HDR emissive beacon, Editor UI dark chrome unshifted/excluded from metering, `Error 0`. | `M15-U8-02.1, 02.3, 03.1, 04` *(self-verified visual)* |
| **U9 (`U9_Materials.axlescene`, `.axleasset`)** | Debug DX12, Release DX12, Release Vulkan | Hero trio displays Charlie Sheen plum velvet halo, SSS terminator glow with crisp silhouette boundary (`Adjacent Slate Boundary Block` unbled), and Beer-Lambert absorption + Cauchy chromatic dispersion prism fringe; `Inspector` auto-expands `Sheen`, `Subsurface Scattering`, and `Absorption & Dispersion` beside live `Asset Preview` sphere (`ImGuiCond_Once`), `Error 0`. *(Re-verified post-final edit after `MaterialInspectorControls.cpp` update.)* | `M15-U9-04.3, 06.4, 06.5` *(self-verified visual)* |

---

## 4. Remaining Independent Codex Gates (Unchanged Policy)

The later [independent U4–U6 follow-up](M15_U4_U6_CODEX_FOLLOWUP.md) records the results for the scoped U4–U6 and capture-tool checks listed here.

Automated & Editor visual blockers are cleared on the live RTX 3070 display/GPU; **no independent Codex acceptance is claimed for Codex-pending items:**
1. **`M15-U4-GTAO-HALO-FIX`** (`e4b977fe`..`d4a07cd7`) independent Codex review.
2. **`M15-U6-E`** separate-context Codex recheck on `232298f5` (`M15-U6-05.1, 05.2, 07.5, 07.6` kept `[ ]` in `PLAN.md`).
3. **`M15-U7`** (`39372690..1f0b293c`), **`M15-U8`** (`a13155b4..6f22017a`), and **`M15-U9`** (`5c140f37..7568cd5e`) independent Codex reviews, plus Codex audit of `93234b4e` & `82e82d82`.
