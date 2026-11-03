#include "AnimationTestHarness.hpp"
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/AnimationStateMachine.hpp>
#include <Axle/Animator.hpp>
#include <Axle/AnimationDefinition.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/AssetDSL.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>
#include <limits>

static bool TestBaselineSampling() {
    Axle::Skeleton skeleton("Fixture");
    Axle::Bone root;
    root.Name = "Root";
    skeleton.AddBone(root);
    Axle::Bone child;
    child.Name = "Child"; child.ParentID = 0;
    child.LocalTransform = glm::translate(glm::mat4(1), glm::vec3(0, 2, 0));
    skeleton.AddBone(child);
    skeleton.ComputeInverseBindMatrices();
    Axle::AnimationClip clip("Translation", 1);
    Axle::AnimationChannel channel;
    channel.BoneName = "Root";
    channel.Positions = {{0, glm::vec3(0)}, {1, glm::vec3(2, 0, 0)}};
    clip.AddChannel(channel);
    Axle::AnimationPose pose;
    clip.SamplePose(0.5f, skeleton, pose, false);
    std::vector<glm::mat4> globals, palette;
    pose.ComputeGlobalMatrices(skeleton, globals);
    pose.ComputeSkinningMatrices(skeleton, palette);
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[1], glm::translate(glm::mat4(1), {1, 2, 0})), "Hierarchical translated pose");
    ANIMATION_CHECK(Axle::AnimationTests::Near(palette[1], glm::translate(glm::mat4(1), {1, 0, 0})), "Bind-relative skin palette");
    return true;
}

static bool TestChildBeforeParentTopologicalOrder() {
    Axle::Skeleton skeleton("ChildBeforeParent");
    // Bone 0 is child of Bone 1
    Axle::Bone child;
    child.Name = "Child";
    child.ParentID = 1;
    child.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f));
    skeleton.AddBone(child);

    // Bone 1 is parent (root)
    Axle::Bone parent;
    parent.Name = "Parent";
    parent.ParentID = -1;
    parent.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    skeleton.AddBone(parent);

    ANIMATION_CHECK(skeleton.IsValid(), "Skeleton with child before parent index is valid");
    std::vector<size_t> order;
    ANIMATION_CHECK(skeleton.BuildEvaluationOrder(order), "Build evaluation order succeeds");
    ANIMATION_CHECK(order.size() == 2 && order[0] == 1 && order[1] == 0, "Evaluation order puts parent before child");

    ANIMATION_CHECK(skeleton.ComputeInverseBindMatrices(), "ComputeInverseBindMatrices succeeds");
    std::vector<glm::mat4> globals;
    ANIMATION_CHECK(skeleton.ComputeBindPoseGlobalTransforms(globals), "ComputeBindPoseGlobalTransforms succeeds");
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[1], glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f))), "Parent global transform");
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[0], glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 5.0f, 0.0f))), "Child global transform with child-before-parent index");
    return true;
}

static bool TestMultiRootSkeleton() {
    Axle::Skeleton skeleton("MultiRoot");
    Axle::Bone rootA;
    rootA.Name = "RootA";
    rootA.ParentID = -1;
    rootA.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    skeleton.AddBone(rootA);

    Axle::Bone childA;
    childA.Name = "ChildA";
    childA.ParentID = 0;
    childA.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    skeleton.AddBone(childA);

    Axle::Bone rootB;
    rootB.Name = "RootB";
    rootB.ParentID = -1;
    rootB.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f));
    skeleton.AddBone(rootB);

    Axle::Bone childB;
    childB.Name = "ChildB";
    childB.ParentID = 2;
    childB.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f));
    skeleton.AddBone(childB);

    ANIMATION_CHECK(skeleton.IsValid(), "Multi-root skeleton is valid");
    ANIMATION_CHECK(skeleton.ComputeInverseBindMatrices(), "ComputeInverseBindMatrices for multi-root");
    std::vector<glm::mat4> globals;
    ANIMATION_CHECK(skeleton.ComputeBindPoseGlobalTransforms(globals), "Compute globals for multi-root");
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[0], glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f))), "RootA global");
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[1], glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 0.0f))), "ChildA global");
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[2], glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f))), "RootB global");
    ANIMATION_CHECK(Axle::AnimationTests::Near(globals[3], glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 3.0f))), "ChildB global");
    return true;
}

