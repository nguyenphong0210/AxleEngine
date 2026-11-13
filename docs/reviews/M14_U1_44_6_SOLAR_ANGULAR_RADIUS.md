# M14-U1 — Phase 44.6 Sun Angular Radius Specular Expansion

## Status

Implemented and verified. The primary directional-light payload now carries the physical solar angular radius, defaulting to a 0.53 degree solar diameter. The Forward PBR sun lobe convolves GGX roughness with that finite source radius, while wider authored radii provide a bounded diagnostic control for fixture coverage.

## Changed Sources

- `Engine/Renderer/include/Axle/RenderWorld.hpp`
- `Engine/RHI/include/Axle/RHI/RHILight.hpp`
- `Engine/Resources/Shaders/ForwardPBR.hlsl`
- `Engine/Scene/src/RHISceneRenderer.cpp`
- `Tests/src/Test_EnvironmentRendering.cpp`
- `docs/milestones/MILESTONE_14_LIGHTING_MATERIALS/PLAN.md`

## Verification

- Debug build: `cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 6` — exit 0.
- Debug D3D12 native rendering fixture — exit 0.
- Debug Vulkan native rendering fixture — exit 0.
- Debug OpenGL policy fixture — exit 0.
- Release build: `cmake --build build --config Release --target AxleEnvironmentRenderingTests --parallel 6` — exit 0.
- Release D3D12, Vulkan, and OpenGL fixtures — exit 0 for each backend.
- Native regression verifies that changing the solar radius changes the direct metallic response while the physical default remains active.
- Existing HDR, procedural sky, cache, fallback, and validation assertions remain green.
