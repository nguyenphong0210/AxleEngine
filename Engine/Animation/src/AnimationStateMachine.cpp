#include <Axle/AnimationStateMachine.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    bool TransitionRule::Evaluate(const AnimationStateMachine& sm) const {
        switch (Operator) {
            case TransitionConditionOperator::AlwaysTrue:
                return true;
            case TransitionConditionOperator::Greater:
                return sm.GetFloat(Parameter) > FloatOperand;
            case TransitionConditionOperator::Less:
                return sm.GetFloat(Parameter) < FloatOperand;
            case TransitionConditionOperator::GreaterOrEqual:
                return sm.GetFloat(Parameter) >= FloatOperand;
            case TransitionConditionOperator::LessOrEqual:
                return sm.GetFloat(Parameter) <= FloatOperand;
            case TransitionConditionOperator::Equal:
                return std::abs(sm.GetFloat(Parameter) - FloatOperand) < 1e-4f;
            case TransitionConditionOperator::NotEqual:
                return std::abs(sm.GetFloat(Parameter) - FloatOperand) >= 1e-4f;
            case TransitionConditionOperator::IsTrue:
                return sm.GetBool(Parameter, false) == true;
            case TransitionConditionOperator::IsFalse:
                return sm.GetBool(Parameter, false) == false;
            case TransitionConditionOperator::Triggered:
                return sm.GetBool(Parameter, false);
            default:
                return true;
        }
    }

    void AnimationState::Sample(float time, float blendParam, const Skeleton& skeleton, AnimationPose& outPose) const {
        if (Type == AnimationNodeType::Clip) {
            if (Clip) {
                Clip->SamplePose(time, skeleton, outPose, Loop);
            } else {
                outPose.InitFromSkeletonBindPose(skeleton);
            }
        } else if (Type == AnimationNodeType::BlendSpace1D) {
            if (BlendSpace) {
                BlendSpace->SamplePose(blendParam, time, skeleton, outPose, Loop);
            } else {
                outPose.InitFromSkeletonBindPose(skeleton);
            }
        }
    }

    float AnimationState::GetDuration(float blendParam) const {
        if (Type == AnimationNodeType::Clip) {
            return Clip ? Clip->GetDuration() : 1.0f;
        } else if (Type == AnimationNodeType::BlendSpace1D) {
            return BlendSpace ? BlendSpace->GetBlendedDuration(blendParam) : 1.0f;
        }
        return 1.0f;
    }

    void AnimationStateMachine::AddState(const AnimationState& state) {
        bool setInitial = m_States.empty();
        m_States[state.Name] = state;
        if (setInitial) {
            m_InitialStateName = state.Name;
            m_CurrentStateName = state.Name;
            m_CurrentStateTime = 0.0f;
        }
    }

    void AnimationStateMachine::AddTransition(const AnimationTransition& transition) {
        m_Transitions.push_back(transition);
    }

    const AnimationState* AnimationStateMachine::FindState(const std::string& name) const {
        auto it = m_States.find(name);
        if (it != m_States.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void AnimationStateMachine::SetCurrentState(const std::string& stateName, float transitionDuration) {
        if (m_CurrentStateName == stateName && !m_IsTransitioning) return;
        if (m_States.find(stateName) == m_States.end()) return;

        if (transitionDuration <= 0.0f || m_CurrentStateName.empty()) {
            m_CurrentStateName = stateName;
            m_CurrentStateTime = 0.0f;
            m_IsTransitioning = false;
        } else {
            m_IsTransitioning = true;
            m_TransitionTargetName = stateName;
            m_TransitionDuration = transitionDuration;
            m_TransitionElapsed = 0.0f;
            m_TargetStateTime = 0.0f;
        }
    }

    float AnimationStateMachine::GetTransitionProgress() const {
        if (!m_IsTransitioning || m_TransitionDuration <= 0.0f) return 1.0f;
        return std::clamp(m_TransitionElapsed / m_TransitionDuration, 0.0f, 1.0f);
    }

    float AnimationStateMachine::GetFloat(const std::string& name, float defaultVal) const {
        auto it = m_FloatParams.find(name);
        return (it != m_FloatParams.end()) ? it->second : defaultVal;
    }

    int32_t AnimationStateMachine::GetInt(const std::string& name, int32_t defaultVal) const {
        auto it = m_IntParams.find(name);
        return (it != m_IntParams.end()) ? it->second : defaultVal;
    }

    bool AnimationStateMachine::GetBool(const std::string& name, bool defaultVal) const {
        auto it = m_BoolParams.find(name);
        return (it != m_BoolParams.end()) ? it->second : defaultVal;
    }

    bool AnimationStateMachine::CheckAndConsumeTrigger(const std::string& name) {
        auto it = m_TriggerParams.find(name);
        if (it != m_TriggerParams.end() && it->second) {
            it->second = false; // consume
            return true;
        }
        return false;
    }

    void AnimationStateMachine::Reset() {
        m_CurrentStateName = m_InitialStateName;
        m_CurrentStateTime = 0.0f;
        m_IsTransitioning = false;
        m_TransitionTargetName.clear();
        m_TransitionElapsed = 0.0f;
        m_TargetStateTime = 0.0f;
        m_TriggerParams.clear();
    }

    std::shared_ptr<AnimationStateMachine> AnimationStateMachine::Clone() const {
        auto clone = std::make_shared<AnimationStateMachine>(m_Name);
        clone->m_InitialStateName = m_InitialStateName;
        clone->m_States = m_States;
        clone->m_Transitions = m_Transitions;

        // Copy persistent parameters, but clear pending triggers and reset clocks to initial state
        clone->m_FloatParams = m_FloatParams;
        clone->m_IntParams = m_IntParams;
        clone->m_BoolParams = m_BoolParams;
        clone->m_TriggerParams.clear();

        clone->m_CurrentStateName = m_CurrentStateName;
        clone->m_CurrentStateTime = m_CurrentStateTime;
        clone->m_IsTransitioning = m_IsTransitioning;
        clone->m_TransitionTargetName = m_TransitionTargetName;
        clone->m_TargetStateTime = m_TargetStateTime;
        clone->m_TransitionDuration = m_TransitionDuration;
        clone->m_TransitionElapsed = m_TransitionElapsed;

        clone->m_CurrentPose = m_CurrentPose;
        clone->m_GlobalMatrices = m_GlobalMatrices;
        clone->m_SkinningMatrices = m_SkinningMatrices;
        return clone;
    }

    void AnimationStateMachine::CheckTransitions() {
        if (m_IsTransitioning) return; // Don't interrupt active transition
        const auto* curState = FindState(m_CurrentStateName);
        if (!curState) return;

        float blendParam = GetFloat(curState->BlendParameterName, 0.0f);
        float duration = curState->GetDuration(blendParam);
        float normalizedTime = (duration > 1e-4f) ? (m_CurrentStateTime / duration) : 0.0f;

        for (const auto& trans : m_Transitions) {
            if (trans.FromState != m_CurrentStateName) continue;

            if (trans.HasExitTime && normalizedTime < trans.ExitTime) {
                continue;
            }

            // Check typed rules
            bool rulesPassed = true;
            for (const auto& rule : trans.Rules) {
                if (!rule.Evaluate(*this)) {
                    rulesPassed = false;
                    break;
                }
            }
            if (!rulesPassed) continue;

            // Check instance condition predicate
            if (trans.InstanceCondition && !trans.InstanceCondition(*this)) {
                continue;
            }

            // Legacy condition predicate
            if (trans.Condition && !trans.Condition()) {
                continue;
            }

            // Transition condition satisfied!
            SetCurrentState(trans.ToState, trans.Duration);
            break;
        }
    }

    void AnimationStateMachine::Update(float dt, const Skeleton& skeleton) {
        if (m_CurrentStateName.empty() || m_States.empty()) {
            m_CurrentPose.InitFromSkeletonBindPose(skeleton);
            m_CurrentPose.ComputeGlobalMatrices(skeleton, m_GlobalMatrices);
            m_CurrentPose.ComputeSkinningMatrices(skeleton, m_SkinningMatrices);
            return;
        }

        CheckTransitions();

        const auto* curState = FindState(m_CurrentStateName);
        if (!curState) return;

        float curSpeed = curState->SpeedMultiplier;
        m_CurrentStateTime += dt * curSpeed;

        float curParam = GetFloat(curState->BlendParameterName, 0.0f);

        if (m_IsTransitioning) {
            const auto* targetState = FindState(m_TransitionTargetName);
            if (targetState) {
                m_TargetStateTime += dt * targetState->SpeedMultiplier;
                m_TransitionElapsed += dt;

                float alpha = GetTransitionProgress();

                AnimationPose poseSource(skeleton.GetBoneCount());
                AnimationPose poseTarget(skeleton.GetBoneCount());

                curState->Sample(m_CurrentStateTime, curParam, skeleton, poseSource);

                float targetParam = GetFloat(targetState->BlendParameterName, 0.0f);
                targetState->Sample(m_TargetStateTime, targetParam, skeleton, poseTarget);

                AnimationPose::Blend(poseSource, poseTarget, alpha, m_CurrentPose);

                if (m_TransitionElapsed >= m_TransitionDuration) {
                    m_CurrentStateName = m_TransitionTargetName;
                    m_CurrentStateTime = m_TargetStateTime;
                    m_IsTransitioning = false;
                }
            } else {
                m_IsTransitioning = false;
                curState->Sample(m_CurrentStateTime, curParam, skeleton, m_CurrentPose);
            }
        } else {
            curState->Sample(m_CurrentStateTime, curParam, skeleton, m_CurrentPose);
        }

        m_CurrentPose.ComputeGlobalMatrices(skeleton, m_GlobalMatrices);
        m_CurrentPose.ComputeSkinningMatrices(skeleton, m_SkinningMatrices);
    }

}
