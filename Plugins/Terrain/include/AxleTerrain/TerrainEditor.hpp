#pragma once

#include <AxleTerrain/TerrainComponent.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Scene.hpp>

namespace Axle::Terrain {

    class TerrainEditor {
    public:
        // Draw Inspector UI for TerrainComponent
        static void DrawInspector(
            Entity entity,
            PhysicsScene* physicsScene,
            Scene* scene = nullptr
        );

        // Rebuild terrain GPU meshes and Jolt physics shape
        static void RebuildTerrain(
            TerrainComponent& tc,
            PhysicsScene* physicsScene,
            Entity entity = {}
        );
    };

}
