#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/Log.hpp>
#include <cmath>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <glm/glm.hpp>

namespace Axle {

    namespace {
        constexpr f32 PI = 3.14159265358979323846f;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::CreateCube() {
        // 24 unique vertices (4 per face * 6 faces) for clean non-distorted UV texture mapping
        f32 vertices[24 * 5] = {
            // Front face (+Z)
            -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,

            // Back face (-Z)
             0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
            -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
             0.5f,  0.5f, -0.5f,  0.0f, 1.0f,

            // Top face (+Y)
            -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
             0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
             0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,

            // Bottom face (-Y)
            -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
             0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
             0.5f, -0.5f,  0.5f,  1.0f, 1.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, 1.0f,

            // Right face (+X)
             0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
             0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
             0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
             0.5f,  0.5f,  0.5f,  0.0f, 1.0f,

            // Left face (-X)
            -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
            -0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
            -0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
        };

        u32 indices[36] = {
            0,  1,  2,   2,  3,  0,   // Front
            4,  5,  6,   6,  7,  4,   // Back
            8,  9,  10,  10, 11, 8,   // Top
            12, 13, 14,  14, 15, 12,  // Bottom
            16, 17, 18,  18, 19, 16,  // Right
            20, 21, 22,  22, 23, 20   // Left
        };

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices, static_cast<u32>(sizeof(vertices)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices, 36);
        va->SetIndexBuffer(ib);

        return va;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::CreateSphere(u32 stacks, u32 sectors) {
        std::vector<f32> vertices;
        std::vector<u32> indices;

        for (u32 i = 0; i <= stacks; ++i) {
            const f32 stackAngle = (PI * 0.5f) - static_cast<f32>(i) * (PI / static_cast<f32>(stacks));
            const f32 y = std::sin(stackAngle);
            const f32 xz = std::cos(stackAngle);
            const f32 v = static_cast<f32>(i) / static_cast<f32>(stacks);

            for (u32 j = 0; j <= sectors; ++j) {
                const f32 sectorAngle = static_cast<f32>(j) * (2.0f * PI / static_cast<f32>(sectors));
                const f32 x = xz * std::cos(sectorAngle);
                const f32 z = xz * std::sin(sectorAngle);
                const f32 u = static_cast<f32>(j) / static_cast<f32>(sectors);

                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
                vertices.push_back(u);
                vertices.push_back(v);
            }
        }

        for (u32 i = 0; i < stacks; ++i) {
            const u32 k1 = i * (sectors + 1);
            const u32 k2 = k1 + sectors + 1;

            for (u32 j = 0; j < sectors; ++j) {
                if (i != 0) {
                    indices.push_back(k1 + j);
                    indices.push_back(k2 + j);
                    indices.push_back(k1 + j + 1);
                }

                if (i != (stacks - 1)) {
                    indices.push_back(k1 + j + 1);
                    indices.push_back(k2 + j);
                    indices.push_back(k2 + j + 1);
                }
            }
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices.data(), static_cast<u32>(vertices.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices.data(), static_cast<u32>(indices.size()));
        va->SetIndexBuffer(ib);

        return va;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::CreateCylinder(u32 segments) {
        std::vector<f32> vertices;
        std::vector<u32> indices;

        // Tube vertices (2 vertices per segment: top rim and bottom rim)
        for (u32 i = 0; i <= segments; ++i) {
            const f32 angle = static_cast<f32>(i) * (2.0f * PI / static_cast<f32>(segments));
            const f32 x = std::cos(angle);
            const f32 z = std::sin(angle);
            const f32 u = static_cast<f32>(i) / static_cast<f32>(segments);

            // Top rim vertex at Y = +1.0
            vertices.push_back(x);
            vertices.push_back(1.0f);
            vertices.push_back(z);
            vertices.push_back(u);
            vertices.push_back(1.0f);

            // Bottom rim vertex at Y = -1.0
            vertices.push_back(x);
            vertices.push_back(-1.0f);
            vertices.push_back(z);
            vertices.push_back(u);
            vertices.push_back(0.0f);
        }

        // Tube side faces
        for (u32 i = 0; i < segments; ++i) {
            const u32 top1 = i * 2;
            const u32 bot1 = top1 + 1;
            const u32 top2 = (i + 1) * 2;
            const u32 bot2 = top2 + 1;

            indices.push_back(top1);
            indices.push_back(bot1);
            indices.push_back(top2);

            indices.push_back(top2);
            indices.push_back(bot1);
            indices.push_back(bot2);
        }

        // Top cap center vertex
        const u32 topCenterIdx = static_cast<u32>(vertices.size() / 5);
        vertices.push_back(0.0f);
        vertices.push_back(1.0f);
        vertices.push_back(0.0f);
        vertices.push_back(0.5f);
        vertices.push_back(0.5f);

        // Top cap rim vertices
        const u32 topRimStart = static_cast<u32>(vertices.size() / 5);
        for (u32 i = 0; i <= segments; ++i) {
            const f32 angle = static_cast<f32>(i) * (2.0f * PI / static_cast<f32>(segments));
            const f32 x = std::cos(angle);
            const f32 z = std::sin(angle);
            vertices.push_back(x);
            vertices.push_back(1.0f);
            vertices.push_back(z);
            vertices.push_back(0.5f + 0.5f * x);
            vertices.push_back(0.5f + 0.5f * z);
        }

        // Top cap triangles
        for (u32 i = 0; i < segments; ++i) {
            indices.push_back(topCenterIdx);
            indices.push_back(topRimStart + i);
            indices.push_back(topRimStart + i + 1);
        }

        // Bottom cap center vertex
        const u32 botCenterIdx = static_cast<u32>(vertices.size() / 5);
        vertices.push_back(0.0f);
        vertices.push_back(-1.0f);
        vertices.push_back(0.0f);
        vertices.push_back(0.5f);
        vertices.push_back(0.5f);

        // Bottom cap rim vertices
        const u32 botRimStart = static_cast<u32>(vertices.size() / 5);
        for (u32 i = 0; i <= segments; ++i) {
            const f32 angle = static_cast<f32>(i) * (2.0f * PI / static_cast<f32>(segments));
            const f32 x = std::cos(angle);
            const f32 z = std::sin(angle);
            vertices.push_back(x);
            vertices.push_back(-1.0f);
            vertices.push_back(z);
            vertices.push_back(0.5f + 0.5f * x);
            vertices.push_back(0.5f + 0.5f * z);
        }

        // Bottom cap triangles
        for (u32 i = 0; i < segments; ++i) {
            indices.push_back(botCenterIdx);
            indices.push_back(botRimStart + i + 1);
            indices.push_back(botRimStart + i);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices.data(), static_cast<u32>(vertices.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices.data(), static_cast<u32>(indices.size()));
        va->SetIndexBuffer(ib);

        return va;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::CreateCone(u32 segments) {
        std::vector<f32> vertices;
        std::vector<u32> indices;

        // Apex vertex at Y = +1.0 (Index 0)
        vertices.push_back(0.0f);
        vertices.push_back(1.0f);
        vertices.push_back(0.0f);
        vertices.push_back(0.5f);
        vertices.push_back(1.0f);

        // Base rim vertices at Y = -1.0 (Indices 1 to segments + 1)
        for (u32 i = 0; i <= segments; ++i) {
            const f32 angle = static_cast<f32>(i) * (2.0f * PI / static_cast<f32>(segments));
            const f32 x = std::cos(angle);
            const f32 z = std::sin(angle);
            const f32 u = static_cast<f32>(i) / static_cast<f32>(segments);

            vertices.push_back(x);
            vertices.push_back(-1.0f);
            vertices.push_back(z);
            vertices.push_back(u);
            vertices.push_back(0.0f);
        }

        // Side triangles connecting apex (0) to rim vertices
        for (u32 i = 0; i < segments; ++i) {
            const u32 r1 = 1 + i;
            const u32 r2 = 1 + i + 1;

            indices.push_back(0);
            indices.push_back(r1);
            indices.push_back(r2);
        }

        // Base cap center vertex at Y = -1.0
        const u32 baseCenterIdx = static_cast<u32>(vertices.size() / 5);
        vertices.push_back(0.0f);
        vertices.push_back(-1.0f);
        vertices.push_back(0.0f);
        vertices.push_back(0.5f);
        vertices.push_back(0.5f);

        // Base cap rim vertices
        const u32 baseRimStart = static_cast<u32>(vertices.size() / 5);
        for (u32 i = 0; i <= segments; ++i) {
            const f32 angle = static_cast<f32>(i) * (2.0f * PI / static_cast<f32>(segments));
            const f32 x = std::cos(angle);
            const f32 z = std::sin(angle);
            vertices.push_back(x);
            vertices.push_back(-1.0f);
            vertices.push_back(z);
            vertices.push_back(0.5f + 0.5f * x);
            vertices.push_back(0.5f + 0.5f * z);
        }

        // Base cap triangles
        for (u32 i = 0; i < segments; ++i) {
            indices.push_back(baseCenterIdx);
            indices.push_back(baseRimStart + i + 1);
            indices.push_back(baseRimStart + i);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices.data(), static_cast<u32>(vertices.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices.data(), static_cast<u32>(indices.size()));
        va->SetIndexBuffer(ib);

        return va;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::CreatePlane(u32 resolutionX, u32 resolutionZ) {
        resolutionX = std::max(1u, resolutionX);
        resolutionZ = std::max(1u, resolutionZ);

        std::vector<f32> vertices;
        std::vector<u32> indices;

        for (u32 z = 0; z <= resolutionZ; ++z) {
            const f32 zPos = -0.5f + static_cast<f32>(z) / static_cast<f32>(resolutionZ);
            const f32 v = static_cast<f32>(z) / static_cast<f32>(resolutionZ);

            for (u32 x = 0; x <= resolutionX; ++x) {
                const f32 xPos = -0.5f + static_cast<f32>(x) / static_cast<f32>(resolutionX);
                const f32 u = static_cast<f32>(x) / static_cast<f32>(resolutionX);

                vertices.push_back(xPos);
                vertices.push_back(0.0f);
                vertices.push_back(zPos);
                vertices.push_back(u);
                vertices.push_back(v);
            }
        }

        for (u32 z = 0; z < resolutionZ; ++z) {
            for (u32 x = 0; x < resolutionX; ++x) {
                const u32 row1 = z * (resolutionX + 1);
                const u32 row2 = (z + 1) * (resolutionX + 1);

                indices.push_back(row1 + x);
                indices.push_back(row2 + x);
                indices.push_back(row1 + x + 1);

                indices.push_back(row1 + x + 1);
                indices.push_back(row2 + x);
                indices.push_back(row2 + x + 1);
            }
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices.data(), static_cast<u32>(vertices.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices.data(), static_cast<u32>(indices.size()));
        va->SetIndexBuffer(ib);

        return va;
    }

    MeshInfo PrimitiveMesh::LoadOBJEx(const std::string& filepath, bool centerGeometry) {
        MeshInfo result;

        std::ifstream file(filepath);
        if (!file.is_open()) {
            AXLE_ERROR("PrimitiveMesh: Failed to open OBJ file '{0}'", filepath);
            return result;
        }

        std::vector<glm::vec3> temp_positions;
        std::vector<glm::vec2> temp_uvs;
        std::vector<f32> vertexData;
        std::vector<u32> indices;

        std::string line;
        while (std::getline(file, line)) {
            size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
            if (firstNonSpace == std::string::npos) continue;
            if (line[firstNonSpace] == '#') continue;

            std::string sub = line.substr(firstNonSpace);
            if (sub.rfind("v ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                glm::vec3 v(0.0f);
                s >> v.x >> v.y >> v.z;
                temp_positions.push_back(v);
            } else if (sub.rfind("vt ", 0) == 0) {
                std::istringstream s(sub.substr(3));
                glm::vec2 uv(0.0f);
                s >> uv.x >> uv.y;
                temp_uvs.push_back(uv);
            } else if (sub.rfind("f ", 0) == 0) {
                std::istringstream s(sub.substr(2));
                std::string token;
                std::vector<u32> faceIdx;
                while (s >> token) {
                    size_t slash = token.find('/');
                    std::string idxStr = (slash == std::string::npos) ? token : token.substr(0, slash);
                    if (!idxStr.empty()) {
                        try {
                            int idx = std::stoi(idxStr);
                            if (idx < 0) {
                                idx = static_cast<int>(temp_positions.size()) + idx + 1;
                            }
                            if (idx > 0 && static_cast<size_t>(idx) <= temp_positions.size()) {
                                faceIdx.push_back(static_cast<u32>(idx - 1));
                            }
                        } catch (...) {
                        }
                    }
                }
                // Triangulate n-gon / quad into triangle fan
                for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                    indices.push_back(faceIdx[0]);
                    indices.push_back(faceIdx[i]);
                    indices.push_back(faceIdx[i + 1]);
                }
            }
        }

        if (temp_positions.empty() || indices.empty()) {
            AXLE_WARN("PrimitiveMesh: OBJ file '{0}' contains no valid geometry", filepath);
            return result;
        }

        // Calculate bounding box and geometric center
        glm::vec3 minBound( 1e9f);
        glm::vec3 maxBound(-1e9f);
        for (const auto& pos : temp_positions) {
            minBound = glm::min(minBound, pos);
            maxBound = glm::max(maxBound, pos);
        }

        glm::vec3 center = (minBound + maxBound) * 0.5f;
        result.BoundingSize = maxBound - minBound;
        result.HalfExtent = glm::max(result.BoundingSize * 0.5f, glm::vec3(0.05f));
        glm::vec3 size = glm::max(result.BoundingSize, glm::vec3(0.01f));

        vertexData.reserve(temp_positions.size() * 5);
        for (size_t i = 0; i < temp_positions.size(); ++i) {
            const auto& pos = temp_positions[i];
            glm::vec3 p = centerGeometry ? (pos - center) : pos;
            vertexData.push_back(p.x);
            vertexData.push_back(p.y);
            vertexData.push_back(p.z);

            if (i < temp_uvs.size()) {
                vertexData.push_back(temp_uvs[i].x);
                vertexData.push_back(temp_uvs[i].y);
            } else {
                // Planar projection UV fallback
                float u = (pos.x - minBound.x) / size.x;
                float v = (pos.z - minBound.z) / size.z;
                vertexData.push_back(u);
                vertexData.push_back(v);
            }
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertexData.data(), static_cast<u32>(vertexData.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices.data(), static_cast<u32>(indices.size()));
        va->SetIndexBuffer(ib);

        result.VAO = va;

        AXLE_INFO("PrimitiveMesh: Loaded OBJ '{0}' ({1} vertices, {2} triangles)", 
            filepath, temp_positions.size(), indices.size() / 3);

        return result;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::LoadOBJ(const std::string& filepath) {
        return LoadOBJEx(filepath, true).VAO;
    }

}
