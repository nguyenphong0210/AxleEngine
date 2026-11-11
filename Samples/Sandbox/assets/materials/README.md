# Sandbox starter materials

Drag an `.axleasset` from this folder onto a mesh or its Material slot. These
ForwardPBR assets have embedded identities and no external texture dependencies.

| Asset | Surface | Metallic | Roughness |
| --- | --- | ---: | ---: |
| Metal_SteelPolished | Polished steel | 1 | 0.16 |
| Metal_AluminumSatin | Satin aluminum | 1 | 0.40 |
| Metal_Gold | Gold | 1 | 0.22 |
| Rubber_Black | Dark matte rubber | 0 | 0.85 |
| Rubber_Gray | Gray matte rubber | 0 | 0.75 |
| Glass_Clear | Clear glass | 0 | 0.04 |
| Glass_Tinted | Blue tinted glass | 0 | 0.09 |

Colors are linear shader values. Metals need direct light or explicitly enabled
Skybox Environment Lighting to show reflections; a visible sky alone adds no
lighting. None of these materials emits light.

Glass uses a transparent surface, depth writes off, queue 3000 and IOR 1.5.
The native D3D12/Vulkan renderer can use its existing screen-space refraction
path with a single-sample view and scene depth. Transparency sorting and
screen-space visibility limitations still apply; these are starter surface
presets rather than volumetric or ray-traced glass. Transparent surfaces do not
cast the conventional depth-map shadows. OpenGL keeps its existing
transparent compatibility appearance and does not reproduce native refraction.
