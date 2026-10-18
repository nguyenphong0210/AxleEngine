#include <Axle/AnimationPose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    glm::mat4 BoneTransform::ToMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), Translation);
        glm::mat4 r = glm::mat4_cast(Rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), Scale);
        return t * r * s;
    }

    BoneTransform BoneTransform::FromMatrix(const glm::mat4& mat) {
        BoneTransform result;
        result.Translation = glm::vec3(mat[3]);

        glm::vec3 col0 = glm::vec3(mat[0]);
        glm::vec3 col1 = glm::vec3(mat[1]);
        glm::vec3 col2 = glm::vec3(mat[2]);

        result.Scale.x = glm::length(col0);
        result.Scale.y = glm::length(col1);
        result.Scale.z = glm::length(col2);

        // Normalize rotation columns if scale is non-zero
        if (result.Scale.x > 1e-6f) col0 /= result.Scale.x;
        if (result.Scale.y > 1e-6f) col1 /= result.Scale.y;
        if (result.Scale.z > 1e-6f) col2 /= result.Scale.z;

        glm::mat3 rotMat(col0, col1, col2);
        result.Rotation = glm::normalize(glm::quat_cast(rotMat));
        return result;
    }

    BoneTransform BoneTransform::Blend(const BoneTransform& a, const BoneTransform& b, float weight) {
        float factor = std::clamp(weight, 0.0f, 1.0f);
        BoneTransform result;
        result.Translation = glm::mix(a.Translation, b.Translation, factor);

        // Shortest-path spherical linear interpolation (SLERP)
        glm::quat rotB = b.Rotation;
        if (glm::dot(a.Rotation, rotB) < 0.0f) {
            rotB = -rotB;
        }
        result.Rotation = glm::normalize(glm::slerp(a.Rotation, rotB, factor));
        result.Scale = glm::mix(a.Scale, b.Scale, factor);
        return result;
    }

    AnimationPose::AnimationPose(size_t boneCount) {
        Resize(boneCount);
    }

    void AnimationPose::Resize(size_t boneCount) {
        Transforms.resize(boneCount);
    }

    void AnimationPose::InitFromSkeletonBindPose(const Skeleton& skeleton) {
        size_t count = skeleton.GetBoneCount();
        Transforms.resize(count);
        for (size_t i = 0; i < count; ++i) {
            Transforms[i] = BoneTransform::FromMatrix(skeleton.GetBones()[i].LocalTransform);
        }
    }

    void AnimationPose::Blend(const AnimationPose& a, const AnimationPose& b, float weight, AnimationPose& outPose) {
        size_t count = std::min(a.Transforms.size(), b.Transforms.size());
        outPose.Transforms.resize(count);

        float factor = std::clamp(weight, 0.0f, 1.0f);
        if (factor <= 0.0001f) {
            outPose = a;
            return;
        }
        if (factor >= 0.9999f) {
            outPose = b;
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            outPose.Transforms[i] = BoneTransform::Blend(a.Transforms[i], b.Transforms[i], factor);
        }
    }

    void AnimationPose::ToLocalMatrices(std::vector<glm::mat4>& outLocalMatrices) const {
        outLocalMatrices.resize(Transforms.size());
        for (size_t i = 0; i < Transforms.size(); ++i) {
            outLocalMatrices[i] = Transforms[i].ToMatrix();
        }
    }

    void AnimationPose::ComputeGlobalMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outGlobalMatrices) const {
        size_t count = skeleton.GetBoneCount();
        outGlobalMatrices.resize(count, glm::mat4(1.0f));

        for (size_t i = 0; i < count; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            glm::mat4 localMat = (i < Transforms.size()) ? Transforms[i].ToMatrix() : bone.LocalTransform;

            int parentID = bone.ParentID;
            if (parentID >= 0 && parentID < static_cast<int>(count)) {
                outGlobalMatrices[i] = outGlobalMatrices[parentID] * localMat;
            } else {
                outGlobalMatrices[i] = localMat;
            }
        }
    }

    void AnimationPose::ComputeSkinningMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outSkinningMatrices) const {
        std::vector<glm::mat4> globalMatrices;
        ComputeGlobalMatrices(skeleton, globalMatrices);

        size_t count = skeleton.GetBoneCount();
        outSkinningMatrices.resize(count);
        for (size_t i = 0; i < count; ++i) {
            outSkinningMatrices[i] = globalMatrices[i] * skeleton.GetBones()[i].InverseBindMatrix;
        }
    }

}
