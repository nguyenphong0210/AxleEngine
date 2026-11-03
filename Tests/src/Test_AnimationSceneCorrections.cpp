#include "AnimationTestHarness.hpp"
#include <Axle/AnimationSceneBinding.hpp>
#include <Axle/AnimationResources.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <chrono>

namespace {
struct Scratch {
    const std::filesystem::path Root = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const std::filesystem::path Path = Root /
        ("axle_animation_binding_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { std::filesystem::create_directories(Path); }
    ~Scratch() {
        std::error_code error;
        const auto resolved = std::filesystem::weakly_canonical(Path,error);
        if (!error && resolved.parent_path() == Root && resolved.filename().string().starts_with("axle_animation_binding_"))
            std::filesystem::remove_all(resolved,error);
    }
};
}

bool RunAnimationSceneCorrectionTests() {
    using namespace Axle;
    Scratch scratch;
    auto scene = std::make_shared<Scene>(); auto entity = scene->CreateEntity("KeyedTriangle");
    auto rig = std::make_shared<Skeleton>("CustomRig"); Bone bone; bone.Name = "Root"; rig->AddBone(bone);
    SkeletalMeshData data; data.Name = "Triangle"; data.SkeletonRef = rig; data.Vertices.resize(3);
    data.Vertices[1].Position.x = 1; data.Vertices[2].Position.y = 1; data.Indices = {0,1,2};
    for (auto& vertex : data.Vertices) { vertex.BoneIDs[0] = 0; vertex.Weights[0] = 1; }
    auto& mesh = entity.AddComponent<SkeletalMeshComponent>(); mesh.Mesh = SkeletalMesh::Create(data);
    mesh.AssetPath = "missing-source.gltf";
    auto machine = std::make_shared<AnimationStateMachine>("Keyed");
    machine->DeclareParameter({"speed-id", "Speed", AnimatorParameterType::Float, 0.0f});
    for (int i = 0; i < 2; ++i) {
        AnimationState state; state.Name = i ? "Walk" : "Idle"; state.Clip = std::make_shared<AnimationClip>(state.Name, 1);
        AnimationChannel channel; channel.BoneName = "Root";
        channel.Positions = {{0, glm::vec3(i*4,0,0)}, {1, glm::vec3(i*4+2,0,0)}};
        state.Clip->AddChannel(channel); machine->AddState(state);
    }
    AnimationTransition transition; transition.FromState = "Idle"; transition.ToState = "Walk"; transition.Duration = 0.1f;
    transition.Rules.push_back({"Speed", ConditionOperator::Greater, 0.5f}); machine->AddTransition(transition);
    auto& component = entity.AddComponent<AnimatorComponent>(); component.AnimatorInstance = std::make_shared<Animator>(rig);
    component.AnimatorInstance->SetStateMachine(machine); component.FloatOverrides["speed-id"] = 0.25f;
    component.ControllerPath = "missing-controller.axleasset";
    component.SetFloat("speed-id",0.3f);
    ANIMATION_CHECK(component.GetFloat("Speed") == 0.3f && component.Speed == 0.3f, "Component stable-ID writes update name reads and conventional cache");
    component.SetBool("speed-id",false);
    ANIMATION_CHECK(component.BoolOverrides.empty(), "Wrong-type component writes never enter serialized overrides");
    component.FloatOverrides["Speed"] = 0.75f;
    component.FloatOverrides["speed-id"] = 0.25f;
    component.Speed = 0;
    AnimatorComponent unbound; unbound.SetInt("Count",7);
    ANIMATION_CHECK(unbound.GetInt("Count") == 7, "Unbound authored typed overrides remain readable");
    SceneSerializer serializer(scene.get());
    const auto path = scratch.Path / "scene.axle";
    ANIMATION_CHECK(serializer.Serialize(path), "Custom keyed mesh/controller saves without source files");
    Scene reopened; SceneSerializer reader(&reopened);
    ANIMATION_CHECK(reader.Deserialize(path), "Self-contained scene reopens");
    auto view = reopened.GetRegistry().view<SkeletalMeshComponent, AnimatorComponent>();
    ANIMATION_CHECK(view.size_hint() == 1, "Exactly one keyed entity reopens");
    auto id = *view.begin(); auto& loadedMesh = view.get<SkeletalMeshComponent>(id); auto& loaded = view.get<AnimatorComponent>(id);
    ANIMATION_CHECK(loadedMesh.Mesh && loadedMesh.Mesh->GetData().Indices == data.Indices && loaded.AnimatorInstance, "Custom geometry and animator both resolve");
    ANIMATION_CHECK(AnimationTests::Near(loaded.AnimatorInstance->GetFloat("Speed"),0.25f), "Stable-ID override supersedes conventional Speed cache");
    component.AnimatorInstance->Update(0.25f); loaded.AnimatorInstance->Update(0.25f);
    ANIMATION_CHECK(AnimationTests::Near(component.AnimatorInstance->GetSkinningMatrices()[0], loaded.AnimatorInstance->GetSkinningMatrices()[0]), "Scene round trip preserves keyed skinning, not just metadata");
    loaded.AnimatorInstance->SetFloat("Speed", 1); loaded.AnimatorInstance->Update(0.2f);
    ANIMATION_CHECK(loaded.AnimatorInstance->GetCurrentStateName() == "Walk" && loaded.AnimatorInstance->GetCurrentPose().Transforms[0].Translation.x > 4, "Reopened typed transition evaluates actual target motion");
    // Re-saving must capture current authored edits, rather than the old fallback.
    auto channel = loaded.AnimatorInstance->GetStateMachine()->FindState("Idle")->Clip->GetChannels()[0];
    channel.Positions[1].Value.x = 8;
    ANIMATION_CHECK(loaded.AnimatorInstance->GetStateMachine()->FindState("Idle")->Clip->ReplaceChannels({channel}), "Authored clip edit publishes");
    const auto editedPath = scratch.Path / "edited.axle";
    ANIMATION_CHECK(reader.Serialize(editedPath), "Reopened authored edits can save");
    Scene edited; SceneSerializer editedReader(&edited); ANIMATION_CHECK(editedReader.Deserialize(editedPath), "Edited scene reopens");
    auto editedId = *edited.GetRegistry().view<AnimatorComponent>().begin();
    ANIMATION_CHECK(edited.GetRegistry().get<AnimatorComponent>(editedId).AnimatorInstance->GetStateMachine()->FindState("Idle")->Clip->GetChannels()[0].Positions[1].Value.x == 8, "Fresh clip keys supersede stale fallback");
    const auto assembly = scratch.Path / "keyed.axle";
    ANIMATION_CHECK(AssemblySerializer::Serialize(assembly, entity), "Keyed assembly saves");
    auto document = AssemblySerializer::LoadAssemblyData(assembly);
    ANIMATION_CHECK(document.IsValid && AssemblySerializer::SaveAssemblyData(assembly, document), "Assembly editor document preserves inline rig and motion");
    Scene target; auto instance = AssemblySerializer::Deserialize(assembly, &target);
    ANIMATION_CHECK(instance && instance.GetComponent<SkeletalMeshComponent>().Mesh && instance.GetComponent<AnimatorComponent>().AnimatorInstance, "Assembly document round trip resolves custom mesh/controller");
    auto other = AssemblySerializer::Deserialize(assembly, &target);
    auto firstAnimator = instance.GetComponent<AnimatorComponent>().AnimatorInstance;
    auto secondAnimator = other.GetComponent<AnimatorComponent>().AnimatorInstance;
    firstAnimator->Update(0.25f);
    ANIMATION_CHECK(AnimationTests::Near(firstAnimator->GetCurrentPose().Transforms[0].Translation.x,0.5f) && AnimationTests::Near(secondAnimator->GetCurrentPose().Transforms[0].Translation.x,0), "Assembly instances have independent clocks");
    // Cooked relative dependencies remain usable after the source model has disappeared.
    SkeletonDefinition skeleton; skeleton.Id = "rig"; skeleton.Name = rig->GetName(); skeleton.Bones = rig->GetBones();
    skeleton.CompatibilitySignature = SkeletonDefinition::ComputeSignature(skeleton.Bones);
    SkeletalMeshDefinition cooked; cooked.Id = "mesh"; cooked.Name = "Cooked"; cooked.SkeletonRef = "rig.axleasset"; cooked.GeometryRef = "mesh.bin";
    ANIMATION_CHECK(AnimationAssetCodec::SaveSkeleton((scratch.Path/"rig.axleasset").string(), skeleton) &&
        AnimationAssetCodec::BakeSkeletalMeshGeometry((scratch.Path/"mesh.bin").string(), data, skeleton.CompatibilitySignature) &&
        AnimationAssetCodec::SaveSkeletalMesh((scratch.Path/"mesh.axleasset").string(), cooked), "Cooked resource graph writes");
    SkeletalMeshComponent cookedComponent; cookedComponent.AssetPath = "mesh.axleasset";
    ANIMATION_CHECK(ResolveAnimationBinding(&cookedComponent,nullptr,path.string()) && cookedComponent.Mesh, "Scene binding reads owner-relative cooked geometry without source model");
    auto external = AnimationAssetCodec::ExtractDefinitionFromStateMachine(*machine, rig.get()); external.EntryStateId = external.FindStateByName("Walk")->Id;
    ANIMATION_CHECK(AnimationAssetCodec::SaveAnimatorController((scratch.Path/"controller.axleasset").string(), external), "External controller writes");
    std::string rigHex, controllerHex; ANIMATION_CHECK(CaptureAnimationBinding(nullptr,&component,rigHex,controllerHex), "Capture local controller");
    AnimatorComponent unique; unique.IsUnique = true; unique.ControllerPath = "controller.axleasset"; unique.InlineFallbackData = controllerHex;
    ANIMATION_CHECK(ResolveAnimationBinding(&cookedComponent,&unique,path.string()) && unique.AnimatorInstance->GetCurrentStateName() == "Idle", "Unique controller prefers authored local definition");
    AnimatorComponent referenced; referenced.ControllerPath = "controller.axleasset"; referenced.InlineFallbackData = controllerHex;
    ANIMATION_CHECK(ResolveAnimationBinding(&cookedComponent,&referenced,path.string()) && referenced.AnimatorInstance->GetCurrentStateName() == "Walk", "Referenced controller uses external definition");
    // An unsupported opaque predicate must not truncate an already saved scene/assembly.
    const auto previousScene = FileSystem::ReadTextFile(path), previousAssembly = FileSystem::ReadTextFile(assembly);
    AnimationTransition opaque; opaque.FromState = "Idle"; opaque.ToState = "Walk"; opaque.Condition = [] { return true; }; machine->AddTransition(opaque);
    ANIMATION_CHECK(!serializer.Serialize(path) && FileSystem::ReadTextFile(path) == previousScene, "Failed scene save preserves prior bytes");
    ANIMATION_CHECK(!AssemblySerializer::Serialize(assembly,entity) && FileSystem::ReadTextFile(assembly) == previousAssembly, "Failed assembly save preserves prior bytes");
    return true;
}
