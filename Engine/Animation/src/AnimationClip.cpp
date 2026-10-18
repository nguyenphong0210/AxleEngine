#include <Axle/AnimationClip.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace Axle {

    glm::vec3 AnimationChannel::InterpolatePosition(float time) const {
        if (Positions.empty()) return glm::vec3(0.0f);
        if (Positions.size() == 1 || time <= Positions.front().TimeStamp) {
            return Positions.front().Value;
        }
        if (time >= Positions.back().TimeStamp) {
            return Positions.back().Value;
        }

        // Find keyframe interval
        for (size_t i = 0; i < Positions.size() - 1; ++i) {
            if (time < Positions[i + 1].TimeStamp) {
                float dt = Positions[i + 1].TimeStamp - Positions[i].TimeStamp;
                float factor = (dt > 1e-6f) ? (time - Positions[i].TimeStamp) / dt : 0.0f;
                return glm::mix(Positions[i].Value, Positions[i + 1].Value, factor);
            }
        }
        return Positions.back().Value;
    }

    glm::quat AnimationChannel::InterpolateRotation(float time) const {
        if (Rotations.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (Rotations.size() == 1 || time <= Rotations.front().TimeStamp) {
            return Rotations.front().Value;
        }
        if (time >= Rotations.back().TimeStamp) {
            return Rotations.back().Value;
        }

        for (size_t i = 0; i < Rotations.size() - 1; ++i) {
            if (time < Rotations[i + 1].TimeStamp) {
                float dt = Rotations[i + 1].TimeStamp - Rotations[i].TimeStamp;
                float factor = (dt > 1e-6f) ? (time - Rotations[i].TimeStamp) / dt : 0.0f;
                return glm::slerp(Rotations[i].Value, Rotations[i + 1].Value, factor);
            }
        }
        return Rotations.back().Value;
    }

    glm::vec3 AnimationChannel::InterpolateScale(float time) const {
        if (Scales.empty()) return glm::vec3(1.0f);
        if (Scales.size() == 1 || time <= Scales.front().TimeStamp) {
            return Scales.front().Value;
        }
        if (time >= Scales.back().TimeStamp) {
            return Scales.back().Value;
        }

        for (size_t i = 0; i < Scales.size() - 1; ++i) {
            if (time < Scales[i + 1].TimeStamp) {
                float dt = Scales[i + 1].TimeStamp - Scales[i].TimeStamp;
                float factor = (dt > 1e-6f) ? (time - Scales[i].TimeStamp) / dt : 0.0f;
                return glm::mix(Scales[i].Value, Scales[i + 1].Value, factor);
            }
        }
        return Scales.back().Value;
    }

    glm::mat4 AnimationChannel::EvaluateTransform(float time) const {
        glm::vec3 pos = InterpolatePosition(time);
        glm::quat rot = InterpolateRotation(time);
        glm::vec3 scl = InterpolateScale(time);

        glm::mat4 t = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 r = glm::mat4_cast(rot);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scl);
        return t * r * s;
    }

    BoneTransform AnimationChannel::EvaluateBoneTransform(float time) const {
        BoneTransform xform;
        xform.Translation = InterpolatePosition(time);
        xform.Rotation = InterpolateRotation(time);
        xform.Scale = InterpolateScale(time);
        return xform;
    }

    void AnimationClip::AddChannel(const AnimationChannel& channel) {
        size_t index = m_Channels.size();
        m_Channels.push_back(channel);
        m_BoneChannelMap[channel.BoneName] = index;
    }

    const AnimationChannel* AnimationClip::FindChannel(const std::string& boneName) const {
        auto it = m_BoneChannelMap.find(boneName);
        if (it != m_BoneChannelMap.end()) {
            return &m_Channels[it->second];
        }
        return nullptr;
    }

    const AnimationChannel* AnimationClip::FindChannel(int boneIndex) const {
        if (boneIndex < 0) return nullptr;
        for (const auto& ch : m_Channels) {
            if (ch.BoneIndex == boneIndex) return &ch;
        }
        return nullptr;
    }

    float AnimationClip::WrapTime(float time, bool loop) const {
        if (m_Duration <= 0.0f) return 0.0f;
        if (!loop) {
            return std::clamp(time, 0.0f, m_Duration);
        }
        float wrapped = std::fmod(time, m_Duration);
        if (wrapped < 0.0f) wrapped += m_Duration;
        return wrapped;
    }

    void AnimationClip::SamplePose(float time, const Skeleton& skeleton, std::vector<glm::mat4>& outLocalTransforms, bool loop) const {
        size_t boneCount = skeleton.GetBoneCount();
        outLocalTransforms.resize(boneCount);

        float animTime = WrapTime(time, loop);

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            const AnimationChannel* channel = FindChannel(bone.Name);
            if (!channel && bone.ID >= 0) {
                channel = FindChannel(bone.ID);
            }

            if (channel) {
                outLocalTransforms[i] = channel->EvaluateTransform(animTime);
            } else {
                outLocalTransforms[i] = bone.LocalTransform;
            }
        }
    }

    void AnimationClip::SamplePose(float time, const Skeleton& skeleton, AnimationPose& outPose, bool loop) const {
        size_t boneCount = skeleton.GetBoneCount();
        outPose.Resize(boneCount);

        float animTime = WrapTime(time, loop);

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            const AnimationChannel* channel = FindChannel(bone.Name);
            if (!channel && bone.ID >= 0) {
                channel = FindChannel(bone.ID);
            }

            if (channel) {
                outPose.Transforms[i] = channel->EvaluateBoneTransform(animTime);
            } else {
                outPose.Transforms[i] = BoneTransform::FromMatrix(bone.LocalTransform);
            }
        }
    }

    void AnimationClip::ComputeGlobalPose(const Skeleton& skeleton, const std::vector<glm::mat4>& localTransforms, std::vector<glm::mat4>& outGlobalMatrices) {
        size_t boneCount = skeleton.GetBoneCount();
        outGlobalMatrices.resize(boneCount, glm::mat4(1.0f));

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            glm::mat4 localMat = (i < localTransforms.size()) ? localTransforms[i] : bone.LocalTransform;

            int parentID = bone.ParentID;
            if (parentID >= 0 && parentID < static_cast<int>(boneCount)) {
                outGlobalMatrices[i] = outGlobalMatrices[parentID] * localMat;
            } else {
                outGlobalMatrices[i] = localMat;
            }
        }
    }

    void AnimationClip::ComputeSkinningMatrices(const Skeleton& skeleton, const std::vector<glm::mat4>& globalMatrices, std::vector<glm::mat4>& outSkinningMatrices) {
        size_t boneCount = skeleton.GetBoneCount();
        outSkinningMatrices.resize(boneCount, glm::mat4(1.0f));

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            const glm::mat4& globalMat = (i < globalMatrices.size()) ? globalMatrices[i] : glm::mat4(1.0f);
            outSkinningMatrices[i] = globalMat * bone.InverseBindMatrix;
        }
    }

}
