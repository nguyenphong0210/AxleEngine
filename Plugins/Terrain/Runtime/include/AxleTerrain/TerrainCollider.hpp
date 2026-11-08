#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Types.hpp>

namespace Axle::Terrain {

    class TerrainCollider {
    public:
        // Create static Jolt HeightField collision body
        static BodyID CreateTerrainCollider(
            const TerrainData& terrain,
            PhysicsScene& scene,
            f32 friction = 0.85f,
            f32 restitution = 0.05f
        );

        // Update heights or recreate body if necessary
        static BodyID UpdateTerrainCollider(
            BodyID bodyID,
            const TerrainData& terrain,
            PhysicsScene& scene,
            f32 friction = 0.85f,
            f32 restitution = 0.05f
        );

        // Remove and destroy body
        static void DestroyTerrainCollider(BodyID bodyID, PhysicsScene& scene);

        // Analytical raycast against terrain heightfield (useful in Editor when physics is stopped)
        static bool RaycastTerrain(
            const glm::vec3& rayOrigin,
            const glm::vec3& rayDir,
            const TerrainData& terrain,
            glm::vec3& outHitPoint,
            f32 maxDistance = 1000.0f,
            f32 stepSize = 0.5f
        );
    };

}
