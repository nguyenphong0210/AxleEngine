#include <Axle/AnimationPose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    bool Bone::GetBindTransform(BoneTransform& out) const {
        if (!HasExplicitBindTRS) return BoneTransform::TryFromMatrix(LocalTransform, out);
        BoneTransform candidate(BindTranslation, BindRotation, BindScale);
        if (!candidate.IsValid()) return false;
        candidate.Rotation = glm::normalize(candidate.Rotation);
        const auto matrix = candidate.ToMatrix();
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (std::abs(matrix[c][r] - LocalTransform[c][r]) > 1e-4f * std::max(1.0f, std::abs(LocalTransform[c][r]))) return false;
        out = candidate;
        return true;
    }

    glm::mat4 BoneTransform::ToMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), Translation);
        glm::mat4 r = glm::mat4_cast(Rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), Scale);
        return t * r * s;
    }

    bool BoneTransform::IsValid() const {
        if (!std::isfinite(Translation.x) || !std::isfinite(Translation.y) || !std::isfinite(Translation.z))
            return false;
        if (!std::isfinite(Scale.x) || !std::isfinite(Scale.y) || !std::isfinite(Scale.z))
            return false;
        if (std::abs(Scale.x) < 1e-6f || std::abs(Scale.y) < 1e-6f || std::abs(Scale.z) < 1e-6f)
            return false;
        if (!std::isfinite(Rotation.x) || !std::isfinite(Rotation.y) || !std::isfinite(Rotation.z) || !std::isfinite(Rotation.w))
            return false;
        float qlen2 = glm::dot(Rotation, Rotation);
        if (!std::isfinite(qlen2) || qlen2 < 1e-6f)
            return false;
        return true;
    }

    bool BoneTransform::TryFromMatrix(const glm::mat4& mat, BoneTransform& out) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(mat[c][r])) return false;
            }
        }
        // Affine bottom row check [0, 0, 0, 1]
        if (std::abs(mat[0][3]) > 1e-4f || std::abs(mat[1][3]) > 1e-4f ||
            std::abs(mat[2][3]) > 1e-4f || std::abs(mat[3][3] - 1.0f) > 1e-4f) {
            return false;
        }

        glm::vec3 col0 = glm::vec3(mat[0]);
        glm::vec3 col1 = glm::vec3(mat[1]);
        glm::vec3 col2 = glm::vec3(mat[2]);

        float sx = glm::length(col0);
        float sy = glm::length(col1);
        float sz = glm::length(col2);

        if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f) return false;

        col0 /= sx;
        col1 /= sy;
        col2 /= sz;

        // Orthogonality check (reject sheared matrices)
        if (std::abs(glm::dot(col0, col1)) > 1e-3f ||
            std::abs(glm::dot(col1, col2)) > 1e-3f ||
            std::abs(glm::dot(col2, col0)) > 1e-3f) {
            return false;
        }

        // Signed determinant / handedness check
        float det = glm::dot(col0, glm::cross(col1, col2));
        if (std::abs(det) < 1e-6f) return false;

        if (det < 0.0f) {
            // Negative determinant indicates reflection: flip col0 and sx
            col0 = -col0;
            sx = -sx;
        }

        glm::mat3 rotMat(col0, col1, col2);
        out.Translation = glm::vec3(mat[3]);
        out.Rotation = glm::normalize(glm::quat_cast(rotMat));
        out.Scale = glm::vec3(sx, sy, sz);

        if (!out.IsValid()) return false;

        // Verify recompose against mat
        glm::mat4 recomposed = out.ToMatrix();
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (std::abs(recomposed[c][r] - mat[c][r]) > 1e-3f) {
                    return false;
                }
            }
        }

        return true;
    }

    BoneTransform BoneTransform::FromMatrix(const glm::mat4& mat) {
        BoneTransform result;
        if (TryFromMatrix(mat, result)) {
            return result;
        }

        // Fallback for non-strictly-affine matrices: best-effort extraction
        result.Translation = glm::vec3(mat[3]);
        glm::vec3 col0 = glm::vec3(mat[0]);
        glm::vec3 col1 = glm::vec3(mat[1]);
        glm::vec3 col2 = glm::vec3(mat[2]);

        result.Scale.x = glm::length(col0);
        result.Scale.y = glm::length(col1);
        result.Scale.z = glm::length(col2);

        if (result.Scale.x > 1e-6f) col0 /= result.Scale.x;
        if (result.Scale.y > 1e-6f) col1 /= result.Scale.y;
        if (result.Scale.z > 1e-6f) col2 /= result.Scale.z;

        float det = glm::dot(col0, glm::cross(col1, col2));
        if (det < 0.0f) {
            col0 = -col0;
            result.Scale.x = -result.Scale.x;
        }

        glm::mat3 rotMat(col0, col1, col2);
        result.Rotation = glm::normalize(glm::quat_cast(rotMat));
        return result;
    }

    BoneTransform BoneTransform::Blend(const BoneTransform& a, const BoneTransform& b, float weight) {
        float factor = std::clamp(weight, 0.0f, 1.0f);
        BoneTransform result;
        result.Translation = glm::mix(a.Translation, b.Translation, factor);

        glm::quat rotA = glm::normalize(a.Rotation);
        glm::quat rotB = glm::normalize(b.Rotation);
        // Shortest-path spherical linear interpolation (SLERP)
        if (glm::dot(rotA, rotB) < 0.0f) {
            rotB = -rotB;
        }
        result.Rotation = glm::normalize(glm::slerp(rotA, rotB, factor));
        result.Scale = glm::mix(a.Scale, b.Scale, factor);
        return result;
    }

    AnimationPose::AnimationPose(size_t boneCount) {
        Resize(boneCount);
    }

    void AnimationPose::Resize(size_t boneCount) {
        Transforms.resize(boneCount);
    }

    bool AnimationPose::InitFromSkeletonBindPose(const Skeleton& skeleton) {
        if (!skeleton.IsValid()) return false;
        size_t count = skeleton.GetBoneCount();
        Transforms.resize(count);
        for (size_t i = 0; i < count; ++i) {
            if (!skeleton.GetBones()[i].GetBindTransform(Transforms[i])) {
                Transforms[i] = BoneTransform::FromMatrix(skeleton.GetBones()[i].LocalTransform);
            }
        }
        BindSkeleton(skeleton);
        return true;
    }

    bool AnimationPose::Blend(const AnimationPose& a, const AnimationPose& b, float weight, AnimationPose& outPose) {
        if (a.Transforms.size() != b.Transforms.size() || a.Transforms.empty() ||
            a.m_SkeletonBinding != b.m_SkeletonBinding || !std::isfinite(weight)) return false;
        AnimationPose result(a.Transforms.size());
        result.m_SkeletonBinding = a.m_SkeletonBinding;
        for (size_t i = 0; i < result.Transforms.size(); ++i) {
            if (!a.Transforms[i].IsValid() || !b.Transforms[i].IsValid()) return false;
            result.Transforms[i] = BoneTransform::Blend(a.Transforms[i], b.Transforms[i], weight);
            if (!result.Transforms[i].IsValid()) return false;
        }
        outPose = std::move(result);
        return true;
    }

    void AnimationPose::ToLocalMatrices(std::vector<glm::mat4>& outLocalMatrices) const {
        outLocalMatrices.resize(Transforms.size());
        for (size_t i = 0; i < Transforms.size(); ++i) {
            outLocalMatrices[i] = Transforms[i].ToMatrix();
        }
    }

    bool AnimationPose::ComputeGlobalMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outGlobalMatrices) const {
        std::vector<size_t> order;
        if (Transforms.size() != skeleton.GetBoneCount() || (m_SkeletonBinding && m_SkeletonBinding != &skeleton) || !skeleton.BuildEvaluationOrder(order)) {
            outGlobalMatrices.clear();
            return false;
        }

        size_t count = skeleton.GetBoneCount();
        outGlobalMatrices.assign(count, glm::mat4(1.0f));

        for (size_t i : order) {
            const Bone& bone = skeleton.GetBones()[i];
            if (!Transforms[i].IsValid()) { outGlobalMatrices.clear(); return false; }
            glm::mat4 localMat = Transforms[i].ToMatrix();

            int parentID = bone.ParentID;
            if (parentID >= 0 && parentID < static_cast<int>(count)) {
                outGlobalMatrices[i] = outGlobalMatrices[parentID] * localMat;
            } else {
                outGlobalMatrices[i] = localMat;
            }
        }
        return true;
    }

    bool AnimationPose::ComputeSkinningMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outSkinningMatrices) const {
        std::vector<glm::mat4> globalMatrices;
        if (!ComputeGlobalMatrices(skeleton, globalMatrices)) {
            outSkinningMatrices.clear();
            return false;
        }

        size_t count = skeleton.GetBoneCount();
        outSkinningMatrices.resize(count);
        for (size_t i = 0; i < count; ++i) {
            outSkinningMatrices[i] = globalMatrices[i] * skeleton.GetBones()[i].InverseBindMatrix;
        }
        return true;
    }

}
