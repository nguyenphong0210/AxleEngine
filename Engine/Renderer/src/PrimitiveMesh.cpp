#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Buffer.hpp>
#include <cmath>
#include <vector>

namespace Axle {

    namespace {
        constexpr f32 PI = 3.14159265358979323846f;
    }

    std::shared_ptr<VertexArray> PrimitiveMesh::CreateCube() {
        f32 vertices[8 * 3] = {
            -0.5f, -0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f
        };

        u32 indices[36] = {
            4, 5, 6,  6, 7, 4,
            1, 0, 3,  3, 2, 1,
            0, 4, 7,  7, 3, 0,
            5, 1, 2,  2, 6, 5,
            7, 6, 2,  2, 3, 7,
            0, 1, 5,  5, 4, 0
        };

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices, static_cast<u32>(sizeof(vertices)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" }
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

            for (u32 j = 0; j <= sectors; ++j) {
                const f32 sectorAngle = static_cast<f32>(j) * (2.0f * PI / static_cast<f32>(sectors));
                const f32 x = xz * std::cos(sectorAngle);
                const f32 z = xz * std::sin(sectorAngle);

                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
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
            { ShaderDataType::Float3, "a_Position" }
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

            // Top rim vertex at Y = +1.0
            vertices.push_back(x);
            vertices.push_back(1.0f);
            vertices.push_back(z);

            // Bottom rim vertex at Y = -1.0
            vertices.push_back(x);
            vertices.push_back(-1.0f);
            vertices.push_back(z);
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
        const u32 topCenterIdx = static_cast<u32>(vertices.size() / 3);
        vertices.push_back(0.0f);
        vertices.push_back(1.0f);
        vertices.push_back(0.0f);

        // Top cap triangles
        for (u32 i = 0; i < segments; ++i) {
            const u32 top1 = i * 2;
            const u32 top2 = (i + 1) * 2;

            indices.push_back(topCenterIdx);
            indices.push_back(top1);
            indices.push_back(top2);
        }

        // Bottom cap center vertex
        const u32 botCenterIdx = static_cast<u32>(vertices.size() / 3);
        vertices.push_back(0.0f);
        vertices.push_back(-1.0f);
        vertices.push_back(0.0f);

        // Bottom cap triangles
        for (u32 i = 0; i < segments; ++i) {
            const u32 bot1 = i * 2 + 1;
            const u32 bot2 = (i + 1) * 2 + 1;

            indices.push_back(botCenterIdx);
            indices.push_back(bot2);
            indices.push_back(bot1);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices.data(), static_cast<u32>(vertices.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" }
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

        // Base rim vertices at Y = -1.0 (Indices 1 to segments + 1)
        for (u32 i = 0; i <= segments; ++i) {
            const f32 angle = static_cast<f32>(i) * (2.0f * PI / static_cast<f32>(segments));
            const f32 x = std::cos(angle);
            const f32 z = std::sin(angle);

            vertices.push_back(x);
            vertices.push_back(-1.0f);
            vertices.push_back(z);
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
        const u32 baseCenterIdx = static_cast<u32>(vertices.size() / 3);
        vertices.push_back(0.0f);
        vertices.push_back(-1.0f);
        vertices.push_back(0.0f);

        // Base cap triangles
        for (u32 i = 0; i < segments; ++i) {
            const u32 r1 = 1 + i;
            const u32 r2 = 1 + i + 1;

            indices.push_back(baseCenterIdx);
            indices.push_back(r2);
            indices.push_back(r1);
        }

        auto va = std::make_shared<VertexArray>();
        auto vb = std::make_shared<VertexBuffer>(vertices.data(), static_cast<u32>(vertices.size() * sizeof(f32)));
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" }
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
            for (u32 x = 0; x <= resolutionX; ++x) {
                const f32 xPos = -0.5f + static_cast<f32>(x) / static_cast<f32>(resolutionX);
                vertices.push_back(xPos);
                vertices.push_back(0.0f);
                vertices.push_back(zPos);
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
            { ShaderDataType::Float3, "a_Position" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(indices.data(), static_cast<u32>(indices.size()));
        va->SetIndexBuffer(ib);

        return va;
    }

}
