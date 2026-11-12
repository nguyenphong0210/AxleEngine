# Milestone 14 — Lighting and Material Fidelity Plan

**Status:** Phase 44 Baseline (formerly OPT-13 Baseline) completed and accepted; future lighting and material fidelity phases are queued.
**Supporting Architectural Document:** [`ARCHITECTURE.md`](ARCHITECTURE.md).
**Supporting Detailed Plan:** [`docs/archive/design/deferred/ADVANCED_MATERIAL_FIDELITY_ROADMAP.md`](../../archive/design/deferred/ADVANCED_MATERIAL_FIDELITY_ROADMAP.md).

---

## 1. Executive Summary

Milestone 14 elevates AxleEngine's lighting, shading, and material systems from foundational Cook-Torrance Forward+ PBR to cinematic-grade photorealism (AAA fidelity). It builds upon the verified Phase 44 baseline (CSM, HDR/IBL, Local Shadows, and Viewport Fixture Grounding) and defines the roadmap for real-time global illumination (DDGI), volumetric atmosphere, contact occlusion, and physically based surface models.

---

## 2. Completed Work Packages (Phase 44 Baseline, formerly OPT-13)

- [x] **Phase 44.1: Cascaded Shadow Mapping (CSM)** — 4-cascade camera-space split, texel stabilization, depth bias, PCF 5x5 sampling across D3D12 and Vulkan, with OpenGL fallback (`C13-01`).
- [x] **Phase 44.2: HDR Environment & Split-Sum IBL** — Equirectangular HDR cubemap loading, irradiance convolution, prefilter roughness mip chain, and 2D split-sum BRDF LUT integration (`C13-02`).
- [x] **Phase 44.3: Local Shadows & Viewport Fixture Correction** — Point and Spot light shadow atlas, conservative bounds culling, corrected Sphere CCW winding order, grounded primitive creation ($Y=1.0$ for Sphere, $Y=0.0$ for Plane), and starter HDR scene connection (`Studio_Softbox.hdr`).
- *Evidence:* [`docs/reviews/OPT_13_C13_01_NATIVE_DELIVERY.md`](../../reviews/OPT_13_C13_01_NATIVE_DELIVERY.md), [`OPT_13_C13_02_DELIVERY.md`](../../reviews/OPT_13_C13_02_DELIVERY.md), [`OPT_13_LOCAL_SHADOWS_DELIVERY.md`](../../reviews/OPT_13_LOCAL_SHADOWS_DELIVERY.md).

---

## 3. Pending Work Packages (Fidelity Roadmap)

- [ ] **Phase 44.4: Multi-Bounce GGX Energy Compensation** — Integrate Kulla-Conty 2D LUT / analytical energy compensation term into `ForwardPBR.hlsl` to eliminate darkening on rough metals.
- [ ] **Phase 44.5: Dynamic Procedural Skybox IBL** — Render `Procedural Atmospheric Skybox` to a dynamic $128 \times 128 \times 6$ cubemap and compute Irradiance + Prefilter mips so procedural skies provide real IBL reflections.
- [ ] **Phase 44.6: Sun Angular Radius Specular Expansion** — Expand Directional Light specular lobe based on actual solar angular size ($0.53^\circ$), softening the sun reflection hot spot.
- [ ] **Phase 44.7: GTAO (Ground-Truth Ambient Occlusion)** — Implement horizon-based hemispherical integration compute pass at half-resolution ($960 \times 540$) with bilateral spatial reconstruction denoiser.
- [ ] **Phase 44.8: Screen-Space Contact Shadows** — Implement short-range raymarched depth contact shadows for directional sun light, eliminating shadow acne and Peter Panning.
- [ ] **Phase 44.9: Hi-Z Depth Pyramid & Screen-Space Reflections (SSR)** — Hierarchical depth buffer generator pass computing min-depth mips for screen-space raymarching and roughness cone-tracing.
- [ ] **Phase 44.10: Parallax-Corrected Reflection Probes** — Local box and sphere reflection cubemaps with smooth falloff and seamless blending with SSR fallback.
- [ ] **Phase 44.11: LTC Area Lights** — Linearly Transformed Cosines evaluation for Rectangular and Disk studio area lights.
- [ ] **Phase 44.12: Volumetric Fog & God Rays (formerly C13-04)** — Frustum Voxel Grid (*Froxel* $160 \times 90 \times 64$) with in-scattering, Mie phase function, and temporal reprojection for cinematic sun shafts.
- [ ] **Phase 44.13: Dynamic Diffuse Global Illumination (DDGI Probes, formerly C13-05)** — 3D axis-aligned probe grid ($32 \times 16 \times 32$), dual octahedral texture atlases (irradiance $8 \times 8$ and distance/variance $16 \times 16$), Chebyshev visibility, and temporally staggered ray updates (25% probes/frame).
- [ ] **Phase 44.14: Film-Grade Post-Processing (formerly C13-06)** — 6-tier Kawase downsample/upsample physically based bloom, compute log-luminance histogram auto-exposure with smooth eye adaptation, and ACES Filmic tonemapping curve.
- [ ] **Phase 44.15: Temporal Anti-Aliasing (TAA)** — Halton sub-pixel jittering, history reprojection, and YCoCg neighborhood clamping to eliminate specular shimmer.
- [ ] **Phase 44.16: Complex Shading Models** — Screen-Space Subsurface Scattering (SSS) with separable Gaussian diffusion profile, Charlie micro-fiber retro-reflection model for cloth/velvet/suede, and Beer-Lambert volumetric absorption with Cauchy chromatic dispersion for thick glass and liquids.
- [ ] **Phase 44.17: Final Lighting Acceptance & Performance Budget Verification (formerly C13-07)** — Comprehensive budget verification maintaining $3.5 - 5.0\text{ ms}$ GPU lighting time on mainstream PC hardware (1080p/1440p @ 120-144+ FPS).

---

## 4. Review and Evidence Requirements

1. **Physical GPU Verification:** Native D3D12 and Vulkan rendering must be verified on hardware adapters with zero validation layer errors.
2. **Parity Testing:** Each new shading pass must maintain automated regression tests comparing off/on states.
3. **Explicit Fallbacks:** When running on unsupported hardware or OpenGL, features must degrade gracefully with diagnostics rather than crashing.