static bool TestSkeletonCycleDetection() {
    Axle::Skeleton skeleton("Cyclic");
    Axle::Bone boneA;
    boneA.Name = "BoneA";
    boneA.ParentID = 1;
    skeleton.AddBone(boneA);

    Axle::Bone boneB;
    boneB.Name = "BoneB";
    boneB.ParentID = 0;
    skeleton.AddBone(boneB);

    std::vector<size_t> order;
    std::vector<std::string> errors;
    bool valid = skeleton.BuildEvaluationOrder(order, &errors);
    ANIMATION_CHECK(!valid, "Skeleton cycle detected and rejected");
    ANIMATION_CHECK(!errors.empty(), "Cycle produces diagnostic errors");
    return true;
}

static bool TestRotationOnlyTrackPreservesBindPose() {
    Axle::Skeleton skeleton("PartialTrack");
    Axle::Bone root;
    root.Name = "Root";
    root.ParentID = -1;
    skeleton.AddBone(root);

    Axle::Bone child;
    child.Name = "Child";
    child.ParentID = 0;
    child.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f))
                         * glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 1.5f, 1.5f));
    skeleton.AddBone(child);
    skeleton.ComputeInverseBindMatrices();

    Axle::AnimationClip clip("RotationOnly", 1.0f);
    Axle::AnimationChannel channel;
    channel.BoneName = "Child";
    // Channel contains only rotation keyframes! No translation or scale keys.
    channel.Rotations = {
        {0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
        {1.0f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))}
    };
    clip.AddChannel(channel);

    Axle::AnimationPose pose;
    ANIMATION_CHECK(clip.SamplePose(0.5f, skeleton, pose, false), "SamplePose succeeds");
    ANIMATION_CHECK(Axle::AnimationTests::Near(pose.Transforms[1].Translation, glm::vec3(0.0f, 2.0f, 0.0f)),
                    "Rotation-only channel preserves bind translation (0, 2, 0)");
    ANIMATION_CHECK(Axle::AnimationTests::Near(pose.Transforms[1].Scale, glm::vec3(1.5f, 1.5f, 1.5f)),
                    "Rotation-only channel preserves bind scale (1.5, 1.5, 1.5)");
    return true;
}

static bool TestAntipodalQuaternionInterpolation() {
    glm::quat qA = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat qB = -qA; // Antipodal: represents identical orientation
    Axle::BoneTransform bA(glm::vec3(0.0f), qA, glm::vec3(1.0f));
    Axle::BoneTransform bB(glm::vec3(0.0f), qB, glm::vec3(1.0f));
    Axle::BoneTransform blended = Axle::BoneTransform::Blend(bA, bB, 0.5f);
    ANIMATION_CHECK(Axle::AnimationTests::Near(std::abs(blended.Rotation.w), 1.0f),
                    "Antipodal quaternions slerp smoothly without flipping");
    ANIMATION_CHECK(Axle::AnimationTests::Near(blended.Rotation.x, 0.0f), "Antipodal x is 0");
    ANIMATION_CHECK(Axle::AnimationTests::Near(blended.Rotation.y, 0.0f), "Antipodal y is 0");
    ANIMATION_CHECK(Axle::AnimationTests::Near(blended.Rotation.z, 0.0f), "Antipodal z is 0");
    return true;
}

static bool TestSignedScaleDecomposition() {
    glm::mat4 mOriginal = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 2.0f, 3.0f)); // negative scale reflection
    Axle::BoneTransform bt;
    bool ok = Axle::BoneTransform::TryFromMatrix(mOriginal, bt);
    ANIMATION_CHECK(ok, "Signed scale reflection matrix successfully decomposed");
    ANIMATION_CHECK(bt.Scale.x < 0.0f, "Signed scale preserves negative scale / reflection");
    glm::mat4 mRecomposed = bt.ToMatrix();
    ANIMATION_CHECK(Axle::AnimationTests::Near(mOriginal, mRecomposed), "Signed scale matrix recomposition matches original");

    // Sheared matrix rejection
    glm::mat4 mSheared = glm::mat4(1.0f);
    mSheared[0][1] = 0.8f;
    Axle::BoneTransform btSheared;
    ANIMATION_CHECK(!Axle::BoneTransform::TryFromMatrix(mSheared, btSheared), "Sheared matrix correctly rejected");
    return true;
}

