#include "AnimationTestHarness.hpp"
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <filesystem>

bool RunAnimationImporterCorrectionTests() {
    using namespace Axle;
    const auto fixtures = std::filesystem::path(AXLE_ANIMATION_SOURCE_DIR) / "Editor/src/Tests/Fixtures/Animation";
    SkeletalMeshData mesh; std::shared_ptr<Skeleton> rig;
    std::vector<std::shared_ptr<AnimationClip>> clips; std::string error;
    for (const auto* name : {"Step", "Cubic", "Helper", "Signed"}) {
        ANIMATION_CHECK(SkeletalMeshImporter::LoadGLTF(fixtures / (std::string(name)+".gltf"), mesh, rig, clips, &error), "Valid source fixture imports without graphics state");
        ANIMATION_CHECK(mesh.Validate() && clips.size() == 1, "Imported geometry and clip validated");
        AnimationPose pose;
        ANIMATION_CHECK(clips[0]->SamplePose(0.25f, *rig, pose, false), "Imported channel samples");
        if (std::string(name) == "Step") ANIMATION_CHECK(pose.Transforms[0].Translation.x == 0, "STEP holds previous value");
        if (std::string(name) == "Cubic") {
            ANIMATION_CHECK(AnimationTests::Near(pose.Transforms[0].Translation.x, 0.3125f), "Cubic Hermite curve uses actual tangents");
            AnimationClipDefinition definition; definition.Id = "cubic"; definition.Name = "Cubic";
            definition.Duration = 1; definition.Channels = clips[0]->GetChannels();
            AssetDocument doc; AnimationClipDefinition loaded;
            ANIMATION_CHECK(AnimationAssetCodec::EncodeAnimationClip(definition, doc) && AnimationAssetCodec::DecodeAnimationClip(doc, loaded), "Interpolation and tangent codec round trip");
            AnimationClip reopened("Cubic", 1); reopened.ReplaceChannels(loaded.Channels, rig.get());
            AnimationPose after; reopened.SamplePose(0.25f, *rig, after, false);
            ANIMATION_CHECK(AnimationTests::Near(after.Transforms[0].Translation.x, 0.3125f), "Saved cubic interior samples remain unchanged");
        }
        if (std::string(name) == "Helper") {
            std::vector<glm::mat4> bind, globals, palette;
            rig->ComputeBindPoseGlobalTransforms(bind); pose.ComputeGlobalMatrices(*rig, globals);
            ANIMATION_CHECK(rig->GetBoneCount() == 2 && AnimationTests::Near(bind[0][3].x, 11), "Helper above the root remains in the canonical hierarchy");
            ANIMATION_CHECK(AnimationTests::Near(globals[0][3].x, 10.5f), "Helper transform also affects animated samples");
            ANIMATION_CHECK(AnimationTests::Near(bind[0] * rig->GetBones()[0].InverseBindMatrix, glm::mat4(1)), "Imported inverse bind uses the same rig basis");
            ANIMATION_CHECK(mesh.Vertices[0].Position.x == 0, "Skinned mesh node transform is not applied twice");
        }
        if (std::string(name) == "Signed") ANIMATION_CHECK(pose.Transforms[0].Scale == glm::vec3(1,-1,1), "Rotation-only animation preserves explicit signed bind scale");
    }
    const auto previousRig = rig; const auto previousCount = mesh.Vertices.size();
    ANIMATION_CHECK(!SkeletalMeshImporter::LoadGLTF(fixtures / "MultipleSkins.gltf", mesh, rig, clips, &error) && !error.empty(), "Multi-skin source requires explicit selection");
    ANIMATION_CHECK(rig == previousRig && mesh.Vertices.size() == previousCount, "Failed import preserves previous resource");
    ANIMATION_CHECK(!SkeletalMeshImporter::LoadGLTF(fixtures / "Malformed.gltf", mesh, rig, clips, &error), "Malformed source is rejected before reading invalid accessors");
    return true;
}
