# Sandbox Viewport Rendering Diagnosis (D3D12 / ForwardPBR)

**Date:** 2026-11-12  
**Target:** Direct3D 12 Native Viewport & PBR Pipeline  
**Scene Fixture:** `Samples/Sandbox/assets/scenes/Test.axlescene`  
**Related Documents:** `docs/agents/WORKING_PROTOCOL.md`, `docs/reviews/SCENE_LIGHTING_PARITY.md`

---

## 1. Context & Setup Overview

In the Axle Editor running on Windows with the **Direct3D 12** RHI backend (`DXIL` shaders compiled via `DXC -Zpc`), the Sandbox scene (`Test.axlescene`) exhibits noticeable visual discrepancies:

| Entity | Transform | Components & Properties |
| :--- | :--- | :--- |
| **Atmospheric Skybox** | `T:[0, 0, 0]`, `R:[1,0,0,0]`, `S:[1,1,1]` | `SkyboxComponent`: `SkyboxType: 1 (HDRCubemap)`, `HDRPath: assets/lighting/Studio_Softbox.hdr`, `EnvironmentLighting: true`, `Intensity: 1.0` |
| **Plane** | `T:[0, 2, 0]`, `R:[1,0,0,0]`, `S:[20, 1, 20]` | `MeshInstance3D`: `builtin://plane`, `PatternMode: 1 (Checkerboard)`, `TileScale: 8.0`, `Color1: [0.35, 0.38, 0.42]`, `Color2: [0.20, 0.22, 0.25]` |
| **Directional Light** | `T:[0, 25, 0]`, `R:[1,0,0,0]`, `S:[1,1,1]` | `DirectionalLightComponent`: `Elevation: 50.0°`, `Azimuth: 45.0°`, `Color: [1, 0.98, 0.92]`, `Intensity: 1.0`, `AmbientIntensity: 0.28`, `CastShadows: true` |
| **Sphere** | `T:[0, 3, 0]`, `R:[1,0,0,0]`, `S:[1,1,1]` | `MeshInstance3D`: `builtin://sphere` (radius 1.0). `MaterialComponent`: `materials/Metal_Gold.axleasset` (`Metallic: 1.0`, `Roughness: 0.22`, `AlbedoColor: [1, 0.766, 0.336]`) |
| **Editor Camera** | `Pos:[0, 6, 18]`, `Rot:[-12°, 0°, 0°]` | `PerspectiveCamera`: looking down toward origin along $-Z$. |

---

## 2. Identified Defects & Technical Root Causes

### Defect 1: Inverted Specular Highlight (Illumination from Underneath)
* **Symptom:** The sun directional light has `Elevation = 50.0°` (high in the sky, overhead-front). However, on the sphere:
  * The top hemisphere is shaded dark grey.
  * The bright white specular highlight appears at the **lower-left hemisphere** (pixel coordinate offset $\Delta x < 0, \Delta y > 0$ relative to sphere center).
  * The sphere appears lit from beneath the floor rather than from the sky.
* **Root Cause Trace:**
  1. `Engine/Scene/include/Axle/Components.hpp` line 891:
     ```cpp
     glm::vec3 GetDirection() const {
         float radAz = glm::radians(Azimuth);
         float radEl = glm::radians(std::clamp(Elevation, -90.0f, 90.0f));
         return glm::normalize(glm::vec3(
             std::cos(radEl) * std::sin(radAz),
             std::sin(radEl),
             std::cos(radEl) * std::cos(radAz)
         ));
     }
     ```
     For $\text{El} = 50^\circ, \text{Az} = 45^\circ$, $\vec{L}_{\text{source}} = (+0.454, +0.766, +0.454)$ (points towards the sun in the upper-right-front).
  2. In `Engine/Scene/src/SceneExtraction.cpp` (lines 124–129) and `Engine/Scene/src/RHISceneRenderer.cpp` (lines 1150–1154):
     `lightData.SunLight.DirectionAndIlluminance = glm::vec4(dl.Direction, lux);`
  3. In `Engine/Resources/Shaders/ForwardPBR.hlsl` (lines 436–450):
     `float3 sunDir = normalize(u_SunLight.DirectionAndIlluminance.xyz);`
     `float NdotL_Sun = max(dot(N, sunDir), 0.0);`
     `float3 H = normalize(V + sunDir);`
  4. With camera view vector $\vec{V} = (0, +0.164, +0.986)$ and $\vec{L} = (+0.454, +0.766, +0.454)$, the true half-vector $\vec{H}$ has $+X$ (right) and $+Y$ (up).
  5. The rendered highlight matches $-\vec{L} = (-0.454, -0.766, -0.454)$, causing the specular peak $N \approx H$ to occur in the negative $X, Y$ quadrant. Verify whether `dl.Direction` sign convention is inverted in the RHI uniform pass or if normal handedness cofactor in `VSMain` interacts with camera view space conventions.

---