static bool TestPoseValidationAndBlendMismatchedRejection() {
    Axle::AnimationPose poseA(2);
    Axle::AnimationPose poseB(3);
    Axle::AnimationPose outPose;
    ANIMATION_CHECK(!Axle::AnimationPose::Blend(poseA, poseB, 0.5f, outPose),
                    "Mismatched pose sizes rejected in Blend");
    return true;
}

static bool TestCPUSkeletalMeshConstructionWithoutGLContext() {
    Axle::SkeletalMeshData data;
    data.Name = "CPUMesh";
    Axle::SkeletalVertex v0;
    v0.Position = {0.0f, 0.0f, 0.0f};
    v0.Normal = {0.0f, 1.0f, 0.0f};
    Axle::SkeletalVertex v1;
    v1.Position = {1.0f, 0.0f, 0.0f};
    v1.Normal = {0.0f, 1.0f, 0.0f};
    Axle::SkeletalVertex v2;
    v2.Position = {0.0f, 1.0f, 0.0f};
    v2.Normal = {0.0f, 1.0f, 0.0f};
    data.Vertices = {v0, v1, v2};
    data.Indices = {0, 1, 2};

    Axle::SkeletalMesh mesh(data);
    ANIMATION_CHECK(mesh.GetVertexCount() == 3, "SkeletalMesh vertex count correct on CPU");
    ANIMATION_CHECK(mesh.GetIndexCount() == 3, "SkeletalMesh index count correct on CPU");
    ANIMATION_CHECK(mesh.GetVAO() == nullptr, "VAO is null when constructed without active OpenGL context");
    return true;
}

static bool TestVertexInfluencePruningAndMerging() {
    Axle::SkeletalVertex v;
    v.AddBoneData(0, 0.2f);
    v.AddBoneData(0, 0.3f);
    ANIMATION_CHECK(v.BoneIDs[0] == 0, "Bone ID 0 stored");
    ANIMATION_CHECK(Axle::AnimationTests::Near(v.Weights[0], 0.5f), "Repeated bone influences merged");

    v.AddBoneData(1, 0.1f);
    v.AddBoneData(2, 0.4f);
    v.AddBoneData(3, 0.05f);
    // Add bone 4 with weight 0.8 -> replaces smallest (0.05)
    v.AddBoneData(4, 0.8f);

    v.NormalizeWeights(0);
    float sum = v.Weights.x + v.Weights.y + v.Weights.z + v.Weights.w;
    ANIMATION_CHECK(Axle::AnimationTests::Near(sum, 1.0f), "Weights strictly normalized to 1.0");

    Axle::SkeletalVertex unweighted;
    unweighted.NormalizeWeights(2);
    ANIMATION_CHECK(unweighted.BoneIDs[0] == 2, "Unweighted vertex falls back to root bone");
    ANIMATION_CHECK(Axle::AnimationTests::Near(unweighted.Weights[0], 1.0f), "Unweighted vertex gets weight 1.0");
    return true;
}

static bool TestProceduralBipedImportAndCPUNormalization() {
    Axle::SkeletalMeshData meshData;
    std::shared_ptr<Axle::Skeleton> skeleton;
    std::vector<std::shared_ptr<Axle::AnimationClip>> clips;
    bool ok = Axle::SkeletalMeshImporter::CreateProceduralBiped(meshData, skeleton, clips);
    ANIMATION_CHECK(ok, "CreateProceduralBiped succeeds on CPU");
    ANIMATION_CHECK(skeleton != nullptr && skeleton->IsValid(), "Procedural biped skeleton valid");
    ANIMATION_CHECK(!meshData.Vertices.empty(), "Procedural biped mesh has vertices");
    ANIMATION_CHECK(!meshData.Indices.empty(), "Procedural biped mesh has indices");
    ANIMATION_CHECK(clips.size() >= 2, "Procedural biped has at least 2 animation clips");

    for (const auto& v : meshData.Vertices) {
        float sum = v.Weights.x + v.Weights.y + v.Weights.z + v.Weights.w;
        ANIMATION_CHECK(Axle::AnimationTests::Near(sum, 1.0f), "All imported vertices have normalized influences");
    }
    return true;
}

