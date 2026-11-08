#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle::Terrain {

    enum class BrushMode {
        Raise = 0,
        Lower,
        Smooth,
        Flatten,
        PaintTexture
    };

    enum class BrushFalloff {
        Linear = 0,
        Smoothstep,
        Spherical
    };

    struct SculptSettings {
        BrushMode Mode = BrushMode::Raise;
        BrushFalloff Falloff = BrushFalloff::Smoothstep;
        f32 Radius = 12.0f;
        f32 Strength = 6.0f;
        f32 TargetHeight = 0.0f;
        u32 PaintLayer = TerrainSplatLayers::DIRT;
    };

    struct SculptResult {
        bool Modified = false;
        glm::vec3 DirtyMin{ 0.0f };
        glm::vec3 DirtyMax{ 0.0f };
    };

} // namespace Axle::Terrain