### Defect 2: Metal_Gold Material Appears Dull & Muddy (Missing IBL Reflections)
* **Symptom:** The sphere is assigned `Metal_Gold.axleasset` (`Metallic: 1.0`, `Roughness: 0.22`), which is a reflective gold metal. In the viewport, it renders as a flat, muddy olive/khaki plastic with zero metallic reflection.
* **Root Cause Trace:**
  1. In standard PBR, pure metals (`Metallic = 1.0`) have zero diffuse reflection ($\vec{k}_D = (1 - \text{metallic}) = 0$). All color and luminance originate from specular reflections of direct lights and environment maps (IBL).
  2. In `Engine/Scene/src/RHISceneRenderer.cpp` (lines 1199–1203):
     ```cpp
     bool useIBL = m_IBLAllowed && (m_Environment.Ready() || m_PreparedManualIBL) && lighting.EnvironmentLighting && world.Skybox.Type == 1;
     lightData.LightCounts.w = useIBL ? 1u : 0u;
     ```
  3. In `Engine/Scene/src/RHIEnvironment.cpp` (lines 68–88):
     `RHIEnvironment::Prepare()` inspects `sky.HDRPath`. When authored as a raw `.hdr` file (`assets/lighting/Studio_Softbox.hdr`), it attempts direct decode via `LoadRawImageSource()`. If the path is relative to the asset root or if baking through `BakeEnvironment()` fails/falls back, `m_Environment.Ready()` returns `nullptr`.
  4. When `useIBL == false`, `ForwardPBR.hlsl` falls back to the analytical ambient branch (lines 598–606):
     ```hlsl
     ambient = (diffuseAmbient + specularAmbient * (1 - saturate(u_LightReserved[0].z))) * max(u_LightReserved[0].y, 0.0) * ao;
     ```
     Without pre-filtered cubemap samples (`u_PrefilterMap`), the metal receives only flat ambient color, stripping away its metallic luster.

---

### Defect 3: Studio HDR Background Not Displayed
* **Symptom:** The user added `Studio_Softbox.hdr` to the project and configured `SkyboxType: 1` (`HDRCubemap`), but the viewport continues to draw the procedural Rayleigh & Mie atmospheric sky gradient.
* **Root Cause Trace:**
  1. In `Engine/Scene/src/RHISceneRenderer.cpp` (line 1450):
     ```cpp
     const bool hdrEnvironment = activeSkybox.Type == SkyboxType::HDRCubemap && bool(m_Environment.Ready());
     if (activeSkybox.Enabled && (activeSkybox.Type == SkyboxType::ProceduralAtmospheric || hdrEnvironment))
     ```
  2. If `m_Environment.Ready()` is null, `hdrEnvironment` is false.
  3. In `Engine/Resources/Shaders/AtmosphericSkybox.hlsl`:
     The HDR cubemap branch requires `u_Reserved[0].x > 0.5`. If `hdrEnvironment` is false, this uniform flag is not set, resulting in either procedural atmospheric sky evaluation or failure to present the studio environment.

---

### Defect 4: Ground Plane Clipping & Floating Sphere Illusion
* **Symptom:** The checkerboard floor cuts off abruptly with a horizontal edge across the bottom of the viewport, exposing a grey void. The sphere appears to float above the floor.
* **Root Cause Trace:**
  1. In `Test.axlescene`: `Plane` has `Translation: [0, 2, 0]` and `Scale: [20, 1, 20]`. The floor is elevated to $Y = 2.0$ instead of $Y = 0.0$.
  2. The editor camera is at `[0, 6, 18]` ($Z = 18$). The front edge of the plane ends at $Z = 10$, leaving an 8-unit empty gap between the camera and the floor.
  3. The sphere center is at `[0, 3, 0]` with radius 1.0 (bottom touches $Y = 2.0$). Due to the inverted specular highlight at the bottom and lack of contact ambient occlusion (SSAO), the contact point lacks grounding shadow contrast, producing an optical illusion that the sphere is hovering.

---

## 3. Recommended Remediation Plan for Codex

1. **Light Direction Convention Audit:**
   - Verify whether `DirectionalLightComponent::GetDirection()` represents $\vec{L}_{\text{to\_sun}}$ or $\vec{L}_{\text{from\_sun}}$.
   - Check `RHISceneRenderer.cpp` and `ForwardPBR.hlsl` line 443 (`dot(N, sunDir)`) and line 450 (`normalize(V + sunDir)`). Ensure identical vector orientation across D3D12, Vulkan, and OpenGL pipelines without breaking existing regression tests.
2. **HDR / IBL Environment Pipeline Audit:**
   - Trace `RHIEnvironment::Prepare()` in `Engine/Scene/src/RHIEnvironment.cpp` with `assets/lighting/Studio_Softbox.hdr`. Verify path resolution via `FileSystem::ResolveAssetPath()`.
   - Confirm whether `Studio_Softbox.hdr` (32x16 RGBE) decodes and bakes valid cubemap mip levels in `BakeEnvironment()`.
   - Verify `m_Environment.Ready()` and ensure `u_LightCounts.w = 1` activates the IBL path in `ForwardPBR.hlsl`.
3. **Scene Setup Adjustment (`Test.axlescene`):**
   - Ground the Plane at `Translation: [0, 0, 0]` and Sphere at `Translation: [0, 1, 0]` (or scale Plane to cover the camera frustum).
   - Ensure `SkyboxComponent` points to a verified `.hdr` or `.axleasset` cubemap descriptor.
4. **Verification:**
   - Run existing automated test suites:
     `ctest --test-dir build -C Debug -R '^Test_EnvironmentRendering$' --output-on-failure`
     `ctest --test-dir build -C Debug -R '^Test_MaterialRendering$' --output-on-failure`
