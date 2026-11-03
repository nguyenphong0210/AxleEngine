#include "AnimationTestHarness.hpp"
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/AnimationStateMachine.hpp>
#include <Axle/Animator.hpp>
#include <Axle/AnimationDefinition.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>
#include <limits>
#include <filesystem>

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

static bool TestAnimationCodecRoundTripAndAtomicSave() {
    std::filesystem::path testDir = std::filesystem::current_path() / "test_tmp_anim";
    std::error_code ec;
    std::filesystem::create_directories(testDir, ec);

    std::string skelFile = (testDir / "test.skeleton").string();
    std::string ctrlFile = (testDir / "test.animcontroller").string();
    std::string clipFile = (testDir / "test.animclip").string();

    // 1. Skeleton Round Trip
    Axle::SkeletonDefinition skelDef;
    skelDef.Id = "skel_001";
    skelDef.Name = "TestRig";
    Axle::Bone r;
    r.Name = "Root";
    r.ParentID = -1;
    skelDef.Bones.push_back(r);
    Axle::Bone c;
    c.Name = "Spine";
    c.ParentID = 0;
    c.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0));
    skelDef.Bones.push_back(c);

    std::string err;
    ANIMATION_CHECK(Axle::AnimationAssetCodec::SaveSkeleton(skelFile, skelDef, &err), "SaveSkeleton succeeds");
    Axle::SkeletonDefinition loadedSkel;
    ANIMATION_CHECK(Axle::AnimationAssetCodec::LoadSkeleton(skelFile, loadedSkel, &err), "LoadSkeleton succeeds");
    ANIMATION_CHECK(loadedSkel.Id == skelDef.Id, "Skeleton ID matches");
    ANIMATION_CHECK(loadedSkel.Name == skelDef.Name, "Skeleton Name matches");
    ANIMATION_CHECK(loadedSkel.Bones.size() == 2, "Loaded skeleton has 2 bones");
    ANIMATION_CHECK(loadedSkel.Bones[0].Name == "Root", "Bone 0 Name matches");
    ANIMATION_CHECK(loadedSkel.Bones[1].Name == "Spine", "Bone 1 Name matches");
    ANIMATION_CHECK(loadedSkel.Bones[1].ParentID == 0, "Bone 1 ParentID matches");

    // 2. AnimatorController Round Trip
    Axle::AnimatorControllerDefinition ctrlDef;
    ctrlDef.Id = "ctrl_001";
    ctrlDef.Name = "PlayerLocomotion";
    ctrlDef.SkeletonRef = "test.skeleton";
    ctrlDef.EntryStateId = "st_idle";

    Axle::ParameterDefinition pFloat;
    pFloat.Id = "p_spd";
    pFloat.Name = "Speed";
    pFloat.Type = Axle::AnimatorParameterType::Float;
    pFloat.Default = 1.5f;
    ctrlDef.Parameters.push_back(pFloat);

    Axle::ParameterDefinition pInt;
    pInt.Id = "p_mode";
    pInt.Name = "Mode";
    pInt.Type = Axle::AnimatorParameterType::Int;
    pInt.Default = 3;
    ctrlDef.Parameters.push_back(pInt);

    Axle::StateDefinition stIdle;
    stIdle.Id = "st_idle";
    stIdle.Name = "Idle";
    stIdle.NodeType = Axle::AnimationNodeType::Clip;
    stIdle.MotionAssetRef = "idle.animclip";
    ctrlDef.States.push_back(stIdle);

    Axle::StateDefinition stWalk;
    stWalk.Id = "st_walk";
    stWalk.Name = "Walk";
    stWalk.NodeType = Axle::AnimationNodeType::Clip;
    stWalk.MotionAssetRef = "walk.animclip";
    ctrlDef.States.push_back(stWalk);

    Axle::TransitionDefinition tr;
    tr.Id = "tr_001";
    tr.FromState = "st_idle";
    tr.ToState = "st_walk";
    tr.CrossfadeSeconds = 0.3f;
    Axle::ConditionDefinition c1;
    c1.Parameter = "p_spd";
    c1.Operator = Axle::ConditionOperator::Greater;
    c1.Operand = 0.5f;
    tr.AllConditions.push_back(c1);
    ctrlDef.Transitions.push_back(tr);

    ANIMATION_CHECK(Axle::AnimationAssetCodec::SaveAnimatorController(ctrlFile, ctrlDef, &err), "SaveAnimatorController succeeds");
    Axle::AnimatorControllerDefinition loadedCtrl;
    ANIMATION_CHECK(Axle::AnimationAssetCodec::LoadAnimatorController(ctrlFile, loadedCtrl, &err), "LoadAnimatorController succeeds");
    ANIMATION_CHECK(loadedCtrl.Id == ctrlDef.Id, "Controller Id matches");
    ANIMATION_CHECK(loadedCtrl.Name == ctrlDef.Name, "Controller Name matches");
    ANIMATION_CHECK(loadedCtrl.EntryStateId == "st_idle", "Controller EntryState matches");
    ANIMATION_CHECK(loadedCtrl.Parameters.size() == 2, "Loaded controller has 2 parameters");
    ANIMATION_CHECK(loadedCtrl.States.size() == 2, "Loaded controller has 2 states");
    ANIMATION_CHECK(loadedCtrl.Transitions.size() == 1, "Loaded controller has 1 transition");
    ANIMATION_CHECK(loadedCtrl.Transitions[0].CrossfadeSeconds == 0.3f, "Loaded transition crossfade matches");
    ANIMATION_CHECK(loadedCtrl.Transitions[0].AllConditions.size() == 1, "Loaded transition condition count matches");

    // 3. AnimationClip Round Trip
    Axle::AnimationClipDefinition clipDef;
    clipDef.Id = "clip_001";
    clipDef.Name = "WalkForward";
    clipDef.Duration = 1.0f;
    clipDef.FrameRate = 60.0f;
    Axle::AnimationChannel ch;
    ch.BoneName = "Spine";
    Axle::Keyframe<glm::vec3> pk0{0.0f, glm::vec3(0.0f)};
    Axle::Keyframe<glm::vec3> pk1{1.0f, glm::vec3(0.0f, 2.0f, 0.0f)};
    ch.Positions = {pk0, pk1};
    clipDef.Channels.push_back(ch);
    Axle::AnimationEventDefinition ev;
    ev.Id = "ev_step";
    ev.Name = "StepSound";
    ev.Time = 0.5f;
    clipDef.Events.push_back(ev);

    ANIMATION_CHECK(Axle::AnimationAssetCodec::SaveAnimationClip(clipFile, clipDef, &err), "SaveAnimationClip succeeds");
    Axle::AnimationClipDefinition loadedClip;
    ANIMATION_CHECK(Axle::AnimationAssetCodec::LoadAnimationClip(clipFile, loadedClip, &err), "LoadAnimationClip succeeds");
    ANIMATION_CHECK(loadedClip.Id == clipDef.Id, "Clip Id matches");
    ANIMATION_CHECK(loadedClip.Duration == 1.0f, "Clip duration matches");
    ANIMATION_CHECK(loadedClip.Channels.size() == 1, "Clip channel count matches");
    ANIMATION_CHECK(loadedClip.Channels[0].Positions.size() == 2, "Clip channel keyframe count matches");
    ANIMATION_CHECK(loadedClip.Events.size() == 1, "Clip event count matches");

    // 4. Atomic Save Failure Preserves Old File
    std::string canaryFile = (testDir / "canary.txt").string();
    std::string originalGoodContent = "GOOD_ORIGINAL_PAYLOAD";
    ANIMATION_CHECK(Axle::AnimationAssetCodec::WriteTextFileAtomically(canaryFile, originalGoodContent), "Write initial canary file");

    // Attempt to write atomically with empty path -> failure
    bool failedSave = Axle::AnimationAssetCodec::WriteTextFileAtomically("", "CORRUPT_PAYLOAD", &err);
    ANIMATION_CHECK(!failedSave, "Write with empty filepath correctly fails");
    ANIMATION_CHECK(Axle::FileSystem::ReadTextFile(canaryFile) == originalGoodContent, "Canary file preserved unchanged on failure");

    // Clean up temporary files
    std::filesystem::remove_all(testDir, ec);
    return true;
}

