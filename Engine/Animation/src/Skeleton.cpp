#include <Axle/Skeleton.hpp>
#include <Axle/AnimationPose.hpp>
#include <queue>
#include <unordered_set>
#include <cmath>
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
        // Mutable legacy bone access must not make a stale name map authoritative.
        for (size_t i = 0; i < m_Bones.size(); ++i) if (m_Bones[i].Name == name) return static_cast<int>(i);
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

    bool Skeleton::BuildEvaluationOrder(std::vector<size_t>& order, std::vector<std::string>* errors) const {
        order.clear();
        if (errors) errors->clear();
        bool valid = !m_Bones.empty();
        auto fail = [&](const std::string& text) { valid = false; if (errors) errors->push_back(text); };
        if (!valid) fail("A skeleton requires at least one bone.");
        std::unordered_set<std::string> names;
        std::vector<std::vector<size_t>> children(m_Bones.size());
        std::vector<unsigned> degrees(m_Bones.size(), 0);
        for (size_t i = 0; i < m_Bones.size(); ++i) {
            const auto& bone = m_Bones[i];
            if (bone.Name.empty() || !names.insert(bone.Name).second) fail("Empty or duplicate bone name at index " + std::to_string(i));
            BoneTransform local;
            if (!BoneTransform::TryFromMatrix(bone.LocalTransform, local)) fail("Unsupported local bind TRS: " + bone.Name);
            bool finite = true;
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                finite &= std::isfinite(bone.InverseBindMatrix[c][r]);
            const float determinant = glm::determinant(bone.InverseBindMatrix);
            if (!finite || !std::isfinite(determinant) || std::abs(determinant) < 1e-12f) fail("Invalid inverse bind: " + bone.Name);
            if (bone.ParentID < -1 || bone.ParentID >= static_cast<int>(m_Bones.size()) || bone.ParentID == static_cast<int>(i)) {
                fail("Invalid parent: " + bone.Name); continue;
            }
            if (bone.ParentID >= 0) { children[bone.ParentID].push_back(i); degrees[i] = 1; }
        }
        if (!valid) return false;
        std::queue<size_t> ready;
        for (size_t i = 0; i < degrees.size(); ++i) if (degrees[i] == 0) ready.push(i);
        while (!ready.empty()) {
            const auto parent = ready.front(); ready.pop(); order.push_back(parent);
            for (const auto child : children[parent]) if (--degrees[child] == 0) ready.push(child);
        }
        if (order.size() != m_Bones.size()) { fail("Skeleton parent graph contains a cycle."); order.clear(); }
        return valid;
    }

    bool Skeleton::IsValid() const {
        std::vector<size_t> order;
        return BuildEvaluationOrder(order);
    }

    bool Skeleton::RebuildDerivedData(std::vector<std::string>* errors) {
        std::vector<size_t> order;
        if (!BuildEvaluationOrder(order, errors)) return false;
        m_BoneNameToIndex.clear(); m_RootBoneIndex = -1;
        for (size_t i = 0; i < m_Bones.size(); ++i) {
            m_Bones[i].ID = static_cast<int>(i);
            m_Bones[i].Children.clear();
            m_BoneNameToIndex[m_Bones[i].Name] = static_cast<int>(i);
        }
        for (size_t i = 0; i < m_Bones.size(); ++i) {
            const auto parent = m_Bones[i].ParentID;
            if (parent >= 0) m_Bones[parent].Children.push_back(static_cast<int>(i));
            else if (m_RootBoneIndex < 0) m_RootBoneIndex = static_cast<int>(i);
        }
        return true;
    }

    bool Skeleton::ComputeBindPoseGlobalTransforms(std::vector<glm::mat4>& out) const {
        std::vector<size_t> order;
        if (!BuildEvaluationOrder(order)) { out.clear(); return false; }
        out.assign(m_Bones.size(), glm::mat4(1));
        for (const auto i : order) {
            const auto parent = m_Bones[i].ParentID;
            out[i] = parent < 0 ? m_Bones[i].LocalTransform : out[parent] * m_Bones[i].LocalTransform;
        }
        return true;
    }

    bool Skeleton::ComputeInverseBindMatrices() {
        std::vector<glm::mat4> globals;
        if (!ComputeBindPoseGlobalTransforms(globals)) return false;
        for (size_t i = 0; i < m_Bones.size(); ++i) m_Bones[i].InverseBindMatrix = glm::affineInverse(globals[i]);
        return true;
    }

}
