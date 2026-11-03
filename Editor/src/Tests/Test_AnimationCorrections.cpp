#include "AnimationTestHarness.hpp"
#include <Axle/AnimationStateMachine.hpp>
#include <limits>

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

bool RunAnimationCorrectionTests() { return TestCanonicalParameters(); }
