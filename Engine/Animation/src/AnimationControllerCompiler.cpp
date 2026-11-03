#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/AnimationResources.hpp>
#include <Axle/FileSystem.hpp>
#include <algorithm>
#include <limits>
#include <cmath>
#include <unordered_map>

namespace Axle {
std::shared_ptr<AnimationStateMachine> AnimationAssetCodec::CompileStateMachineFromDefinition(
    const AnimatorControllerDefinition& definition, std::shared_ptr<Skeleton> rig,
    const std::string& ownerPath, std::string* diagnostic) {
    auto fail = [&](const std::string& text) { if (diagnostic) *diagnostic = text; return std::shared_ptr<AnimationStateMachine>{}; };
    if (diagnostic) diagnostic->clear();
    std::vector<std::string> errors;
    if (!definition.Validate(&errors)) return fail(errors.empty() ? "Invalid controller" : errors.front());
    auto source = definition;
    if (!rig && !definition.SkeletonRef.empty()) {
        SkeletonDefinition data;
        const auto path = ResolveAnimationReference(definition.SkeletonRef, ownerPath);
        if (FileSystem::Exists(path) && LoadSkeleton(path, data, diagnostic)) rig = BuildAnimationSkeleton(data, diagnostic);
    }
    if (!rig && definition.SkeletonFallback) rig = BuildAnimationSkeleton(*definition.SkeletonFallback, diagnostic);
    if (!rig || !rig->IsValid() || rig->GetBoneCount() > 128) return fail("A compatible validated rig is required to compile a controller");
    const auto signature = SkeletonDefinition::ComputeSignature(rig->GetBones());
    if (!definition.CompatibilitySignature.empty() && definition.CompatibilitySignature != signature) return fail("Controller/rig compatibility mismatch");
    SkeletonDefinition skeleton; skeleton.Id = definition.SkeletonFallback ? definition.SkeletonFallback->Id : GenerateAnimationId();
    skeleton.Name = rig->GetName(); skeleton.Bones = rig->GetBones(); skeleton.CompatibilitySignature = signature;
    source.SkeletonFallback = skeleton; source.CompatibilitySignature = signature;
    const auto resolveClip = [&](const std::string& reference) -> std::shared_ptr<AnimationClip> {
        AnimationClipDefinition data; bool found = false;
        const auto path = ResolveAnimationReference(reference, ownerPath);
        if (!reference.starts_with("inline:") && FileSystem::Exists(path)) {
            if (!LoadAnimationClip(path, data, diagnostic)) return nullptr;
            found = true;
        } else for (const auto& embedded : definition.InlineClips) if (embedded.Reference == reference) { data = embedded.Clip; found = true; break; }
        if (!found) { if (diagnostic) *diagnostic = "Missing clip resource: " + reference; return nullptr; }
        if (!data.CompatibilitySignature.empty() && data.CompatibilitySignature != signature) {
            if (diagnostic) *diagnostic = "Clip/rig compatibility mismatch: " + reference; return nullptr;
        }
        if (data.CompatibilitySignature.empty() && !data.SkeletonRef.empty()) {
            SkeletonDefinition declared;
            if (!LoadSkeleton(ResolveAnimationReference(data.SkeletonRef, path), declared, diagnostic) || SkeletonDefinition::ComputeSignature(declared.Bones) != signature) {
                if (diagnostic) *diagnostic = "Unverified clip skeleton: " + reference; return nullptr;
            }
        }
        auto clip = std::make_shared<AnimationClip>(data.Name, data.Duration, data.FrameRate);
        if (!clip->ReplaceChannels(data.Channels, rig.get(), &errors)) { if (diagnostic) *diagnostic = errors.empty() ? "Invalid clip binding" : errors.front(); return nullptr; }
        data.CompatibilitySignature = signature;
        auto it = std::find_if(source.InlineClips.begin(), source.InlineClips.end(), [&](const auto& value) { return value.Reference == reference; });
        if (it == source.InlineClips.end()) source.InlineClips.push_back({reference, data}); else it->Clip = data;
        return clip;
    };
    auto result = std::make_shared<AnimationStateMachine>(definition.Name);
    for (const auto& parameter : definition.Parameters) result->DeclareParameter(parameter);
    std::unordered_map<std::string, std::string> names;
    for (const auto& state : definition.States) {
        AnimationState runtime; runtime.Id = state.Id; runtime.Name = state.Name; runtime.Type = state.NodeType;
        runtime.MotionAssetRef = state.MotionAssetRef; runtime.SpeedMultiplier = state.SpeedMultiplier; runtime.Loop = state.Loop;
        runtime.BlendParameterName = state.BlendParameterX; runtime.BlendParameterY = state.BlendParameterY; runtime.GraphPosition = state.GraphPosition;
        if (state.NodeType == AnimationNodeType::Clip) {
            runtime.Clip = resolveClip(state.MotionAssetRef); if (!runtime.Clip) return nullptr;
        } else if (state.NodeType == AnimationNodeType::BlendSpace1D) {
            if (state.BlendPoints.empty()) return fail("A 1D motion requires clip samples");
            runtime.BlendSpace = std::make_shared<BlendSpace1D>(state.Name);
            auto points = state.BlendPoints;
            std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.Value < b.Value; });
            float previous = -std::numeric_limits<float>::infinity();
            for (const auto& point : points) {
                if (!std::isfinite(point.Value) || point.Value <= previous) return fail("Invalid/duplicate 1D sample coordinate");
                previous = point.Value; auto clip = resolveClip(point.ClipRef); if (!clip) return nullptr;
                runtime.BlendSpace->AddPoint(point.Value, clip); runtime.BlendMotionRefs.push_back(point.ClipRef);
            }
        } else return fail("BlendSpace2D runtime is not implemented yet (Phase 40 D2)");
        names[state.Id] = state.Name; result->AddState(runtime);
    }
    if (!definition.States.empty()) result->SetEntryState(names.at(definition.EntryStateId));
    for (const auto& transition : definition.Transitions) {
        AnimationTransition runtime; runtime.Id = transition.Id;
        runtime.FromState = names.at(transition.FromState); runtime.ToState = names.at(transition.ToState);
        runtime.Duration = transition.CrossfadeSeconds; runtime.HasExitTime = transition.HasExitTime;
        runtime.ExitTime = runtime.ExitPhase = transition.ExitPhase; runtime.Priority = transition.Priority;
        runtime.PureConditions = transition.AllConditions; result->AddTransition(runtime);
    }
    result->SetResourceRig(rig); result->SetSourceDefinition(std::move(source)); result->Update(0, *rig); return result;
}

