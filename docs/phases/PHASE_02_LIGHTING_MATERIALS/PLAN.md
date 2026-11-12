# Milestone 02 — Lighting and Material Fidelity Plan

**Status:** Released OPT-13 scope completed and accepted; future lighting and material fidelity units are queued.
**Supporting Architectural Document:** [ARCHITECTURE.md](ARCHITECTURE.md).
**Supporting Detailed Plan:** [docs/archive/design/deferred/ADVANCED_MATERIAL_FIDELITY_ROADMAP.md](../../archive/design/deferred/ADVANCED_MATERIAL_FIDELITY_ROADMAP.md).

---

## 1. Executive Summary

Phase 02 elevates AxleEngine\'s lighting, shading, and material systems from foundational Cook-Torrance Forward+ PBR to cinematic-grade photorealism (AAA fidelity). It builds upon the verified OPT-13 foundation (CSM, HDR/IBL, Local Shadows, and Viewport Fixture Grounding) and defines the roadmap for real-time global illumination (DDGI), volumetric atmosphere, contact occlusion, and physically based surface models.

---

## 2. Completed Work Packages (OPT-13 Baseline)

- [x] **C13-01: Cascaded Shadow Mapping (CSM)** — 4-cascade camera-space split, texel stabilization, depth bias, PCF 5x5 sampling across D3D12 and Vulkan, with OpenGL fallback.
- [x] **C13-02: HDR Environment & Split-Sum IBL** — Equirectangular HDR cubemap loading, irradiance convolution, prefilter roughness mip chain, and 2D split-sum BRDF LUT integration.
- [x] **C13-Local: Local Shadows & Materials** — Point and Spot light shadow atlas, conservative bounds culling, and 7 starter materials (Metal, Rubber, Glass) in Sandbox.
- [x] **C13-Viewport: Viewport Fixture Correction** — Corrected Sphere CCW winding order, grounded primitive creation (=1.0$ for Sphere, =0.0$ for Plane), and starter HDR scene connection (Studio_Softbox.hdr).
- *Evidence:* [docs/reviews/OPT_13_C13_01_NATIVE_DELIVERY.md](../../reviews/OPT_13_C13_01_NATIVE_DELIVERY.md), [OPT_13_C13_02_DELIVERY.md](../../reviews/OPT_13_C13_02_DELIVERY.md), [OPT_13_LOCAL_SHADOWS_DELIVERY.md](../../reviews/OPT_13_LOCAL_SHADOWS_DELIVERY.md).

---

## 3. Pending Work Units (Fidelity Roadmap)

### Unit 2.1: Multi-Bounce GGX Energy Compensation (Priority 1)
- [ ] Integrate Kulla-Conty 2D LUT / analytical energy compensation term into ForwardPBR.hlsl to eliminate darkening on rough metals.

### Unit 2.2: Dynamic Procedural Skybox IBL (Priority 1)
- [ ] Render Procedural Atmospheric Skybox to a dynamic  \times 128 \times 6$ cubemap and compute Irradiance + Prefilter mips so procedural skies provide real IBL reflections.

### Unit 2.3: Sun Angular Radius Specular Expansion (Priority 1)
- [ ] Expand Directional Light specular lobe based on actual solar angular size (.53^\circ$), softening the sun reflection hot spot.

### Unit 2.4: GTAO (Ground-Truth Ambient Occlusion) (Priority 2)
- [ ] Implement horizon-based hemispherical integration compute pass at half-resolution ( \times 540$) with bilateral spatial reconstruction denoiser.

### Unit 2.5: Screen-Space Contact Shadows (Priority 2)
- [ ] Implement short-range raymarched depth contact shadows for directional sun light, eliminating shadow acne and Peter Panning.

### Unit 2.6: Hi-Z Depth Pyramid Generation (Priority 3)
- [ ] Hierarchical depth buffer generator pass computing min-depth mips for screen-space raymarching and GPU culling.

### Unit 2.7: Hi-Z Screen-Space Reflections (SSR) (Priority 3)
- [ ] Roughness cone-tracing reflection compute pass across Hi-Z depth mip chains for local geometry and floor reflections.

### Unit 2.8: Parallax-Corrected Reflection Probes (Priority 3)
- [ ] Local box and sphere reflection cubemaps with smooth falloff and seamless blending with SSR fallback.

### Unit 2.9: LTC Area Lights (Priority 3)
- [ ] Linearly Transformed Cosines evaluation for Rectangular and Disk studio area lights.

### Unit 2.10: Volumetric Fog & God Rays (C13-04 / Froxel Grid) (Priority 4)
- [ ] Frustum Voxel Grid (*Froxel*  \times 90 \times 64$) with in-scattering, Mie phase function, and temporal reprojection for cinematic sun shafts.

### Unit 2.11: Dynamic Diffuse Global Illumination (C13-05 / DDGI Probes) (Priority 4)
- [ ] 3D axis-aligned probe grid ( \times 16 \times 32$), dual octahedral texture atlases (irradiance  \times 8$ and distance/variance  \times 16$), Chebyshev visibility, and temporally staggered ray updates (25% probes/frame).

### Unit 2.12: Cinematic Post-Processing (C13-06) (Priority 4)
- [ ] 6-tier Kawase downsample/upsample physically based bloom.
- [ ] Compute-based log-luminance histogram auto-exposure with smooth eye adaptation.
- [ ] ACES Filmic tonemapping curve.

### Unit 2.13: Temporal Anti-Aliasing (TAA) (Priority 4)
- [ ] Halton sub-pixel jittering, history reprojection, and YCoCg neighborhood clamping to eliminate specular shimmer.

### Unit 2.14: Complex Shading Models (Priority 5)
- [ ] Screen-Space Subsurface Scattering (SSS) with separable Gaussian diffusion profile.
- [ ] Charlie micro-fiber retro-reflection model for cloth, velvet, and suede.
- [ ] Beer-Lambert volumetric absorption ( = e^{-\\sigma_a \\cdot d}$) and chromatic dispersion for thick glass and liquids.

### Unit 2.15: Final Acceptance & Budget Verification (C13-07)
- [ ] Comprehensive budget verification maintaining .5 - 5.0\text{ ms}$ GPU lighting time on mainstream PC hardware (1080p/1440p @ 120-144+ FPS).

---

## 4. Review and Evidence Requirements

1. **Physical GPU Verification:** Native D3D12 and Vulkan rendering must be verified on hardware adapters with zero validation layer errors.
2. **Parity Testing:** Each new shading pass must maintain automated regression tests comparing off/on states.
3. **Explicit Fallbacks:** When running on unsupported hardware or OpenGL, features must degrade gracefully with diagnostics rather than crashing.
