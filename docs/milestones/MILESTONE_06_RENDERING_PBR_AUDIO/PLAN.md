# Milestone 06 — High-Fidelity Rendering, PBR & Audio Synthesis Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [x] **Phase 24: Decoupled Material Asset System & Flyweight Architecture (`Engine/Renderer`, `Engine/Asset` & `Engine/Scene`)**
  - [x] Core `Axle::Material` Asset Class encapsulating PBR properties and texture maps.
  - [x] Centralized Asset Caching in `AssetManager` (`TextureCache` & `MaterialCache`) eliminating duplicate GPU texture allocations.
  - [x] Flyweight `MaterialComponent` in ECS storing 8-byte `UUID MaterialHandle` with `MakeUnique()` instance overrides.
- [x] **Phase 25: AAA Soft Shadows & Precision Lighting Pipeline (`Engine/Renderer`, `Engine/Scene` & `Engine/UI`)**
  - [x] Hardware Bilinear PCF (`GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE`).
  - [x] 16-Tap Vogel Spiral / Poisson Disk soft shadows with interleaved gradient noise.
  - [x] Dynamic Ortho Size, Shadow Distance, and Bias in `DirectionalLightComponent`.
- [x] **Phase 26: HDR Skybox Cubemap & Image-Based Lighting (IBL) Pipeline (`Engine/Renderer`, `Engine/Scene` & `Engine/UI`)**
  - [x] HDR Image Loader & Equirectangular-to-Cubemap Converter (`.hdr` / `.exr`).
  - [x] Image-Based Lighting (IBL) Convolution: Diffuse Irradiance Map, Specular Pre-filtered Environment Map, and 2D BRDF Integration LUT.
  - [x] Hyper-realistic metallic reflections on PBR materials matching surrounding environment.
- [x] **Phase 27: High-Fidelity 3D Audio System & Vehicle Acoustics (`Engine/Audio`)**
  - [x] Core Audio Subsystem (`miniaudio.h` integration): `AudioEngine`, `AudioClip`, `AudioSource`, `AudioListener`.
  - [x] ECS Audio Components: `AudioListenerComponent` and `AudioSourceComponent`.
  - [x] Vehicle Audio Synthesizer: Granular engine RPM synthesizer, tire squeal & slip acoustics, suspension travel thuds, and collision impact acoustics.
- [x] **Phase 28: Gameplay Polish, Multi-Part Vehicle Rigging & Dynamic HUD (`Engine/Vehicle`, `Engine/UI` & Content)**
  - [x] **Step 1 - Multi-Part 3D Car Rigging & Assembly Pipeline**: Modular chassis body mesh, 4 separate wheel meshes, visual kinematics (`AutoSyncMesh`), and dynamic lighting (headlights, brake lights, reverse lights).
  - [x] **Step 2 - Multi-Angle Dynamic Vehicle Camera Controller**: Spring-Arm Chase Camera, Cockpit First-Person Driver POV, Hood Camera, Free Orbit Camera, and smooth transition interpolation.
  - [x] **Step 3 - Real-Time Racing HUD & Dashboard Instrumentation**: Analog/circular Tachometer, Digital Speedometer (km/h & mph), Gear Indicator, G-Force meter, and input telemetry meters.
  - [x] **Step 4 - Scene Integration & Visual Polish**: Completed `CarTest.axlescene` and `VehicleSandbox.axlescene` automotive testing playground (Launch Ramp, Landing Pad, Washboard Bumps, Slalom Cones, Destructible Crates Pyramid), with Cabin Acoustic Insulation (Cockpit volume attenuation).
  - [x] **Step 5 - Automated Test Suite Verification**: Step 32 test in `Editor/src/Test.cpp` validating multi-part rigging, kinematics, lighting, camera modes, HUD rendering, cabin acoustics, and sandbox deserialization.
  - [x] **Step 6 - Runtime HUD Canvas & Public API Hardening (Fast-Follow)**:
    - [x] Deploy Direction A: In-game transparent HUD canvas overlay (`##GameHUDCanvas`) in `AxleRuntime.exe` via `Axle::ImGuiLayer`.
    - [x] Standardize `RacingHUD.hpp` on 100% public ImGui APIs (eliminate `#include <imgui_internal.h>` and private struct access).
    - [x] Fix input edge cases: Shift+F1 physics wireframe toggle guard, Esc cursor unlock vs scene stop frame consumption, and mouse scroll Y accumulation.

---
