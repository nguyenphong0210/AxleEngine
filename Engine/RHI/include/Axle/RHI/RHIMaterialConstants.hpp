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
        glm::vec4 Reserved[12]{};
    };
    static_assert(sizeof(RHIMaterialConstants) == 256);
    static_assert(offsetof(RHIMaterialConstants, Surface) == 16);
    static_assert(offsetof(RHIMaterialConstants, CoatAnisotropy) == 32);
    static_assert(offsetof(RHIMaterialConstants, Flags) == 48);
}
