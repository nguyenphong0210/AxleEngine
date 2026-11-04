# Screen-space material refraction

> **Deferred reference:** outside the active architecture cleanup. Follow the
> [optimization roadmap](../../../ROADMAP.md); embedded prompts and old priorities
> are historical guidance, not a current assignment. Recheck APIs before resuming.

Use **Surface / Transparency > Transparent**, disable depth writes, then enable
**Refraction**. IOR 1.5 is a useful starting point for automotive glass. Strength
scales displacement; thickness is a distance in scene units. Tint attenuates
transmitted linear RGB. Alpha controls the ordinary transparent fallback.

The forward renderer draws opaque/cutout geometry and the sky, copies color and
depth into separate textures, then draws the transparent queue. A refracted view
ray is projected through the material thickness to offset screen coordinates.
Sampling clamps at screen edges. If the distorted location contains opaque depth
in front of the glass, the shader samples the undistorted location instead.
IOR controls Schlick Fresnel reflection versus tinted transmission. Refraction
outputs the complete composite, so its alpha is one when the snapshot is usable.

This is a single-interface, opaque-only approximation. It does not trace back
faces, reconstruct hidden surfaces, refract other transparent objects, or model
multiple internal reflections. Thickness is authored rather than inferred from
geometry. Roughness affects the surface reflection, not transmission blur.

MSAA and missing color/depth inputs use ordinary alpha blending. A transparent
material-preview clear also falls back to alpha, since the ImGui checkerboard
is composed after the 3D pass and is absent from the scene-color texture. Use
the reference scene with opaque geometry behind glass to inspect distortion.
Snapshots retain the active SDR/HDR format, resize with the viewport, and are
created/copied only when a transparent material requests refraction.

Step 106 checks asset/scene/RHI persistence, strength, IOR=1, tint, the MSAA
fallback and resizing with actual rendered images. DXIL and SPIR-V compilation
cover the same shader source.
