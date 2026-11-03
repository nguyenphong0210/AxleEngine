#include "AnimationTestHarness.hpp"
#include <Axle/AnimationEditorDocument.hpp>
#include <Axle/AnimatorEditorDocument.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace {

    std::filesystem::path CreateTestDir(const std::string& subDir) {
        auto dir = std::filesystem::temp_directory_path() / "AxleAnimationDocTests" / subDir;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    void CleanupTestDir(const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

}

static bool TestDocumentRoundTripAndAuthoredPreservation() {
    using namespace Axle;
    auto testDir = CreateTestDir("RoundTrip");
    std::string clipPath = (testDir / "InitialClip.anim").generic_string();
    std::string clipSaveAsPath = (testDir / "SavedClip.anim").generic_string();

    // 1. Clip round trip with StepNext, CubicSpline, exact keyframes, and events
    {
        AnimationEditorDocument doc;
        ANIMATION_CHECK(doc.SetClipProperties("HeroSprint", 1.5f, 60.0f, "../Skeletons/Hero.skel", "SIG_HERO_1"), "Set properties");

        AnimationChannel ch;
        ch.BoneName = "Spine";
        ch.PositionsInterpolation = AnimationInterpolation::StepNext;
        ch.Positions = {
            {0.0f, glm::vec3(0.0f, 1.0f, 0.0f)},
            {0.5f, glm::vec3(0.0f, 1.2f, 0.1f)},
            {1.5f, glm::vec3(0.0f, 1.0f, 0.0f)}
        };
        ch.RotationsInterpolation = AnimationInterpolation::CubicSpline;
        Keyframe<glm::quat> rKey0;
        rKey0.TimeStamp = 0.0f;
        rKey0.Value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        rKey0.InTangent = glm::vec4(0.0f);
        rKey0.OutTangent = glm::vec4(0.1f, 0.2f, 0.3f, 0.0f);
        ch.Rotations.push_back(rKey0);

        Keyframe<glm::quat> rKey1;
        rKey1.TimeStamp = 1.5f;
        rKey1.Value = glm::quat(0.7071f, 0.7071f, 0.0f, 0.0f);
        rKey1.InTangent = glm::vec4(0.1f, 0.2f, 0.3f, 0.0f);
        rKey1.OutTangent = glm::vec4(0.0f);
        ch.Rotations.push_back(rKey1);

        ANIMATION_CHECK(doc.AddChannel(ch), "Add channel with StepNext and Cubic tangents");

        AnimationEventDefinition evt;
        evt.Id = "evt_footstep_01";
        evt.Name = "Footstep";
        evt.Time = 0.35f;
        evt.StringParam = "Surface_Stone";
        evt.FloatParam = 0.85f;
        evt.IntParam = 1;
        ANIMATION_CHECK(doc.AddEvent(evt), "Add event");

        std::string err;
        ANIMATION_CHECK(doc.SaveAs(clipPath, &err), "SaveAs initial clip");
        ANIMATION_CHECK(!doc.IsDirty(), "Clean after SaveAs");

        // Now open via a second document
        AnimationEditorDocument doc2;
        ANIMATION_CHECK(doc2.OpenClip(clipPath, &err) == LifecycleResult::Executed, "Open saved clip");
        ANIMATION_CHECK(doc2.GetDefinition().Name == "HeroSprint", "Clip name matches");
        ANIMATION_CHECK(doc2.GetDefinition().Channels.size() == 1, "One channel decoded");
        const auto& chDec = doc2.GetDefinition().Channels[0];
        ANIMATION_CHECK(chDec.BoneName == "Spine", "BoneName matches");
        ANIMATION_CHECK(chDec.PositionsInterpolation == AnimationInterpolation::StepNext, "StepNext preserved");
        ANIMATION_CHECK(chDec.Positions.size() == 3, "Positions count preserved");
        ANIMATION_CHECK(chDec.Positions[1].TimeStamp == 0.5f, "Exact timestamp preserved");
        ANIMATION_CHECK(chDec.RotationsInterpolation == AnimationInterpolation::CubicSpline, "CubicSpline preserved");
        ANIMATION_CHECK(chDec.Rotations[0].OutTangent == glm::vec4(0.1f, 0.2f, 0.3f, 0.0f), "Tangents preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Events.size() == 1, "Event preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Events[0].Id == "evt_footstep_01", "Event ID preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Events[0].StringParam == "Surface_Stone", "Event param preserved");

        // Edit via transaction and SaveAs new path
        ANIMATION_CHECK(doc2.BeginTransaction(), "Begin transaction on doc2");
        auto* cand = doc2.GetTransactionCandidate();
        cand->Duration = 2.0f;
        AnimationEventDefinition evt2;
        evt2.Id = "evt_footstep_02";
        evt2.Name = "Footstep";
        evt2.Time = 1.1f;
        cand->Events.push_back(evt2);
        ANIMATION_CHECK(doc2.CommitTransaction("Add Second Event"), "Commit edit");
        ANIMATION_CHECK(doc2.IsDirty(), "Dirty after commit");

        ANIMATION_CHECK(doc2.SaveAs(clipSaveAsPath, &err), "SaveAs second path");
        ANIMATION_CHECK(!doc2.IsDirty(), "Clean after SaveAs");

        // Reopen and verify
        AnimationEditorDocument doc3;
        ANIMATION_CHECK(doc3.OpenClip(clipSaveAsPath, &err) == LifecycleResult::Executed, "Open second path");
        ANIMATION_CHECK(doc3.GetDefinition().Duration == 2.0f, "Updated duration preserved");
        ANIMATION_CHECK(doc3.GetDefinition().Events.size() == 2, "Both events preserved");
        ANIMATION_CHECK(doc3.GetDefinition().Events[1].Id == "evt_footstep_02", "Second event ID preserved");
    }

    // 2. Controller round trip with typed parameters, states, blend points, and conditions
    {
        std::string ctrlPath = (testDir / "Combat.controller").generic_string();
        std::string ctrlSaveAs = (testDir / "Combat_V2.controller").generic_string();

        AnimatorEditorDocument doc;
        ANIMATION_CHECK(doc.SetControllerProperties("CombatController", "../Skeletons/Hero.skel", "SIG_HERO_1"), "Set controller properties");

        ParameterDefinition pSpeed;
        pSpeed.Id = "param_speed";
        pSpeed.Name = "Speed";
        pSpeed.Type = AnimatorParameterType::Float;
        pSpeed.Default = 1.25f;
        ANIMATION_CHECK(doc.AddParameter(pSpeed), "Add float parameter");

        ParameterDefinition pGrounded;
        pGrounded.Id = "param_grounded";
        pGrounded.Name = "IsGrounded";
        pGrounded.Type = AnimatorParameterType::Bool;
        pGrounded.Default = true;
        ANIMATION_CHECK(doc.AddParameter(pGrounded), "Add bool parameter");

        StateDefinition sIdle;
        sIdle.Id = "state_idle";
        sIdle.Name = "Idle";
        sIdle.NodeType = AnimationNodeType::Clip;
        sIdle.MotionAssetRef = "../Anims/Idle.anim";
        sIdle.Loop = true;
        ANIMATION_CHECK(doc.AddState(sIdle), "Add Idle state");

        StateDefinition sMove;
        sMove.Id = "state_move";
        sMove.Name = "Movement";
        sMove.NodeType = AnimationNodeType::BlendSpace1D;
        sMove.BlendParameterX = "Speed";
        sMove.BlendPoints = {
            {0.0f, "../Anims/Walk.anim"},
            {1.0f, "../Anims/Run.anim"}
        };
        ANIMATION_CHECK(doc.AddState(sMove), "Add BlendSpace1D Movement state");

        TransitionDefinition tIdleToMove;
        tIdleToMove.Id = "trans_idle_to_move";
        tIdleToMove.FromState = "state_idle";
        tIdleToMove.ToState = "state_move";
        tIdleToMove.CrossfadeSeconds = 0.25f;
        tIdleToMove.AllConditions = {
            {"param_speed", ConditionOperator::Greater, 0.1f}
        };
        ANIMATION_CHECK(doc.AddTransition(tIdleToMove), "Add transition");

        std::string err;
        ANIMATION_CHECK(doc.SaveAs(ctrlPath, &err), "SaveAs controller");

        // Reopen in new document
        AnimatorEditorDocument doc2;
        ANIMATION_CHECK(doc2.OpenController(ctrlPath, &err) == LifecycleResult::Executed, "Open controller");
        ANIMATION_CHECK(doc2.GetDefinition().Name == "CombatController", "Name preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Parameters.size() == 2, "Parameters preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Parameters[0].Id == "param_speed", "Param ID preserved");
        ANIMATION_CHECK(std::get<float>(doc2.GetDefinition().Parameters[0].Default) == 1.25f, "Float default preserved");
        ANIMATION_CHECK(doc2.GetDefinition().States.size() == 2, "States preserved");
        ANIMATION_CHECK(doc2.GetDefinition().States[1].NodeType == AnimationNodeType::BlendSpace1D, "BlendSpace1D preserved");
        ANIMATION_CHECK(doc2.GetDefinition().States[1].BlendPoints.size() == 2, "Blend points preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Transitions.size() == 1, "Transition preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Transitions[0].Id == "trans_idle_to_move", "Transition ID preserved");
        ANIMATION_CHECK(doc2.GetDefinition().Transitions[0].AllConditions[0].Operator == ConditionOperator::Greater, "Condition preserved");
    }

    CleanupTestDir(testDir);
    return true;
}

static bool TestDocumentIsolation() {
    using namespace Axle;
    // Two documents must not share any draft or internal state
    AnimationEditorDocument docA;
    AnimationEditorDocument docB;

    docA.SetClipProperties("DocA_Clip", 2.0f, 30.0f, "SkelA");
    docB.SetClipProperties("DocB_Clip", 5.0f, 60.0f, "SkelB");

    ANIMATION_CHECK(docA.GetDefinition().Name == "DocA_Clip", "DocA has distinct name");
    ANIMATION_CHECK(docB.GetDefinition().Name == "DocB_Clip", "DocB has distinct name");
    ANIMATION_CHECK(docA.GetDefinition().Duration == 2.0f, "DocA distinct duration");
    ANIMATION_CHECK(docB.GetDefinition().Duration == 5.0f, "DocB distinct duration");

    // Modify candidate in docA transaction
    docA.BeginTransaction();
    auto* cand = docA.GetTransactionCandidate();
    cand->Duration = 99.0f;

    ANIMATION_CHECK(docB.GetDefinition().Duration == 5.0f, "DocB unchanged during DocA candidate edit");
    docA.CancelTransaction();
    ANIMATION_CHECK(docA.GetDefinition().Duration == 2.0f, "DocA restored after cancel");
    ANIMATION_CHECK(docB.GetDefinition().Duration == 5.0f, "DocB still unaffected");

    return true;
}

static bool TestRevisionIdentityAndBranchAtSameDepth() {
    using namespace Axle;
    auto testDir = CreateTestDir("RevisionBranch");
    std::string path = (testDir / "RevisionTest.anim").generic_string();

    AnimationEditorDocument doc;
    ANIMATION_CHECK(!doc.IsDirty(), "Initially clean");
    uint64_t revClean = doc.GetCurrentRevision();
    ANIMATION_CHECK(doc.GetSavedRevision() == revClean, "Saved revision matches current revision");

    // Edit 1
    AnimationChannel ch1; ch1.BoneName = "Bone1";
    doc.AddChannel(ch1);
    ANIMATION_CHECK(doc.IsDirty(), "Dirty after edit 1");
    uint64_t revEdit1 = doc.GetCurrentRevision();
    ANIMATION_CHECK(revEdit1 > revClean, "Revision incremented on edit 1");

    // Save
    std::string err;
    ANIMATION_CHECK(doc.SaveAs(path, &err), "SaveAs succeeds");
    ANIMATION_CHECK(!doc.IsDirty(), "Clean after SaveAs");
    ANIMATION_CHECK(doc.GetSavedRevision() == revEdit1, "Saved revision now equals revEdit1");
    ANIMATION_CHECK(doc.CanUndo(), "Saving does not erase history");

    // Edit 2
    AnimationChannel ch2; ch2.BoneName = "Bone2";
    doc.AddChannel(ch2);
    ANIMATION_CHECK(doc.IsDirty(), "Dirty after edit 2");
    uint64_t revEdit2 = doc.GetCurrentRevision();
    ANIMATION_CHECK(revEdit2 > revEdit1, "Revision incremented on edit 2");

    // Undo edit 2 -> back to revEdit1 (saved revision)
    ANIMATION_CHECK(doc.Undo(), "Undo edit 2");
    ANIMATION_CHECK(doc.GetCurrentRevision() == revEdit1, "Current revision restored to revEdit1");
    ANIMATION_CHECK(!doc.IsDirty(), "Undoing back to saved revision is CLEAN");

    // Redo edit 2 -> back to revEdit2
    ANIMATION_CHECK(doc.Redo(), "Redo edit 2");
    ANIMATION_CHECK(doc.GetCurrentRevision() == revEdit2, "Current revision restored to revEdit2");
    ANIMATION_CHECK(doc.IsDirty(), "Redo away from saved revision is DIRTY");

    // Undo again back to saved state
    ANIMATION_CHECK(doc.Undo(), "Undo again");
    ANIMATION_CHECK(!doc.IsDirty(), "Clean again at saved state");

    // Branch at same depth: make Edit 3 instead
    AnimationChannel ch3; ch3.BoneName = "Bone3";
    doc.AddChannel(ch3);
    uint64_t revBranch = doc.GetCurrentRevision();
    ANIMATION_CHECK(revBranch != revEdit2, "Branch gets a unique revision different from abandoned redo branch");
    ANIMATION_CHECK(revBranch != revEdit1, "Branch revision differs from saved revision");
    ANIMATION_CHECK(doc.IsDirty(), "Branch at same stack depth is DIRTY because revision differs from saved");
    ANIMATION_CHECK(!doc.CanRedo(), "Redo stack was cleared on branch");

    // Undo branch -> back to revEdit1
    ANIMATION_CHECK(doc.Undo(), "Undo branch");
    ANIMATION_CHECK(doc.GetCurrentRevision() == revEdit1, "Current revision back to saved");
    ANIMATION_CHECK(!doc.IsDirty(), "Clean once more after undoing branch");

    CleanupTestDir(testDir);
    return true;
}

static bool TestSemanticNoOpInvalidEditsAndCancel() {
    using namespace Axle;
    AnimationEditorDocument doc;
    uint64_t initialRev = doc.GetCurrentRevision();
    size_t initialUndoCount = doc.GetStats().UndoCount;

    // 1. Semantic no-op transaction
    ANIMATION_CHECK(doc.BeginTransaction(), "Begin transaction for no-op");
    // Make no changes, or assign exact same properties
    doc.GetTransactionCandidate()->Name = doc.GetDefinition().Name;
    std::string err;
    ANIMATION_CHECK(doc.CommitTransaction("No-Op", &err), "Commit no-op succeeds");
    ANIMATION_CHECK(doc.GetCurrentRevision() == initialRev, "No-op does NOT modify revision");
    ANIMATION_CHECK(!doc.IsDirty(), "No-op does NOT change dirty state");
    ANIMATION_CHECK(doc.GetStats().UndoCount == initialUndoCount, "No-op adds NO history entries");

    // 2. Invalid edit rejected
    ANIMATION_CHECK(doc.BeginTransaction(), "Begin transaction for invalid edit");
    auto* cand = doc.GetTransactionCandidate();
    cand->Duration = -5.0f; // Invalid negative duration
    ANIMATION_CHECK(!doc.CommitTransaction("Invalid Duration", &err), "Commit fails for invalid duration");
    ANIMATION_CHECK(doc.IsInTransaction(), "Document remains in transaction after failed validation");
    doc.CancelTransaction();
    ANIMATION_CHECK(!doc.IsInTransaction(), "Transaction cancelled");
    ANIMATION_CHECK(doc.GetDefinition().Duration == 1.0f, "Duration untouched");
    ANIMATION_CHECK(doc.GetCurrentRevision() == initialRev, "Revision untouched after failed invalid edit");
    ANIMATION_CHECK(doc.GetStats().UndoCount == initialUndoCount, "No history entry added");

    // 3. Multi-sub-edit single commit
    ANIMATION_CHECK(doc.BeginTransaction(), "Begin multi-edit transaction");
    cand = doc.GetTransactionCandidate();
    cand->Duration = 3.0f;
    AnimationChannel ch; ch.BoneName = "Arm";
    cand->Channels.push_back(ch);
    AnimationEventDefinition evt; evt.Id = "e1"; evt.Name = "Strike"; evt.Time = 1.0f;
    cand->Events.push_back(evt);
    ANIMATION_CHECK(doc.CommitTransaction("Multi Edit", &err), "Commit multi edit");

    ANIMATION_CHECK(doc.GetStats().UndoCount == initialUndoCount + 1, "Exactly ONE history command recorded");
    ANIMATION_CHECK(doc.GetDefinition().Duration == 3.0f, "Duration updated");
    ANIMATION_CHECK(doc.GetDefinition().Channels.size() == 1, "Channel added");
    ANIMATION_CHECK(doc.GetDefinition().Events.size() == 1, "Event added");

    // Single undo reverts all 3 changes at once
    ANIMATION_CHECK(doc.Undo(), "Undo multi edit");
    ANIMATION_CHECK(doc.GetDefinition().Duration == 1.0f, "Duration restored");
    ANIMATION_CHECK(doc.GetDefinition().Channels.empty(), "Channels restored empty");
    ANIMATION_CHECK(doc.GetDefinition().Events.empty(), "Events restored empty");
    ANIMATION_CHECK(!doc.IsDirty(), "Back to clean state");

    return true;
}

static bool TestHistoryLimitsAndOversizedEdit() {
    using namespace Axle;
    AnimationEditorDocument doc;
    // Set max 3 commands with generous memory budget for the count test
    doc.SetHistoryLimits(3, 1024 * 1024, 64 * 1024);

    // Commit 5 edits
    for (int i = 1; i <= 5; ++i) {
        AnimationChannel ch;
        ch.BoneName = "Bone_" + std::to_string(i);
        ANIMATION_CHECK(doc.AddChannel(ch), "Add channel");
    }

    ANIMATION_CHECK(doc.GetStats().UndoCount == 3, "Undo stack capped at max 3 commands");

    // Undo 3 times
    ANIMATION_CHECK(doc.Undo(), "Undo 1");
    ANIMATION_CHECK(doc.Undo(), "Undo 2");
    ANIMATION_CHECK(doc.Undo(), "Undo 3");
    ANIMATION_CHECK(!doc.CanUndo(), "Cannot undo beyond history bound");
    ANIMATION_CHECK(doc.GetDefinition().Channels.size() == 2, "Earliest available state has 2 channels");

    // Now test oversized edit rejection by lowering single command budget to 100 bytes
    doc.SetHistoryLimits(3, 1024 * 1024, 100);
    ANIMATION_CHECK(doc.BeginTransaction(), "Begin transaction for oversized edit");
    auto* cand = doc.GetTransactionCandidate();
    AnimationChannel largeCh;
    largeCh.BoneName = "HugeTrack";
    for (int k = 0; k < 10; ++k) {
        largeCh.Positions.push_back({float(k), glm::vec3(float(k))});
    }
    cand->Channels.push_back(largeCh);

    std::string err;
    ANIMATION_CHECK(!doc.CommitTransaction("Oversized Edit", &err), "Oversized edit rejected");
    ANIMATION_CHECK(!err.empty(), "Diagnostic returned for oversized edit");
    doc.CancelTransaction();

    return true;
}

static bool TestGuardedLifecycleAndStaleResponse() {
    using namespace Axle;
    auto testDir = CreateTestDir("Lifecycle");
    std::string filePath = (testDir / "GuardedTest.anim").generic_string();

    AnimationEditorDocument doc;
    ANIMATION_CHECK(doc.SaveAs(filePath), "Initial save");
    ANIMATION_CHECK(!doc.IsDirty(), "Clean");

    // Make an edit -> dirty
    AnimationChannel ch; ch.BoneName = "Pelvis";
    doc.AddChannel(ch);
    ANIMATION_CHECK(doc.IsDirty(), "Document is dirty");
    uint64_t dirtyRev = doc.GetCurrentRevision();

    // 1. Guarded New
    ANIMATION_CHECK(doc.NewClip("Other") == LifecycleResult::PendingConfirmation, "New returns PendingConfirmation when dirty");
    ANIMATION_CHECK(doc.GetPendingAction() == DocumentPendingAction::New, "Pending action is New");
    // Cancel pending action
    ANIMATION_CHECK(doc.CancelPendingAction() == LifecycleResult::Cancelled, "Cancel pending action");
    ANIMATION_CHECK(doc.IsDirty(), "Still dirty after cancel");
    ANIMATION_CHECK(doc.GetDefinition().Channels.size() == 1, "Edit preserved after cancel");

    // Guarded New again -> Discard
    ANIMATION_CHECK(doc.NewClip("Other") == LifecycleResult::PendingConfirmation, "New returns PendingConfirmation");
    ANIMATION_CHECK(doc.ConfirmDiscardAndProceed() == LifecycleResult::Executed, "Confirm discard executes New");
    ANIMATION_CHECK(!doc.IsDirty(), "Clean after confirmed discard");
    ANIMATION_CHECK(doc.GetDefinition().Name == "Other", "New clip name applied");
    ANIMATION_CHECK(doc.GetDefinition().Channels.empty(), "Discarded dirty channels");

    // 2. Stale response rejection
    // Open file
    ANIMATION_CHECK(doc.OpenClip(filePath) == LifecycleResult::Executed, "Open file");
    // Edit to dirty
    doc.AddChannel(ch);
    ANIMATION_CHECK(doc.IsDirty(), "Dirty");
    // Call Close -> PendingConfirmation at current revision
    ANIMATION_CHECK(doc.Close() == LifecycleResult::PendingConfirmation, "Close pending");
    // Before confirming, an edit happens!
    AnimationEventDefinition evt; evt.Id = "e1"; evt.Name = "Jump"; evt.Time = 0.5f;
    doc.AddEvent(evt);
    ANIMATION_CHECK(doc.GetCurrentRevision() > dirtyRev, "Revision advanced by new edit");

    // Stale confirm attempt
    std::string err;
    ANIMATION_CHECK(doc.ConfirmDiscardAndProceed(&err) == LifecycleResult::StaleResponseRejected, "Stale confirm rejected");
    ANIMATION_CHECK(doc.IsDirty(), "Still dirty, edit preserved");
    ANIMATION_CHECK(doc.GetDefinition().Events.size() == 1, "Newer edit was NOT lost");
    doc.CancelPendingAction();

    // 3. Failed save preservation
    AnimationEditorDocument untitledDoc;
    untitledDoc.AddChannel(ch); // Dirty, but no file path
    ANIMATION_CHECK(untitledDoc.Close() == LifecycleResult::PendingConfirmation, "Close pending");
    ANIMATION_CHECK(untitledDoc.SaveAndProceed(&err) == LifecycleResult::SaveFailed, "SaveAndProceed fails when untitled");
    ANIMATION_CHECK(untitledDoc.IsDirty(), "Document remains dirty");
    ANIMATION_CHECK(untitledDoc.GetDefinition().Channels.size() == 1, "Destructive close NOT executed on failed save");

    // 4. Package read-only save rejection
    AnimationEditorDocument pkgDoc;
    // Simulate open from package path
    pkgDoc.SaveAs(filePath);
    // Directly setting virtual path isn't exposed publicly, but let's test SaveAs with package path
    ANIMATION_CHECK(!pkgDoc.SaveAs("package://test.anim", &err), "SaveAs package path rejected");

    CleanupTestDir(testDir);
    return true;
}

static bool TestSelectionSurvivesReorderAndRepairsDelete() {
    using namespace Axle;
    // 1. Clip selection
    {
        AnimationEditorDocument doc;
        AnimationChannel cA; cA.BoneName = "BoneA";
        AnimationChannel cB; cB.BoneName = "BoneB";
        AnimationChannel cC; cC.BoneName = "BoneC";
        doc.AddChannel(cA);
        doc.AddChannel(cB);
        doc.AddChannel(cC);

        doc.SelectChannel("BoneB", AnimationChannelType::Translation);
        ANIMATION_CHECK(doc.GetSelection().Kind == ClipSelectionKind::Channel, "Channel selected");
        ANIMATION_CHECK(doc.GetSelection().BoneName == "BoneB", "BoneB selected");

        // Reorder channels: move BoneB from index 1 to index 0
        ANIMATION_CHECK(doc.ReorderChannels(1, 0), "Reorder channels");
        ANIMATION_CHECK(doc.GetDefinition().Channels[0].BoneName == "BoneB", "BoneB now at index 0");
        ANIMATION_CHECK(doc.GetSelection().BoneName == "BoneB", "Selection still points to BoneB after reorder");

        // Remove BoneB -> selection should repair to None
        ANIMATION_CHECK(doc.RemoveChannel("BoneB"), "Remove BoneB");
        ANIMATION_CHECK(doc.GetSelection().Kind == ClipSelectionKind::None, "Selection cleared upon delete");

        // Undo -> BoneB restored AND selection restored to BoneB!
        ANIMATION_CHECK(doc.Undo(), "Undo delete");
        ANIMATION_CHECK(doc.GetSelection().Kind == ClipSelectionKind::Channel, "Selection restored");
        ANIMATION_CHECK(doc.GetSelection().BoneName == "BoneB", "Selection restored to BoneB");

        // Redo -> BoneB deleted AND selection cleared!
        ANIMATION_CHECK(doc.Redo(), "Redo delete");
        ANIMATION_CHECK(doc.GetSelection().Kind == ClipSelectionKind::None, "Selection cleared on redo");
    }

    // 2. Controller selection
    {
        AnimatorEditorDocument doc;
        StateDefinition s1; s1.Id = "state_1"; s1.Name = "StateOne";
        StateDefinition s2; s2.Id = "state_2"; s2.Name = "StateTwo";
        StateDefinition s3; s3.Id = "state_3"; s3.Name = "StateThree";
        doc.AddState(s1);
        doc.AddState(s2);
        doc.AddState(s3);

        doc.SelectState("state_2");
        ANIMATION_CHECK(doc.GetSelection().Kind == ControllerSelectionKind::State, "State selected");
        ANIMATION_CHECK(doc.GetSelection().SelectedId == "state_2", "state_2 selected");

        // Reorder states: move state_2 from index 1 to index 2
        ANIMATION_CHECK(doc.ReorderStates(1, 2), "Reorder states");
        ANIMATION_CHECK(doc.GetDefinition().States[2].Id == "state_2", "state_2 now at index 2");
        ANIMATION_CHECK(doc.GetSelection().SelectedId == "state_2", "Selection survives state reorder");

        // Delete state_2 -> selection clears
        ANIMATION_CHECK(doc.RemoveState("state_2"), "Remove state_2");
        ANIMATION_CHECK(doc.GetSelection().Kind == ControllerSelectionKind::None, "Selection repaired to None");

        // Undo -> restored state AND selection
        ANIMATION_CHECK(doc.Undo(), "Undo state delete");
        ANIMATION_CHECK(doc.GetSelection().Kind == ControllerSelectionKind::State, "Selection restored");
        ANIMATION_CHECK(doc.GetSelection().SelectedId == "state_2", "Selection restored to state_2");

        // Redo -> removed state AND cleared selection
        ANIMATION_CHECK(doc.Redo(), "Redo state delete");
        ANIMATION_CHECK(doc.GetSelection().Kind == ControllerSelectionKind::None, "Selection cleared on redo");
    }

    return true;
}

static bool TestMalformedMissingWrongKindInputs() {
    using namespace Axle;
    auto testDir = CreateTestDir("Malformed");
    std::string missingFile = (testDir / "DoesNotExist.anim").generic_string();
    std::string malformedFile = (testDir / "Corrupt.anim").generic_string();
    std::string controllerFile = (testDir / "ValidCtrl.controller").generic_string();
    std::string clipFile = (testDir / "ValidClip.anim").generic_string();

    // Write malformed file
    {
        std::ofstream out(malformedFile);
        out << "@#$ INVALID ASSET SYNTAX }{][\n";
    }

    // Write valid controller file
    {
        AnimatorControllerDefinition cdef;
        cdef.Id = "c1"; cdef.Name = "Controller";
        AnimationAssetCodec::SaveAnimatorController(controllerFile, cdef);
    }

    // Write valid clip file
    {
        AnimationClipDefinition adef;
        adef.Id = "a1"; adef.Name = "Clip"; adef.Duration = 1.0f; adef.FrameRate = 30.0f;
        AnimationAssetCodec::SaveAnimationClip(clipFile, adef);
    }

    // Test on AnimationEditorDocument
    AnimationEditorDocument clipDoc;
    clipDoc.SetClipProperties("LastGoodClip", 2.5f, 30.0f, "Rig");
    AnimationChannel ch; ch.BoneName = "Root"; clipDoc.AddChannel(ch);
    clipDoc.SaveAs((testDir / "LastGood.anim").generic_string());
    ANIMATION_CHECK(!clipDoc.IsDirty(), "Clean after saving last good state");

    std::string err;
    // Missing file
    ANIMATION_CHECK(clipDoc.OpenClip(missingFile, &err) == LifecycleResult::LoadFailed, "Missing file fails to load");
    ANIMATION_CHECK(clipDoc.GetDefinition().Name == "LastGoodClip", "Document preserved on missing file");
    ANIMATION_CHECK(clipDoc.GetDefinition().Channels.size() == 1, "Channels preserved");

    // Malformed file
    ANIMATION_CHECK(clipDoc.OpenClip(malformedFile, &err) == LifecycleResult::LoadFailed, "Malformed file fails to load");
    ANIMATION_CHECK(clipDoc.GetDefinition().Name == "LastGoodClip", "Document preserved on malformed file");

    // Wrong kind (trying to open controller file in clip document)
    ANIMATION_CHECK(clipDoc.OpenClip(controllerFile, &err) == LifecycleResult::LoadFailed, "Wrong kind fails to load");
    ANIMATION_CHECK(clipDoc.GetDefinition().Name == "LastGoodClip", "Document preserved on wrong kind");

    // Test on AnimatorEditorDocument opening clip file
    AnimatorEditorDocument ctrlDoc;
    ctrlDoc.SetControllerProperties("LastGoodController", "Rig");
    ctrlDoc.SaveAs((testDir / "LastGoodCtrl.controller").generic_string());
    ANIMATION_CHECK(!ctrlDoc.IsDirty(), "Controller doc clean after saving");
    ANIMATION_CHECK(ctrlDoc.OpenController(clipFile, &err) == LifecycleResult::LoadFailed, "Clip file in controller doc fails");
    ANIMATION_CHECK(ctrlDoc.GetDefinition().Name == "LastGoodController", "Controller doc preserved on wrong kind");

    CleanupTestDir(testDir);
    return true;
}

static bool TestRelocationPolicy() {
    using namespace Axle;
    auto testDir = CreateTestDir("Relocation");
    auto animsDir = testDir / "Assets" / "Animations";
    auto combatDir = testDir / "Assets" / "Combat" / "Special";
    std::error_code ec;
    std::filesystem::create_directories(animsDir, ec);
    std::filesystem::create_directories(combatDir, ec);

    std::string origPath = (animsDir / "Run.anim").generic_string();
    std::string newPath = (combatDir / "Run.anim").generic_string();

    AnimationEditorDocument doc;
    // Relative reference: skeleton is at Assets/Skeletons/Hero.skel, which from Assets/Animations is ../Skeletons/Hero.skel
    ANIMATION_CHECK(doc.SetClipProperties("Run", 1.0f, 30.0f, "../Skeletons/Hero.skel"), "Authored relative skeleton ref");

    std::string err;
    ANIMATION_CHECK(doc.SaveAs(origPath, &err), "Save to original location");
    ANIMATION_CHECK(doc.GetDefinition().SkeletonRef == "../Skeletons/Hero.skel", "Ref preserved at original location");

    // Now SaveAs to Assets/Combat/Special/Run.anim
    // From Assets/Combat/Special to Assets/Skeletons/Hero.skel is ../../../Skeletons/Hero.skel
    ANIMATION_CHECK(doc.SaveAs(newPath, &err), "SaveAs to new directory");

    std::string expectedRel = "../../Skeletons/Hero.skel";
    // Check relocated ref in memory
    ANIMATION_CHECK(doc.GetDefinition().SkeletonRef == expectedRel, "SkeletonRef relocated relative to new directory");

    // Reopen from disk to verify file on disk has the relocated path
    AnimationEditorDocument reopened;
    ANIMATION_CHECK(reopened.OpenClip(newPath, &err) == LifecycleResult::Executed, "Reopen relocated file");
    ANIMATION_CHECK(reopened.GetDefinition().SkeletonRef == expectedRel, "Relocated ref verified from disk");

    // Verify package path is preserved intact without relocation
    doc.SetClipProperties("Run", 1.0f, 30.0f, "package://core/Hero.skel");
    doc.SaveAs(origPath);
    doc.SaveAs(newPath);
    ANIMATION_CHECK(doc.GetDefinition().SkeletonRef == "package://core/Hero.skel", "Package reference untouched by relocation");

    CleanupTestDir(testDir);
    return true;
}

bool RunAnimationDocumentUnitTests() {
    std::cout << "--- Running Animation Document Unit Tests (G1) ---\n";
    ANIMATION_CHECK(TestDocumentRoundTripAndAuthoredPreservation(), "TestDocumentRoundTripAndAuthoredPreservation");
    ANIMATION_CHECK(TestDocumentIsolation(), "TestDocumentIsolation");
    ANIMATION_CHECK(TestRevisionIdentityAndBranchAtSameDepth(), "TestRevisionIdentityAndBranchAtSameDepth");
    ANIMATION_CHECK(TestSemanticNoOpInvalidEditsAndCancel(), "TestSemanticNoOpInvalidEditsAndCancel");
    ANIMATION_CHECK(TestHistoryLimitsAndOversizedEdit(), "TestHistoryLimitsAndOversizedEdit");
    ANIMATION_CHECK(TestGuardedLifecycleAndStaleResponse(), "TestGuardedLifecycleAndStaleResponse");
    ANIMATION_CHECK(TestSelectionSurvivesReorderAndRepairsDelete(), "TestSelectionSurvivesReorderAndRepairsDelete");
    ANIMATION_CHECK(TestMalformedMissingWrongKindInputs(), "TestMalformedMissingWrongKindInputs");
    ANIMATION_CHECK(TestRelocationPolicy(), "TestRelocationPolicy");
    std::cout << "--- Animation Document Unit Tests PASS ---\n";
    return true;
}