static bool TestCookedGeometryBakeAndTruncatedBinaryRejection() {
    std::filesystem::path testDir = std::filesystem::current_path() / "test_tmp_geom";
    std::error_code ec;
    std::filesystem::create_directories(testDir, ec);

    std::string binFile = (testDir / "mesh.bin").string();

    Axle::SkeletalMeshData meshData;
    meshData.Name = "TestMesh";
    Axle::SkeletalVertex v0;
    v0.Position = {0.0f, 0.0f, 0.0f};
    v0.Normal = {0.0f, 1.0f, 0.0f};
    v0.AddBoneData(0, 0.7f);
    v0.AddBoneData(1, 0.3f);
    v0.NormalizeWeights(0);

    Axle::SkeletalVertex v1;
    v1.Position = {1.0f, 0.0f, 0.0f};
    v1.Normal = {0.0f, 1.0f, 0.0f};
    v1.AddBoneData(0, 1.0f);
    v1.NormalizeWeights(0);

    Axle::SkeletalVertex v2;
    v2.Position = {0.0f, 1.0f, 0.0f};
    v2.Normal = {0.0f, 1.0f, 0.0f};
    v2.AddBoneData(1, 1.0f);
    v2.NormalizeWeights(0);

    meshData.Vertices = {v0, v1, v2};
    meshData.Indices = {0, 1, 2};

    std::string err;
    std::string signature = "SIG_SKELETON_XYZ_123";
    ANIMATION_CHECK(Axle::AnimationAssetCodec::BakeSkeletalMeshGeometry(binFile, meshData, signature, &err), "BakeSkeletalMeshGeometry succeeds");

    Axle::SkeletalMeshData loadedData;
    std::string loadedSig;
    ANIMATION_CHECK(Axle::AnimationAssetCodec::LoadSkeletalMeshGeometry(binFile, loadedData, &loadedSig, &err), "LoadSkeletalMeshGeometry succeeds");
    ANIMATION_CHECK(loadedSig == signature, "Baked geometry signature matches");
    ANIMATION_CHECK(loadedData.Vertices.size() == 3, "Loaded geometry has 3 vertices");
    ANIMATION_CHECK(loadedData.Indices.size() == 3, "Loaded geometry has 3 indices");
    ANIMATION_CHECK(Axle::AnimationTests::Near(loadedData.Vertices[0].Weights[0], 0.7f), "Bone weight 0 preserved");
    ANIMATION_CHECK(Axle::AnimationTests::Near(loadedData.Vertices[0].Weights[1], 0.3f), "Bone weight 1 preserved");

    // Test truncated binary rejection
    std::string truncFile = (testDir / "truncated.bin").string();
    std::vector<uint8_t> validBytes = Axle::FileSystem::ReadBinaryFile(binFile);
    ANIMATION_CHECK(!validBytes.empty(), "Valid bytes read back");

    // Truncate to 10 bytes (smaller than header)
    std::vector<uint8_t> smallBytes(validBytes.begin(), validBytes.begin() + 10);
    Axle::AnimationAssetCodec::WriteBinaryFileAtomically(truncFile, smallBytes.data(), smallBytes.size());
    Axle::SkeletalMeshData badData;
    ANIMATION_CHECK(!Axle::AnimationAssetCodec::LoadSkeletalMeshGeometry(truncFile, badData, nullptr, &err), "Truncated header rejected");

    // Truncate halfway through vertex payload
    std::vector<uint8_t> partialBytes(validBytes.begin(), validBytes.begin() + validBytes.size() - 20);
    Axle::AnimationAssetCodec::WriteBinaryFileAtomically(truncFile, partialBytes.data(), partialBytes.size());
    ANIMATION_CHECK(!Axle::AnimationAssetCodec::LoadSkeletalMeshGeometry(truncFile, badData, nullptr, &err), "Truncated vertex payload rejected");

    // Clean up
    std::filesystem::remove_all(testDir, ec);
    return true;
}

