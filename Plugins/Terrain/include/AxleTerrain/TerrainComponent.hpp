#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <AxleTerrain/TerrainChunk.hpp>
#include <AxleTerrain/TerrainSculptor.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/VertexArray.hpp>
#include <memory>
#include <vector>

namespace Axle::Terrain {

    struct TerrainComponent {
        TerrainData Data;
        std::vector<TerrainChunk> Chunks;
        std::shared_ptr<VertexArray> CombinedVAO;
        BodyID PhysicsBodyID;

        bool UseChunkedLOD = false;
        u32 ChunkSize = 32;
        bool GeneratePhysics = true;
        f32 Friction = 0.85f;
        f32 Restitution = 0.05f;

        SculptSettings Sculpt;
        bool SculptingActive = false;

        TerrainComponent(u32 resX = 128, u32 resZ = 128, f32 spacingX = 1.0f, f32 spacingZ = 1.0f)
            : Data(resX, resZ, spacingX, spacingZ) {}
        TerrainComponent(int resX, int resZ, f32 spacingX = 1.0f, f32 spacingZ = 1.0f)
            : Data(static_cast<u32>(resX), static_cast<u32>(resZ), spacingX, spacingZ) {}
    };

}
