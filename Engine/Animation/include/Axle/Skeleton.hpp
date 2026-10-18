#pragma once
#include <Axle/Bone.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

namespace Axle {

    class Skeleton {
    public:
        Skeleton() = default;
        explicit Skeleton(std::string name) : m_Name(std::move(name)) {}

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        int AddBone(const Bone& bone);
        int FindBoneIndex(const std::string& name) const;

        const Bone* GetBone(int index) const;
        Bone* GetBone(int index);
        const Bone* GetBone(const std::string& name) const;
        Bone* GetBone(const std::string& name);

        size_t GetBoneCount() const { return m_Bones.size(); }
        const std::vector<Bone>& GetBones() const { return m_Bones; }
        std::vector<Bone>& GetBones() { return m_Bones; }

        int GetRootBoneIndex() const { return m_RootBoneIndex; }
        void SetRootBoneIndex(int index) { m_RootBoneIndex = index; }
        bool IsValid() const { return !m_Bones.empty(); }

        // Compute bind-pose global transforms: Global[i] = Global[parent] * Local[i]
        void ComputeBindPoseGlobalTransforms(std::vector<glm::mat4>& outGlobalMatrices) const;

        // Auto-compute InverseBindMatrices from bind pose if not explicitly provided
        void ComputeInverseBindMatrices();

    private:
        std::string m_Name;
        std::vector<Bone> m_Bones;
        std::unordered_map<std::string, int> m_BoneNameToIndex;
        int m_RootBoneIndex = -1;
    };

}
