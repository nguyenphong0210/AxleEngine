#pragma once
#include <glm/glm.hpp>
#include <cstddef>

namespace Axle::RHI {
    // b3, space0. Keep material data separate from the portable 128-byte object
    // push constants; every field has the same offset in ForwardPBR.hlsl.
    struct alignas(256) RHIMaterialConstants {
        glm::vec4 UVTransform{1.0f, 1.0f, 0.0f, 0.0f}; // scale.xy, offset.xy
        glm::vec4 Surface{1.0f, 1.0f, 1.0f, 1.0f};     // normal, AO, direct specular, environment reflection
        glm::vec4 CoatAnisotropy{0.0f, 0.04f, 0.0f, 0.0f}; // coat, coat roughness, anisotropy, rotation in turns
        glm::uvec4 Flags{0u}; // x: assigned texture bitmask, y: authored material (versus procedural mesh)
        glm::vec4 DetailUVTransform{1.0f,1.0f,0.0f,0.0f};
        glm::vec4 DetailParameters{1.0f,0.0f,0.0f,0.0f}; // x: normal strength
        glm::vec4 Refraction{1.0f,1.5f,0.1f,0.0f}; // strength, IOR, thickness, enabled
        glm::vec4 RefractionTint{1.0f};
        glm::vec4 RefractionFrame{}; // width, height, color/depth snapshot ready, reserved
        glm::vec4 Sheen{0.0f, 0.0f, 0.0f, -1.0f}; // rgb: sheen color, w: sheen roughness (>= 0 enabled)
        glm::vec4 Subsurface{0.0f, 0.0f, 0.0f, 0.0f}; // rgb: subsurface color, w: scatter radius / scale (> 0 enabled)
        glm::vec4 Absorption{0.0f, 0.0f, 0.0f, 0.0f}; // rgb: Beer-Lambert absorption coeff, w: Cauchy dispersion
        glm::vec4 Reserved[4]{};
    };
    static_assert(sizeof(RHIMaterialConstants) == 256);
    static_assert(offsetof(RHIMaterialConstants, Surface) == 16);
    static_assert(offsetof(RHIMaterialConstants, CoatAnisotropy) == 32);
    static_assert(offsetof(RHIMaterialConstants, Flags) == 48);
    static_assert(offsetof(RHIMaterialConstants, DetailUVTransform) == 64);
    static_assert(offsetof(RHIMaterialConstants, DetailParameters) == 80);
    static_assert(offsetof(RHIMaterialConstants, Refraction) == 96);
    static_assert(offsetof(RHIMaterialConstants, RefractionFrame) == 128);
    static_assert(offsetof(RHIMaterialConstants, Sheen) == 144);
    static_assert(offsetof(RHIMaterialConstants, Subsurface) == 160);
    static_assert(offsetof(RHIMaterialConstants, Absorption) == 176);
}
