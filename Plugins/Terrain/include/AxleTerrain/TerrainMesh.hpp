#pragma once

#include <AxleTerrain/TerrainData.hpp>
#include <AxleTerrain/TerrainChunk.hpp>
#include <Axle/VertexArray.hpp>
#include <memory>
#include <vector>

namespace Axle::Terrain {

    class TerrainMesh {
    public:
        // Initialize chunk structure based on chunk size (e.g. 16 or 32 grid cells)
        static void InitializeChunks(
            const TerrainData& terrain,
            std::vector<TerrainChunk>& outChunks,
            u32 chunkSize = 32
        );

        // Generate geometry for a single chunk at a specific LOD level
        static void BuildChunkGeometry(
            const TerrainData& terrain,
            TerrainChunk& chunk,
            u32 lod = 0
        );

        // Multithreaded generation of all chunks using enkiTS (Axle::JobSystem)
        static void BuildAllChunksParallel(
            const TerrainData& terrain,
            std::vector<TerrainChunk>& chunks
        );

        // Create or refresh GPU VertexArray for a chunk (Main Thread)
        static void UploadChunkGPU(TerrainChunk& chunk);

        // Upload all chunks that need rebuild to GPU (Main Thread)
        static void UploadAllChunksGPU(std::vector<TerrainChunk>& chunks);

        // Dynamically update chunk LODs based on camera distance
        static bool UpdateChunkLODs(
            const glm::vec3& cameraPos,
            std::vector<TerrainChunk>& chunks,
            f32 lod1Dist = 80.0f,
            f32 lod2Dist = 160.0f
        );

        // Build a single unified VAO for the entire terrain (optimal for single-draw-call scenes)
        static std::shared_ptr<VertexArray> BuildCombinedVAO(const TerrainData& terrain);
    };

}
