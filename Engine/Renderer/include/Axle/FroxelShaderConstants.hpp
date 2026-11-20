#pragma once

#include <glm/glm.hpp>

namespace Axle::FroxelFogContract {

    // Shared 512-byte (256-byte CBV aligned) shader constant block matching VolumetricFog.hlsl
    struct alignas(256) FroxelShaderConstants {
        glm::mat4 InvView{1.0f};
        glm::mat4 InvProjection{1.0f};
        glm::vec4 ScreenSize{};              // Width, height, reciprocal width and height
        glm::vec4 SunDirectionIntensity{};   // Direction toward light and enabled flag
        glm::vec4 SunRadiance{};
        glm::vec4 FogParams{};               // Effective density, height falloff, anisotropy
        glm::vec4 FogAlbedo{};
        glm::vec4 FroxelGridParams{};        // Grid extent and orthographic flag
        glm::vec4 AmbientRadiance{};
        glm::vec4 FroxelDepthParams{};       // Log depth ratio, near and far
        glm::mat4 PrevViewProjection{1.0f};
        glm::vec4 PrevViewDepthRow{};
        glm::vec4 TemporalParams{};          // History weight (zero for seed frames)
    };
    static_assert(sizeof(FroxelShaderConstants) == 512, "FroxelShaderConstants must be 512 bytes aligned!");

} // namespace Axle::FroxelFogContract
