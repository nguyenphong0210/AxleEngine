#pragma once
#include <cstdint>
#include <string_view>

namespace Axle {
    using u8  = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;

    using i8  = int8_t;
    using i16 = int16_t;
    using i32 = int32_t;
    using i64 = int64_t;

    using f32 = float;
    using f64 = double;

    // Standard Surface Type matching Unity & Unreal Material Models
    enum class SurfaceType : uint32_t {
        Opaque      = 0, // Solid geometry (Writes depth, no blending)
        Cutout      = 1, // Alpha testing / MASK (Foliage, fences, grates - writes depth if alpha >= cutoff)
        Transparent = 2, // Standard Alpha Blending (Glass, tinted acrylic - back-to-front sorted, no depth write)
        Additive    = 3  // Additive Blending (Neon glow, laser beams, fire particles, taillight bloom)
    };

    // Standard Rasterizer Face Culling Mode
    enum class CullMode : uint32_t {
        Back  = 0, // Default back-face culling
        Front = 1, // Front-face culling
        None  = 2  // Two-sided geometry (Foliage leaves, fabric, flags, thin glass)
    };

    inline const char* SurfaceTypeToString(SurfaceType type) {
        switch (type) {
            case SurfaceType::Opaque:      return "Opaque";
            case SurfaceType::Cutout:      return "Cutout";
            case SurfaceType::Transparent: return "Transparent";
            case SurfaceType::Additive:    return "Additive";
            default:                       return "Opaque";
        }
    }

    inline SurfaceType StringToSurfaceType(std::string_view str) {
        if (str == "Cutout" || str == "cutout") return SurfaceType::Cutout;
        if (str == "Transparent" || str == "transparent") return SurfaceType::Transparent;
        if (str == "Additive" || str == "additive") return SurfaceType::Additive;
        return SurfaceType::Opaque;
    }

    inline const char* CullModeToString(CullMode mode) {
        switch (mode) {
            case CullMode::Back:  return "Back";
            case CullMode::Front: return "Front";
            case CullMode::None:  return "None";
            default:              return "Back";
        }
    }

    inline CullMode StringToCullMode(std::string_view str) {
        if (str == "Front" || str == "front") return CullMode::Front;
        if (str == "None" || str == "none" || str == "TwoSided" || str == "twosided" || str == "Two-Sided") return CullMode::None;
        return CullMode::Back;
    }
}
