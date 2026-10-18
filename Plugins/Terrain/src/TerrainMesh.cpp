#include <AxleTerrain/TerrainMesh.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/Log.hpp>
#include <algorithm>

namespace Axle::Terrain {

    void TerrainMesh::InitializeChunks(
        const TerrainData& terrain,
        std::vector<TerrainChunk>& outChunks,
        u32 chunkSize
    ) {
        outChunks.clear();

        u32 resX = terrain.GetResolutionX();
        u32 resZ = terrain.GetResolutionZ();

        u32 cellsX = resX - 1;
        u32 cellsZ = resZ - 1;

        u32 numChunksX = (cellsX + chunkSize - 1) / chunkSize;
        u32 numChunksZ = (cellsZ + chunkSize - 1) / chunkSize;

        outChunks.reserve(numChunksX * numChunksZ);

        for (u32 cz = 0; cz < numChunksZ; ++cz) {
            for (u32 cx = 0; cx < numChunksX; ++cx) {
                TerrainChunk chunk;
                chunk.ChunkX = cx;
                chunk.ChunkZ = cz;

                chunk.StartGridX = cx * chunkSize;
                chunk.EndGridX = std::min(chunk.StartGridX + chunkSize, cellsX);

                chunk.StartGridZ = cz * chunkSize;
                chunk.EndGridZ = std::min(chunk.StartGridZ + chunkSize, cellsZ);

                chunk.CurrentLOD = 0;
                chunk.NeedsRebuild = true;

                // Initial world bounds estimation
                glm::vec3 minP = terrain.GridToWorld(chunk.StartGridX, chunk.StartGridZ);
                glm::vec3 maxP = terrain.GridToWorld(chunk.EndGridX, chunk.EndGridZ);
                chunk.BoundsMin = glm::vec3(minP.x, -100.0f, minP.z);
                chunk.BoundsMax = glm::vec3(maxP.x, 100.0f, maxP.z);

                outChunks.push_back(chunk);
            }
        }
    }

    void TerrainMesh::BuildChunkGeometry(
        const TerrainData& terrain,
        TerrainChunk& chunk,
        u32 lod
    ) {
        chunk.CurrentLOD = lod;
        u32 step = 1 << std::min(lod, 3u); // lod 0: 1, lod 1: 2, lod 2: 4, lod 3: 8

        u32 gridSpanX = chunk.EndGridX - chunk.StartGridX;
        u32 gridSpanZ = chunk.EndGridZ - chunk.StartGridZ;

        u32 numVertsX = (gridSpanX / step) + 1;
        u32 numVertsZ = (gridSpanZ / step) + 1;

        chunk.Vertices.clear();
        chunk.Vertices.reserve(numVertsX * numVertsZ);

        f32 minY = 1e9f;
        f32 maxY = -1e9f;

        f32 totalWidth = terrain.GetWidth();
        f32 totalLength = terrain.GetLength();

        // 1. Generate vertices
        for (u32 z = chunk.StartGridZ; z <= chunk.EndGridZ; z += step) {
            for (u32 x = chunk.StartGridX; x <= chunk.EndGridX; x += step) {
                TerrainVertex vert;
                vert.Position = terrain.GridToWorld(x, z);
                vert.Normal = terrain.CalculateGridNormal(x, z);

                // Texture coordinates (scaled for tiling detail)
                vert.TexCoord = glm::vec2(
                    (vert.Position.x - terrain.GetOrigin().x) / 8.0f,
                    (vert.Position.z - terrain.GetOrigin().z) / 8.0f
                );

                vert.SplatWeight = terrain.GetSplatWeight(x, z);

                minY = std::min(minY, vert.Position.y);
                maxY = std::max(maxY, vert.Position.y);

                chunk.Vertices.push_back(vert);
            }
        }

        // Update bounds
        glm::vec3 pMin = terrain.GridToWorld(chunk.StartGridX, chunk.StartGridZ);
        glm::vec3 pMax = terrain.GridToWorld(chunk.EndGridX, chunk.EndGridZ);
        chunk.BoundsMin = glm::vec3(pMin.x, minY, pMin.z);
        chunk.BoundsMax = glm::vec3(pMax.x, maxY, pMax.z);

        // 2. Generate triangle indices
        chunk.Indices.clear();
        u32 quadsX = numVertsX - 1;
        u32 quadsZ = numVertsZ - 1;
        chunk.Indices.reserve(quadsX * quadsZ * 6);

        for (u32 rz = 0; rz < quadsZ; ++rz) {
            for (u32 rx = 0; rx < quadsX; ++rx) {
                u32 i00 = rz * numVertsX + rx;
                u32 i10 = rz * numVertsX + (rx + 1);
                u32 i01 = (rz + 1) * numVertsX + rx;
                u32 i11 = (rz + 1) * numVertsX + (rx + 1);

                // CCW winding
                chunk.Indices.push_back(i00);
                chunk.Indices.push_back(i01);
                chunk.Indices.push_back(i10);

                chunk.Indices.push_back(i10);
                chunk.Indices.push_back(i01);
                chunk.Indices.push_back(i11);
            }
        }

        chunk.NeedsRebuild = true;
    }

