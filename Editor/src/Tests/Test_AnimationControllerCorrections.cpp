#include "AnimationTestHarness.hpp"
#include <Axle/AnimationResources.hpp>
#include <Axle/AnimationAssetCodec.hpp>

bool RunAnimationControllerCorrectionTests() {
    using namespace Axle;
    auto rig = std::make_shared<Skeleton>("Rig"); Bone bone; bone.Name = "Root"; rig->AddBone(bone);
    auto machine = std::make_shared<AnimationStateMachine>("Controller");
    for (int i = 0; i < 2; ++i) {
        AnimationState state; state.Id = i ? "walk-id" : "idle-id"; state.Name = i ? "Walk" : "Idle";
        state.GraphPosition = glm::vec2(i*30, 15);
        state.Clip = std::make_shared<AnimationClip>(state.Name, 1);
        AnimationChannel channel; channel.BoneName = "Root";
        channel.Positions = {{0, glm::vec3(i*5,0,0)}, {1, glm::vec3(i*5+2,0,0)}};
        state.Clip->AddChannel(channel); machine->AddState(state);
    }
    machine->DeclareParameter({"speed", "Speed", AnimatorParameterType::Float, 0.0f});
    AnimationTransition transition; transition.Id = "go"; transition.FromState = "Idle"; transition.ToState = "Walk";
    transition.Duration = 0.1f; transition.Rules.push_back({"Speed", ConditionOperator::Greater, 0.0f}); machine->AddTransition(transition);
    machine->SetEntryState("Idle");
    Animator original(rig); original.SetStateMachine(machine);
    auto definition = AnimationAssetCodec::ExtractDefinitionFromStateMachine(*machine, rig.get());
    ANIMATION_CHECK(definition.Validate() && definition.InlineClips.size() == 2 && definition.Transitions[0].AllConditions.size() == 1, "Extraction retains actual clips and legacy typed rules");
    ANIMATION_CHECK(AnimationAssetCodec::ExtractDefinitionFromStateMachine(*machine, rig.get()).Id == definition.Id, "Repeated extraction keeps controller identity");
    const auto text = AnimationAssetCodec::EncodeControllerToText(definition);
    ANIMATION_CHECK(!text.empty(), "Complete controller text encodes");
    std::string diagnostic;
    auto loaded = LoadAnimationAnimator("missing/controller.axleasset", text, true, nullptr, "", &diagnostic);
    ANIMATION_CHECK(loaded && loaded->GetSkeleton(), "Self-contained fallback resolves its rig and keyed clips");
    original.Update(0.25f); loaded->Update(0.25f);
    ANIMATION_CHECK(AnimationTests::Near(original.GetCurrentPose().Transforms[0].Translation.x, loaded->GetCurrentPose().Transforms[0].Translation.x), "Reopened keyed pose matches before save");
    ANIMATION_CHECK(loaded->GetCurrentStateName() == "Idle", "Reopened rules do not become unconditional");
    loaded->SetFloat("Speed", 1); loaded->Update(0.2f);
    ANIMATION_CHECK(loaded->GetCurrentStateName() == "Walk" && loaded->GetCurrentPose().Transforms[0].Translation.x > 5, "Reopened transition executes the actual target clip");
    const auto again = AnimationAssetCodec::ExtractDefinitionFromStateMachine(*loaded->GetStateMachine(), loaded->GetSkeleton().get());
    ANIMATION_CHECK(again.Id == definition.Id && again.FindState("idle-id") && again.Transitions[0].Id == "go", "Controller/state/transition identity survives compilation");
    ANIMATION_CHECK(again.FindState("idle-id")->GraphPosition == glm::vec2(0,15), "Graph metadata survives compilation");
    auto changed = definition; changed.EntryStateId = "walk-id";
    auto entry = AnimationAssetCodec::CompileStateMachineFromDefinition(changed, rig);
    ANIMATION_CHECK(entry && entry->GetCurrentStateName() == "Walk", "Compiler honors non-first authored entry");
    entry->SetCurrentState("Idle"); entry->Reset();
    ANIMATION_CHECK(entry->GetCurrentStateName() == "Walk", "Compiled Reset honors non-first entry");
    changed.States[0].MotionAssetRef = "missing/no-fallback.axleasset";
    ANIMATION_CHECK(!AnimationAssetCodec::CompileStateMachineFromDefinition(changed, rig, "", &diagnostic) && !diagnostic.empty(), "Missing motion fails with diagnostics rather than bind-pose success");
    changed = definition; changed.CompatibilitySignature = "wrong-rig";
    ANIMATION_CHECK(!AnimationAssetCodec::CompileStateMachineFromDefinition(changed, rig, "", &diagnostic), "Incompatible controller rig rejected");
    auto collision = definition;
    collision.Parameters.push_back({"other-id","speed",AnimatorParameterType::Float,0.0f});
    ANIMATION_CHECK(!collision.Validate() && !AnimationAssetCodec::CompileStateMachineFromDefinition(collision,rig), "Controller publication rejects name/ID alias collisions");
    auto invalidExit = definition; invalidExit.Transitions[0].HasExitTime = true; invalidExit.Transitions[0].ExitPhase = 2;
    ANIMATION_CHECK(!invalidExit.Validate(), "Unreachable loop exit phases reject before compilation");
    auto invalidNode = definition; invalidNode.States[0].NodeType = static_cast<AnimationNodeType>(99);
    ANIMATION_CHECK(!invalidNode.Validate(), "Unknown motion node rejects before publication");
    return true;
}
