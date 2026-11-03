#include <Axle/AnimationStateMachine.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    bool TransitionRule::Evaluate(const AnimationStateMachine& sm) const {
        const auto* decl = sm.GetParameterDeclaration(Parameter);
        if (decl && decl->Type == AnimatorParameterType::Int) {
            int32_t val = sm.GetInt(Parameter);
            switch (Operator) {
                case TransitionConditionOperator::AlwaysTrue: return true;
                case TransitionConditionOperator::Greater: return val > IntOperand;
                case TransitionConditionOperator::Less: return val < IntOperand;
                case TransitionConditionOperator::GreaterOrEqual: return val >= IntOperand;
                case TransitionConditionOperator::LessOrEqual: return val <= IntOperand;
                case TransitionConditionOperator::Equal: return val == IntOperand;
                case TransitionConditionOperator::NotEqual: return val != IntOperand;
                default: break;
            }
        }
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
                return sm.PeekTrigger(Parameter);
            default:
                return false;
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
        auto authored = state;
        if (authored.Id.empty()) authored.Id = GenerateAnimationId();
        m_States[state.Name] = std::move(authored);
        if (setInitial) {
            m_InitialStateName = state.Name;
            m_CurrentStateName = state.Name;
            m_CurrentStateTime = 0.0f;
        }
    }

    void AnimationStateMachine::AddTransition(const AnimationTransition& transition) {
        auto authored = transition;
        if (authored.Id.empty()) authored.Id = GenerateAnimationId();
        m_Transitions.push_back(std::move(authored));
    }

    const AnimationState* AnimationStateMachine::FindState(const std::string& name) const {
        auto it = m_States.find(name);
        if (it != m_States.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool AnimationStateMachine::SetEntryState(const std::string& name) {
        if (!FindState(name)) return false;
        m_InitialStateName = name;
        Reset();
        return true;
    }

    void AnimationStateMachine::SetCurrentState(const std::string& stateName, float transitionDuration) {
        if (!std::isfinite(transitionDuration) || transitionDuration < 0) return;
        if (m_States.find(stateName) == m_States.end()) return;

        if (transitionDuration <= 0.0f || m_CurrentStateName.empty()) {
            m_CurrentStateName = stateName;
            m_CurrentStateTime = 0.0f;
            m_IsTransitioning = false;
            m_TransitionTargetName.clear();
            m_TransitionDuration = m_TransitionElapsed = m_TargetStateTime = 0;
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

    std::string AnimationStateMachine::ResolveParamId(const std::string& key) const {
        if (m_DeclaredParameters.contains(key)) return key;
        const auto it = m_NameToIdMap.find(key);
        return it == m_NameToIdMap.end() ? key : it->second;
    }

    void AnimationStateMachine::SetFloat(const std::string& key, float value) {
        if (key.empty() || !ValidateParameterType(key, AnimatorParameterType::Float) || !std::isfinite(value)) return;
        m_FloatParams[ResolveParamId(key)] = value;
    }

    float AnimationStateMachine::GetFloat(const std::string& key, float fallback) const {
        if (!ValidateParameterType(key, AnimatorParameterType::Float)) return fallback;
        const auto it = m_FloatParams.find(ResolveParamId(key));
        return it == m_FloatParams.end() ? fallback : it->second;
    }

    void AnimationStateMachine::SetInt(const std::string& key, int32_t value) {
        if (key.empty() || !ValidateParameterType(key, AnimatorParameterType::Int)) return;
        m_IntParams[ResolveParamId(key)] = value;
    }

    int32_t AnimationStateMachine::GetInt(const std::string& key, int32_t fallback) const {
        if (!ValidateParameterType(key, AnimatorParameterType::Int)) return fallback;
        const auto it = m_IntParams.find(ResolveParamId(key));
        return it == m_IntParams.end() ? fallback : it->second;
    }

    void AnimationStateMachine::SetBool(const std::string& key, bool value) {
        if (key.empty() || !ValidateParameterType(key, AnimatorParameterType::Bool)) return;
        m_BoolParams[ResolveParamId(key)] = value;
    }

    bool AnimationStateMachine::GetBool(const std::string& key, bool fallback) const {
        if (!ValidateParameterType(key, AnimatorParameterType::Bool)) return fallback;
        const auto it = m_BoolParams.find(ResolveParamId(key));
        return it == m_BoolParams.end() ? fallback : it->second;
    }

    void AnimationStateMachine::SetTrigger(const std::string& key) {
        if (!key.empty() && ValidateParameterType(key, AnimatorParameterType::Trigger))
            m_TriggerParams[ResolveParamId(key)] = true;
    }
    bool AnimationStateMachine::PeekTrigger(const std::string& key) const {
        if (!ValidateParameterType(key, AnimatorParameterType::Trigger)) return false;
        const auto it = m_TriggerParams.find(ResolveParamId(key));
        return it != m_TriggerParams.end() && it->second;
    }
    bool AnimationStateMachine::CheckAndConsumeTrigger(const std::string& key) {
        if (!PeekTrigger(key)) return false;
        m_TriggerParams[ResolveParamId(key)] = false;
        return true;
    }

    void AnimationStateMachine::DeclareParameter(const ParameterDefinition& param) {
        if (!param.Validate() || m_DeclaredParameters.contains(param.Id) || m_NameToIdMap.contains(param.Name)) return;
        // IDs and names share the lookup namespace; reject ambiguous declarations.
        if (m_NameToIdMap.contains(param.Id) || m_DeclaredParameters.contains(param.Name)) return;
        m_DeclaredParameters.emplace(param.Id, param);
        m_NameToIdMap.emplace(param.Name, param.Id);
        for (const auto& key : {param.Id, param.Name}) {
            m_FloatParams.erase(key); m_IntParams.erase(key); m_BoolParams.erase(key); m_TriggerParams.erase(key);
        }
        switch (param.Type) {
            case AnimatorParameterType::Float: m_FloatParams[param.Id] = std::get<float>(param.Default); break;
            case AnimatorParameterType::Int: m_IntParams[param.Id] = std::get<int32_t>(param.Default); break;
            case AnimatorParameterType::Bool: m_BoolParams[param.Id] = std::get<bool>(param.Default); break;
            case AnimatorParameterType::Trigger: m_TriggerParams[param.Id] = false; break;
        }
    }

    const ParameterDefinition* AnimationStateMachine::GetParameterDeclaration(const std::string& nameOrId) const {
        auto it = m_DeclaredParameters.find(nameOrId);
        if (it != m_DeclaredParameters.end()) return &it->second;
        auto itId = m_NameToIdMap.find(nameOrId);
        if (itId != m_NameToIdMap.end()) {
            auto it2 = m_DeclaredParameters.find(itId->second);
            if (it2 != m_DeclaredParameters.end()) return &it2->second;
        }
        return nullptr;
    }

    bool AnimationStateMachine::RenameParameter(const std::string& oldName, const std::string& newName) {
        auto it = m_NameToIdMap.find(oldName);
        if (it == m_NameToIdMap.end() || newName.empty()) return false;
        if (oldName == newName) return true;
        const auto id = it->second;
        if (m_NameToIdMap.contains(newName) || (m_DeclaredParameters.contains(newName) && newName != id) ||
            (newName != id && (m_FloatParams.contains(newName) || m_IntParams.contains(newName) ||
                              m_BoolParams.contains(newName) || m_TriggerParams.contains(newName)))) return false;
        m_NameToIdMap.erase(it);
        m_NameToIdMap[newName] = id;
        m_DeclaredParameters.at(id).Name = newName;
        return true;
    }

    bool AnimationStateMachine::ValidateParameterType(const std::string& nameOrId, AnimatorParameterType expectedType) const {
        const auto* decl = GetParameterDeclaration(nameOrId);
        if (!decl) return true;
        return decl->Type == expectedType;
    }

    bool AnimationStateMachine::EvaluateCondition(const ConditionDefinition& cond, const AnimationStateMachine& sm) {
        if (cond.Operator == ConditionOperator::AlwaysTrue) return true;
        if (const auto* declared = sm.GetParameterDeclaration(cond.Parameter); declared && !cond.Validate(declared)) return false;

        std::string paramKey = cond.Parameter;
        const auto* decl = sm.GetParameterDeclaration(paramKey);
        AnimatorParameterType pType = AnimatorParameterType::Float;
        if (decl) {
            pType = decl->Type;
        } else {
            if (cond.Operator == ConditionOperator::IsTrue || cond.Operator == ConditionOperator::IsFalse) {
                pType = AnimatorParameterType::Bool;
            } else if (cond.Operator == ConditionOperator::Triggered) {
                pType = AnimatorParameterType::Trigger;
            } else if (cond.Operand.index() == 1) {
                pType = AnimatorParameterType::Int;
            } else {
                pType = AnimatorParameterType::Float;
            }
        }

        switch (pType) {
            case AnimatorParameterType::Float: {
                float val = sm.GetFloat(paramKey);
                float op = (cond.Operand.index() == 0) ? std::get<float>(cond.Operand) : 0.0f;
                switch (cond.Operator) {
                    case ConditionOperator::Greater: return val > op;
                    case ConditionOperator::Less: return val < op;
                    case ConditionOperator::GreaterOrEqual: return val >= op;
                    case ConditionOperator::LessOrEqual: return val <= op;
                    case ConditionOperator::Equal: return std::abs(val - op) < 1e-4f;
                    case ConditionOperator::NotEqual: return std::abs(val - op) >= 1e-4f;
                    default: return false;
                }
            }
            case AnimatorParameterType::Int: {
                int32_t val = sm.GetInt(paramKey);
                int32_t op = (cond.Operand.index() == 1) ? std::get<int32_t>(cond.Operand) : 0;
                switch (cond.Operator) {
                    case ConditionOperator::Greater: return val > op;
                    case ConditionOperator::Less: return val < op;
                    case ConditionOperator::GreaterOrEqual: return val >= op;
                    case ConditionOperator::LessOrEqual: return val <= op;
                    case ConditionOperator::Equal: return val == op;
                    case ConditionOperator::NotEqual: return val != op;
                    default: return false;
                }
            }
            case AnimatorParameterType::Bool: {
                bool val = sm.GetBool(paramKey);
                bool op = (cond.Operand.index() == 2) ? std::get<bool>(cond.Operand) : true;
                switch (cond.Operator) {
                    case ConditionOperator::IsTrue: return val == true;
                    case ConditionOperator::IsFalse: return val == false;
                    case ConditionOperator::Equal: return val == op;
                    case ConditionOperator::NotEqual: return val != op;
                    default: return false;
                }
            }
            case AnimatorParameterType::Trigger: {
                if (cond.Operator == ConditionOperator::Triggered) {
                    return sm.PeekTrigger(paramKey);
                }
                return false;
            }
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

    std::shared_ptr<AnimationStateMachine> AnimationStateMachine::Clone(std::string* diagnostic) const {
        if (diagnostic) diagnostic->clear();
        for (const auto& transition : m_Transitions) if (transition.Condition || transition.InstanceCondition) {
            if (diagnostic) *diagnostic = "Cannot clone opaque transition predicates; use typed rules or rebuild the predicate for the new owner.";
            return nullptr;
        }
        auto clone = std::make_shared<AnimationStateMachine>(m_Name);
        clone->m_DefinitionId = m_DefinitionId;
        clone->m_SourceDefinition = m_SourceDefinition;
        clone->m_InitialStateName = m_InitialStateName;
        clone->m_States = m_States;
        clone->m_Transitions = m_Transitions;
        std::unordered_map<const AnimationClip*, std::shared_ptr<AnimationClip>> clips;
        auto copyClip = [&](const std::shared_ptr<AnimationClip>& source) {
            if (!source) return std::shared_ptr<AnimationClip>{};
            auto& copy = clips[source.get()];
            if (!copy) copy = std::make_shared<AnimationClip>(*source);
            return copy;
        };
        for (auto& [name, state] : clone->m_States) {
            state.Clip = copyClip(state.Clip);
            if (state.BlendSpace) {
                auto blend = std::make_shared<BlendSpace1D>(state.BlendSpace->GetName());
                for (const auto& point : state.BlendSpace->GetPoints()) blend->AddPoint(point.Value, copyClip(point.Clip));
                state.BlendSpace = std::move(blend);
            }
        }

        clone->m_DeclaredParameters = m_DeclaredParameters;
        clone->m_NameToIdMap = m_NameToIdMap;

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

        clone->m_ResourceRig = m_ResourceRig ? std::make_shared<Skeleton>(*m_ResourceRig) : nullptr;
        clone->m_CurrentPose = m_CurrentPose;
        if (clone->m_ResourceRig) clone->m_CurrentPose.BindSkeleton(*clone->m_ResourceRig);
        clone->m_GlobalMatrices = m_GlobalMatrices;
        clone->m_SkinningMatrices = m_SkinningMatrices;
        return clone;
    }

    bool AnimationStateMachine::CheckTransitions() {
        if (m_IsTransitioning) return false; // Don't interrupt active transition
        const auto* curState = FindState(m_CurrentStateName);
        if (!curState) return false;

        float blendParam = GetFloat(curState->BlendParameterName, 0.0f);
        float duration = curState->GetDuration(blendParam);
        float normalizedTime = (duration > 1e-4f) ? (m_CurrentStateTime / duration) : 0.0f;

        // Gather candidate transitions matching FromState
        struct Candidate {
            size_t Index;
            const AnimationTransition* Trans;
        };
        std::vector<Candidate> candidates;
        for (size_t i = 0; i < m_Transitions.size(); ++i) {
            if (m_Transitions[i].FromState == m_CurrentStateName) {
                candidates.push_back({ i, &m_Transitions[i] });
            }
        }

        // Sort candidates deterministically: greater Priority first, preserving authored order
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.Trans->Priority > b.Trans->Priority;
        });

        for (const auto& cand : candidates) {
            const auto& trans = *cand.Trans;

            float exitThreshold = trans.ExitPhase < 0.9999f ? trans.ExitPhase : trans.ExitTime;
            if (!FindState(trans.ToState) || !std::isfinite(trans.Duration) || trans.Duration < 0) continue;
            if (trans.HasExitTime) {
                if (!std::isfinite(exitThreshold) || exitThreshold < 0 || exitThreshold > 1) continue;
                if (curState->Loop) {
                    // A loop gate is an instant in each cycle, not a permanently open interval.
                    const double nearestCycle = std::round(double(normalizedTime) - exitThreshold);
                    if (nearestCycle < 0 || std::abs(double(normalizedTime) - (nearestCycle + exitThreshold)) > 1e-5) continue;
                } else if (normalizedTime + 1e-5f < exitThreshold) continue;
            }

            // Check typed rules
            bool rulesPassed = true;
            std::vector<std::string> triggersToConsume;
            for (const auto& rule : trans.Rules) {
                if (!rule.Evaluate(*this)) {
                    rulesPassed = false;
                    break;
                }
                if (rule.Operator == TransitionConditionOperator::Triggered) {
                    triggersToConsume.push_back(rule.Parameter);
                }
            }
            if (!rulesPassed) continue;

            // Check pure conditions
            bool pureCondsPassed = true;
            for (const auto& cond : trans.PureConditions) {
                if (!EvaluateCondition(cond, *this)) {
                    pureCondsPassed = false;
                    break;
                }
                if (cond.Operator == ConditionOperator::Triggered) {
                    triggersToConsume.push_back(cond.Parameter);
                }
            }
            if (!pureCondsPassed) continue;

            // Check instance condition predicate
            if (trans.InstanceCondition && !trans.InstanceCondition(*this)) {
                continue;
            }

            // Legacy condition predicate
            if (trans.Condition && !trans.Condition()) {
                continue;
            }

            // Transition condition satisfied! Consume triggers atomically for selected candidate
            for (const auto& tr : triggersToConsume) {
                CheckAndConsumeTrigger(tr);
            }

            SetCurrentState(trans.ToState, trans.Duration);
            return true;
        }

        return false;
    }

    void AnimationStateMachine::Update(float dt, const Skeleton& skeleton) {
        if (m_CurrentStateName.empty() || m_States.empty()) {
            m_CurrentPose.InitFromSkeletonBindPose(skeleton);
            m_CurrentPose.ComputeGlobalMatrices(skeleton, m_GlobalMatrices);
            m_CurrentPose.ComputeSkinningMatrices(skeleton, m_SkinningMatrices);
            return;
        }

        // Validate dt: reject negative or non-finite dt
        if (std::isnan(dt) || std::isinf(dt) || dt < 0.0f) {
            return;
        }

        if (dt == 0.0f) {
            // dt=0 evaluates pose without advancing clocks, transitions, or consuming triggers
            const auto* curState = FindState(m_CurrentStateName);
            if (curState) {
                float curParam = GetFloat(curState->BlendParameterName, 0.0f);
                if (m_IsTransitioning) {
                    const auto* targetState = FindState(m_TransitionTargetName);
                    if (targetState) {
                        float alpha = GetTransitionProgress();
                        AnimationPose poseSource(skeleton.GetBoneCount());
                        AnimationPose poseTarget(skeleton.GetBoneCount());
                        curState->Sample(m_CurrentStateTime, curParam, skeleton, poseSource);
                        float targetParam = GetFloat(targetState->BlendParameterName, 0.0f);
                        targetState->Sample(m_TargetStateTime, targetParam, skeleton, poseTarget);
                        AnimationPose::Blend(poseSource, poseTarget, alpha, m_CurrentPose);
                    } else {
                        curState->Sample(m_CurrentStateTime, curParam, skeleton, m_CurrentPose);
                    }
                } else {
                    curState->Sample(m_CurrentStateTime, curParam, skeleton, m_CurrentPose);
                }
            }
            m_CurrentPose.ComputeGlobalMatrices(skeleton, m_GlobalMatrices);
            m_CurrentPose.ComputeSkinningMatrices(skeleton, m_SkinningMatrices);
            return;
        }

        float remainingDt = dt;
        int hops = 0;
        constexpr int MAX_HOPS = 16;

        while (hops < MAX_HOPS) {
            hops++;

            if (!m_IsTransitioning) {
                bool triggered = CheckTransitions();
                if (triggered && !m_IsTransitioning) continue; // Instant transition; retain residual time.
            }

            if (remainingDt <= 1e-6f) {
                break;
            }

            const auto* curState = FindState(m_CurrentStateName);
            if (!curState) break;
            float curSpeed = std::max(0.0f, curState->SpeedMultiplier);

            if (m_IsTransitioning) {
                const auto* targetState = FindState(m_TransitionTargetName);
                float targetSpeed = targetState ? std::max(0.0f, targetState->SpeedMultiplier) : 1.0f;

                float timeToFadeEnd = m_TransitionDuration - m_TransitionElapsed;
                float stepDt = std::min(remainingDt, timeToFadeEnd);

                m_CurrentStateTime += stepDt * curSpeed;
                m_TargetStateTime += stepDt * targetSpeed;
                m_TransitionElapsed += stepDt;
                remainingDt -= stepDt;

                if (m_TransitionElapsed >= m_TransitionDuration) {
                    m_CurrentStateName = m_TransitionTargetName;
                    m_CurrentStateTime = m_TargetStateTime;
                    m_IsTransitioning = false;
                    m_TransitionTargetName.clear();
                    m_TargetStateTime = 0.0f;
                    m_TransitionElapsed = 0.0f;
                }
            } else {
                // Find earliest exit gate among candidate transitions whose conditions are met
                float minDtToGate = std::numeric_limits<float>::infinity();

                for (const auto& trans : m_Transitions) {
                    if (trans.FromState != m_CurrentStateName) continue;
                    if (!trans.HasExitTime) continue;

                    bool rulesPassed = true;
                    for (const auto& rule : trans.Rules) {
                        if (!rule.Evaluate(*this)) {
                            rulesPassed = false;
                            break;
                        }
                    }
                    if (!rulesPassed) continue;

                    bool pureCondsPassed = true;
                    for (const auto& cond : trans.PureConditions) {
                        if (!EvaluateCondition(cond, *this)) {
                            pureCondsPassed = false;
                            break;
                        }
                    }
                    if (!pureCondsPassed) continue;

                    if (trans.InstanceCondition && !trans.InstanceCondition(*this)) continue;
                    if (trans.Condition && !trans.Condition()) continue;

                    float exitThreshold = trans.ExitPhase < 0.9999f ? trans.ExitPhase : trans.ExitTime;
                    exitThreshold = std::clamp(exitThreshold, 0.0f, 1.0f);

                    float blendParam = GetFloat(curState->BlendParameterName, 0.0f);
                    float duration = curState->GetDuration(blendParam);
                    if (duration <= 1e-4f || curSpeed <= 1e-6f) continue;

                    float curNorm = m_CurrentStateTime / duration;
                    if (curState->Loop) {
                        float curPhase = curNorm - std::floor(curNorm);
                        if (curPhase < exitThreshold) {
                            float dtGate = (exitThreshold - curPhase) * duration / curSpeed;
                            if (dtGate < minDtToGate) minDtToGate = dtGate;
                        } else if (exitThreshold > 1e-4f) {
                            float dtGate = (1.0f - curPhase + exitThreshold) * duration / curSpeed;
                            if (dtGate < minDtToGate) minDtToGate = dtGate;
                        }
                    } else {
                        if (curNorm < exitThreshold) {
                            float dtGate = (exitThreshold - curNorm) * duration / curSpeed;
                            if (dtGate < minDtToGate) minDtToGate = dtGate;
                        }
                    }
                }

                float stepDt = remainingDt;
                if (minDtToGate <= remainingDt && minDtToGate > 1e-6f) {
                    stepDt = minDtToGate;
                }
                m_CurrentStateTime += stepDt * curSpeed;
                remainingDt -= stepDt;
            }
        }

        if (remainingDt > 0) {
            if (const auto* state = FindState(m_CurrentStateName)) m_CurrentStateTime += remainingDt * std::max(0.0f, state->SpeedMultiplier);
            if (m_IsTransitioning) {
                if (const auto* state = FindState(m_TransitionTargetName)) m_TargetStateTime += remainingDt * std::max(0.0f, state->SpeedMultiplier);
                m_TransitionElapsed += remainingDt;
                if (m_TransitionElapsed >= m_TransitionDuration) {
                    m_CurrentStateName = m_TransitionTargetName; m_CurrentStateTime = m_TargetStateTime;
                    m_IsTransitioning = false; m_TransitionTargetName.clear();
                }
            }
        }

        // Sample resulting pose and compute palettes
        const auto* curState = FindState(m_CurrentStateName);
        if (curState) {
            float curParam = GetFloat(curState->BlendParameterName, 0.0f);
            if (m_IsTransitioning) {
                const auto* targetState = FindState(m_TransitionTargetName);
                if (targetState) {
                    float alpha = GetTransitionProgress();
                    AnimationPose poseSource(skeleton.GetBoneCount());
                    AnimationPose poseTarget(skeleton.GetBoneCount());
                    curState->Sample(m_CurrentStateTime, curParam, skeleton, poseSource);
                    float targetParam = GetFloat(targetState->BlendParameterName, 0.0f);
                    targetState->Sample(m_TargetStateTime, targetParam, skeleton, poseTarget);
                    AnimationPose::Blend(poseSource, poseTarget, alpha, m_CurrentPose);
                } else {
                    curState->Sample(m_CurrentStateTime, curParam, skeleton, m_CurrentPose);
                }
            } else {
                curState->Sample(m_CurrentStateTime, curParam, skeleton, m_CurrentPose);
            }
        }

        m_CurrentPose.ComputeGlobalMatrices(skeleton, m_GlobalMatrices);
        m_CurrentPose.ComputeSkinningMatrices(skeleton, m_SkinningMatrices);
    }

}
