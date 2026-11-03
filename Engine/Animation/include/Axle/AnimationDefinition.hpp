#pragma once

#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <optional>

namespace Axle {

    using AnimationId = std::string;
    AnimationId GenerateAnimationId();

    enum class AnimatorParameterType : uint32_t {
        Float = 0,
        Int = 1,
        Bool = 2,
        Trigger = 3
    };

    using ParameterDefault = std::variant<float, int32_t, bool>;

    struct ParameterDefinition {
        AnimationId Id;
        std::string Name;
        AnimatorParameterType Type = AnimatorParameterType::Float;
        ParameterDefault Default = 0.0f;

        bool Validate(std::vector<std::string>* outErrors = nullptr) const;
    };

    enum class ConditionOperator : uint32_t {
        AlwaysTrue = 0,
        Greater,
        Less,
        GreaterOrEqual,
        LessOrEqual,
        Equal,
        NotEqual,
        IsTrue,
        IsFalse,
        Triggered
    };

    struct ConditionDefinition {
        AnimationId Parameter;
        ConditionOperator Operator = ConditionOperator::AlwaysTrue;
        ParameterDefault Operand = 0.0f;

        bool Validate(const ParameterDefinition* paramDef = nullptr, std::vector<std::string>* outErrors = nullptr) const;
    };

    struct TransitionDefinition {
        AnimationId Id;
        AnimationId FromState;
        AnimationId ToState;
        std::vector<ConditionDefinition> AllConditions;
        float CrossfadeSeconds = 0.2f;
        bool HasExitTime = false;
        float ExitPhase = 1.0f;
        int32_t Priority = 0;

        bool Validate(const std::unordered_map<AnimationId, struct StateDefinition>& states,
                      const std::unordered_map<AnimationId, ParameterDefinition>& params,
                      std::vector<std::string>* outErrors = nullptr) const;
    };

    enum class AnimationNodeType : uint32_t {
        Clip = 0,
        BlendSpace1D = 1,
        BlendSpace2D = 2
    };

    struct BlendClipDefinition { float Value = 0; std::string ClipRef; };

    struct StateDefinition {
        AnimationId Id;
        std::string Name;
        AnimationNodeType NodeType = AnimationNodeType::Clip;
        std::string MotionAssetRef;
        float SpeedMultiplier = 1.0f;
        bool Loop = true;
        std::string BlendParameterX;
        std::string BlendParameterY;
        glm::vec2 GraphPosition = glm::vec2(0.0f);
        std::vector<BlendClipDefinition> BlendPoints;

        bool Validate(std::vector<std::string>* outErrors = nullptr) const;
    };

    struct AnimationEventDefinition {
        AnimationId Id;
        std::string Name;
        float Time = 0.0f; // Seconds
        std::string StringParam;
        float FloatParam = 0.0f;
        int32_t IntParam = 0;

        bool Validate(float clipDuration = 0.0f, std::vector<std::string>* outErrors = nullptr) const;
    };

    struct AnimationClipDefinition {
        uint32_t SchemaVersion = 1;
        AnimationId Id;
        std::string Name;
        std::string SkeletonRef;
        std::string CompatibilitySignature;
        float Duration = 0.0f;
        float FrameRate = 30.0f;
        std::vector<AnimationChannel> Channels;
        std::vector<AnimationEventDefinition> Events;

        bool Validate(std::vector<std::string>* outErrors = nullptr) const;
    };

    struct SkeletonDefinition {
        uint32_t SchemaVersion = 1;
        AnimationId Id;
        std::string Name;
        std::string CompatibilitySignature;
        std::vector<Bone> Bones;

        bool Validate(std::vector<std::string>* outErrors = nullptr) const;
        static std::string ComputeSignature(const std::vector<Bone>& bones);
    };

    struct SkeletalMeshDefinition {
        uint32_t SchemaVersion = 1;
        AnimationId Id;
        std::string Name;
        std::string SkeletonRef;
        std::string GeometryRef;
        std::string SourceImportPath;
        std::vector<std::string> MaterialSlots;

        bool Validate(std::vector<std::string>* outErrors = nullptr) const;
    };

    struct InlineAnimationClip { std::string Reference; AnimationClipDefinition Clip; };

    struct AnimatorControllerDefinition {
        uint32_t SchemaVersion = 1;
        AnimationId Id;
        std::string Name;
        std::string SkeletonRef;
        std::string CompatibilitySignature;
        AnimationId EntryStateId;

        std::optional<SkeletonDefinition> SkeletonFallback;
        std::vector<InlineAnimationClip> InlineClips;
        std::vector<ParameterDefinition> Parameters;
        std::vector<StateDefinition> States;
        std::vector<TransitionDefinition> Transitions;

        const ParameterDefinition* FindParameter(const AnimationId& id) const;
        const ParameterDefinition* FindParameterByName(const std::string& name) const;
        const StateDefinition* FindState(const AnimationId& id) const;
        const StateDefinition* FindStateByName(const std::string& name) const;

        bool Validate(std::vector<std::string>* outErrors = nullptr) const;
    };


}