static bool TestTypedAnimatorParametersAndPureConditionEvaluator() {
    Axle::AnimationStateMachine sm("TestSM");

    // 1. Trigger Peek without consumption vs CheckAndConsumeTrigger
    sm.SetTrigger("Jump");
    ANIMATION_CHECK(sm.PeekTrigger("Jump"), "Trigger Jump peek returns true");
    ANIMATION_CHECK(sm.PeekTrigger("Jump"), "Second trigger Jump peek still returns true (not consumed)");
    ANIMATION_CHECK(sm.CheckAndConsumeTrigger("Jump"), "CheckAndConsumeTrigger consumes trigger");
    ANIMATION_CHECK(!sm.PeekTrigger("Jump"), "PeekTrigger after consume returns false");
    ANIMATION_CHECK(!sm.CheckAndConsumeTrigger("Jump"), "Second CheckAndConsumeTrigger returns false");

    // 2. Stable-ID rename preserving condition evaluation
    Axle::ParameterDefinition pSpeed;
    pSpeed.Id = "param_speed_id";
    pSpeed.Name = "Speed";
    pSpeed.Type = Axle::AnimatorParameterType::Float;
    pSpeed.Default = 0.0f;
    sm.DeclareParameter(pSpeed);

    sm.SetFloat("Speed", 3.5f);
    ANIMATION_CHECK(Axle::AnimationTests::Near(sm.GetFloat("param_speed_id"), 3.5f), "SetFloat by Name updates stable ID");
    ANIMATION_CHECK(Axle::AnimationTests::Near(sm.GetFloat("Speed"), 3.5f), "GetFloat by Name returns 3.5");

    // Rename parameter from "Speed" to "ForwardSpeed"
    ANIMATION_CHECK(sm.RenameParameter("Speed", "ForwardSpeed"), "RenameParameter succeeds");
    ANIMATION_CHECK(Axle::AnimationTests::Near(sm.GetFloat("ForwardSpeed"), 3.5f), "GetFloat by new name returns 3.5");

    // Setting by new name updates both
    sm.SetFloat("ForwardSpeed", 5.0f);
    ANIMATION_CHECK(Axle::AnimationTests::Near(sm.GetFloat("param_speed_id"), 5.0f), "Stable ID updated via new parameter name");

    // Condition bound to stable ID evaluates correctly regardless of rename!
    Axle::ConditionDefinition condSpeed;
    condSpeed.Parameter = "param_speed_id";
    condSpeed.Operator = Axle::ConditionOperator::Greater;
    condSpeed.Operand = 4.0f;
    ANIMATION_CHECK(Axle::AnimationStateMachine::EvaluateCondition(condSpeed, sm), "Condition bound to stable ID evaluates true after rename");

    // 3. Type mismatch validation
    ANIMATION_CHECK(sm.ValidateParameterType("param_speed_id", Axle::AnimatorParameterType::Float), "Type validation Float matches");
    ANIMATION_CHECK(!sm.ValidateParameterType("param_speed_id", Axle::AnimatorParameterType::Int), "Type validation Int rejected for Float param");
    ANIMATION_CHECK(!sm.ValidateParameterType("param_speed_id", Axle::AnimatorParameterType::Bool), "Type validation Bool rejected for Float param");

    // 4. Pure condition evaluation across types
    Axle::ParameterDefinition pStance;
    pStance.Id = "param_stance_id";
    pStance.Name = "Stance";
    pStance.Type = Axle::AnimatorParameterType::Int;
    pStance.Default = 1;
    sm.DeclareParameter(pStance);
    sm.SetInt("Stance", 2);

    Axle::ConditionDefinition condIntEq;
    condIntEq.Parameter = "param_stance_id";
    condIntEq.Operator = Axle::ConditionOperator::Equal;
    condIntEq.Operand = 2;
    ANIMATION_CHECK(Axle::AnimationStateMachine::EvaluateCondition(condIntEq, sm), "Int Equal condition evaluates true");

    Axle::ConditionDefinition condIntNeq;
    condIntNeq.Parameter = "param_stance_id";
    condIntNeq.Operator = Axle::ConditionOperator::NotEqual;
    condIntNeq.Operand = 1;
    ANIMATION_CHECK(Axle::AnimationStateMachine::EvaluateCondition(condIntNeq, sm), "Int NotEqual condition evaluates true");

    Axle::ParameterDefinition pGrounded;
    pGrounded.Id = "param_grounded_id";
    pGrounded.Name = "IsGrounded";
    pGrounded.Type = Axle::AnimatorParameterType::Bool;
    pGrounded.Default = true;
    sm.DeclareParameter(pGrounded);

    Axle::ConditionDefinition condBool;
    condBool.Parameter = "param_grounded_id";
    condBool.Operator = Axle::ConditionOperator::IsTrue;
    ANIMATION_CHECK(Axle::AnimationStateMachine::EvaluateCondition(condBool, sm), "Bool IsTrue condition evaluates true");
    sm.SetBool("IsGrounded", false);
    ANIMATION_CHECK(!Axle::AnimationStateMachine::EvaluateCondition(condBool, sm), "Bool IsTrue condition evaluates false when grounded false");

    // Trigger condition evaluation
    Axle::ConditionDefinition condTrig;
    condTrig.Parameter = "Attack";
    condTrig.Operator = Axle::ConditionOperator::Triggered;
    ANIMATION_CHECK(!Axle::AnimationStateMachine::EvaluateCondition(condTrig, sm), "Trigger condition false when not set");
    sm.SetTrigger("Attack");
    ANIMATION_CHECK(Axle::AnimationStateMachine::EvaluateCondition(condTrig, sm), "Trigger condition true when set (pure evaluate peeks)");
    ANIMATION_CHECK(sm.PeekTrigger("Attack"), "Trigger still set after pure evaluateCondition (not consumed)");

    // 5. Legacy Speed/IsGrounded compatibility
    sm.SetFloat("Speed", 2.2f);
    ANIMATION_CHECK(Axle::AnimationTests::Near(sm.GetFloat("Speed"), 2.2f), "Legacy Speed get/set compatible");
    sm.SetBool("IsGrounded", true);
    ANIMATION_CHECK(sm.GetBool("IsGrounded"), "Legacy IsGrounded get/set compatible");

    return true;
}