static bool TestAnimatorInstanceIsolationAndReset() {
    auto skeleton = std::make_shared<Axle::Skeleton>("Rig");
    Axle::Bone root;
    root.Name = "Root";
    root.ParentID = -1;
    skeleton->AddBone(root);
    skeleton->ComputeInverseBindMatrices();

    auto clipIdle = std::make_shared<Axle::AnimationClip>("Idle", 1.0f);
    auto clipWalk = std::make_shared<Axle::AnimationClip>("Walk", 1.0f);

    auto sm = std::make_shared<Axle::AnimationStateMachine>("Locomotion");

    Axle::AnimationState stateIdle;
    stateIdle.Name = "Idle";
    stateIdle.Type = Axle::AnimationNodeType::Clip;
    stateIdle.Clip = clipIdle;
    stateIdle.Loop = true;
    sm->AddState(stateIdle);

    Axle::AnimationState stateWalk;
    stateWalk.Name = "Walk";
    stateWalk.Type = Axle::AnimationNodeType::Clip;
    stateWalk.Clip = clipWalk;
    stateWalk.Loop = true;
    sm->AddState(stateWalk);

    Axle::AnimationTransition tIdleToWalk;
    tIdleToWalk.FromState = "Idle";
    tIdleToWalk.ToState = "Walk";
    tIdleToWalk.Duration = 0.2f;
    tIdleToWalk.Rules.push_back({"Speed", Axle::TransitionConditionOperator::Greater, 0.5f});
    sm->AddTransition(tIdleToWalk);

    Axle::AnimationTransition tWalkToIdle;
    tWalkToIdle.FromState = "Walk";
    tWalkToIdle.ToState = "Idle";
    tWalkToIdle.Duration = 0.2f;
    tWalkToIdle.Rules.push_back({"Stop", Axle::TransitionConditionOperator::Triggered});
    sm->AddTransition(tWalkToIdle);

    auto anim1 = std::make_shared<Axle::Animator>(skeleton);
    anim1->SetStateMachine(sm);
    anim1->Update(0.0f);

    ANIMATION_CHECK(anim1->GetCurrentStateName() == "Idle", "Anim1 starts in Idle state");

    // Trigger transition to Walk
    anim1->SetFloat("Speed", 1.0f);
    anim1->Update(0.01f);
    ANIMATION_CHECK(anim1->IsTransitioning(), "Anim1 started transition to Walk");
    anim1->Update(0.3f);
    ANIMATION_CHECK(anim1->GetCurrentStateName() == "Walk", "Anim1 completed transition to Walk");

    // Deep clone anim1 into anim2
    auto anim2 = anim1->Clone();
    ANIMATION_CHECK(anim2 != nullptr, "Anim2 cloned successfully");
    ANIMATION_CHECK(anim2->GetStateMachine() != anim1->GetStateMachine(), "Anim1 and Anim2 have distinct state machine instances");

    // Check independent cloned state
    ANIMATION_CHECK(anim2->GetCurrentStateName() == "Walk", "Cloned animator retains active state Walk");
    ANIMATION_CHECK(!anim2->IsTransitioning(), "Cloned animator is not transitioning");

    // Test parameter isolation: modifying anim2 does not affect anim1
    anim2->SetFloat("Speed", 0.0f);
    anim2->SetInt("Stance", 42);
    anim2->SetBool("Grounded", true);
    anim2->SetTrigger("Stop");

    ANIMATION_CHECK(Axle::AnimationTests::Near(anim1->GetFloat("Speed"), 1.0f), "Anim1 Speed unchanged by Anim2 modification");
    ANIMATION_CHECK(Axle::AnimationTests::Near(anim2->GetFloat("Speed"), 0.0f), "Anim2 Speed is 0.0");
    ANIMATION_CHECK(anim1->GetInt("Stance") == 0, "Anim1 Stance unchanged by Anim2 modification");
    ANIMATION_CHECK(anim2->GetInt("Stance") == 42, "Anim2 Stance is 42");
    ANIMATION_CHECK(!anim1->GetBool("Grounded"), "Anim1 Grounded unchanged by Anim2 modification");
    ANIMATION_CHECK(anim2->GetBool("Grounded"), "Anim2 Grounded is true");

    // Trigger consumption isolation
    ANIMATION_CHECK(!anim1->CheckAndConsumeTrigger("Stop"), "Anim1 did not receive Anim2 Stop trigger");
    ANIMATION_CHECK(anim2->CheckAndConsumeTrigger("Stop"), "Anim2 consumes its own Stop trigger");
    ANIMATION_CHECK(!anim2->CheckAndConsumeTrigger("Stop"), "Anim2 Stop trigger consumed atomically (false on second check)");

    // Test Reset on anim2 vs anim1
    anim2->Reset();
    ANIMATION_CHECK(anim2->GetCurrentStateName() == "Idle", "Reset returns anim2 to initial state Idle");
    ANIMATION_CHECK(anim1->GetCurrentStateName() == "Walk", "Reset on anim2 leaves anim1 in Walk");

    // Test Reset on anim1
    anim1->SetTrigger("AnyTrigger");
    anim1->Reset();
    ANIMATION_CHECK(anim1->GetCurrentStateName() == "Idle", "Reset returns anim1 to initial state Idle");
    ANIMATION_CHECK(!anim1->IsTransitioning(), "Reset clears anim1 active transition");
    ANIMATION_CHECK(!anim1->CheckAndConsumeTrigger("AnyTrigger"), "Reset clears transient triggers");

    return true;
}

