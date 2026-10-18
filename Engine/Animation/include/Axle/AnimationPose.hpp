#pragma once
#include <Axle/Types.hpp>
#include <Axle/Skeleton.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace Axle {

    struct BoneTransform {
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Identity w=1, x=y=z=0
        glm::vec3 Scale{ 1.0f };

        BoneTransform() = default;
        BoneTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale = glm::vec3(1.0f))
            : Translation(translation), Rotation(rotation), Scale(scale) {}

        glm::mat4 ToMatrix() const;
        static BoneTransform FromMatrix(const glm::mat4& mat);
        static BoneTransform Blend(const BoneTransform& a, const BoneTransform& b, float weight);
    };

    struct AnimationPose {
        std::vector<BoneTransform> Transforms;

        AnimationPose() = default;
        explicit AnimationPose(size_t boneCount);

        void Resize(size_t boneCount);
        size_t GetBoneCount() const { return Transforms.size(); }
        bool IsEmpty() const { return Transforms.empty(); }

        void InitFromSkeletonBindPose(const Skeleton& skeleton);

        // Blends two poses with factor [0..1]: 0.0 = 100% a, 1.0 = 100% b
        static void Blend(const AnimationPose& a, const AnimationPose& b, float weight, AnimationPose& outPose);

        // Convert local bone transforms to local 4x4 matrices
        void ToLocalMatrices(std::vector<glm::mat4>& outLocalMatrices) const;

        // Compute forward kinematics hierarchical global matrices
        void ComputeGlobalMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outGlobalMatrices) const;

        // Compute final skinning palette matrices for GPU upload: Global[i] * InverseBindMatrix[i]
        void ComputeSkinningMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outSkinningMatrices) const;
    };

}
