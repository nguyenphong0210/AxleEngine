# Editor GPU Visual Capture Guide (`--capture-plan`)

This guide explains how to use AxleEngine's deterministic GPU capture runner (added in `82e82d82`) to inspect live Debug/Release `AxleEditor` rendering and ImGui UI frames when running in non-interactive or secondary-desktop terminals where OS window capture (`PrintWindow` / Windows Graphics Capture) returns blank white frames from DWM.

> **Authority reminder:** This is a tooling reference only (`docs/agents`). It does not release implementation tasks or alter `WORKING_PROTOCOL.md` acceptance rules. Always inspect generated PNGs directly and distinguish GPU pre-DWM readbacks from physical monitor output.

---

## 1. Architecture Summary

Two in-engine readback paths capture real GPU frames inside the normal process without crossing desktop boundaries:

1. **Full Composited Editor Window (`capture_editor`):**
   - Owned by `Axle::RHI::RHIPresentationContext::RequestNextFrameCapture` (`Engine/RHI/include/Axle/RHI/RHIPresentationContext.hpp`, `Engine/RHI/src/RHIPresentationContext.cpp`).
   - During `RenderFrame`, renders the viewport blit + ImGui UI into an offscreen `CaptureColor` attachment, copies it to a `HostReadback` buffer (`ReadbackTextureToBuffer`), swizzles `BGRA8 -> RGBA8` if needed, presents cleanly to the `SwapChain`, and writes a lossless RGBA8 PNG.
2. **Raw 3D Viewport Texture (`capture_viewport`):**
   - Owned by `VisualCaptureLayer` (`Editor/src/VisualCapture.hpp`, `Editor/src/VisualCapture.cpp`).
   - Calls `RHIDevice::WaitIdle()`, reads `EditorLayer::GetRHIViewport()->GetColorTexture()` (`RGBA16_FLOAT`, `RGBA8_UNORM`/`SRGB`, or `BGRA8_UNORM`), checks `nonFinite == 0`, computes `min`/`max`/`meanLuma`/`nonBlack`, clamps `[0.0, 1.0]` linear HDR values to UNORM8, and writes a PNG.

---

## 2. Launching `AxleEditor` with a Plan

Run from the **repository root** (`d:\WorkSpace\AxleEngine`) so relative asset/shader paths resolve cleanly:

```powershell
# Debug Direct3D 12 (strict, GPU validation enabled)
build/Editor/Debug/AxleEditor.exe `
  --project=Tests/Fixtures/M15/M15_RoughnessSweep.axleproj `
  --rhi=dx12 --no-fallback --rhi-validation `
  --capture-plan=build/agent-runs/my_capture/plan.txt

# Release Vulkan (strict, GPU validation enabled)
build/Editor/Release/AxleEditor.exe `
  --project=Tests/Fixtures/M15/M15_RoughnessSweep.axleproj `
  --rhi=vulkan --no-fallback --rhi-validation `
  --capture-plan=build/agent-runs/my_capture/plan.txt
```

### Relevant CLI Flags (`Editor/src/Main.cpp`, `Engine/RHI/src/RHISwitcher.cpp`)
- `--project=<path.axleproj>` (or `--project <path>`): startup project (use a copied project under `build/agent-runs/` if you do not want Editor edits/metadata touching tracked fixtures).
- `--empty-project`: start without opening a project fixture.
- `--rhi=<backend>` (or `-rhi=`, `--backend=`): `dx12`/`d3d12`, `vulkan`/`vk`, `gl`/`opengl`, or `auto`.
- `--no-fallback` (or `--strict-rhi`): fail instead of silently falling back to another RHI backend.
- `--rhi-validation` (or `--gpu-validation`): enable native D3D12/Vulkan validation layers.
- `--capture-plan=<plan_file>`: attach `VisualCaptureLayer` and exit automatically once the plan finishes. A failed load, assertion, capture or early close returns a nonzero exit code.

---

## 3. Capture Plan Syntax (`Editor/src/VisualCapture.cpp`)

- **Encoding:** Write the plan file in **UTF-8 without BOM** (Windows PowerShell 5.1 `Set-Content -Encoding UTF8` prepends a BOM that triggers `unknown command`; use Python `encoding="utf-8"` or `[System.Text.UTF8Encoding]::new($false)`).
- One command per line (`<op> [args]`). Blank lines and `#` comment lines are ignored.
- Relative paths in `output_dir` and `capture_*` resolve from the current `output_dir` (which initializes to `m_PlanPath.parent_path()`). Parent directories are created automatically.
- Relative paths in `scene` check `FileSystem::ResolveAssetPath(p)` (project asset root) first and otherwise fall back to `m_PlanPath.parent_path() / p`; use an **absolute path** (or asset-root-relative path) when loading scenes outside the plan folder.

### Scene, UI & State Commands (Immediate Phase)

