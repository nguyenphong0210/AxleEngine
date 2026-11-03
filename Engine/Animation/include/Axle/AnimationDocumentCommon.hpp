#pragma once

#include <Axle/AnimationDefinition.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Axle {

    enum class AnimationChannelType : uint32_t {
        Translation = 0,
        Rotation = 1,
        Scale = 2
    };

    enum class ClipSelectionKind : uint32_t {
        None = 0,
        Channel = 1,
        Event = 2
    };

    struct AnimationClipSelection {
        ClipSelectionKind Kind = ClipSelectionKind::None;
        std::string BoneName;
        AnimationChannelType ChannelType = AnimationChannelType::Translation;
        AnimationId EventId;

        bool operator==(const AnimationClipSelection& other) const = default;
    };

    enum class ControllerSelectionKind : uint32_t {
        None = 0,
        State = 1,
        Transition = 2,
        Parameter = 3
    };

    struct AnimatorControllerSelection {
        ControllerSelectionKind Kind = ControllerSelectionKind::None;
        AnimationId SelectedId;

        bool operator==(const AnimatorControllerSelection& other) const = default;
    };

    enum class DocumentPendingAction : uint32_t {
        None = 0,
        New = 1,
        Open = 2,
        Close = 3,
        Reload = 4
    };

    enum class LifecycleResult : uint32_t {
        Executed = 0,
        PendingConfirmation = 1,
        StaleResponseRejected = 2,
        SaveFailed = 3,
        LoadFailed = 4,
        ValidationFailed = 5,
        NoPendingAction = 6,
        Cancelled = 7,
        AlreadyInTransaction = 8,
        HistoryRejectedOversized = 9
    };

    struct AnimationDocumentStats {
        size_t UndoCount = 0;
        size_t RedoCount = 0;
        size_t HistoryMemoryBytes = 0;
        uint64_t CurrentRevision = 0;
        uint64_t SavedRevision = 0;
        bool IsDirty = false;
        DocumentPendingAction PendingAction = DocumentPendingAction::None;
    };

    // --- Lossless Value Comparison Helpers ---
    inline bool AreEqual(const glm::vec2& a, const glm::vec2& b) {
        return a.x == b.x && a.y == b.y;
    }

    inline bool AreEqual(const glm::vec3& a, const glm::vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    inline bool AreEqual(const glm::vec4& a, const glm::vec4& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    inline bool AreEqual(const glm::quat& a, const glm::quat& b) {
        return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
    }

    inline bool AreEqual(const glm::mat4& a, const glm::mat4& b) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (a[c][r] != b[c][r]) return false;
            }
        }
        return true;
    }

    inline bool operator==(const Keyframe<glm::vec3>& a, const Keyframe<glm::vec3>& b) {
        return a.TimeStamp == b.TimeStamp &&
               AreEqual(a.Value, b.Value) &&
               AreEqual(a.InTangent, b.InTangent) &&
               AreEqual(a.OutTangent, b.OutTangent);
    }

    inline bool operator==(const Keyframe<glm::quat>& a, const Keyframe<glm::quat>& b) {
        return a.TimeStamp == b.TimeStamp &&
               AreEqual(a.Value, b.Value) &&
               AreEqual(a.InTangent, b.InTangent) &&
               AreEqual(a.OutTangent, b.OutTangent);
    }

    inline bool operator==(const AnimationChannel& a, const AnimationChannel& b) {
        return a.BoneName == b.BoneName &&
               a.PositionsInterpolation == b.PositionsInterpolation &&
               a.RotationsInterpolation == b.RotationsInterpolation &&
               a.ScalesInterpolation == b.ScalesInterpolation &&
               a.Positions == b.Positions &&
               a.Rotations == b.Rotations &&
               a.Scales == b.Scales;
    }

    inline bool operator==(const AnimationEventDefinition& a, const AnimationEventDefinition& b) {
        return a.Id == b.Id &&
               a.Name == b.Name &&
               a.Time == b.Time &&
               a.StringParam == b.StringParam &&
               a.FloatParam == b.FloatParam &&
               a.IntParam == b.IntParam;
    }

    inline bool operator==(const AnimationClipDefinition& a, const AnimationClipDefinition& b) {
        return a.SchemaVersion == b.SchemaVersion &&
               a.Id == b.Id &&
               a.Name == b.Name &&
               a.SkeletonRef == b.SkeletonRef &&
               a.CompatibilitySignature == b.CompatibilitySignature &&
               a.Duration == b.Duration &&
               a.FrameRate == b.FrameRate &&
               a.Channels == b.Channels &&
               a.Events == b.Events;
    }

    inline bool operator==(const ParameterDefinition& a, const ParameterDefinition& b) {
        return a.Id == b.Id &&
               a.Name == b.Name &&
               a.Type == b.Type &&
               a.Default == b.Default;
    }

    inline bool operator==(const ConditionDefinition& a, const ConditionDefinition& b) {
        return a.Parameter == b.Parameter &&
               a.Operator == b.Operator &&
               a.Operand == b.Operand;
    }

    inline bool operator==(const BlendClipDefinition& a, const BlendClipDefinition& b) {
        return a.Value == b.Value &&
               a.ClipRef == b.ClipRef;
    }

    inline bool operator==(const StateDefinition& a, const StateDefinition& b) {
        return a.Id == b.Id &&
               a.Name == b.Name &&
               a.NodeType == b.NodeType &&
               a.MotionAssetRef == b.MotionAssetRef &&
               a.SpeedMultiplier == b.SpeedMultiplier &&
               a.Loop == b.Loop &&
               a.BlendParameterX == b.BlendParameterX &&
               a.BlendParameterY == b.BlendParameterY &&
               AreEqual(a.GraphPosition, b.GraphPosition) &&
               a.BlendPoints == b.BlendPoints;
    }

    inline bool operator==(const TransitionDefinition& a, const TransitionDefinition& b) {
        return a.Id == b.Id &&
               a.FromState == b.FromState &&
               a.ToState == b.ToState &&
               a.AllConditions == b.AllConditions &&
               a.CrossfadeSeconds == b.CrossfadeSeconds &&
               a.HasExitTime == b.HasExitTime &&
               a.ExitPhase == b.ExitPhase &&
               a.Priority == b.Priority;
    }

    inline bool operator==(const InlineAnimationClip& a, const InlineAnimationClip& b) {
        return a.Reference == b.Reference &&
               a.Clip == b.Clip;
    }

    inline bool operator==(const Bone& a, const Bone& b) {
        return a.Name == b.Name &&
               a.ParentID == b.ParentID &&
               AreEqual(a.LocalTransform, b.LocalTransform) &&
               AreEqual(a.InverseBindMatrix, b.InverseBindMatrix);
    }

    inline bool operator==(const SkeletonDefinition& a, const SkeletonDefinition& b) {
        return a.SchemaVersion == b.SchemaVersion &&
               a.Id == b.Id &&
               a.Name == b.Name &&
               a.CompatibilitySignature == b.CompatibilitySignature &&
               a.Bones == b.Bones;
    }

    inline bool operator==(const AnimatorControllerDefinition& a, const AnimatorControllerDefinition& b) {
        return a.SchemaVersion == b.SchemaVersion &&
               a.Id == b.Id &&
               a.Name == b.Name &&
               a.SkeletonRef == b.SkeletonRef &&
               a.CompatibilitySignature == b.CompatibilitySignature &&
               a.EntryStateId == b.EntryStateId &&
               a.SkeletonFallback == b.SkeletonFallback &&
               a.InlineClips == b.InlineClips &&
               a.Parameters == b.Parameters &&
               a.States == b.States &&
               a.Transitions == b.Transitions;
    }

    // --- Path Relocation Helper ---
    inline std::string RelocateReferencePath(const std::string& ref, const std::string& oldDocPath, const std::string& newDocPath) {
        if (ref.empty() || oldDocPath.empty() || newDocPath.empty()) {
            return ref;
        }
        if (ref.find("://") != std::string::npos) {
            return ref;
        }
        if (ref.find('/') == std::string::npos && ref.find('\\') == std::string::npos) {
            return ref;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path oldDoc(oldDocPath);
        fs::path newDoc(newDocPath);
        fs::path oldDir = oldDoc.parent_path();
        fs::path newDir = newDoc.parent_path();
        if (oldDir.empty() || newDir.empty() || oldDir == newDir) {
            return ref;
        }
        fs::path refPath(ref);
        if (refPath.is_absolute()) {
            return ref;
        }
        fs::path absTarget = (oldDir / refPath).lexically_normal();
        fs::path relTarget = fs::relative(absTarget, newDir, ec);
        if (!ec && !relTarget.empty()) {
            return relTarget.generic_string();
        }
        return ref;
    }

    inline bool IsPackageOrReadOnlyPath(const std::string& path) {
        if (path.rfind("package://", 0) == 0) return true;
        return false;
    }

    // --- Memory Estimation Helpers ---
    inline size_t EstimateClipDefinitionMemory(const AnimationClipDefinition& def) {
        size_t bytes = sizeof(AnimationClipDefinition);
        bytes += def.Id.capacity() + def.Name.capacity() + def.SkeletonRef.capacity() + def.CompatibilitySignature.capacity();
        bytes += def.Channels.capacity() * sizeof(AnimationChannel);
        for (const auto& ch : def.Channels) {
            bytes += ch.BoneName.capacity();
            bytes += ch.Positions.capacity() * sizeof(Keyframe<glm::vec3>);
            bytes += ch.Rotations.capacity() * sizeof(Keyframe<glm::quat>);
            bytes += ch.Scales.capacity() * sizeof(Keyframe<glm::vec3>);
        }
        bytes += def.Events.capacity() * sizeof(AnimationEventDefinition);
        for (const auto& ev : def.Events) {
            bytes += ev.Id.capacity() + ev.Name.capacity() + ev.StringParam.capacity();
        }
        return bytes;
    }

    inline size_t EstimateControllerDefinitionMemory(const AnimatorControllerDefinition& def) {
        size_t bytes = sizeof(AnimatorControllerDefinition);
        bytes += def.Id.capacity() + def.Name.capacity() + def.SkeletonRef.capacity() + def.CompatibilitySignature.capacity() + def.EntryStateId.capacity();
        bytes += def.Parameters.capacity() * sizeof(ParameterDefinition);
        for (const auto& p : def.Parameters) {
            bytes += p.Id.capacity() + p.Name.capacity();
        }
        bytes += def.States.capacity() * sizeof(StateDefinition);
        for (const auto& s : def.States) {
            bytes += s.Id.capacity() + s.Name.capacity() + s.MotionAssetRef.capacity() + s.BlendParameterX.capacity() + s.BlendParameterY.capacity();
            bytes += s.BlendPoints.capacity() * sizeof(BlendClipDefinition);
            for (const auto& bp : s.BlendPoints) {
                bytes += bp.ClipRef.capacity();
            }
        }
        bytes += def.Transitions.capacity() * sizeof(TransitionDefinition);
        for (const auto& t : def.Transitions) {
            bytes += t.Id.capacity() + t.FromState.capacity() + t.ToState.capacity();
            bytes += t.AllConditions.capacity() * sizeof(ConditionDefinition);
            for (const auto& c : t.AllConditions) {
                bytes += c.Parameter.capacity();
            }
        }
        bytes += def.InlineClips.capacity() * sizeof(InlineAnimationClip);
        for (const auto& ic : def.InlineClips) {
            bytes += ic.Reference.capacity() + EstimateClipDefinitionMemory(ic.Clip);
        }
        if (def.SkeletonFallback.has_value()) {
            bytes += sizeof(SkeletonDefinition) + def.SkeletonFallback->Id.capacity() + def.SkeletonFallback->Name.capacity() + def.SkeletonFallback->CompatibilitySignature.capacity();
            bytes += def.SkeletonFallback->Bones.capacity() * sizeof(Bone);
            for (const auto& b : def.SkeletonFallback->Bones) {
                bytes += b.Name.capacity();
            }
        }
        return bytes;
    }

}

