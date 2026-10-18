#include <Axle/SkeletalMesh.hpp>
#include <Axle/Buffer.hpp>
#include <algorithm>

namespace Axle {

    void SkeletalVertex::AddBoneData(int boneID, float weight) {
        if (weight <= 1e-6f || boneID < 0) return;

        // Try to insert into first empty slot
        for (int i = 0; i < 4; ++i) {
            if (BoneIDs[i] < 0 || Weights[i] <= 0.0f) {
                BoneIDs[i] = boneID;
                Weights[i] = weight;
                return;
            }
        }

        // If all 4 slots are full, replace the slot with the smallest weight if the new weight is greater
        int minIdx = 0;
        float minWeight = Weights[0];
        for (int i = 1; i < 4; ++i) {
            if (Weights[i] < minWeight) {
                minWeight = Weights[i];
                minIdx = i;
            }
        }

        if (weight > minWeight) {
            BoneIDs[minIdx] = boneID;
            Weights[minIdx] = weight;
        }
    }

    SkeletalMesh::SkeletalMesh(SkeletalMeshData meshData)
        : m_Data(std::move(meshData)) {
        BuildBuffers();
    }

    void SkeletalMesh::BuildBuffers() {
        if (m_Data.Vertices.empty() || m_Data.Indices.empty()) return;

        // Normalize weights across all 4 components for each vertex
        for (auto& v : m_Data.Vertices) {
            float total = v.Weights.x + v.Weights.y + v.Weights.z + v.Weights.w;
            if (total > 1e-6f) {
                v.Weights /= total;
            } else {
                // Default fallback: bind to root bone with weight 1.0
                v.BoneIDs = glm::ivec4(0, -1, -1, -1);
                v.Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        m_VAO = std::make_shared<VertexArray>();

        BufferLayout layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Int4,   "a_BoneIDs" },
            { ShaderDataType::Float4, "a_BoneWeights" }
        };

        auto vb = std::make_shared<VertexBuffer>(
            m_Data.Vertices.data(),
            static_cast<u32>(m_Data.Vertices.size() * sizeof(SkeletalVertex))
        );
        vb->SetLayout(layout);
        m_VAO->AddVertexBuffer(vb);

        auto ib = std::make_shared<IndexBuffer>(
            m_Data.Indices.data(),
            static_cast<u32>(m_Data.Indices.size())
        );
        m_VAO->SetIndexBuffer(ib);
    }

}
