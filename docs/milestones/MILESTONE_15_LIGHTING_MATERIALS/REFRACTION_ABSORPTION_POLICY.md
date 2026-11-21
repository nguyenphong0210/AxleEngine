# Optical Absorption & Chromatic Dispersion Policy (Milestone 15 Unit 9)

## 1. Beer-Lambert Volumetric Absorption (`M15-U9-02`)

Transparent refractive surfaces attenuate transmitted scene radiance along the internal optical ray according to the Beer-Lambert law:

$$T(\lambda) = \exp\!\bigl(-\sigma(\lambda)\, d\bigr)$$

where:
- **Absorption Coefficient ($\sigma \ge 0$)** comes from `u_Absorption.rgb` (`AbsorptionColor`, clamped component-wise to $[0, \infty)$ so zero or negative coefficients never amplify transmitted radiance). When `AbsorptionEnabled == false`, $\sigma = (0, 0, 0)$ and $T = (1, 1, 1)$.
- **Thickness ($t \ge 0$)** comes from `u_Refraction.z` (`RefractionThickness`, clamped to $[0, 100]$ world units). When $t = 0$, $d = 0$ and $T = (1, 1, 1)$.
- **Snell Optical Path Length ($d$)**:
  Using base index of refraction $\eta = 1 / n_G$ where $n_G = \operatorname{clamp}(\text{RefractionIOR}, 1.0, 3.0)$, the transmitted cosine obeys Snell's law:
  $$\cos\theta_t = \sqrt{\max\!\bigl(0,\; 1 - \eta^2 \max(0, 1 - (N \cdot V)^2)\bigr)}$$
  To avoid unbounded grazing-edge darkening or singularities on low-tessellation silhouettes, the effective transmission cosine is clamped by `kMinTransmissionCos = 0.20` (capping path stretch at $5\times$ authored thickness):
  $$d = \frac{t}{\max(\cos\theta_t,\; 0.20)}$$

In legacy SDR forward mode (`u_CameraPosition.w <= 0`), the opaque color copy has already passed through the forward display transform. The shader removes its 2.2 gamma before applying $T$ in the SDR working space. The HDR path applies $T$ to scene-linear radiance.

## 2. Cauchy Wavelength Dispersion (`M15-U9-03`)

Chromatic dispersion models wavelength-dependent indices of refraction at representative red, green, and blue wavelengths ($\lambda_R = 0.65\,\mu\text{m},\; \lambda_G = 0.55\,\mu\text{m},\; \lambda_B = 0.45\,\mu\text{m}$):

$$n(\lambda) = n_G + B_{\text{eff}} \left(\frac{1}{\lambda^2} - \frac{1}{\lambda_G^2}\right)$$

- `Dispersion` is clamped to $[0, 1]$ on CPU/GPU packing (`u_Absorption.w`, forwarded whenever `RefractionEnabled` or `AbsorptionEnabled` is active).
- When $n_G \le 1.001$ (vacuum/air), $B_{\text{eff}} = 0$ so no spurious dispersion occurs without refraction.
- Otherwise $B_{\text{eff}} = 0.25 \cdot \operatorname{clamp}(\text{Dispersion}, 0, 1)$, giving $n_R \le n_G \le n_B$ (`iorR = clamp(iorG - 0.9389 * B_eff, 1.0, 3.5)`, `iorB = clamp(iorG + 1.6325 * B_eff, 1.0, 3.5)`).
- Three independent Snell refracted rays (`ComputeSafeRefractRay`) project screen-space offsets for R, G, and B channels, each guarded against foreground opaque depth occluders (`u_OpaqueSceneDepth`).

## 3. Scope & Screen-Space Limits vs Thick Traced Glass

This real-time forward implementation operates under explicit **single-bounce screen-space refraction limits**:
1. **Opaque background only:** Refraction samples `u_OpaqueSceneColor` / `u_OpaqueSceneDepth`; transparent objects behind other transparent objects are not double-refracted.
2. **Single front-interface trace + thickness parameter:** Rays bend once using front-face normal `N` and `RefractionIOR`; exit curvature and internal multi-bounce reflections are represented analytically by authored `RefractionThickness` and Fresnel blending (`fresnel`), rather than ray-marching mesh backfaces or BVH geometry.
3. **Screen-edge clamping & occluder protection:** Refracted sample UVs are clamped to valid half-texel screen bounds; off-screen scene geometry cannot appear, and samples falling on closer foreground opaque geometry revert to unrefracted `screenUV`.
4. **Opaque/Cutout fallback:** Non-transparent surfaces (`SurfaceType != Transparent`) ignore refraction, absorption, and dispersion identically.
