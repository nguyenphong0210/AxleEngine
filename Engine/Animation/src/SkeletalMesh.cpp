#include <Axle/SkeletalMesh.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/Window.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    void SkeletalVertex::AddBoneData(int boneID, float weight) {
        if (weight <= 1e-6f || boneID < 0 || !std::isfinite(weight)) return;

        // Merge repeated joint influences
        for (int i = 0; i < 4; ++i) {
            if (BoneIDs[i] == boneID) {
                Weights[i] += weight;
                return;
            }
        }

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

    void SkeletalVertex::NormalizeWeights(int rootBoneIndex) {
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if (BoneIDs[i] >= 0 && std::isfinite(Weights[i]) && Weights[i] > 0.0f) {
                sum += Weights[i];
            } else {
                BoneIDs[i] = -1;
                Weights[i] = 0.0f;
            }
        }

        if (sum > 1e-6f) {
            for (int i = 0; i < 4; ++i) {
                if (BoneIDs[i] >= 0) {
                    Weights[i] /= sum;
                }
            }
        } else {
            // Truly unweighted vertex: fallback to root bone with weight 1.0
            BoneIDs = glm::ivec4(rootBoneIndex >= 0 ? rootBoneIndex : 0, -1, -1, -1);
            Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    void SkeletalMeshData::Normalize(int rootBoneIndex) {
        for (auto& v : Vertices) {
            v.NormalizeWeights(rootBoneIndex);
        }
    }

    bool SkeletalMeshData::Validate(std::string* diagnostic) const {
        auto fail = [&](const char* text) { if (diagnostic) *diagnostic = text; return false; };
        if (Vertices.empty() || Vertices.size() > 10000000 || Indices.empty() || Indices.size() > 30000000 || Indices.size() % 3)
            return fail("Invalid geometry vertex/triangle counts");
        if (SkeletonRef && (!SkeletonRef->IsValid() || SkeletonRef->GetBoneCount() > 128)) return fail("Invalid or oversized rig");
        for (auto index : Indices) if (index >= Vertices.size()) return fail("Geometry index exceeds vertex count");
        const size_t bones = SkeletonRef ? SkeletonRef->GetBoneCount() : 128;
        for (const auto& vertex : Vertices) {
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(vertex.Position[axis]) || !std::isfinite(vertex.Normal[axis])) return fail("Nonfinite vertex attributes");
            if (!std::isfinite(vertex.TexCoords.x) || !std::isfinite(vertex.TexCoords.y) || glm::length(vertex.Normal) < 1e-6f)
                return fail("Invalid UVs or zero normal");
            float sum = 0;
            for (int i = 0; i < 4; ++i) {
                const auto id = vertex.BoneIDs[i]; const float weight = vertex.Weights[i];
                if (!std::isfinite(weight) || weight < 0 || id < -1 || (id == -1 && weight != 0) || (id >= 0 && size_t(id) >= bones))
                    return fail("Invalid vertex joint or weight");
                for (int j = 0; j < i; ++j) if (weight > 0 && vertex.Weights[j] > 0 && id == vertex.BoneIDs[j]) return fail("Duplicate vertex joint influence");
                sum += weight;
            }
            if (std::abs(sum - 1) > 1e-4f) return fail("Vertex weights are not normalized");
        }
        if (!Submeshes.empty()) {
            uint64_t cursor = 0;
            for (const auto& range : Submeshes) {
                if (range.FirstIndex != cursor || range.IndexCount == 0 || range.IndexCount % 3 ||
                    uint64_t(range.FirstIndex) + range.IndexCount > Indices.size() || range.MaterialSlot >= MaterialSlots.size())
                    return fail("Invalid submesh partition or material slot");
                cursor += range.IndexCount;
            }
            if (cursor != Indices.size()) return fail("Submeshes do not cover geometry");
        }
        return true;
    }

    std::shared_ptr<SkeletalMesh> SkeletalMesh::Create(SkeletalMeshData meshData, std::string* diagnostic) {
        int rootIndex = meshData.SkeletonRef ? meshData.SkeletonRef->GetRootBoneIndex() : 0;
        meshData.Normalize(rootIndex >= 0 ? rootIndex : 0);
        if (!meshData.Validate(diagnostic)) {
            return nullptr;
        }
        return std::make_shared<SkeletalMesh>(std::move(meshData));
    }

    SkeletalMesh::SkeletalMesh(SkeletalMeshData meshData)
        : m_Data(std::move(meshData)) {
        // Detach retained builder aliases: own an independent immutable skeleton snapshot
        if (m_Data.SkeletonRef) {
            m_Data.SkeletonRef = std::make_shared<Skeleton>(*m_Data.SkeletonRef);
        }
        int rootIndex = m_Data.SkeletonRef ? m_Data.SkeletonRef->GetRootBoneIndex() : 0;
        m_Data.Normalize(rootIndex >= 0 ? rootIndex : 0);
        if (Window::HasActiveContext()) {
            BuildBuffers();
        }
    }

    const std::shared_ptr<VertexArray>& SkeletalMesh::GetVAO() const {
        if (!m_VAO && Window::HasActiveContext()) {
            const_cast<SkeletalMesh*>(this)->BuildBuffers();
        }
        return m_VAO;
    }

    void SkeletalMesh::BuildBuffers() {
        if (m_Data.Vertices.empty() || m_Data.Indices.empty()) return;
        if (!Window::HasActiveContext()) return;

        // Note: m_Data is already normalized at publication; GPU buffer upload never mutates CPU geometry
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