static bool TestDeterministicTransitionsAndCrossfades() {
    auto skeleton = std::make_shared<Axle::Skeleton>("Rig");
    Axle::Bone root;
    root.Name = "Root";
    root.ParentID = -1;
    skeleton->AddBone(root);
    skeleton->ComputeInverseBindMatrices();

    auto clip1s = std::make_shared<Axle::AnimationClip>("Clip1s", 1.0f);
    auto clip2s = std::make_shared<Axle::AnimationClip>("Clip2s", 2.0f);

    // 1. Priority Selection and Atomic Trigger Consumption
    Axle::AnimationStateMachine sm("PrioritySM");

    Axle::AnimationState stateA;
    stateA.Name = "StateA";
    stateA.Type = Axle::AnimationNodeType::Clip;
    stateA.Clip = clip1s;
    sm.AddState(stateA);

    Axle::AnimationState stateLow;
    stateLow.Name = "StateLow";
    stateLow.Type = Axle::AnimationNodeType::Clip;
    stateLow.Clip = clip1s;
    sm.AddState(stateLow);

    Axle::AnimationState stateHigh;
    stateHigh.Name = "StateHigh";
    stateHigh.Type = Axle::AnimationNodeType::Clip;
    stateHigh.Clip = clip1s;
    sm.AddState(stateHigh);

    // Transition Low: Priority = 0, requires Trigger "Action"
    Axle::AnimationTransition tLow;
    tLow.FromState = "StateA";
    tLow.ToState = "StateLow";
    tLow.Priority = 0;
    tLow.Duration = 0.1f;
    tLow.Rules.push_back({"Action", Axle::TransitionConditionOperator::Triggered});
    sm.AddTransition(tLow);

    // Transition High: Priority = 10, requires "Check" > 5.0f AND Trigger "Action"
    Axle::AnimationTransition tHigh;
    tHigh.FromState = "StateA";
    tHigh.ToState = "StateHigh";
    tHigh.Priority = 10;
    tHigh.Duration = 0.1f;
    tHigh.Rules.push_back({"Check", Axle::TransitionConditionOperator::Greater, 5.0f});
    tHigh.Rules.push_back({"Action", Axle::TransitionConditionOperator::Triggered});
    sm.AddTransition(tHigh);

    // Case 1A: Check = 2.0f (tHigh condition fails). Action trigger set.
    sm.SetFloat("Check", 2.0f);
    sm.SetTrigger("Action");
    sm.Update(0.01f, *skeleton);
    // tHigh failed -> tLow was selected!
    ANIMATION_CHECK(sm.IsTransitioning(), "Transition active");
    ANIMATION_CHECK(sm.GetCurrentStateName() == "StateA", "Still transitioning from StateA");
    sm.Update(0.15f, *skeleton);
    ANIMATION_CHECK(sm.GetCurrentStateName() == "StateLow", "StateLow selected when tHigh condition not met");
    ANIMATION_CHECK(!sm.PeekTrigger("Action"), "Action trigger consumed atomically by winning transition");

    // Case 1B: Reset to StateA. Check = 8.0f (both tHigh and tLow conditions met!).
    // tHigh (Priority 10) MUST win over tLow (Priority 0).
    sm.Reset();
    ANIMATION_CHECK(sm.GetCurrentStateName() == "StateA", "Reset returns to StateA");
    sm.SetFloat("Check", 8.0f);
    sm.SetTrigger("Action");
    sm.Update(0.01f, *skeleton);
    sm.Update(0.15f, *skeleton);
    ANIMATION_CHECK(sm.GetCurrentStateName() == "StateHigh", "Deterministic priority: StateHigh (Priority 10) won over StateLow (Priority 0)");

    // 2. Zero-duration transition and residual dt carry-over
    Axle::AnimationStateMachine smZero("ZeroDurationSM");
    Axle::AnimationState zA;
    zA.Name = "ZA";
    zA.Type = Axle::AnimationNodeType::Clip;
    zA.Clip = clip1s;
    smZero.AddState(zA);

    Axle::AnimationState zB;
    zB.Name = "ZB";
    zB.Type = Axle::AnimationNodeType::Clip;
    zB.Clip = clip2s;
    zB.SpeedMultiplier = 1.0f;
    smZero.AddState(zB);

    Axle::AnimationTransition tZero;
    tZero.FromState = "ZA";
    tZero.ToState = "ZB";
    tZero.Duration = 0.0f; // Zero duration!
    tZero.Rules.push_back({"Go", Axle::TransitionConditionOperator::Triggered});
    smZero.AddTransition(tZero);

    smZero.SetTrigger("Go");
    // Update with dt = 0.4f
    smZero.Update(0.4f, *skeleton);
    ANIMATION_CHECK(smZero.GetCurrentStateName() == "ZB", "Zero-duration transition completes immediately in same frame");
    ANIMATION_CHECK(!smZero.IsTransitioning(), "Not transitioning after zero-duration switch");

    // 3. Exit-time boundary crossing
    Axle::AnimationStateMachine smExit("ExitSM");
    Axle::AnimationState eA;
    eA.Name = "EA";
    eA.Type = Axle::AnimationNodeType::Clip;
    eA.Clip = clip1s; // 1 second duration
    eA.Loop = false;
    smExit.AddState(eA);

    Axle::AnimationState eB;
    eB.Name = "EB";
    eB.Type = Axle::AnimationNodeType::Clip;
    eB.Clip = clip1s;
    smExit.AddState(eB);

    Axle::AnimationTransition tExit;
    tExit.FromState = "EA";
    tExit.ToState = "EB";
    tExit.HasExitTime = true;
    tExit.ExitTime = 0.7f;
    tExit.Duration = 0.2f;
    smExit.AddTransition(tExit);

    // At t=0.3s (normalizedTime 0.3 < 0.7): transition should NOT trigger
    smExit.Update(0.3f, *skeleton);
    ANIMATION_CHECK(smExit.GetCurrentStateName() == "EA" && !smExit.IsTransitioning(), "Exit-time gate prevents transition before 0.7");

    // Advance to t=0.8s: transition triggers!
    smExit.Update(0.5f, *skeleton);
    ANIMATION_CHECK(smExit.IsTransitioning(), "Exit-time gate crossed: transition active");
    smExit.Update(0.15f, *skeleton);
    ANIMATION_CHECK(smExit.GetCurrentStateName() == "EB", "Transitioned to EB after exit time crossing");

    // 4. Large dt crossfade completion without overflow
    smExit.Reset();
    smExit.Update(0.0f, *skeleton);
    ANIMATION_CHECK(smExit.GetCurrentStateName() == "EA", "Reset back to EA");
    // Advance with large dt = 5.0f
    smExit.Update(5.0f, *skeleton);
    ANIMATION_CHECK(smExit.GetCurrentStateName() == "EB", "Large dt cleanly completes transition and advances target state");
    ANIMATION_CHECK(!smExit.IsTransitioning(), "Not transitioning after large dt completion");

    // 5. dt=0 does not consume triggers or advance
    smExit.Reset();
    smExit.SetTrigger("Dummy");
    smExit.Update(0.0f, *skeleton);
    ANIMATION_CHECK(smExit.PeekTrigger("Dummy"), "dt=0 does not consume triggers");
    ANIMATION_CHECK(smExit.GetCurrentStateName() == "EA", "dt=0 does not change state");

    // Negative dt rejected
    smExit.Update(-1.0f, *skeleton);
    ANIMATION_CHECK(smExit.GetCurrentStateName() == "EA", "Negative dt rejected");

    return true;
}

