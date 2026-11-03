#include "AnimationTestHarness.hpp"
#include <Axle/AnimationStateMachine.hpp>
#include <limits>
#include <Axle/Animator.hpp>

static bool TestCanonicalParameters() {
    using namespace Axle;
    AnimationStateMachine sm;
    sm.DeclareParameter({"speed-id", "Speed", AnimatorParameterType::Float, 0.0f});
    sm.SetFloat("speed-id", 3);
    ANIMATION_CHECK(sm.GetFloat("Speed") == 3 && sm.GetFloatParameters().size() == 1, "One slot per declared parameter");
    sm.SetFloat("Speed", 4);
    ANIMATION_CHECK(sm.RenameParameter("Speed", "Velocity") && sm.GetFloat("Velocity") == 4, "Rename preserves canonical value");
    sm.SetFloat("speed-id", 7);
    ANIMATION_CHECK(sm.GetFloat("Velocity") == 7, "ID writes remain visible after rename");
    sm.SetFloat("Velocity", std::numeric_limits<float>::infinity());
    ANIMATION_CHECK(sm.GetFloat("Velocity") == 7, "Nonfinite writes retain last good value");
    sm.DeclareParameter({"count-id", "Count", AnimatorParameterType::Int, int32_t(2)});
    sm.SetFloat("count-id", 9); sm.SetBool("Count", true); sm.SetTrigger("Count");
    ANIMATION_CHECK(sm.GetInt("Count") == 2 && sm.GetFloat("Count", -1) == -1 && !sm.PeekTrigger("Count"), "Typed writes cannot create shadow values");
    ANIMATION_CHECK(!sm.RenameParameter("Velocity", "Count"), "Conflicting rename rejected");
    sm.DeclareParameter({"fire-id", "Fire", AnimatorParameterType::Trigger, false});
    sm.SetTrigger("Fire");
    ANIMATION_CHECK(sm.CheckAndConsumeTrigger("fire-id") && !sm.PeekTrigger("Fire"), "ID consumption clears name trigger");
    sm.SetTrigger("fire-id");
    ANIMATION_CHECK(sm.CheckAndConsumeTrigger("Fire") && !sm.PeekTrigger("fire-id"), "Name consumption clears ID trigger");
    ConditionDefinition invalid{"count-id", ConditionOperator::IsTrue, false};
    ANIMATION_CHECK(!AnimationStateMachine::EvaluateCondition(invalid, sm), "Invalid typed conditions fail closed");
    ParameterDefinition invalidDefault{"bad", "Bad", AnimatorParameterType::Trigger, true};
    ANIMATION_CHECK(!invalidDefault.Validate(), "Pending trigger cannot be an authored default");
    sm.SetFloat("Legacy", 2);
    ANIMATION_CHECK(sm.GetFloat("Legacy") == 2, "Undeclared legacy API still works");
    return true;
}

static bool TestLoopGatesEntryAndClone() {
    using namespace Axle;
    Skeleton rig("Rig"); Bone root; root.Name = "Root"; rig.AddBone(root);
    auto make = [] {
        AnimationStateMachine sm;
        for (const auto* name : {"Idle", "Walk"}) {
            AnimationState state; state.Name = name; state.Clip = std::make_shared<AnimationClip>(name, 1);
            sm.AddState(state);
        }
        AnimationTransition transition; transition.FromState = "Idle"; transition.ToState = "Walk";
        transition.Duration = 0.1f; transition.HasExitTime = true; transition.ExitPhase = transition.ExitTime = 0.5f;
        TransitionRule rule; rule.Parameter = "Go"; rule.Operator = TransitionConditionOperator::IsTrue;
        transition.Rules.push_back(rule); sm.AddTransition(transition);
        return sm;
    };
    auto direct = make(), divided = make();
    direct.Update(1.2f, rig); divided.Update(1.2f, rig);
    direct.SetBool("Go", true); divided.SetBool("Go", true);
    direct.Update(0.1f, rig);
    ANIMATION_CHECK(!direct.IsTransitioning() && direct.GetCurrentStateName() == "Idle", "Late loop condition waits for its cycle gate");
    direct.Update(0.5f, rig);
    for (int i = 0; i < 6; ++i) divided.Update(0.1f, rig);
    ANIMATION_CHECK(direct.GetCurrentStateName() == "Walk" && divided.GetCurrentStateName() == "Walk", "Both frame subdivisions cross the gate");
    ANIMATION_CHECK(AnimationTests::Near(direct.GetCurrentStateTime(), divided.GetCurrentStateTime()), "Crossfade residual time is frame independent");
    ANIMATION_CHECK(direct.SetEntryState("Walk"), "Explicit authored entry accepted");
    direct.SetCurrentState("Idle"); direct.Reset();
    ANIMATION_CHECK(direct.GetCurrentStateName() == "Walk", "Reset uses explicit entry");
    AnimationTransition opaque; opaque.FromState = "Idle"; opaque.ToState = "Walk";
    auto owner = std::make_shared<AnimationStateMachine>(make());
    std::weak_ptr<AnimationStateMachine> weak = owner;
    opaque.Condition = [weak] { return weak.lock()->GetBool("Go"); };
    owner->AddTransition(opaque);
    std::string error;
    ANIMATION_CHECK(!owner->Clone(&error) && !error.empty(), "Unsafe closure cloning is explicitly rejected");
    auto snapshot = divided.Clone(&error);
    ANIMATION_CHECK(snapshot && error.empty(), "Typed-rule snapshots remain cloneable");
    snapshot->FindState("Idle")->Clip->SetDuration(3);
    ANIMATION_CHECK(divided.FindState("Idle")->Clip->GetDuration() == 1, "Mutable legacy resources are isolated in clones");
    std::shared_ptr<AnimationStateMachine> standalone;
    {
        auto temporaryRig = std::make_shared<Skeleton>("Temporary"); Bone bone; bone.Name = "Root"; temporaryRig->AddBone(bone);
        auto temporaryMachine = make(); temporaryMachine.Update(0.25f,*temporaryRig);
        standalone = temporaryMachine.Clone(&error);
    }
    ANIMATION_CHECK(standalone && standalone->GetResourceRig() && standalone->GetCurrentPose().GetSkeletonBinding() == standalone->GetResourceRig().get(), "Standalone runtime clone owns its sampled rig after source destruction");
    return true;
}

bool RunAnimationAssetCorrectionTests();
bool RunAnimationImporterCorrectionTests();
bool RunAnimationControllerCorrectionTests();
bool RunAnimationSceneCorrectionTests();
bool RunAnimationPackageCorrectionTests();

bool RunAnimationCorrectionTests() { return TestCanonicalParameters() && TestLoopGatesEntryAndClone() && RunAnimationAssetCorrectionTests() && RunAnimationImporterCorrectionTests() && RunAnimationControllerCorrectionTests() && RunAnimationSceneCorrectionTests() && RunAnimationPackageCorrectionTests(); }
