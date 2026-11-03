#include <Axle/AnimationSceneBinding.hpp>
#include <Axle/AnimationResources.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/Components.hpp>
#include <Axle/SkeletalMeshImporter.hpp>

namespace Axle {
bool CaptureAnimationBinding(const SkeletalMeshComponent* mesh, const AnimatorComponent* animator,
                             std::string& meshFallback, std::string& controllerFallback, std::string* diagnostic) {
    meshFallback.clear(); controllerFallback.clear();
    if (mesh) {
        if (mesh->Mesh) {
            auto text = EncodeAnimationRigSnapshot(mesh->Mesh->GetData(), diagnostic);
            if (text.empty()) return false;
            meshFallback = AnimationAssetCodec::HexEncode(text);
        } else if (!mesh->RigFallbackData.empty()) {
            if (!DecodeAnimationRigSnapshot(AnimationAssetCodec::HexDecode(mesh->RigFallbackData), diagnostic)) return false;
            meshFallback = mesh->RigFallbackData;
        }
    }
    if (animator) {
        // Runtime authoring edits supersede any older serialized snapshot.
        if (animator->AnimatorInstance && animator->AnimatorInstance->GetStateMachine()) {
            auto definition = AnimationAssetCodec::ExtractDefinitionFromStateMachine(
                *animator->AnimatorInstance->GetStateMachine(), animator->AnimatorInstance->GetSkeleton().get(), diagnostic);
            auto text = AnimationAssetCodec::EncodeControllerToText(definition);
            if (text.empty()) return false;
            controllerFallback = AnimationAssetCodec::HexEncode(text);
        } else if (!animator->InlineFallbackData.empty()) {
            AnimatorControllerDefinition definition;
            if (!AnimationAssetCodec::DecodeControllerFromText(AnimationAssetCodec::HexDecode(animator->InlineFallbackData), definition, diagnostic)) return false;
            controllerFallback = animator->InlineFallbackData;
        }
    }
    return true;
}

bool ResolveAnimationBinding(SkeletalMeshComponent* mesh, AnimatorComponent* component, const std::string& owner) {
    std::shared_ptr<Skeleton> rig;
    bool success = true;
    if (mesh) {
        mesh->BindingDiagnostic.clear();
        if (!mesh->Mesh && (!mesh->AssetPath.empty() || !mesh->RigFallbackData.empty())) {
            auto candidate = LoadAnimationMesh(mesh->AssetPath, AnimationAssetCodec::HexDecode(mesh->RigFallbackData), owner, &mesh->BindingDiagnostic);
            if (candidate) mesh->Mesh = std::move(candidate);
            else success = false;
        }
        if (mesh->Mesh) rig = mesh->Mesh->GetSkeleton();
        if (!rig && !mesh->SkeletonPath.empty()) {
            SkeletonDefinition definition;
            if (AnimationAssetCodec::LoadSkeleton(ResolveAnimationReference(mesh->SkeletonPath, owner), definition, &mesh->BindingDiagnostic))
                rig = BuildAnimationSkeleton(definition, &mesh->BindingDiagnostic);
            if (!rig) success = false;
        }
    }
    if (!component) return success;
    auto& ac = *component; ac.BindingDiagnostic.clear();
    if (!ac.AnimatorInstance) {
        // Compatibility is restricted to the explicit legacy procedural URI.
        if (ac.ControllerPath.empty() && ac.InlineFallbackData.empty() && mesh && mesh->AssetPath == "procedural://biped") {
            SkeletalMeshData data; std::shared_ptr<Skeleton> legacyRig; std::vector<std::shared_ptr<AnimationClip>> clips;
            if (SkeletalMeshImporter::CreateProceduralBiped(data, legacyRig, clips) && clips.size() >= 2) {
                auto machine = std::make_shared<AnimationStateMachine>("LocomotionStateMachine");
                for (int i = 0; i < 2; ++i) {
                    AnimationState state; state.Name = i ? "Walk" : "Idle"; state.Clip = clips[i]; machine->AddState(state);
                }
                for (int i = 0; i < 2; ++i) {
                    AnimationTransition transition; transition.FromState = i ? "Walk" : "Idle"; transition.ToState = i ? "Idle" : "Walk";
                    transition.Duration = 0.2f;
                    transition.Rules.push_back({"Speed", i ? ConditionOperator::LessOrEqual : ConditionOperator::Greater, 0.1f});
                    machine->AddTransition(transition);
                }
                ac.AnimatorInstance = std::make_shared<Animator>(rig ? rig : legacyRig);
                ac.AnimatorInstance->SetStateMachine(machine);
            }
        } else if (!ac.ControllerPath.empty() || !ac.InlineFallbackData.empty()) {
            auto candidate = LoadAnimationAnimator(ac.ControllerPath, AnimationAssetCodec::HexDecode(ac.InlineFallbackData), ac.IsUnique, rig, owner, &ac.BindingDiagnostic);
            if (candidate) ac.AnimatorInstance = std::move(candidate);
            else success = false;
        }
    }
    if (ac.AnimatorInstance) {
        auto& animator = *ac.AnimatorInstance;
        animator.SetFloat("Speed", ac.Speed);
        animator.SetBool("IsGrounded", ac.IsGrounded);
        animator.SetFloat("VerticalVelocity", ac.VerticalVelocity);
        // Stable-ID overrides have precedence over legacy conventional names.
        for (const auto& [p,v] : ac.FloatOverrides) animator.SetFloat(p,v);
        for (const auto& [p,v] : ac.IntOverrides) animator.SetInt(p,v);
        for (const auto& [p,v] : ac.BoolOverrides) animator.SetBool(p,v);
        animator.Update(0);
    }
    return success;
}
}
