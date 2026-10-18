#pragma once
#include <Axle/Types.hpp>
#include <Axle/Skeleton.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

namespace Axle {

    struct SkeletalVertex {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        glm::vec2 TexCoords{ 0.0f };
        glm::ivec4 BoneIDs{ -1, -1, -1, -1 };
        glm::vec4 Weights{ 0.0f };

        void AddBoneData(int boneID, float weight);
    };

    struct SkeletalMeshData {
        std::string Name;
        std::vector<SkeletalVertex> Vertices;
        std::vector<u32> Indices;
        std::shared_ptr<Skeleton> SkeletonRef;
    };

    class SkeletalMesh {
    public:
        SkeletalMesh() = default;
        explicit SkeletalMesh(SkeletalMeshData meshData);

        const SkeletalMeshData& GetData() const { return m_Data; }
        SkeletalMeshData& GetData() { return m_Data; }
        const std::shared_ptr<Skeleton>& GetSkeleton() const { return m_Data.SkeletonRef; }
        const std::shared_ptr<VertexArray>& GetVAO() const { return m_VAO; }

        u32 GetIndexCount() const { return static_cast<u32>(m_Data.Indices.size()); }
        u32 GetVertexCount() const { return static_cast<u32>(m_Data.Vertices.size()); }

        void BuildBuffers();

    private:
        SkeletalMeshData m_Data;
        std::shared_ptr<VertexArray> m_VAO;
    };

}
