#include <Axle/AnimationClip.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <type_traits>

namespace Axle {

    namespace {
        template<class T> T Spline(const Keyframe<T>& a, const Keyframe<T>& b, float t, float duration) {
            const auto vector = [](const glm::vec4& v) {
                if constexpr (std::is_same_v<T, glm::quat>) return glm::quat(v.w, v.x, v.y, v.z);
                else return glm::vec3(v);
            };
            const float t2 = t*t, t3 = t2*t;
            return a.Value * (2*t3-3*t2+1) + vector(a.OutTangent) * (duration*(t3-2*t2+t)) +
                   b.Value * (-2*t3+3*t2) + vector(b.InTangent) * (duration*(t3-t2));
        }
        template<class T> T Interpolate(const std::vector<Keyframe<T>>& keys, AnimationInterpolation mode, float time, T fallback) {
            if (keys.empty()) return fallback;
            if (keys.size() == 1 || time <= keys.front().TimeStamp) return keys.front().Value;
            if (time >= keys.back().TimeStamp) return keys.back().Value;
            const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                [](float value, const auto& key) { return value < key.TimeStamp; });
            const auto& a = *(next-1); const auto& b = *next;
            if (mode == AnimationInterpolation::Step) return a.Value;
            const float duration = b.TimeStamp - a.TimeStamp;
            const float factor = (time - a.TimeStamp) / duration;
            if (mode == AnimationInterpolation::CubicSpline) return Spline(a, b, factor, duration);
            if constexpr (std::is_same_v<T, glm::quat>) {
                auto first = glm::normalize(a.Value), second = glm::normalize(b.Value);
                if (glm::dot(first, second) < 0) second = -second;
                return glm::slerp(first, second, factor);
            } else return glm::mix(a.Value, b.Value, factor);
        }
    }
    glm::vec3 AnimationChannel::InterpolatePosition(float time) const {
        return Interpolate(Positions, PositionsInterpolation, time, glm::vec3(0));
    }
    glm::vec3 AnimationChannel::InterpolateScale(float time) const {
        return Interpolate(Scales, ScalesInterpolation, time, glm::vec3(1));
    }
    glm::quat AnimationChannel::InterpolateRotation(float time) const {
        const auto value = Interpolate(Rotations, RotationsInterpolation, time, glm::quat(1,0,0,0));
        return glm::dot(value, value) > 1e-12f ? glm::normalize(value) : glm::quat(0,0,0,0);
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
        for (const auto& channel : m_Channels) if (channel.BoneName == boneName) return &channel;
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

            for (const auto mode : {ch.PositionsInterpolation, ch.RotationsInterpolation, ch.ScalesInterpolation})
                if (static_cast<uint32_t>(mode) > 2) fail("Unknown interpolation mode");
            const auto tangentsValid = [&](const auto& keys, AnimationInterpolation mode) {
                if (mode != AnimationInterpolation::CubicSpline) return;
                if (!keys.empty() && keys.size() < 2) fail("Cubic tracks require at least two keys");
                for (const auto& key : keys) for (int axis = 0; axis < 4; ++axis)
                    if (!std::isfinite(key.InTangent[axis]) || !std::isfinite(key.OutTangent[axis])) fail("Nonfinite cubic tangent");
            };
            tangentsValid(ch.Positions, ch.PositionsInterpolation); tangentsValid(ch.Rotations, ch.RotationsInterpolation); tangentsValid(ch.Scales, ch.ScalesInterpolation);

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
        AnimationPose result(boneCount);

        float animTime = WrapTime(time, loop);

        for (size_t i = 0; i < boneCount; ++i) {
            const Bone& bone = skeleton.GetBones()[i];
            BoneTransform bind;
            if (!bone.GetBindTransform(bind)) {
                bind = BoneTransform::FromMatrix(bone.LocalTransform);
            }

            const AnimationChannel* channel = FindChannel(bone.Name);
            if (!channel && bone.ID >= 0) {
                channel = FindChannel(bone.ID);
            }

            if (channel) {
                result.Transforms[i] = channel->EvaluateBoneTransform(animTime, bind);
            } else {
                result.Transforms[i] = bind;
            }
        }
        for (const auto& transform : result.Transforms) if (!transform.IsValid()) return false;
        result.BindSkeleton(skeleton);
        outPose = std::move(result);
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
