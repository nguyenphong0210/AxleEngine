# Phase 02 — Lighting and Material Fidelity Plan

**Status:** Released OPT-13 scope completed and accepted; future lighting and material fidelity units are queued.
**Supporting Detailed Plan:** [`docs/archive/design/deferred/ADVANCED_MATERIAL_FIDELITY_ROADMAP.md`](../../archive/design/deferred/ADVANCED_MATERIAL_FIDELITY_ROADMAP.md).

---

## 1. Completed Units (OPT-13 Baseline)

- [x] **C13-01: Cascaded Shadow Mapping (CSM)** — 4-cascade stabilization, PCF 5x5, D3D12/Vulkan native delivery.
- [x] **C13-02: HDR Environment & Split-Sum IBL** — Equirectangular cubemap baking, irradiance convolution, prefilter mips, BRDF LUT, UE-style lighting contract.
- [x] **C13-Local: Local Shadows & Materials** — Point/Spot light shadow atlas, conservative bounds culling, 7 starter materials (Metal, Rubber, Glass).
- [x] **C13-Viewport: Viewport Correction** — Corrected Sphere CCW winding order, grounded primitive creation ($Y=1.0$ for Sphere, $Y=0.0$ for Plane), and starter HDR scene connection.

---

## 2. Proposed Work Units (Fidelity Roadmap)

### Unit 2.1: Quick Wins — Energy & Skybox Reflection (Priority 1)
- [ ] **Multi-Bounce GGX Energy Compensation:** Integrate Kulla-Conty 2D LUT / analytical term into `ForwardPBR.hlsl`.
- [ ] **Dynamic Procedural Skybox IBL:** Render `Procedural Atmospheric Skybox` to a dynamic cubemap and compute Irradiance + Prefilter mips so procedural skies provide real IBL reflections.
- [ ] **Sun Angular Radius Expansion:** Expand Directional Light specular lobe based on solar angular size ($0.53^\circ$).

### Unit 2.2: Contact Occlusion & Grounding (Priority 2)
- [ ] **GTAO (Ground-Truth Ambient Occlusion):** Horizon-based integration compute pass with bilateral spatial denoiser.
- [ ] **Screen-Space Contact Shadows:** Raymarched micro-shadows for directional sun light.

### Unit 2.3: Local Reflections & Area Lights (Priority 3)
- [ ] **Hi-Z Depth Pyramid Generation:** Hierarchical depth buffer generator for screen-space raymarching.
- [ ] **Hi-Z Screen-Space Reflections (SSR):** Roughness cone-tracing reflection pass for local geometry and floors.
- [ ] **Parallax-Corrected Reflection Probes:** Local box/sphere reflection probes blending seamlessly with SSR fallback.
- [ ] **LTC Area Lights:** Linearly Transformed Cosines evaluation for Rect and Disk studio area lights.

### Unit 2.4: Cinematic Post-Processing (Priority 4)
- [ ] **Pyramid Physically Based Bloom:** 6-tier downsample/upsample Kawase pyramid.
- [ ] **Histogram Auto-Exposure:** Compute-based log-luminance metering with smooth temporal eye adaptation.
- [ ] **Temporal Anti-Aliasing (TAA):** Halton jittering, history reprojection, and neighborhood clamping.

### Unit 2.5: Complex Shading Models (Priority 5)
- [ ] **Screen-Space Subsurface Scattering (SSS):** Separable Gaussian diffusion profiles.
- [ ] **Sheen & Velvet BRDF:** Charlie micro-fiber retro-reflection model for cloth.
- [ ] **Volumetric Absorption:** Beer-Lambert physical attenuation for deep glass and liquids.

---

## 3. Review and Evidence Requirements

1. **Physical GPU Verification:** Native D3D12 and Vulkan rendering must be verified on hardware adapters with zero validation layer errors.
2. **Parity Testing:** Each new shading pass must maintain automated regression tests comparing off/on states.
3. **Explicit Fallbacks:** When running on unsupported hardware or OpenGL, features must degrade gracefully with diagnostics rather than crashing.