static bool TestAssetDSLAnimationKindsAndTypes() {
    // AssetKind round-trips
    ANIMATION_CHECK(Axle::AssetDSL::StringToAssetKind("Skeleton") == Axle::AssetKind::Skeleton, "StringToAssetKind Skeleton");
    ANIMATION_CHECK(Axle::AssetDSL::StringToAssetKind("SkeletalMesh") == Axle::AssetKind::SkeletalMesh, "StringToAssetKind SkeletalMesh");
    ANIMATION_CHECK(Axle::AssetDSL::StringToAssetKind("AnimationClip") == Axle::AssetKind::AnimationClip, "StringToAssetKind AnimationClip");
    ANIMATION_CHECK(Axle::AssetDSL::StringToAssetKind("AnimatorController") == Axle::AssetKind::AnimatorController, "StringToAssetKind AnimatorController");

    ANIMATION_CHECK(Axle::AssetDSL::AssetKindToString(Axle::AssetKind::Skeleton) == "Skeleton", "AssetKindToString Skeleton");
    ANIMATION_CHECK(Axle::AssetDSL::AssetKindToString(Axle::AssetKind::SkeletalMesh) == "SkeletalMesh", "AssetKindToString SkeletalMesh");
    ANIMATION_CHECK(Axle::AssetDSL::AssetKindToString(Axle::AssetKind::AnimationClip) == "AnimationClip", "AssetKindToString AnimationClip");
    ANIMATION_CHECK(Axle::AssetDSL::AssetKindToString(Axle::AssetKind::AnimatorController) == "AnimatorController", "AssetKindToString AnimatorController");

    // AssetType round-trips
    ANIMATION_CHECK(Axle::AssetTypeFromString("Skeleton") == Axle::AssetType::Skeleton, "AssetTypeFromString Skeleton");
    ANIMATION_CHECK(Axle::AssetTypeFromString("SkeletalMesh") == Axle::AssetType::SkeletalMesh, "AssetTypeFromString SkeletalMesh");
    ANIMATION_CHECK(Axle::AssetTypeFromString("AnimationClip") == Axle::AssetType::AnimationClip, "AssetTypeFromString AnimationClip");
    ANIMATION_CHECK(Axle::AssetTypeFromString("AnimatorController") == Axle::AssetType::AnimatorController, "AssetTypeFromString AnimatorController");

    ANIMATION_CHECK(std::string(Axle::AssetTypeToString(Axle::AssetType::Skeleton)) == "Skeleton", "AssetTypeToString Skeleton");
    ANIMATION_CHECK(std::string(Axle::AssetTypeToString(Axle::AssetType::SkeletalMesh)) == "SkeletalMesh", "AssetTypeToString SkeletalMesh");
    ANIMATION_CHECK(std::string(Axle::AssetTypeToString(Axle::AssetType::AnimationClip)) == "AnimationClip", "AssetTypeToString AnimationClip");
    ANIMATION_CHECK(std::string(Axle::AssetTypeToString(Axle::AssetType::AnimatorController)) == "AnimatorController", "AssetTypeToString AnimatorController");

    // Extension resolution
    ANIMATION_CHECK(Axle::GetAssetTypeFromPath("hero.skeleton") == Axle::AssetType::Skeleton, "Ext .skeleton resolves");
    ANIMATION_CHECK(Axle::GetAssetTypeFromPath("hero.skelmesh") == Axle::AssetType::SkeletalMesh, "Ext .skelmesh resolves");
    ANIMATION_CHECK(Axle::GetAssetTypeFromPath("walk.animclip") == Axle::AssetType::AnimationClip, "Ext .animclip resolves");
    ANIMATION_CHECK(Axle::GetAssetTypeFromPath("hero.animcontroller") == Axle::AssetType::AnimatorController, "Ext .animcontroller resolves");

    return true;
}

