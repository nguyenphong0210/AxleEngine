#include <Axle/AnimationClip.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace Axle {

    glm::vec3 AnimationChannel::InterpolatePosition(float time) const {
        if (Positions.empty()) return glm::vec3(0.0f);
        if (Positions.size() == 1 || time <= Positions.front().TimeStamp) {
            return Positions.front().Value;
        }
        if (time >= Positions.back().TimeStamp) {
            return Positions.back().Value;
        }

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
            return glm::normalize(Rotations.front().Value);
        }
        if (time >= Rotations.back().TimeStamp) {
            return glm::normalize(Rotations.back().Value);
        }

        for (size_t i = 0; i < Rotations.size() - 1; ++i) {
            if (time < Rotations[i + 1].TimeStamp) {
                float dt = Rotations[i + 1].TimeStamp - Rotations[i].TimeStamp;
                float factor = (dt > 1e-6f) ? (time - Rotations[i].TimeStamp) / dt : 0.0f;
                glm::quat q0 = glm::normalize(Rotations[i].Value);
                glm::quat q1 = glm::normalize(Rotations[i + 1].Value);
                if (glm::dot(q0, q1) < 0.0f) {
                    q1 = -q1;
                }
                return glm::normalize(glm::slerp(q0, q1, factor));
            }
        }
        return glm::normalize(Rotations.back().Value);
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

    BoneTransform AnimationChannel::EvaluateBoneTransform(float time, const BoneTransform& bind) const {
        BoneTransform xform = bind;
        if (!Positions.empty()) {
            xform.Translation = InterpolatePosition(time);
        }
        if (!Rotations.empty()) {
            xform.Rotation = InterpolateRotation(time);
        }
        if (!Scales.empty()) {
            xform.Scale = InterpolateScale(time);
        }
        return xform;
    }

    BoneTransform AnimationChannel::EvaluateBoneTransform(float time) const {
        BoneTransform bind;
        return EvaluateBoneTransform(time, bind);
    }

    glm::mat4 AnimationChannel::EvaluateTransform(float time) const {
        return EvaluateBoneTransform(time).ToMatrix();
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

    bool AnimationClip::Validate(const Skeleton* skeleton, std::vector<std::string>* errors) const {
        if (errors) errors->clear();
        bool valid = true;
        auto fail = [&](const std::string& err) {
            valid = false;
            if (errors) errors->push_back(err);
        };

        if (m_Duration < 0.0f || !std::isfinite(m_Duration)) {
            fail("Clip duration must be finite and non-negative.");
        }
        if (m_TicksPerSecond <= 0.0f || !std::isfinite(m_TicksPerSecond)) {
            fail("Ticks per second must be positive and finite.");
        }

        std::unordered_set<std::string> seenBones;
        for (size_t c = 0; c < m_Channels.size(); ++c) {
            const auto& ch = m_Channels[c];
            if (ch.BoneName.empty()) {
                fail("Channel " + std::to_string(c) + " has empty BoneName.");
            } else if (!seenBones.insert(ch.BoneName).second) {
                fail("Duplicate channel for bone: " + ch.BoneName);
            }

            if (skeleton && skeleton->FindBoneIndex(ch.BoneName) == -1) {
                fail("Channel references bone not found in skeleton: " + ch.BoneName);
            }

            for (size_t i = 0; i < ch.Positions.size(); ++i) {
                float t = ch.Positions[i].TimeStamp;
                if (!std::isfinite(t) || t < 0.0f || t > m_Duration + 1e-5f) {
                    fail("Invalid position timestamp at channel " + ch.BoneName);
                }
                if (i > 0 && t <= ch.Positions[i - 1].TimeStamp) {
                    fail("Non-increasing position timestamp at channel " + ch.BoneName);
                }
                const auto& v = ch.Positions[i].Value;
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                    fail("Non-finite position value at channel " + ch.BoneName);
                }
            }

            for (size_t i = 0; i < ch.Rotations.size(); ++i) {
                float t = ch.Rotations[i].TimeStamp;
                if (!std::isfinite(t) || t < 0.0f || t > m_Duration + 1e-5f) {
                    fail("Invalid rotation timestamp at channel " + ch.BoneName);
                }
                if (i > 0 && t <= ch.Rotations[i - 1].TimeStamp) {
                    fail("Non-increasing rotation timestamp at channel " + ch.BoneName);
                }
                const auto& q = ch.Rotations[i].Value;
                if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
                    fail("Non-finite rotation value at channel " + ch.BoneName);
                }
                if (glm::dot(q, q) < 1e-6f) {
                    fail("Near-zero rotation quaternion at channel " + ch.BoneName);
                }
            }

            for (size_t i = 0; i < ch.Scales.size(); ++i) {
                float t = ch.Scales[i].TimeStamp;
                if (!std::isfinite(t) || t < 0.0f || t > m_Duration + 1e-5f) {
                    fail("Invalid scale timestamp at channel " + ch.BoneName);
                }
                if (i > 0 && t <= ch.Scales[i - 1].TimeStamp) {
                    fail("Non-increasing scale timestamp at channel " + ch.BoneName);
                }
                const auto& s = ch.Scales[i].Value;
                if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z)) {
                    fail("Non-finite scale value at channel " + ch.BoneName);
                }
                if (std::abs(s.x) < 1e-6f || std::abs(s.y) < 1e-6f || std::abs(s.z) < 1e-6f) {
                    fail("Near-zero scale component at channel " + ch.BoneName);
                }
            }
        }

        return valid;
    }

    bool AnimationClip::RebuildDerivedData(const Skeleton* skeleton, std::vector<std::string>* errors) {
        m_BoneChannelMap.clear();
        for (size_t i = 0; i < m_Channels.size(); ++i) {
            m_BoneChannelMap[m_Channels[i].BoneName] = i;
            if (skeleton) {
                m_Channels[i].BoneIndex = skeleton->FindBoneIndex(m_Channels[i].BoneName);
            }
        }
        return Validate(skeleton, errors);
    }

    bool AnimationClip::ReplaceChannels(std::vector<AnimationChannel> channels, const Skeleton* skeleton,
                                        std::vector<std::string>* errors) {
        auto candidate = *this;
        candidate.m_Channels = std::move(channels);
        if (!candidate.RebuildDerivedData(skeleton, errors)) return false;
        *this = std::move(candidate);
        return true;
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

    bool AnimationClip::SamplePose(float time, const Skeleton& skeleton, AnimationPose& outPose, bool loop) const {
        if (!std::isfinite(time) || !skeleton.IsValid() || !Validate(&skeleton)) {
            return false;
        }

        size_t boneCount = skeleton.GetBoneCount();
        outPose.Resize(boneCount);

        float animTime = WrapTime(time, loop);

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            BoneTransform bind;
            if (!BoneTransform::TryFromMatrix(bone.LocalTransform, bind)) {
                bind = BoneTransform::FromMatrix(bone.LocalTransform);
            }

            const AnimationChannel* channel = FindChannel(bone.Name);
            if (!channel && bone.ID >= 0) {
                channel = FindChannel(bone.ID);
            }

            if (channel) {
                outPose.Transforms[i] = channel->EvaluateBoneTransform(animTime, bind);
            } else {
                outPose.Transforms[i] = bind;
            }
        }
        outPose.BindSkeleton(skeleton);
        return true;
    }

    bool AnimationClip::SamplePose(float time, const Skeleton& skeleton, std::vector<glm::mat4>& outLocalTransforms, bool loop) const {
        AnimationPose pose;
        if (!SamplePose(time, skeleton, pose, loop)) {
            outLocalTransforms.clear();
            return false;
        }
        pose.ToLocalMatrices(outLocalTransforms);
        return true;
    }

    bool AnimationClip::ComputeGlobalPose(const Skeleton& skeleton, const std::vector<glm::mat4>& localTransforms, std::vector<glm::mat4>& outGlobalMatrices) {
        std::vector<size_t> order;
        if (localTransforms.size() != skeleton.GetBoneCount() || !skeleton.BuildEvaluationOrder(order)) {
            outGlobalMatrices.clear();
            return false;
        }

        size_t boneCount = skeleton.GetBoneCount();
        outGlobalMatrices.assign(boneCount, glm::mat4(1.0f));

        for (size_t i : order) {
            const Bone& bone = skeleton.GetBones()[i];
            glm::mat4 localMat = (i < localTransforms.size()) ? localTransforms[i] : bone.LocalTransform;

            int parentID = bone.ParentID;
            if (parentID >= 0 && parentID < static_cast<int>(boneCount)) {
                outGlobalMatrices[i] = outGlobalMatrices[parentID] * localMat;
            } else {
                outGlobalMatrices[i] = localMat;
            }
        }
        return true;
    }

    bool AnimationClip::ComputeSkinningMatrices(const Skeleton& skeleton, const std::vector<glm::mat4>& globalMatrices, std::vector<glm::mat4>& outSkinningMatrices) {
        if (globalMatrices.size() != skeleton.GetBoneCount()) {
            outSkinningMatrices.clear();
            return false;
        }

        size_t boneCount = skeleton.GetBoneCount();
        outSkinningMatrices.resize(boneCount, glm::mat4(1.0f));

        for (size_t i = 0; i < boneCount; ++i) {
            outSkinningMatrices[i] = globalMatrices[i] * skeleton.GetBones()[i].InverseBindMatrix;
        }
        return true;
    }

}
