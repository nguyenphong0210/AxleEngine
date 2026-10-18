#pragma once

#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Axle::Terrain {

    struct TerrainVertex {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        glm::vec2 TexCoord{ 0.0f };
        glm::vec4 SplatWeight{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

    struct TerrainChunk {
        u32 ChunkX = 0;
        u32 ChunkZ = 0;
        u32 StartGridX = 0;
        u32 EndGridX = 0;
        u32 StartGridZ = 0;
        u32 EndGridZ = 0;

        glm::vec3 BoundsMin{ 0.0f };
        glm::vec3 BoundsMax{ 0.0f };

        u32 CurrentLOD = 0; // 0 = Full, 1 = Step 2, 2 = Step 4
        bool NeedsRebuild = true;

        std::vector<TerrainVertex> Vertices;
        std::vector<u32> Indices;
        std::shared_ptr<VertexArray> VAO;

        glm::vec3 GetCenter() const {
            return (BoundsMin + BoundsMax) * 0.5f;
        }

        f32 GetDistanceTo(const glm::vec3& point) const {
            return glm::distance(GetCenter(), point);
        }
    };

}