| Command | Arguments | Effect |
| --- | --- | --- |
| `output_dir` | `<dir>` | Change base directory (`ResolveOutput`: relative to previous `output_dir`, which starts at plan parent dir). |
| `scene` | `<path.axlescene>` | Open scene (asset-root-relative, plan-dir-relative, or absolute path) and wait 2 frames. |
| `camera` | `<px> <py> <pz> <pitch> <yaw>` | Move Editor camera (`SetCameraPose`, roll `0`). |
| `fx` | `<k=v ...>` | Apply `RenderFXOverride` toggles/values (see table below). |
| `play` / `pause` / `stop` | *(none)* | Control Editor PIE play/pause/stop state. |
| `eject` | `1\|true\|0\|false` | Eject/possess game camera (`SetGameCameraEjected`). |
| `select_entity` | `<exact entity name>` | Select entity in Hierarchy/Inspector (throws if missing). |
| `select_asset` | `<asset path>` | Select asset in Asset Browser / Asset Preview / Inspector (throws if missing). |
| `preview_mesh` | `sphere\|cube\|cylinder\|plane` | Set Asset Preview mesh (`SetPreviewMesh`). |
| `preview_sun` | `<azimuth> <elevation>` | Set Asset Preview directional light (`SetPreviewSun`). |
| `focus` | `<ImGui window name>` | Request focus on an Editor panel (`RequestFocusWindow`). |
| `scroll_inspector` | `<float y>` | Request Inspector vertical scroll offset (`RequestInspectorScroll`). |
| `telemetry` | `1\|true\|0\|false` | Show/hide telemetry overlay (`SetTelemetryVisible`). |

#### `fx` Keys
- Booleans (`1`, `true`, `on` -> `true`; anything else -> `false`):
  `ssr`, `planar`, `gtao`, `contact`, `bloom`, `autoexp`, `ddgi`, `taa`, `hdr` (`HDRViewport`), `popup` (`PinRenderFXPopup`).
- Integers: `hiz=<level>`, `fog_quality=<0..3>`.
- Float: `fog_temporal=<weight>`.
- `fog=scene` (reset override to `-1`) or `fog=1|0|true|false` (`1` / `0`).

### Capture, Sync & Verification Commands (Render Phase)

| Command | Arguments | Effect |
| --- | --- | --- |
| `wait` | `<N>` | Wait `max(1, N)` `OnImGuiRender` frames for TAA/AutoExposure/DDGI convergence. *(Note: plan starts with an initial 2-frame warmup automatically.)* |
| `capture_editor` | `<file.png>` | Capture full composited SwapChain backbuffer (ImGui panels + Viewport) on the next present frame; logs `VISUAL_CAPTURE_EDITOR path=... size=WxH`. |
| `capture_viewport` | `<file.png>` | Capture raw `EditorMainViewport` color texture (`RGBA16_FLOAT` -> clamped `UNORM8` PNG), verify `nonFinite == 0`, log `VISUAL_CAPTURE_VIEWPORT path=... size=WxH nonFinite=... nonBlack=... min=... max=... meanLuma=...`. |
| `assert_no_errors` | *(none)* | Log `VISUAL_CONSOLE infos=... warnings=... errors=... okViews=... failedViews=...`; throws & exits non-zero if `errors != 0`, `failedViews != 0`, or `okViews == 0`. |
| `quit` | *(none)* | Close Editor cleanly (also triggered automatically at EOF). |

---

## 4. Worked Example Plan

Save as UTF-8 without BOM (e.g. `build/agent-runs/u5_recheck/plan.txt`):

```text
# Verify SSR ON vs OFF with RenderFXPopup & Inspector visible
output_dir Release_vulkan

scene d:/WorkSpace/AxleEngine/Tests/Fixtures/M15/M15_RoughnessSweep.axlescene
wait 4
select_entity Directional Light
fx ssr=1 popup=1
wait 4
capture_viewport ssr_on_vp.png
capture_editor ssr_on_editor.png

fx ssr=0 popup=1
wait 4
capture_viewport ssr_off_vp.png
capture_editor ssr_off_editor.png

assert_no_errors
quit
```

---

## 5. Verification Checklist & Known Limits

1. **Always check exit code and console log lines:** confirm `VISUAL_CONSOLE ... errors=0 okViews>=1 failedViews=0` (especially after switching RHI backends or materials).
2. **Inspect images directly, do not rely on byte size alone:** open captured PNGs to confirm reflections, shadows, exposure, silhouettes, and Inspector/Popup UI controls actually render cleanly. Pair `ssr_on_vp.png` vs `ssr_off_vp.png` (or any feature ON/OFF frames) when quantifying visual deltas (`meanAbsDiff`, `changedPct`), keeping in mind `capture_viewport` clamps HDR `> 1.0` to `255` in PNG output while preserving raw stats in stdout (`min`/`max`/`meanLuma`).
3. **Scope limits:**
   - Captures reflect GPU presentation output **before** Windows DWM composition and monitor ICC calibration.
   - `capture_editor` / `capture_viewport` plan execution has been exercised on `dx12` and `vulkan` Editor builds (OpenGL WGL is verified through test binaries such as `AxleEnvironmentRenderingTests.exe gl` and `AxleRenderServiceTests.exe gl`).
