#include <Axle/Skeleton.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Axle {

    int Skeleton::AddBone(const Bone& bone) {
        int index = static_cast<int>(m_Bones.size());
        Bone newBone = bone;
        newBone.ID = index;
        m_BoneNameToIndex[newBone.Name] = index;
        m_Bones.push_back(newBone);

        if (newBone.ParentID >= 0 && newBone.ParentID < static_cast<int>(m_Bones.size()) - 1) {
            m_Bones[newBone.ParentID].Children.push_back(index);
        } else if (newBone.ParentID == -1 && m_RootBoneIndex == -1) {
            m_RootBoneIndex = index;
        }

        return index;
    }

    int Skeleton::FindBoneIndex(const std::string& name) const {
        auto it = m_BoneNameToIndex.find(name);
        if (it != m_BoneNameToIndex.end()) {
            return it->second;
        }
        return -1;
    }

    const Bone* Skeleton::GetBone(int index) const {
        if (index >= 0 && index < static_cast<int>(m_Bones.size())) {
            return &m_Bones[index];
        }
        return nullptr;
    }

    Bone* Skeleton::GetBone(int index) {
        if (index >= 0 && index < static_cast<int>(m_Bones.size())) {
            return &m_Bones[index];
        }
        return nullptr;
    }

    const Bone* Skeleton::GetBone(const std::string& name) const {
        int idx = FindBoneIndex(name);
        return idx != -1 ? &m_Bones[idx] : nullptr;
    }

    Bone* Skeleton::GetBone(const std::string& name) {
        int idx = FindBoneIndex(name);
        return idx != -1 ? &m_Bones[idx] : nullptr;
    }

    void Skeleton::ComputeBindPoseGlobalTransforms(std::vector<glm::mat4>& outGlobalMatrices) const {
        outGlobalMatrices.resize(m_Bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < m_Bones.size(); ++i) {
            int parent = m_Bones[i].ParentID;
            if (parent >= 0 && parent < static_cast<int>(m_Bones.size())) {
                outGlobalMatrices[i] = outGlobalMatrices[parent] * m_Bones[i].LocalTransform;
            } else {
                outGlobalMatrices[i] = m_Bones[i].LocalTransform;
            }
        }
    }

    void Skeleton::ComputeInverseBindMatrices() {
        std::vector<glm::mat4> globalBinds;
        ComputeBindPoseGlobalTransforms(globalBinds);
        for (size_t i = 0; i < m_Bones.size(); ++i) {
            m_Bones[i].InverseBindMatrix = glm::affineInverse(globalBinds[i]);
        }
    }

}
