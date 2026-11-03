#pragma once
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationPose.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Axle {

    enum class AnimationInterpolation : uint32_t { Step = 0, Linear = 1, CubicSpline = 2, StepNext = 3 };

    template<typename T>
    struct Keyframe {
        float TimeStamp = 0.0f; // In seconds
        T Value{};
        glm::vec4 InTangent{0}, OutTangent{0}; // xyz vectors, xyzw quaternion derivatives
    };

    struct AnimationChannel {
        std::string BoneName;
        int BoneIndex = -1; // Cached index in Skeleton

        AnimationInterpolation PositionsInterpolation = AnimationInterpolation::Linear;
        AnimationInterpolation RotationsInterpolation = AnimationInterpolation::Linear;
        AnimationInterpolation ScalesInterpolation = AnimationInterpolation::Linear;
        std::vector<Keyframe<glm::vec3>> Positions;
        std::vector<Keyframe<glm::quat>> Rotations;
        std::vector<Keyframe<glm::vec3>> Scales;

        glm::vec3 InterpolatePosition(float time) const;
        glm::quat InterpolateRotation(float time) const;
        glm::vec3 InterpolateScale(float time) const;
        glm::mat4 EvaluateTransform(float time) const;
        BoneTransform EvaluateBoneTransform(float time) const;
        BoneTransform EvaluateBoneTransform(float time, const BoneTransform& bind) const;
    };

    class AnimationClip {
    public:
        AnimationClip() = default;
        AnimationClip(std::string name, float duration, float ticksPerSecond = 30.0f)
            : m_Name(std::move(name)), m_Duration(duration), m_TicksPerSecond(ticksPerSecond) {}

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        float GetDuration() const { return m_Duration; }
        void SetDuration(float duration) { m_Duration = duration; }

        float GetTicksPerSecond() const { return m_TicksPerSecond; }
        void SetTicksPerSecond(float tps) { m_TicksPerSecond = tps; }

        void AddChannel(const AnimationChannel& channel);
        const AnimationChannel* FindChannel(const std::string& boneName) const;
        const AnimationChannel* FindChannel(int boneIndex) const;

        const std::vector<AnimationChannel>& GetChannels() const { return m_Channels; }
        std::vector<AnimationChannel>& GetChannels() { return m_Channels; }
        size_t GetChannelCount() const { return m_Channels.size(); }

        bool Validate(const Skeleton* skeleton = nullptr, std::vector<std::string>* errors = nullptr) const;
        bool RebuildDerivedData(const Skeleton* skeleton = nullptr, std::vector<std::string>* errors = nullptr);
        bool ReplaceChannels(std::vector<AnimationChannel> channels, const Skeleton* skeleton = nullptr,
                             std::vector<std::string>* errors = nullptr);

        // Wrap or clamp time to clip duration
        float WrapTime(float time, bool loop = true) const;

        // Sample local bone poses at time t for a given skeleton (into raw matrices or AnimationPose)
        bool SamplePose(float time, const Skeleton& skeleton, std::vector<glm::mat4>& outLocalTransforms, bool loop = true) const;
        bool SamplePose(float time, const Skeleton& skeleton, AnimationPose& outPose, bool loop = true) const;

        // Hierarchical forward kinematics solving global transforms: Global[i] = Global[parent] * Local[i]
        static bool ComputeGlobalPose(const Skeleton& skeleton, const std::vector<glm::mat4>& localTransforms, std::vector<glm::mat4>& outGlobalMatrices);

        // Compute final GPU skinning palette matrices: Skinning[i] = Global[i] * InverseBindMatrix[i]
        static bool ComputeSkinningMatrices(const Skeleton& skeleton, const std::vector<glm::mat4>& globalMatrices, std::vector<glm::mat4>& outSkinningMatrices);

    private:
        std::string m_Name;
        float m_Duration = 0.0f;
        float m_TicksPerSecond = 30.0f;
        std::vector<AnimationChannel> m_Channels;
        std::unordered_map<std::string, size_t> m_BoneChannelMap;
    };

}
