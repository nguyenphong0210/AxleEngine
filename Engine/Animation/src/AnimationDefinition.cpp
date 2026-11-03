#include <Axle/AnimationDefinition.hpp>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <algorithm>

namespace Axle {

    static bool IsFinite(float v) {
        return !std::isnan(v) && !std::isinf(v);
    }

    static bool IsFinite(const glm::vec2& v) {
        return IsFinite(v.x) && IsFinite(v.y);
    }

    static bool IsFinite(const glm::vec3& v) {
        return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
    }

    static bool IsFinite(const glm::quat& q) {
        return IsFinite(q.x) && IsFinite(q.y) && IsFinite(q.z) && IsFinite(q.w);
    }

    // --- ParameterDefinition ---

    bool ParameterDefinition::Validate(std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("Parameter '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("Parameter ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("Parameter Name cannot be empty");
        }

        switch (Type) {
            case AnimatorParameterType::Float: {
                if (Default.index() != 0) {
                    ReportError("Float parameter default value must hold float variant");
                } else if (!IsFinite(std::get<float>(Default))) {
                    ReportError("Float parameter default value must be finite");
                }
                break;
            }
            case AnimatorParameterType::Int: {
                if (Default.index() != 1) {
                    ReportError("Int parameter default value must hold int32_t variant");
                }
                break;
            }
            case AnimatorParameterType::Bool: {
                if (Default.index() != 2) {
                    ReportError("Bool parameter default value must hold bool variant");
                }
                break;
            }
            case AnimatorParameterType::Trigger: {
                if (!std::holds_alternative<bool>(Default) || std::get<bool>(Default))
                    ReportError("Trigger defaults must be false; pending triggers are runtime-only");
                break;
            }
            default:
                ReportError("Unknown parameter type");
                break;
        }

        return valid;
    }

    // --- ConditionDefinition ---

    bool ConditionDefinition::Validate(const ParameterDefinition* paramDef, std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("Condition on param '" + Parameter + "': " + err);
        };

        if (Parameter.empty() && Operator != ConditionOperator::AlwaysTrue) {
            ReportError("Condition parameter ID cannot be empty when operator is not AlwaysTrue");
            return false;
        }

        if (Operator == ConditionOperator::AlwaysTrue) {
            return true;
        }

        if (!paramDef) {
            ReportError("Referenced parameter not found in controller parameters");
            return false;
        }

        switch (paramDef->Type) {
            case AnimatorParameterType::Float: {
                if (Operator != ConditionOperator::Greater &&
                    Operator != ConditionOperator::Less &&
                    Operator != ConditionOperator::GreaterOrEqual &&
                    Operator != ConditionOperator::LessOrEqual &&
                    Operator != ConditionOperator::Equal &&
                    Operator != ConditionOperator::NotEqual) {
                    ReportError("Incompatible operator for Float parameter");
                }
                if (Operand.index() != 0 || !IsFinite(std::get<float>(Operand))) {
                    ReportError("Operand for Float parameter must hold finite float");
                }
                break;
            }
            case AnimatorParameterType::Int: {
                if (Operator != ConditionOperator::Greater &&
                    Operator != ConditionOperator::Less &&
                    Operator != ConditionOperator::GreaterOrEqual &&
                    Operator != ConditionOperator::LessOrEqual &&
                    Operator != ConditionOperator::Equal &&
                    Operator != ConditionOperator::NotEqual) {
                    ReportError("Incompatible operator for Int parameter");
                }
                if (Operand.index() != 1) {
                    ReportError("Operand for Int parameter must hold int32_t");
                }
                break;
            }
            case AnimatorParameterType::Bool: {
                if ((Operator == ConditionOperator::Equal || Operator == ConditionOperator::NotEqual) && !std::holds_alternative<bool>(Operand))
                    ReportError("Boolean equality requires a bool operand");
                if (Operator != ConditionOperator::IsTrue &&
                    Operator != ConditionOperator::IsFalse &&
                    Operator != ConditionOperator::Equal &&
                    Operator != ConditionOperator::NotEqual) {
                    ReportError("Incompatible operator for Bool parameter");
                }
                break;
            }
            case AnimatorParameterType::Trigger: {
                if (Operator != ConditionOperator::Triggered) {
                    ReportError("Trigger parameter only supports Triggered operator");
                }
                break;
            }
            default:
                ReportError("Unsupported parameter type for condition");
                break;
        }

