#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <AxleTerrain/TerrainChunk.hpp>
#include <AxleTerrain/TerrainSculptTypes.hpp>
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Axle::Terrain {

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

} // namespace Axle::Terrain