static bool TestSceneAndAssemblyAnimationRoundTrip() {
    auto tempDir = std::filesystem::temp_directory_path() / ("axle_anim_b3_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir);

    // 1. Setup authored Skeleton and Controller
    Axle::SkeletalMeshData bipedData;
    std::shared_ptr<Axle::Skeleton> skeleton;
    std::vector<std::shared_ptr<Axle::AnimationClip>> bipedClips;
    ANIMATION_CHECK(Axle::SkeletalMeshImporter::CreateProceduralBiped(bipedData, skeleton, bipedClips), "B3 fixture uses the referenced rig");

    auto sm = std::make_shared<Axle::AnimationStateMachine>("LocomotionSM");
    Axle::ParameterDefinition speedParam;
    speedParam.Id = "Param_Speed";
    speedParam.Name = "Speed";
    speedParam.Type = Axle::AnimatorParameterType::Float;
    speedParam.Default = 0.0f;
    sm->DeclareParameter(speedParam);

    Axle::ParameterDefinition jumpParam;
    jumpParam.Id = "Param_Jump";
    jumpParam.Name = "JumpCount";
    jumpParam.Type = Axle::AnimatorParameterType::Int;
    jumpParam.Default = 0;
    sm->DeclareParameter(jumpParam);

    Axle::ParameterDefinition groundedParam;
    groundedParam.Id = "Param_Grounded";
    groundedParam.Name = "IsGrounded";
    groundedParam.Type = Axle::AnimatorParameterType::Bool;
    groundedParam.Default = true;
    sm->DeclareParameter(groundedParam);

    auto clip1 = std::make_shared<Axle::AnimationClip>("ClipIdle", 1.0f);
    auto clip2 = std::make_shared<Axle::AnimationClip>("ClipWalk", 1.0f);

    Axle::AnimationState stIdle;
    stIdle.Name = "Idle";
    stIdle.Clip = clip1;
    sm->AddState(stIdle);

    Axle::AnimationState stWalk;
    stWalk.Name = "Walk";
    stWalk.Clip = clip2;
    sm->AddState(stWalk);

    Axle::AnimationTransition trans;
    trans.FromState = "Idle";
    trans.ToState = "Walk";
    trans.Duration = 0.2f;
    trans.Rules.push_back({"Speed", Axle::TransitionConditionOperator::Greater, 0.5f});
    sm->AddTransition(trans);

    auto animator = std::make_shared<Axle::Animator>(skeleton);
    animator->SetStateMachine(sm);

    // 2. Scene round-trip test
    auto sceneA = std::make_shared<Axle::Scene>();
    auto entityA = sceneA->CreateEntity("AnimatedEntity");
    auto& smcA = entityA.AddComponent<Axle::SkeletalMeshComponent>();
    smcA.AssetPath = "procedural://biped";
    smcA.PatternMode = 2;
    smcA.TileScale = 3.5f;

    auto& acA = entityA.AddComponent<Axle::AnimatorComponent>();
    acA.AnimatorInstance = animator;
    acA.Playing = true;
    acA.PlaybackSpeed = 1.25f;
    acA.SetFloat("Speed", 2.5f);
    acA.SetInt("JumpCount", 7);
    acA.SetBool("IsGrounded", false);

    std::filesystem::path scenePath = tempDir / "TestScene.axle";
    Axle::SceneSerializer serializer(sceneA.get());
    bool savedScene = serializer.Serialize(scenePath);
    ANIMATION_CHECK(savedScene, "Scene serialized successfully with skeletal components");

    // Deserialize scene into fresh Scene instance
    auto sceneB = std::make_shared<Axle::Scene>();
    Axle::SceneSerializer deserializer(sceneB.get());
    bool loadedScene = deserializer.Deserialize(scenePath);
    ANIMATION_CHECK(loadedScene, "Scene deserialized successfully");

    // Verify deserialized components
    auto viewB = sceneB->GetRegistry().view<Axle::SkeletalMeshComponent, Axle::AnimatorComponent>();
    size_t countB = 0;
    for (auto ent : viewB) {
        countB++;
        auto& smcB = viewB.get<Axle::SkeletalMeshComponent>(ent);
        auto& acB = viewB.get<Axle::AnimatorComponent>(ent);

        ANIMATION_CHECK(smcB.AssetPath == "procedural://biped", "SkeletalMesh AssetPath preserved");
        ANIMATION_CHECK(smcB.PatternMode == 2, "SkeletalMesh PatternMode preserved");
        ANIMATION_CHECK(std::abs(smcB.TileScale - 3.5f) < 1e-4f, "SkeletalMesh TileScale preserved");

        ANIMATION_CHECK(acB.Playing == true, "Animator Playing state preserved");
        ANIMATION_CHECK(std::abs(acB.PlaybackSpeed - 1.25f) < 1e-4f, "Animator PlaybackSpeed preserved");
        ANIMATION_CHECK(acB.AnimatorInstance != nullptr, "AnimatorInstance reconstructed from inline fallback");
        ANIMATION_CHECK(acB.GetFloat("Speed") == 2.5f, "Float parameter override restored");
        ANIMATION_CHECK(acB.GetInt("JumpCount") == 7, "Int parameter override restored");
        ANIMATION_CHECK(acB.GetBool("IsGrounded") == false, "Bool parameter override restored");
    }
    ANIMATION_CHECK(countB == 1, "Exactly one animated entity in deserialized scene");

    // 3. Fallback preserves values when external file missing
    auto sceneC = std::make_shared<Axle::Scene>();
    auto entityC = sceneC->CreateEntity("MissingFileEntity");
    auto& smcC = entityC.AddComponent<Axle::SkeletalMeshComponent>();
    smcC.AssetPath = "procedural://biped";
    auto& acC = entityC.AddComponent<Axle::AnimatorComponent>();
    acC.ControllerPath = "nonexistent/missing_controller.axleasset";
    // Embed inline fallback
    auto def = Axle::AnimationAssetCodec::ExtractDefinitionFromStateMachine(*sm);
    std::string textDef = Axle::AnimationAssetCodec::EncodeControllerToText(def);
    acC.InlineFallbackData = Axle::AnimationAssetCodec::HexEncode(textDef);
    acC.SetFloat("Speed", 4.0f);

    std::filesystem::path sceneMissingPath = tempDir / "SceneMissing.axle";
    Axle::SceneSerializer serC(sceneC.get());
    serC.Serialize(sceneMissingPath);

    auto sceneD = std::make_shared<Axle::Scene>();
    Axle::SceneSerializer deserD(sceneD.get());
    bool loadedMissing = deserD.Deserialize(sceneMissingPath);
    ANIMATION_CHECK(loadedMissing, "Scene deserialized when external controller is missing");
    auto viewD = sceneD->GetRegistry().view<Axle::AnimatorComponent>();
    for (auto ent : viewD) {
        auto& acD = viewD.get<Axle::AnimatorComponent>(ent);
        ANIMATION_CHECK(acD.AnimatorInstance != nullptr, "AnimatorInstance successfully created from fallback definition");
        ANIMATION_CHECK(acD.GetFloat("Speed") == 4.0f, "Parameter override preserved with missing external file");
    }

    // 4. Two scene instances deserialize independent runtime states
    auto sceneDouble = std::make_shared<Axle::Scene>();
    Axle::SceneSerializer deserDouble(sceneDouble.get());
    deserDouble.Deserialize(scenePath);

    auto viewOrig = sceneDouble->GetRegistry().view<Axle::AnimatorComponent>();
    Axle::Entity firstEntity;
    for (auto ent : viewOrig) {
        firstEntity = { ent, sceneDouble.get() };
        break;
    }
    Axle::Entity secondEntity = sceneDouble->DuplicateEntity(firstEntity);

    auto& ac1 = firstEntity.GetComponent<Axle::AnimatorComponent>();
    auto& ac2 = secondEntity.GetComponent<Axle::AnimatorComponent>();

    ANIMATION_CHECK(ac1.AnimatorInstance != ac2.AnimatorInstance, "Distinct AnimatorInstance pointers across instances");
    ANIMATION_CHECK(ac1.AnimatorInstance->GetStateMachine() != ac2.AnimatorInstance->GetStateMachine(), "Distinct state machine instances");

    // Advance instance 1, mutate parameter
    ac1.SetFloat("Speed", 99.0f);
    ac1.Update(0.1f);

    ANIMATION_CHECK(ac1.GetFloat("Speed") == 99.0f, "Instance 1 parameter mutated");
    ANIMATION_CHECK(ac2.GetFloat("Speed") == 2.5f, "Instance 2 parameter completely isolated from instance 1");

    // 5. Assembly serialization, LoadAssemblyData, and SaveAssemblyData round trip
    std::filesystem::path asmPath = tempDir / "TestAssembly.axle";
    bool savedAsm = Axle::AssemblySerializer::Serialize(asmPath, firstEntity);
    ANIMATION_CHECK(savedAsm, "AssemblySerializer serialized entity with skeletal components");

    Axle::AssemblyData asmData = Axle::AssemblySerializer::LoadAssemblyData(asmPath);
    ANIMATION_CHECK(asmData.IsValid, "LoadAssemblyData loaded valid assembly data");
    ANIMATION_CHECK(asmData.RootNode.HasSkeletalMesh, "AssemblyNode contains HasSkeletalMesh");
    ANIMATION_CHECK(asmData.RootNode.HasAnimator, "AssemblyNode contains HasAnimator");
    ANIMATION_CHECK(asmData.RootNode.Animator.GetFloat("Speed") == 99.0f, "AssemblyNode preserved Animator parameter override");

    // Save assembly data back out
    std::filesystem::path asmSavedPath = tempDir / "TestAssemblyResaved.axle";
    bool resavedAsm = Axle::AssemblySerializer::SaveAssemblyData(asmSavedPath, asmData);
    ANIMATION_CHECK(resavedAsm, "SaveAssemblyData wrote assembly data cleanly");

    // Deserialize resaved assembly into target scene
    auto sceneAsm = std::make_shared<Axle::Scene>();
    Axle::Entity spawnedAsm = Axle::AssemblySerializer::Deserialize(asmSavedPath, sceneAsm.get());
    ANIMATION_CHECK((bool)spawnedAsm, "Spawned entity from assembly is valid");
    ANIMATION_CHECK(spawnedAsm.HasComponent<Axle::SkeletalMeshComponent>(), "Spawned assembly entity has SkeletalMeshComponent");
    ANIMATION_CHECK(spawnedAsm.HasComponent<Axle::AnimatorComponent>(), "Spawned assembly entity has AnimatorComponent");
    auto& acSpawned = spawnedAsm.GetComponent<Axle::AnimatorComponent>();
    ANIMATION_CHECK(acSpawned.AnimatorInstance != nullptr, "Spawned assembly entity has active AnimatorInstance");

    // Clean up temporary files
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    return true;
}

bool RunAnimationCorrectionTests();

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-failure")
        return Axle::AnimationTests::Check(false, "Intentional harness failure", __FILE__, __LINE__) ? 0 : 1;
    std::cout << "Animation unit tests: device-free CPU evaluation\n";
    if (!RunAnimationCorrectionTests()) return 1;
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
    if (!TestAnimationCodecRoundTripAndAtomicSave()) return 1;
    if (!TestCookedGeometryBakeAndTruncatedBinaryRejection()) return 1;
    if (!TestTypedAnimatorParametersAndPureConditionEvaluator()) return 1;
    if (!TestDeterministicTransitionsAndCrossfades()) return 1;
    if (!TestSceneAndAssemblyAnimationRoundTrip()) return 1;
    std::cout << "Animation unit tests PASS\n";
    return 0;
}
