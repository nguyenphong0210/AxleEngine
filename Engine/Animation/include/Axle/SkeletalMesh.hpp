#pragma once
#include <Axle/Types.hpp>
#include <Axle/Skeleton.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <span>

namespace Axle {

    struct SkeletalVertex {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        glm::vec2 TexCoords{ 0.0f };
        glm::ivec4 BoneIDs{ -1, -1, -1, -1 };
        glm::vec4 Weights{ 0.0f };

        void AddBoneData(int boneID, float weight);
        void NormalizeWeights(int rootBoneIndex = 0);
    };

    struct SkeletalSubmesh {
        uint32_t FirstIndex = 0, IndexCount = 0, MaterialSlot = 0;
    };

    struct SkeletalMeshData {
        std::string Name;
        std::vector<SkeletalVertex> Vertices;
        std::vector<u32> Indices;
        std::shared_ptr<const Skeleton> SkeletonRef;
        std::vector<SkeletalSubmesh> Submeshes;
        std::vector<std::string> MaterialSlots;
        glm::vec3 BoundsMin{0}, BoundsMax{0};
        bool Validate(std::string* diagnostic = nullptr) const;

        void Normalize(int rootBoneIndex = 0);
    };

    class SkeletalMesh {
    public:
        SkeletalMesh() = delete;
        explicit SkeletalMesh(SkeletalMeshData meshData);

        // Non-copyable and non-assignable: published resources cannot be overwritten in place
        SkeletalMesh(const SkeletalMesh&) = delete;
        SkeletalMesh& operator=(const SkeletalMesh&) = delete;
        SkeletalMesh(SkeletalMesh&&) = delete;
        SkeletalMesh& operator=(SkeletalMesh&&) = delete;

        // Factory function: validates draft and publishes an immutable snapshot, returning nullptr on failure
        static std::shared_ptr<SkeletalMesh> Create(SkeletalMeshData meshData, std::string* diagnostic = nullptr);

        bool Validate(std::string* diagnostic = nullptr) const { return m_Data.Validate(diagnostic); }

        // Read-only access to published geometry data
        const SkeletalMeshData& GetData() const { return m_Data; }

        // Explicit read-only view accessors
        std::span<const SkeletalVertex> GetVertices() const { return m_Data.Vertices; }
        std::span<const u32> GetIndices() const { return m_Data.Indices; }
        std::span<const SkeletalSubmesh> GetSubmeshes() const { return m_Data.Submeshes; }
        std::span<const std::string> GetMaterialSlots() const { return m_Data.MaterialSlots; }
        const glm::vec3& GetBoundsMin() const { return m_Data.BoundsMin; }
        const glm::vec3& GetBoundsMax() const { return m_Data.BoundsMax; }

        // Read-only rig accessor: does NOT leak a mutable Skeleton
        std::shared_ptr<const Skeleton> GetSkeleton() const { return m_Data.SkeletonRef; }
        size_t GetBoneCount() const { return m_Data.SkeletonRef ? m_Data.SkeletonRef->GetBoneCount() : 0; }

        const std::shared_ptr<VertexArray>& GetVAO() const;

        u32 GetIndexCount() const { return static_cast<u32>(m_Data.Indices.size()); }
        u32 GetVertexCount() const { return static_cast<u32>(m_Data.Vertices.size()); }

        void BuildBuffers();

    private:
        SkeletalMeshData m_Data;
        mutable std::shared_ptr<VertexArray> m_VAO;
    };

}
