#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <AxleTerrain/TerrainChunk.hpp>
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <vector>

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

    class TerrainSculptor {
    public:
        // Apply brush deformation or texture painting at world center (centerX, centerZ)
        static SculptResult ApplyBrush(
            TerrainData& terrain,
            f32 centerX,
            f32 centerZ,
            const SculptSettings& settings,
            f32 dt = 0.016f
        );

        // Calculate falloff weight [0, 1] given distance and radius
        static f32 CalculateFalloff(f32 dist, f32 radius, BrushFalloff falloff);

        // Mark chunks that intersect the dirty region as needing geometry rebuild
        static void MarkDirtyChunks(
            std::vector<TerrainChunk>& chunks,
            const glm::vec3& dirtyMin,
            const glm::vec3& dirtyMax
        );
    };

}
