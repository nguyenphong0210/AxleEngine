#pragma once
#include <Axle/AnimationClip.hpp>
#include <Axle/BlendSpace1D.hpp>
#include <Axle/AnimationPose.hpp>
#include <Axle/Skeleton.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Axle {

    enum class AnimationNodeType {
        Clip,
        BlendSpace1D
    };

    struct AnimationState {
        std::string Name;
        AnimationNodeType Type = AnimationNodeType::Clip;
        std::shared_ptr<AnimationClip> Clip;
        std::shared_ptr<BlendSpace1D> BlendSpace;
        std::string BlendParameterName = "Speed"; // Used if Type == BlendSpace1D
        bool Loop = true;
        float SpeedMultiplier = 1.0f;

        void Sample(float time, float blendParam, const Skeleton& skeleton, AnimationPose& outPose) const;
        float GetDuration(float blendParam = 0.0f) const;
    };

    struct AnimationTransition {
        std::string FromState;
        std::string ToState;
        float Duration = 0.2f;       // Crossfade duration in seconds
        bool HasExitTime = false;     // Wait for state playback to finish
        float ExitTime = 1.0f;        // Normalized time [0..1]
        std::function<bool()> Condition; // Optional condition predicate
    };

    class AnimationStateMachine {
    public:
        AnimationStateMachine() = default;
        explicit AnimationStateMachine(std::string name) : m_Name(std::move(name)) {}

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& name) { m_Name = name; }

        void AddState(const AnimationState& state);
        void AddTransition(const AnimationTransition& transition);

        const AnimationState* FindState(const std::string& name) const;

        void SetCurrentState(const std::string& stateName, float transitionDuration = 0.0f);
        const std::string& GetCurrentStateName() const { return m_CurrentStateName; }

        bool IsTransitioning() const { return m_IsTransitioning; }
        float GetTransitionProgress() const;

        // Parameters
        void SetFloat(const std::string& name, float value) { m_FloatParams[name] = value; }
        float GetFloat(const std::string& name, float defaultVal = 0.0f) const;

        void SetBool(const std::string& name, bool value) { m_BoolParams[name] = value; }
        bool GetBool(const std::string& name, bool defaultVal = false) const;

        void SetTrigger(const std::string& name) { m_TriggerParams[name] = true; }
        bool CheckAndConsumeTrigger(const std::string& name);

        // Update animation clock, evaluate transitions and blend poses
        void Update(float dt, const Skeleton& skeleton);

        const AnimationPose& GetCurrentPose() const { return m_CurrentPose; }
        const std::vector<glm::mat4>& GetGlobalMatrices() const { return m_GlobalMatrices; }
        const std::vector<glm::mat4>& GetSkinningMatrices() const { return m_SkinningMatrices; }

    private:
        void CheckTransitions();

    private:
        std::string m_Name;
        std::unordered_map<std::string, AnimationState> m_States;
        std::vector<AnimationTransition> m_Transitions;

        std::unordered_map<std::string, float> m_FloatParams;
        std::unordered_map<std::string, bool> m_BoolParams;
        std::unordered_map<std::string, bool> m_TriggerParams;

        std::string m_CurrentStateName;
        float m_CurrentStateTime = 0.0f;

        bool m_IsTransitioning = false;
        std::string m_TransitionTargetName;
        float m_TargetStateTime = 0.0f;
        float m_TransitionDuration = 0.2f;
        float m_TransitionElapsed = 0.0f;

        AnimationPose m_CurrentPose;
        std::vector<glm::mat4> m_GlobalMatrices;
        std::vector<glm::mat4> m_SkinningMatrices;
    };

}
