#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Axle::MaterialParameterBounds {
    inline constexpr float MinAlphaCutoff = 0.0f;
    inline constexpr float MaxAlphaCutoff = 1.0f;
    inline constexpr float DefaultAlphaCutoff = 0.5f;

    inline constexpr float MinScalarRoughness = 0.04f;
    inline constexpr float MaxRoughness = 1.0f;
    inline constexpr float DefaultRoughness = 0.5f;

    inline constexpr float MinMetallic = 0.0f;
    inline constexpr float MaxMetallic = 1.0f;
    inline constexpr float DefaultMetallic = 0.0f;

    inline constexpr float MinAO = 0.0f;
    inline constexpr float MaxAO = 1.0f;
    inline constexpr float DefaultAO = 1.0f;

    inline constexpr float MinNormalStrength = 0.0f;
    inline constexpr float MaxNormalStrength = 5.0f;
    inline constexpr float DefaultNormalStrength = 1.0f;

    inline constexpr float MinDetailNormalStrength = 0.0f;
    inline constexpr float MaxDetailNormalStrength = 4.0f;
    inline constexpr float DefaultDetailNormalStrength = 1.0f;

    inline constexpr float MinClearcoatStrength = 0.0f;
    inline constexpr float MaxClearcoatStrength = 1.0f;
    inline constexpr float MinClearcoatRoughness = 0.001f;
    inline constexpr float MinClearcoatRoughnessUI = 0.04f;
    inline constexpr float MaxClearcoatRoughness = 1.0f;
    inline constexpr float DefaultClearcoatRoughness = 0.04f;

    inline constexpr float MinAnisotropyStrength = -1.0f;
    inline constexpr float MaxAnisotropyStrength = 1.0f;
    inline constexpr float MinAnisotropyRotation = 0.0f;
    inline constexpr float MaxAnisotropyRotation = 1.0f;

    inline constexpr float MinRefractionStrength = 0.0f;
    inline constexpr float MaxRefractionStrength = 1.0f;
    inline constexpr float DefaultRefractionStrength = 1.0f;
    inline constexpr float MinRefractionIOR = 1.0f;
    inline constexpr float MaxRefractionIOR = 3.0f;
    inline constexpr float DefaultRefractionIOR = 1.5f;
    inline constexpr float MinRefractionThickness = 0.0f;
    inline constexpr float MaxRefractionThickness = 100.0f;
    inline constexpr float DefaultRefractionThickness = 0.1f;

    inline constexpr float MinSheenRoughness = 0.01f;
    inline constexpr float MaxSheenRoughness = 1.0f;
    inline constexpr float DefaultSheenRoughness = 0.5f;

    inline constexpr float MinSubsurfaceRadius = 0.0f;
    inline constexpr float MaxSubsurfaceRadius = 16.0f;
    inline constexpr float DefaultSubsurfaceRadius = 1.0f;

    inline constexpr float MinDispersion = 0.0f;
    inline constexpr float MaxDispersion = 1.0f;

    inline constexpr float MinEmissionIntensity = 0.0f;
    inline constexpr float MaxEmissionIntensity = 10000.0f;

    inline constexpr int MinRenderQueue = 1000;
    inline constexpr int MaxRenderQueue = 5000;
    inline constexpr int DefaultRenderQueue = 2000;

    inline float ClampFinite(float v, float lo, float hi, float fallback) {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    }

    inline float ClampMinFinite(float v, float lo, float fallback) {
        return std::isfinite(v) ? std::max(v, lo) : fallback;
    }

    inline glm::vec2 SanitizeVec2(const glm::vec2& v, const glm::vec2& fallback = glm::vec2(0.0f)) {
        return {
            std::isfinite(v.x) ? v.x : fallback.x,
            std::isfinite(v.y) ? v.y : fallback.y
        };
    }

    inline glm::vec3 ClampVec3(const glm::vec3& v, float lo, float hi, const glm::vec3& fallback) {
        return {
            ClampFinite(v.x, lo, hi, fallback.x),
            ClampFinite(v.y, lo, hi, fallback.y),
            ClampFinite(v.z, lo, hi, fallback.z)
        };
    }

    inline glm::vec3 ClampMinVec3(const glm::vec3& v, float lo, const glm::vec3& fallback = glm::vec3(0.0f)) {
        return {
            ClampMinFinite(v.x, lo, fallback.x),
            ClampMinFinite(v.y, lo, fallback.y),
            ClampMinFinite(v.z, lo, fallback.z)
        };
    }

    inline glm::vec4 SanitizeAlbedo(const glm::vec4& v) {
        return {
            ClampMinFinite(v.r, 0.0f, 1.0f),
            ClampMinFinite(v.g, 0.0f, 1.0f),
            ClampMinFinite(v.b, 0.0f, 1.0f),
            ClampFinite(v.a, 0.0f, 1.0f, 1.0f)
        };
    }
} // namespace Axle::MaterialParameterBounds

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
