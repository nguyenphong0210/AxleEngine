#include "AnimationTestHarness.hpp"
#include <Axle/AnimationStateMachine.hpp>
#include <limits>
#include <Axle/Animator.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/AnimationDefinition.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <type_traits>

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

static bool TestImmutableSkeletalResourcePublication() {
    using namespace Axle;

    // 1. Compile-time API contracts
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const SkeletalMesh>().GetData())>>,
        "SkeletalMesh::GetData() must return const reference only");
    static_assert(std::is_same_v<decltype(std::declval<const SkeletalMesh>().GetSkeleton()), std::shared_ptr<const Skeleton>>,
        "SkeletalMesh::GetSkeleton() must return shared_ptr<const Skeleton>");
    static_assert(std::is_same_v<decltype(std::declval<SkeletalMesh>().GetSkeleton()), std::shared_ptr<const Skeleton>>,
        "Non-const SkeletalMesh::GetSkeleton() must still return shared_ptr<const Skeleton>");
    static_assert(std::is_same_v<decltype(std::declval<SkeletalMeshData>().SkeletonRef), std::shared_ptr<const Skeleton>>,
        "SkeletalMeshData::SkeletonRef must hold const Skeleton to prevent leaking mutable rig");
    static_assert(!std::is_copy_assignable_v<SkeletalMesh>, "SkeletalMesh must not be copy-assignable");
    static_assert(!std::is_move_assignable_v<SkeletalMesh>, "SkeletalMesh must not be move-assignable");
    static_assert(!std::is_copy_constructible_v<SkeletalMesh>, "SkeletalMesh must not be copy-constructible");
    static_assert(!std::is_move_constructible_v<SkeletalMesh>, "SkeletalMesh must not be move-constructible");
    static_assert(!std::is_default_constructible_v<SkeletalMesh>, "SkeletalMesh must not be default-constructible");

    static_assert(!std::is_constructible_v<SkeletalMesh, SkeletalMeshData>, "Draft construction must not bypass the validated factory");

    // 2. Draft & Builder alias detachment
    auto builderRig = std::make_shared<Skeleton>("BuilderRig");
    Bone rootBone; rootBone.ID = 0; rootBone.Name = "Root"; rootBone.LocalTransform = glm::mat4(1.0f);
    builderRig->AddBone(rootBone);
    Bone childBone; childBone.ID = 1; childBone.Name = "Child"; childBone.ParentID = 0;
    childBone.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    childBone.InverseBindMatrix = glm::inverse(childBone.LocalTransform);
    builderRig->AddBone(childBone);
    builderRig->RebuildDerivedData();

    SkeletalMeshData draft;
    draft.Name = "PublishedTriangle";
    draft.SkeletonRef = builderRig;
    draft.Vertices.resize(3);
    draft.Vertices[0].Position = glm::vec3(0.0f, 0.0f, 0.0f);
    draft.Vertices[1].Position = glm::vec3(1.0f, 0.0f, 0.0f);
    draft.Vertices[2].Position = glm::vec3(0.0f, 1.0f, 0.0f);
    draft.Indices = { 0, 1, 2 };
    for (size_t i = 0; i < draft.Vertices.size(); ++i) {
        draft.Vertices[i].BoneIDs = glm::ivec4(0, 1, -1, -1);
        draft.Vertices[i].Weights = glm::vec4(0.5f, 0.5f, 0.0f, 0.0f);
    }
    draft.Submeshes.push_back({ 0, 3, 0 });
    draft.MaterialSlots.push_back("DefaultMaterial");

    std::string diagnostic;
    auto publishedMesh = SkeletalMesh::Create(draft, &diagnostic);
    ANIMATION_CHECK(publishedMesh && diagnostic.empty(), "Valid draft publishes into immutable SkeletalMesh");
    ANIMATION_CHECK(publishedMesh->GetSkeleton() != nullptr, "Published mesh owns its skeleton");
    ANIMATION_CHECK(publishedMesh->GetSkeleton().get() != builderRig.get(),
        "Published mesh detached builder rig alias (owns deep copy)");

    const auto origSig = SkeletonDefinition::ComputeSignature(publishedMesh->GetSkeleton()->GetBones());
    const auto origV0 = publishedMesh->GetVertices()[0].Position;
    const auto origBoneCount = publishedMesh->GetBoneCount();
    const auto origRootTransform = publishedMesh->GetSkeleton()->GetBone(0)->LocalTransform;

    // Mutate draft vectors and builder rig alias
    draft.Vertices[0].Position = glm::vec3(999.0f, -888.0f, 777.0f);
    draft.Indices[0] = 9999;
    builderRig->SetName("CorruptedRig");
    Bone extraBone; extraBone.ID = 2; extraBone.Name = "Extra"; extraBone.ParentID = 0;
    builderRig->AddBone(extraBone);
    builderRig->GetBones()[0].LocalTransform = glm::scale(glm::mat4(1.0f), glm::vec3(50.0f));

    // Assert published resource remained completely unmodified
    ANIMATION_CHECK(publishedMesh->GetVertices()[0].Position == origV0, "Published vertex data unaffected by draft mutation");
    ANIMATION_CHECK(publishedMesh->GetIndices()[0] == 0, "Published index data unaffected by draft mutation");
    ANIMATION_CHECK(publishedMesh->GetSkeleton()->GetName() == "BuilderRig", "Published rig name unaffected by builder mutation");
    ANIMATION_CHECK(publishedMesh->GetBoneCount() == origBoneCount, "Published bone count unaffected by builder mutation");
    ANIMATION_CHECK(publishedMesh->GetSkeleton()->GetBone(0)->LocalTransform == origRootTransform, "Published bone transform unaffected by builder mutation");
    ANIMATION_CHECK(SkeletonDefinition::ComputeSignature(publishedMesh->GetSkeleton()->GetBones()) == origSig, "Published signature unaffected by builder mutation");

    // 3. Two entities sharing published mesh and independent Animator instances
    auto scene = std::make_shared<Scene>();
    auto entity1 = scene->CreateEntity("Entity1");
    auto entity2 = scene->CreateEntity("Entity2");

    auto& smc1 = entity1.AddComponent<SkeletalMeshComponent>(publishedMesh);
    auto& smc2 = entity2.AddComponent<SkeletalMeshComponent>(publishedMesh);
    ANIMATION_CHECK(smc1.Mesh == smc2.Mesh, "Entities share the same published mesh resource");

    auto makeController = [](const std::shared_ptr<const Skeleton>& rig) {
        auto sm = std::make_shared<AnimationStateMachine>("StateMachine");
        sm->DeclareParameter({ "speed-id", "Speed", AnimatorParameterType::Float, 0.0f });
        sm->DeclareParameter({ "jump-id", "Jump", AnimatorParameterType::Trigger, false });
        for (const auto* name : { "Idle", "Walk" }) {
            AnimationState state; state.Name = name; state.Clip = std::make_shared<AnimationClip>(name, 1.0f);
            sm->AddState(state);
        }
        sm->SetEntryState("Idle");
        auto anim = std::make_shared<Animator>(rig);
        anim->SetStateMachine(sm);
        anim->Update(0.0f);
        return anim;
    };

    auto& ac1 = entity1.AddComponent<AnimatorComponent>(makeController(publishedMesh->GetSkeleton()));
    auto& ac2 = entity2.AddComponent<AnimatorComponent>(makeController(publishedMesh->GetSkeleton()));

    ANIMATION_CHECK(ac1.AnimatorInstance != ac2.AnimatorInstance, "Entities have independent Animator instances");
    ANIMATION_CHECK(ac1.AnimatorInstance->GetSkeleton() != ac2.AnimatorInstance->GetSkeleton(),
        "Entities have independent Animator runtime skeletons");
    ANIMATION_CHECK(ac1.AnimatorInstance->GetSkeleton().get() != publishedMesh->GetSkeleton().get(),
        "Animator runtime skeleton is decoupled from published mesh skeleton");

    // Test parameter & trigger isolation
    ac1.AnimatorInstance->SetFloat("Speed", 5.0f);
    ac1.AnimatorInstance->SetTrigger("Jump");
    ANIMATION_CHECK(ac1.AnimatorInstance->GetFloat("Speed") == 5.0f && ac2.AnimatorInstance->GetFloat("Speed") == 0.0f,
        "Animator parameter writes are isolated between entities");
    ANIMATION_CHECK(ac1.AnimatorInstance->PeekTrigger("Jump") && !ac2.AnimatorInstance->PeekTrigger("Jump"),
        "Animator triggers are isolated between entities");

    // Advance clock on Entity 1
    ac1.AnimatorInstance->Update(0.35f);
    ANIMATION_CHECK(ac1.AnimatorInstance->GetStateMachine()->GetCurrentStateTime() > 0.3f &&
                    ac2.AnimatorInstance->GetStateMachine()->GetCurrentStateTime() == 0.0f,
        "Animator playback clocks are isolated between entities");

    // 4. Entity duplication & Scene::Copy isolation
    auto entity3 = scene->DuplicateEntity(entity1);
    ANIMATION_CHECK(entity3.HasComponent<SkeletalMeshComponent>() && entity3.HasComponent<AnimatorComponent>(),
        "Duplicated entity has both animation components");
    auto& ac3 = entity3.GetComponent<AnimatorComponent>();
    ANIMATION_CHECK(ac3.AnimatorInstance && ac3.AnimatorInstance != ac1.AnimatorInstance,
        "Duplicated entity receives independent cloned AnimatorInstance");
    ANIMATION_CHECK(ac3.AnimatorInstance->GetSkeleton() != ac1.AnimatorInstance->GetSkeleton(),
        "Duplicated entity receives independent cloned Skeleton");

    auto sceneCopy = Scene::Copy(scene);
    Entity copiedEnt1;
    sceneCopy->GetRegistry().view<TagComponent>().each([&](auto entity, auto& tag) {
        if (tag.Tag == "Entity1") copiedEnt1 = Entity(entity, sceneCopy.get());
    });
    ANIMATION_CHECK(copiedEnt1 && copiedEnt1.HasComponent<AnimatorComponent>(), "Scene::Copy preserves AnimatorComponent");
    auto& copiedAc = copiedEnt1.GetComponent<AnimatorComponent>();
    ANIMATION_CHECK(copiedAc.AnimatorInstance && copiedAc.AnimatorInstance != ac1.AnimatorInstance,
        "Scene::Copy creates independent AnimatorInstance");
    ANIMATION_CHECK(copiedAc.AnimatorInstance->GetSkeleton() != ac1.AnimatorInstance->GetSkeleton(),
        "Scene::Copy creates independent runtime Skeleton");

    // 5. Failed replacement keeps last-good resource and diagnostic; builder destruction safety
    SkeletalMeshData badDraft = draft;
    badDraft.Vertices[0].Position.x = std::numeric_limits<float>::quiet_NaN(); // Non-finite attribute
    std::string badDiag;
    auto badMesh = SkeletalMesh::Create(badDraft, &badDiag);
    ANIMATION_CHECK(!badMesh && !badDiag.empty(), "Invalid draft fails publication with diagnostic");
    ANIMATION_CHECK(!smc1.ReplaceMesh(badMesh, &badDiag), "ReplaceMesh rejects null/failed publication");
    ANIMATION_CHECK(smc1.Mesh == publishedMesh, "SkeletalMeshComponent retains last good published mesh");

    // Invalid published objects cannot be constructed; invalid drafts fail at publication.
    auto invalidDraft = publishedMesh->GetData();
    invalidDraft.Indices[1] = 99;
    std::string replaceDiag;
    auto invalidCandidate = SkeletalMesh::Create(invalidDraft, &replaceDiag);
    ANIMATION_CHECK(!invalidCandidate && !replaceDiag.empty(), "Invalid indices cannot bypass publication");
    ANIMATION_CHECK(!smc1.ReplaceMesh(invalidCandidate, &replaceDiag) && smc1.Mesh == publishedMesh,
                    "Failed publication preserves the live component");
    auto invalidRig = std::make_shared<Skeleton>(*publishedMesh->GetSkeleton());
    invalidRig->GetBones()[0].ParentID = 0;
    invalidDraft = publishedMesh->GetData(); invalidDraft.SkeletonRef = invalidRig;
    ANIMATION_CHECK(!SkeletalMesh::Create(invalidDraft, &replaceDiag), "Malformed rig fails before publication");
    invalidDraft = publishedMesh->GetData(); invalidDraft.Vertices[0].Normal.x = std::numeric_limits<float>::infinity();
    ANIMATION_CHECK(!SkeletalMesh::Create(invalidDraft, &replaceDiag), "Nonfinite attributes fail before publication");
    ANIMATION_CHECK(publishedMesh->GetBoundsMin() == glm::vec3(0) && publishedMesh->GetBoundsMax() == glm::vec3(1,1,0),
                    "Published bounds describe the validated vertices");

    // Destroy builder objects; verify live instance continues to evaluate cleanly
    builderRig.reset();
    draft.Vertices.clear();
    draft.Indices.clear();
    ac1.AnimatorInstance->Update(0.1f);
    ANIMATION_CHECK(ac1.AnimatorInstance->GetSkinningMatrices().size() == origBoneCount,
        "Live instances evaluate safely after builder destruction");

    return true;
}

bool RunAnimationAssetCorrectionTests();
bool RunAnimationImporterCorrectionTests();
bool RunAnimationControllerCorrectionTests();
bool RunAnimationSceneCorrectionTests();
bool RunAnimationPackageCorrectionTests();
bool RunAnimationBakeValidationTests();

bool RunAnimationCorrectionTests() {
    return TestCanonicalParameters() &&
           TestLoopGatesEntryAndClone() &&
           TestImmutableSkeletalResourcePublication() &&
           RunAnimationAssetCorrectionTests() &&
           RunAnimationImporterCorrectionTests() &&
           RunAnimationControllerCorrectionTests() &&
           RunAnimationSceneCorrectionTests() &&
           RunAnimationPackageCorrectionTests() && RunAnimationBakeValidationTests();
}