    void TerrainMesh::BuildAllChunksParallel(
        const TerrainData& terrain,
        std::vector<TerrainChunk>& chunks
    ) {
        if (chunks.empty()) return;

        u32 chunkCount = static_cast<u32>(chunks.size());

        // Parallel chunk construction using Axle enkiTS JobSystem
        Axle::JobSystem::ParallelFor(chunkCount, [&](u32 index) {
            BuildChunkGeometry(terrain, chunks[index], chunks[index].CurrentLOD);
        });
    }

    void TerrainMesh::UploadChunkGPU(TerrainChunk& chunk) {
        if (!chunk.NeedsRebuild || chunk.Vertices.empty()) {
            return;
        }

        // Interleaved vertex buffer data
        // Format: Position(3), Normal(3), TexCoord(2), SplatWeight(4) = 12 floats
        std::vector<f32> vertexData;
        vertexData.reserve(chunk.Vertices.size() * 12);

        for (const auto& v : chunk.Vertices) {
            vertexData.push_back(v.Position.x);
            vertexData.push_back(v.Position.y);
            vertexData.push_back(v.Position.z);

            vertexData.push_back(v.Normal.x);
            vertexData.push_back(v.Normal.y);
            vertexData.push_back(v.Normal.z);

            vertexData.push_back(v.TexCoord.x);
            vertexData.push_back(v.TexCoord.y);

            vertexData.push_back(v.SplatWeight.r);
            vertexData.push_back(v.SplatWeight.g);
            vertexData.push_back(v.SplatWeight.b);
            vertexData.push_back(v.SplatWeight.a);
        }

        chunk.VAO = std::make_shared<VertexArray>();

        auto vb = std::make_shared<VertexBuffer>(
            vertexData.data(),
            static_cast<u32>(vertexData.size() * sizeof(f32))
        );
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float4, "a_SplatWeight" }
        });
        chunk.VAO->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(
            chunk.Indices.data(),
            static_cast<u32>(chunk.Indices.size())
        );
        chunk.VAO->SetIndexBuffer(ib);

        chunk.NeedsRebuild = false;
    }

    void TerrainMesh::UploadAllChunksGPU(std::vector<TerrainChunk>& chunks) {
        for (auto& chunk : chunks) {
            UploadChunkGPU(chunk);
        }
    }

    bool TerrainMesh::UpdateChunkLODs(
        const glm::vec3& cameraPos,
        std::vector<TerrainChunk>& chunks,
        f32 lod1Dist,
        f32 lod2Dist
    ) {
        bool anyChanged = false;

        for (auto& chunk : chunks) {
            f32 dist = chunk.GetDistanceTo(cameraPos);
            u32 targetLOD = 0;
            if (dist > lod2Dist) {
                targetLOD = 2;
            } else if (dist > lod1Dist) {
                targetLOD = 1;
            }

            if (targetLOD != chunk.CurrentLOD) {
                chunk.CurrentLOD = targetLOD;
                chunk.NeedsRebuild = true;
                anyChanged = true;
            }
        }

        return anyChanged;
    }

    std::shared_ptr<VertexArray> TerrainMesh::BuildCombinedVAO(const TerrainData& terrain) {
        u32 resX = terrain.GetResolutionX();
        u32 resZ = terrain.GetResolutionZ();

        if (resX < 2 || resZ < 2) {
            return nullptr;
        }

        u32 totalVerts = resX * resZ;
        std::vector<f32> vertexData;
        vertexData.reserve(totalVerts * 12);

        for (u32 z = 0; z < resZ; ++z) {
            for (u32 x = 0; x < resX; ++x) {
                glm::vec3 pos = terrain.GridToWorld(x, z);
                glm::vec3 norm = terrain.CalculateGridNormal(x, z);
                glm::vec2 uv(
                    (pos.x - terrain.GetOrigin().x) / 8.0f,
                    (pos.z - terrain.GetOrigin().z) / 8.0f
                );
                glm::vec4 splat = terrain.GetSplatWeight(x, z);

                vertexData.push_back(pos.x);
                vertexData.push_back(pos.y);
                vertexData.push_back(pos.z);

                vertexData.push_back(norm.x);
                vertexData.push_back(norm.y);
                vertexData.push_back(norm.z);

                vertexData.push_back(uv.x);
                vertexData.push_back(uv.y);

                vertexData.push_back(splat.r);
                vertexData.push_back(splat.g);
                vertexData.push_back(splat.b);
                vertexData.push_back(splat.a);
            }
        }

        std::vector<u32> indices;
        indices.reserve((resX - 1) * (resZ - 1) * 6);

        for (u32 z = 0; z < resZ - 1; ++z) {
            for (u32 x = 0; x < resX - 1; ++x) {
                u32 i00 = z * resX + x;
                u32 i10 = z * resX + (x + 1);
                u32 i01 = (z + 1) * resX + x;
                u32 i11 = (z + 1) * resX + (x + 1);

                indices.push_back(i00);
                indices.push_back(i01);
                indices.push_back(i10);

                indices.push_back(i10);
                indices.push_back(i01);
                indices.push_back(i11);
            }
        }

        auto va = std::make_shared<VertexArray>();

        auto vb = std::make_shared<VertexBuffer>(
            vertexData.data(),
            static_cast<u32>(vertexData.size() * sizeof(f32))
        );
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float4, "a_SplatWeight" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(
            indices.data(),
            static_cast<u32>(indices.size())
        );
        va->SetIndexBuffer(ib);

        return va;
    }

}