static bool TestAnimationDefinitionsAndValidation() {
    // 1. Valid Controller
    Axle::AnimatorControllerDefinition controller;
    controller.Id = "ctrl_locomotion";
    controller.Name = "LocomotionController";
    controller.SkeletonRef = "Assets/Characters/Biped.skeleton";

    Axle::ParameterDefinition pSpeed;
    pSpeed.Id = "param_speed";
    pSpeed.Name = "Speed";
    pSpeed.Type = Axle::AnimatorParameterType::Float;
    pSpeed.Default = 0.0f;
    controller.Parameters.push_back(pSpeed);

    Axle::ParameterDefinition pGrounded;
    pGrounded.Id = "param_grounded";
    pGrounded.Name = "IsGrounded";
    pGrounded.Type = Axle::AnimatorParameterType::Bool;
    pGrounded.Default = true;
    controller.Parameters.push_back(pGrounded);

    Axle::StateDefinition sIdle;
    sIdle.Id = "state_idle";
    sIdle.Name = "Idle";
    sIdle.NodeType = Axle::AnimationNodeType::Clip;
    sIdle.MotionAssetRef = "Assets/Anims/Idle.animclip";
    controller.States.push_back(sIdle);

    Axle::StateDefinition sWalk;
    sWalk.Id = "state_walk";
    sWalk.Name = "Walk";
    sWalk.NodeType = Axle::AnimationNodeType::Clip;
    sWalk.MotionAssetRef = "Assets/Anims/Walk.animclip";
    controller.States.push_back(sWalk);

    controller.EntryStateId = "state_idle";

    Axle::TransitionDefinition t1;
    t1.Id = "trans_idle_to_walk";
    t1.FromState = "state_idle";
    t1.ToState = "state_walk";
    t1.CrossfadeSeconds = 0.25f;

    Axle::ConditionDefinition c1;
    c1.Parameter = "param_speed";
    c1.Operator = Axle::ConditionOperator::Greater;
    c1.Operand = 0.1f;
    t1.AllConditions.push_back(c1);
    controller.Transitions.push_back(t1);

    std::vector<std::string> errors;
    ANIMATION_CHECK(controller.Validate(&errors), "Valid controller passes validation");
    ANIMATION_CHECK(errors.empty(), "No errors on valid controller");

    // 2. Similar names remain distinct
    Axle::StateDefinition sWalkSprint;
    sWalkSprint.Id = "state_walk_sprint";
    sWalkSprint.Name = "Walk_Sprint"; // Similar prefix to "Walk", but distinct
    controller.States.push_back(sWalkSprint);
    ANIMATION_CHECK(controller.Validate(&errors), "Two states with similar names remain distinct and valid");
    controller.States.pop_back();

    // 3. Duplicate parameter ID rejected
    auto badController = controller;
    Axle::ParameterDefinition pDupId;
    pDupId.Id = "param_speed"; // duplicate
    pDupId.Name = "Speed2";
    pDupId.Type = Axle::AnimatorParameterType::Float;
    pDupId.Default = 1.0f;
    badController.Parameters.push_back(pDupId);
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Duplicate parameter ID rejected");

    // 4. Duplicate state Name rejected
    badController = controller;
    Axle::StateDefinition sDupName;
    sDupName.Id = "state_another_walk";
    sDupName.Name = "Walk"; // duplicate name
    badController.States.push_back(sDupName);
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Duplicate state name rejected");

    // 5. Incompatible parameter default (Float holding bool)
    badController = controller;
    badController.Parameters[0].Default = true; // Float holding bool!
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Float holding bool variant rejected");

    // 6. Non-finite float parameter default rejected
    badController = controller;
    badController.Parameters[0].Default = std::numeric_limits<float>::quiet_NaN();
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "NaN float parameter default rejected");

    // 7. Dangling transition link (missing state)
    badController = controller;
    badController.Transitions[0].ToState = "state_nonexistent";
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Dangling transition ToState rejected");

    // 8. Missing EntryStateId rejected
    badController = controller;
    badController.EntryStateId = "";
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Missing EntryStateId rejected");

    // 9. Negative crossfade rejected
    badController = controller;
    badController.Transitions[0].CrossfadeSeconds = -0.5f;
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Negative crossfade rejected");

    // 10. Operator incompatible with parameter type (Bool with Greater operator)
    badController = controller;
    badController.Transitions[0].AllConditions[0].Parameter = "param_grounded";
    badController.Transitions[0].AllConditions[0].Operator = Axle::ConditionOperator::Greater;
    errors.clear();
    ANIMATION_CHECK(!badController.Validate(&errors), "Incompatible operator for Bool parameter rejected");

    // 11. Clip Definition validation
    Axle::AnimationClipDefinition clip;
    clip.Id = "clip_run";
    clip.Name = "Run";
    clip.Duration = 1.2f;
    clip.FrameRate = 30.0f;

    Axle::AnimationEventDefinition ev;
    ev.Id = "ev_footstep";
    ev.Name = "Footstep";
    ev.Time = 0.3f;
    clip.Events.push_back(ev);

    errors.clear();
    ANIMATION_CHECK(clip.Validate(&errors), "Valid AnimationClipDefinition passes");

    clip.Duration = -1.0f;
    errors.clear();
    ANIMATION_CHECK(!clip.Validate(&errors), "Negative clip duration rejected");
    clip.Duration = 1.2f;

    clip.Events[0].Time = 2.0f; // Exceeds duration (1.2f)
    errors.clear();
    ANIMATION_CHECK(!clip.Validate(&errors), "Event time exceeding clip duration rejected");

    // 12. Skeleton Definition and signature
    Axle::SkeletonDefinition skelDef;
    skelDef.Id = "skel_hero";
    skelDef.Name = "HeroSkeleton";
    Axle::Bone bRoot;
    bRoot.Name = "Root";
    bRoot.ParentID = -1;
    skelDef.Bones.push_back(bRoot);
    Axle::Bone bSpine;
    bSpine.Name = "Spine";
    bSpine.ParentID = 0;
    skelDef.Bones.push_back(bSpine);

    errors.clear();
    ANIMATION_CHECK(skelDef.Validate(&errors), "Valid SkeletonDefinition passes");
    std::string sig1 = Axle::SkeletonDefinition::ComputeSignature(skelDef.Bones);
    std::string sig2 = Axle::SkeletonDefinition::ComputeSignature(skelDef.Bones);
    ANIMATION_CHECK(!sig1.empty() && sig1 == sig2, "Skeleton signature is deterministic");

    return true;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-failure")
        return Axle::AnimationTests::Check(false, "Intentional harness failure", __FILE__, __LINE__) ? 0 : 1;
    std::cout << "Animation unit tests: device-free CPU evaluation\n";
    if (!TestBaselineSampling()) return 1;
    if (!TestChildBeforeParentTopologicalOrder()) return 1;
    if (!TestMultiRootSkeleton()) return 1;
    if (!TestSkeletonCycleDetection()) return 1;
    if (!TestRotationOnlyTrackPreservesBindPose()) return 1;
    if (!TestAntipodalQuaternionInterpolation()) return 1;
    if (!TestSignedScaleDecomposition()) return 1;
    if (!TestPoseValidationAndBlendMismatchedRejection()) return 1;
    if (!TestCPUSkeletalMeshConstructionWithoutGLContext()) return 1;
    if (!TestVertexInfluencePruningAndMerging()) return 1;
    if (!TestProceduralBipedImportAndCPUNormalization()) return 1;
    if (!TestAnimatorInstanceIsolationAndReset()) return 1;
    if (!TestAssetDSLAnimationKindsAndTypes()) return 1;
    if (!TestAnimationDefinitionsAndValidation()) return 1;
    std::cout << "Animation unit tests PASS\n";
    return 0;
}