AnimatorControllerDefinition AnimationAssetCodec::ExtractDefinitionFromStateMachine(
    const AnimationStateMachine& machine, const Skeleton* rig, std::string* diagnostic) {
    if (diagnostic) diagnostic->clear();
    auto fail = [&](const char* text) { if (diagnostic) *diagnostic = text; return AnimatorControllerDefinition{}; };
    AnimatorControllerDefinition result;
    if (machine.GetSourceDefinition()) result = *machine.GetSourceDefinition();
    if (result.Id.empty()) result.Id = machine.GetDefinitionId();
    result.Name = machine.GetName().empty() ? "AnimatorController" : machine.GetName();
    result.States.clear(); result.Transitions.clear(); result.Parameters.clear();
    if (!rig) rig = machine.GetCurrentPose().GetSkeletonBinding();
    if (rig) {
        SkeletonDefinition data; data.Id = result.SkeletonFallback ? result.SkeletonFallback->Id : result.Id + ":rig";
        data.Name = rig->GetName(); data.Bones = rig->GetBones(); data.CompatibilitySignature = SkeletonDefinition::ComputeSignature(data.Bones);
        result.SkeletonFallback = data; result.CompatibilitySignature = data.CompatibilitySignature;
    }
    for (const auto& [id, parameter] : machine.GetDeclaredParameters()) result.Parameters.push_back(parameter);
    const auto addLegacyParameter = [&](const auto& values, AnimatorParameterType type) {
        for (const auto& [name, value] : values) if (!machine.GetParameterDeclaration(name)) {
            ParameterDefinition parameter; parameter.Id = "legacy:" + name; parameter.Name = name; parameter.Type = type; parameter.Default = value;
            if (!result.FindParameter(parameter.Id)) result.Parameters.push_back(parameter);
        }
    };
    addLegacyParameter(machine.GetFloatParameters(), AnimatorParameterType::Float);
    addLegacyParameter(machine.GetIntParameters(), AnimatorParameterType::Int);
    addLegacyParameter(machine.GetBoolParameters(), AnimatorParameterType::Bool);
    std::unordered_map<std::string,std::string> ids;
    const auto snapshot = [&](const std::shared_ptr<AnimationClip>& clip, std::string reference, const std::string& id) {
        if (!clip) return std::string{};
        if (reference.empty()) reference = "inline:" + id;
        AnimationClipDefinition data;
        auto it = std::find_if(result.InlineClips.begin(), result.InlineClips.end(), [&](const auto& value) { return value.Reference == reference; });
        if (it != result.InlineClips.end()) data = it->Clip;
        if (data.Id.empty()) data.Id = id;
        data.Name = clip->GetName().empty() ? "Clip" : clip->GetName(); data.Duration = clip->GetDuration(); data.FrameRate = clip->GetTicksPerSecond();
        data.CompatibilitySignature = result.CompatibilitySignature; data.Channels = clip->GetChannels();
        if (it == result.InlineClips.end()) result.InlineClips.push_back({reference, data}); else it->Clip = data;
        return reference;
    };
    for (const auto& [name, state] : machine.GetStates()) {
        StateDefinition data; data.Id = state.Id; data.Name = name; data.NodeType = state.Type;
        data.SpeedMultiplier = state.SpeedMultiplier; data.Loop = state.Loop; data.GraphPosition = state.GraphPosition;
        data.BlendParameterX = state.BlendParameterName; data.BlendParameterY = state.BlendParameterY;
        if (state.Type == AnimationNodeType::Clip) data.MotionAssetRef = snapshot(state.Clip, state.MotionAssetRef, state.Id + ":clip");
        else if (state.BlendSpace) for (size_t i = 0; i < state.BlendSpace->GetPoints().size(); ++i) {
            const auto& point = state.BlendSpace->GetPoints()[i];
            data.BlendPoints.push_back({point.Value, snapshot(point.Clip, i < state.BlendMotionRefs.size() ? state.BlendMotionRefs[i] : "", state.Id + ":point:" + std::to_string(i))});
        }
        ids[name] = data.Id; result.States.push_back(std::move(data));
    }
    std::sort(result.States.begin(), result.States.end(), [](const auto& a, const auto& b) { return a.Id < b.Id; });
    if (const auto it = ids.find(machine.GetInitialStateName()); it != ids.end()) result.EntryStateId = it->second;
    for (const auto& transition : machine.GetTransitions()) {
        if (transition.Condition || transition.InstanceCondition) return fail("Opaque predicates are not serializable; use typed transition rules");
        TransitionDefinition data; data.Id = transition.Id; data.FromState = ids[transition.FromState]; data.ToState = ids[transition.ToState];
        data.CrossfadeSeconds = transition.Duration; data.Priority = transition.Priority;
        data.HasExitTime = transition.HasExitTime; data.ExitPhase = transition.ExitPhase < 0.9999f ? transition.ExitPhase : transition.ExitTime;
        data.AllConditions = transition.PureConditions;
        for (const auto& rule : transition.Rules) {
            ConditionDefinition condition; condition.Parameter = rule.Parameter; condition.Operator = rule.Operator;
            const auto* parameter = machine.GetParameterDeclaration(rule.Parameter);
            if (!parameter && condition.Operator != ConditionOperator::AlwaysTrue) {
                parameter = result.FindParameterByName(rule.Parameter);
                if (!parameter) {
                    ParameterDefinition legacy; legacy.Id = "legacy:" + rule.Parameter; legacy.Name = rule.Parameter;
                    legacy.Type = rule.Operator == ConditionOperator::Triggered ? AnimatorParameterType::Trigger :
                                  (rule.Operator == ConditionOperator::IsTrue || rule.Operator == ConditionOperator::IsFalse) ? AnimatorParameterType::Bool : AnimatorParameterType::Float;
                    legacy.Default = legacy.Type == AnimatorParameterType::Float ? ParameterDefault(0.0f) : ParameterDefault(false);
                    result.Parameters.push_back(legacy); parameter = &result.Parameters.back();
                }
            }
            if (parameter) {
                condition.Parameter = parameter->Id;
                if (parameter->Type == AnimatorParameterType::Int) condition.Operand = rule.IntOperand;
                else if (parameter->Type == AnimatorParameterType::Bool) condition.Operand = rule.BoolOperand;
                else condition.Operand = rule.FloatOperand;
            }
            data.AllConditions.push_back(std::move(condition));
        }
        result.Transitions.push_back(std::move(data));
    }
    std::sort(result.Parameters.begin(), result.Parameters.end(), [](const auto& a, const auto& b) { return a.Id < b.Id; });
    return result;
}
}
