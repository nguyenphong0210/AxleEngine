#include "AnimationTestHarness.hpp"
#include <Axle/AnimationResources.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <chrono>

namespace {
struct PackageScratch {
    const std::filesystem::path Root = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const std::filesystem::path Path = Root /
        ("axle_animation_package_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool Mounted = false;
    PackageScratch() { std::filesystem::create_directories(Path); }
    ~PackageScratch() {
        if (Mounted) Axle::FileSystem::UnmountPackage();
        std::error_code error;
        const auto resolved = std::filesystem::weakly_canonical(Path,error);
        if (!error && resolved.parent_path() == Root && resolved.filename().string().starts_with("axle_animation_package_"))
            std::filesystem::remove_all(resolved,error);
    }
};
}

bool RunAnimationPackageCorrectionTests() {
    using namespace Axle;
    ANIMATION_CHECK(!FileSystem::HasMountedPackage(), "Package regression starts without replacing another mount");
    PackageScratch scratch;
    auto rig = std::make_shared<Skeleton>("PackagedRig"); Bone bone; bone.Name = "Root"; rig->AddBone(bone);
    SkeletalMeshData geometry; geometry.Name = "Triangle"; geometry.SkeletonRef = rig;
    geometry.Vertices.resize(3); geometry.Vertices[1].Position.x = 1; geometry.Vertices[2].Position.y = 1;
    geometry.Indices = {0,1,2};
    for (auto& vertex : geometry.Vertices) { vertex.BoneIDs[0] = 0; vertex.Weights[0] = 1; }
    auto machine = std::make_shared<AnimationStateMachine>("PackagedController");
    AnimationState state; state.Name = "Move"; state.Clip = std::make_shared<AnimationClip>("Move",1);
    AnimationChannel channel; channel.BoneName = "Root"; channel.Positions = {{0,glm::vec3(0)},{1,glm::vec3(2,0,0)}};
    state.Clip->AddChannel(channel); machine->AddState(state);
    auto controller = AnimationAssetCodec::ExtractDefinitionFromStateMachine(*machine,rig.get());
    const auto skeleton = *controller.SkeletonFallback;
    const auto clip = controller.InlineClips[0].Clip;
    controller.States[0].MotionAssetRef = "clip.axleasset"; controller.InlineClips.clear();
    controller.SkeletonRef = "rig.axleasset"; controller.SkeletonFallback.reset();
    SkeletalMeshDefinition mesh; mesh.Id = "mesh"; mesh.Name = "PackagedMesh";
    mesh.SkeletonRef = "rig.axleasset"; mesh.GeometryRef = "geometry.bin";
    ANIMATION_CHECK(AnimationAssetCodec::SaveSkeleton((scratch.Path/"rig.axleasset").string(),skeleton) &&
        AnimationAssetCodec::SaveAnimationClip((scratch.Path/"clip.axleasset").string(),clip) &&
        AnimationAssetCodec::SaveAnimatorController((scratch.Path/"controller.axleasset").string(),controller) &&
        AnimationAssetCodec::SaveSkeletalMesh((scratch.Path/"mesh.axleasset").string(),mesh) &&
        AnimationAssetCodec::BakeSkeletalMeshGeometry((scratch.Path/"geometry.bin").string(),geometry,skeleton.CompatibilitySignature), "Cook complete animation resource graph");
    const auto prefix = scratch.Path.filename().generic_string();
    PackageWriter writer;
    ANIMATION_CHECK(writer.Begin(scratch.Path/"animation.axpak"), "Begin package");
    for (const auto* name : {"rig.axleasset","clip.axleasset","controller.axleasset","mesh.axleasset","geometry.bin"})
        ANIMATION_CHECK(writer.AddFile(prefix+"/"+name,scratch.Path/name), "Add cooked animation dependency");
    ANIMATION_CHECK(writer.Finish() && FileSystem::MountPackage(scratch.Path/"animation.axpak"), "Mount actual packaged resources");
    scratch.Mounted = true;
    ANIMATION_CHECK(!std::filesystem::exists(prefix+"/mesh.axleasset"), "Virtual resource has no loose filesystem equivalent");
    std::string error;
    auto loadedMesh = LoadAnimationMesh(prefix+"/mesh.axleasset","","",&error);
    if (!loadedMesh) std::cerr << error << '\n';
    ANIMATION_CHECK(loadedMesh && loadedMesh->GetData().Indices == geometry.Indices, "VFS resolves mesh, geometry and relative rig");
    auto loaded = LoadAnimationAnimator(prefix+"/controller.axleasset","",false,loadedMesh->GetSkeleton(),"",&error);
    if (!loaded) std::cerr << error << '\n';
    ANIMATION_CHECK(loaded, "VFS resolves controller and external keyed clip without inline fallback");
    loaded->Update(0.25f);
    ANIMATION_CHECK(AnimationTests::Near(loaded->GetSkinningMatrices()[0][3].x,0.5f), "Packaged resource graph evaluates the real keyed skinning palette");
    FileSystem::UnmountPackage(); scratch.Mounted = false;
    loaded->Update(0.25f);
    ANIMATION_CHECK(AnimationTests::Near(loaded->GetSkinningMatrices()[0][3].x,1), "Runtime retains owned resources after VFS source unmount");
    return true;
}