        return valid;
    }

    // --- TransitionDefinition ---

    bool TransitionDefinition::Validate(const std::unordered_map<AnimationId, StateDefinition>& states,
                                        const std::unordered_map<AnimationId, ParameterDefinition>& params,
                                        std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("Transition '" + Id + "' (" + FromState + " -> " + ToState + "): " + err);
        };

        if (Id.empty()) {
            ReportError("Transition ID cannot be empty");
        }
        if (FromState.empty()) {
            ReportError("FromState ID cannot be empty");
        } else if (states.find(FromState) == states.end()) {
            ReportError("FromState '" + FromState + "' not found in controller states");
        }

        if (ToState.empty()) {
            ReportError("ToState ID cannot be empty");
        } else if (states.find(ToState) == states.end()) {
            ReportError("ToState '" + ToState + "' not found in controller states");
        }

        if (!IsFinite(CrossfadeSeconds) || CrossfadeSeconds < 0.0f) {
            ReportError("CrossfadeSeconds must be non-negative and finite");
        }

        if (HasExitTime && (!IsFinite(ExitPhase) || ExitPhase < 0.0f)) {
            ReportError("ExitPhase must be non-negative and finite");
        }

        for (const auto& cond : AllConditions) {
            const ParameterDefinition* pDef = nullptr;
            auto it = params.find(cond.Parameter);
            if (it != params.end()) {
                pDef = &it->second;
            }
            if (!cond.Validate(pDef, outErrors)) {
                valid = false;
            }
        }

        return valid;
    }

    // --- StateDefinition ---

    bool StateDefinition::Validate(std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("State '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("State ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("State Name cannot be empty");
        }
        if (!IsFinite(SpeedMultiplier) || SpeedMultiplier == 0.0f) {
            ReportError("SpeedMultiplier must be finite and non-zero");
        }
        if (!IsFinite(GraphPosition)) {
            ReportError("GraphPosition coordinates must be finite");
        }

        return valid;
    }

    // --- AnimatorControllerDefinition ---

    const ParameterDefinition* AnimatorControllerDefinition::FindParameter(const AnimationId& id) const {
        for (const auto& param : Parameters) {
            if (param.Id == id) return &param;
        }
        return nullptr;
    }

    const ParameterDefinition* AnimatorControllerDefinition::FindParameterByName(const std::string& name) const {
        for (const auto& param : Parameters) {
            if (param.Name == name) return &param;
        }
        return nullptr;
    }

    const StateDefinition* AnimatorControllerDefinition::FindState(const AnimationId& id) const {
        for (const auto& state : States) {
            if (state.Id == id) return &state;
        }
        return nullptr;
    }

    const StateDefinition* AnimatorControllerDefinition::FindStateByName(const std::string& name) const {
        for (const auto& state : States) {
            if (state.Name == name) return &state;
        }
        return nullptr;
    }

    bool AnimatorControllerDefinition::Validate(std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("Controller '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("Controller ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("Controller Name cannot be empty");
        }
        if (SchemaVersion == 0) {
            ReportError("SchemaVersion must be >= 1");
        }

        // Validate unique parameter IDs and unique names
        std::unordered_map<AnimationId, ParameterDefinition> paramMap;
        std::unordered_set<std::string> paramNames;
        for (const auto& param : Parameters) {
            if (paramMap.find(param.Id) != paramMap.end()) {
                ReportError("Duplicate parameter ID '" + param.Id + "'");
            } else {
                paramMap[param.Id] = param;
            }

            if (paramNames.find(param.Name) != paramNames.end()) {
                ReportError("Duplicate parameter Name '" + param.Name + "'");
            } else {
                paramNames.insert(param.Name);
            }

            if (!param.Validate(outErrors)) {
                valid = false;
            }
        }

        // Validate unique state IDs and unique names
        std::unordered_map<AnimationId, StateDefinition> stateMap;
        std::unordered_set<std::string> stateNames;
        for (const auto& state : States) {
            if (stateMap.find(state.Id) != stateMap.end()) {
                ReportError("Duplicate state ID '" + state.Id + "'");
            } else {
                stateMap[state.Id] = state;
            }

            if (stateNames.find(state.Name) != stateNames.end()) {
                ReportError("Duplicate state Name '" + state.Name + "'");
            } else {
                stateNames.insert(state.Name);
            }

            if (!state.Validate(outErrors)) {
                valid = false;
            }
        }

        // Validate entry state
        if (!States.empty()) {
            if (EntryStateId.empty()) {
                ReportError("EntryStateId must be specified when states are defined");
            } else if (stateMap.find(EntryStateId) == stateMap.end()) {
                ReportError("EntryStateId '" + EntryStateId + "' not found in controller states");
            }
        }

        // Validate transitions and unique transition IDs
        std::unordered_set<AnimationId> transIds;
        for (const auto& trans : Transitions) {
            if (transIds.find(trans.Id) != transIds.end()) {
                ReportError("Duplicate transition ID '" + trans.Id + "'");
            } else {
                transIds.insert(trans.Id);
            }

            if (!trans.Validate(stateMap, paramMap, outErrors)) {
                valid = false;
            }
        }

        return valid;
    }

    // --- AnimationEventDefinition ---

    bool AnimationEventDefinition::Validate(float clipDuration, std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("AnimationEvent '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("Event ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("Event Name cannot be empty");
        }
        if (!IsFinite(Time) || Time < 0.0f) {
            ReportError("Event Time must be non-negative and finite");
        }
        if (clipDuration > 0.0f && Time > clipDuration) {
            ReportError("Event Time exceeds clip duration");
        }
        if (!IsFinite(FloatParam)) {
            ReportError("Event FloatParam must be finite");
        }

        return valid;
    }

    // --- AnimationClipDefinition ---

    bool AnimationClipDefinition::Validate(std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("Clip '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("Clip ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("Clip Name cannot be empty");
        }
        if (!IsFinite(Duration) || Duration <= 0.0f) {
            ReportError("Clip Duration must be positive and finite");
        }
        if (!IsFinite(FrameRate) || FrameRate <= 0.0f) {
            ReportError("Clip FrameRate must be positive and finite");
        }

        for (const auto& channel : Channels) {
            if (channel.BoneName.empty()) {
                ReportError("Channel BoneName cannot be empty");
            }
            float lastT = -1.0f;
            for (const auto& k : channel.Positions) {
                if (!IsFinite(k.TimeStamp) || k.TimeStamp < 0.0f || !IsFinite(k.Value)) {
                    ReportError("Channel position keyframe contains invalid/non-finite values");
                }
                if (k.TimeStamp < lastT) {
                    ReportError("Channel position keyframes are not in sorted time order");
                }
                lastT = k.TimeStamp;
            }

            lastT = -1.0f;
            for (const auto& k : channel.Rotations) {
                if (!IsFinite(k.TimeStamp) || k.TimeStamp < 0.0f || !IsFinite(k.Value)) {
                    ReportError("Channel rotation keyframe contains invalid/non-finite values");
                }
                if (k.TimeStamp < lastT) {
                    ReportError("Channel rotation keyframes are not in sorted time order");
                }
                lastT = k.TimeStamp;
            }

            lastT = -1.0f;
            for (const auto& k : channel.Scales) {
                if (!IsFinite(k.TimeStamp) || k.TimeStamp < 0.0f || !IsFinite(k.Value)) {
                    ReportError("Channel scale keyframe contains invalid/non-finite values");
                }
                if (k.TimeStamp < lastT) {
                    ReportError("Channel scale keyframes are not in sorted time order");
                }
                lastT = k.TimeStamp;
            }
        }

        for (const auto& ev : Events) {
            if (!ev.Validate(Duration, outErrors)) {
                valid = false;
            }
        }

        return valid;
    }

    // --- SkeletonDefinition ---

    static uint64_t FNV1a64(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    std::string SkeletonDefinition::ComputeSignature(const std::vector<Bone>& bones) {
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < bones.size(); ++i) {
            const auto& b = bones[i];
            hash = FNV1a64(b.Name.data(), b.Name.size(), hash);
            int32_t pid = b.ParentID;
            hash = FNV1a64(&pid, sizeof(pid), hash);
            // Include bind transform translation, rotation and scale
            hash = FNV1a64(&b.LocalTransform[3][0], sizeof(float) * 3, hash);
        }
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << hash;
        return ss.str();
    }

    bool SkeletonDefinition::Validate(std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("Skeleton '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("Skeleton ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("Skeleton Name cannot be empty");
        }

        std::unordered_set<std::string> boneNames;
        for (size_t i = 0; i < Bones.size(); ++i) {
            const auto& b = Bones[i];
            if (b.Name.empty()) {
                ReportError("Bone at index " + std::to_string(i) + " has empty name");
            }
            if (boneNames.find(b.Name) != boneNames.end()) {
                ReportError("Duplicate bone name '" + b.Name + "'");
            } else {
                boneNames.insert(b.Name);
            }

            if (b.ParentID >= static_cast<int32_t>(Bones.size()) || b.ParentID == static_cast<int32_t>(i)) {
                ReportError("Invalid ParentID " + std::to_string(b.ParentID) + " for bone '" + b.Name + "'");
            }
        }

        // Topology cycle check
        Skeleton skel(Name);
        for (const auto& b : Bones) {
            skel.AddBone(b);
        }
        std::vector<size_t> evalOrder;
        std::vector<std::string> topoErrors;
        if (!skel.BuildEvaluationOrder(evalOrder, &topoErrors)) {
            for (const auto& e : topoErrors) {
                ReportError(e);
            }
        }

        return valid;
    }

    // --- SkeletalMeshDefinition ---

    bool SkeletalMeshDefinition::Validate(std::vector<std::string>* outErrors) const {
        bool valid = true;
        auto ReportError = [&](const std::string& err) {
            valid = false;
            if (outErrors) outErrors->push_back("SkeletalMesh '" + Name + "' (" + Id + "): " + err);
        };

        if (Id.empty()) {
            ReportError("SkeletalMesh ID cannot be empty");
        }
        if (Name.empty()) {
            ReportError("SkeletalMesh Name cannot be empty");
        }
        if (SkeletonRef.empty()) {
            ReportError("SkeletonRef cannot be empty");
        }

        return valid;
    }

}

