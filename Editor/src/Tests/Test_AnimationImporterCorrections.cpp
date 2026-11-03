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
    if (!SkeletalMeshImporter::LoadFBX(fixtures / "HelperSkin.fbx", mesh, rig, clips, &error)) {
        std::cerr << "FBX fixture: " << error << '\n'; return false;
    }
    ANIMATION_CHECK(mesh.Validate() && mesh.Vertices.size() == 6 && mesh.Indices.size() == 6, "FBX corner attributes survive split vertices");
    ANIMATION_CHECK(mesh.Vertices[0].TexCoords == glm::vec2(0,0) && mesh.Vertices[3].TexCoords == glm::vec2(0.5f,0.5f), "FBX UV seam preserved at the shared control point");
    ANIMATION_CHECK(mesh.Vertices[0].Normal == glm::vec3(0,0,1), "FBX source normals retained");
    ANIMATION_CHECK(rig->GetBoneCount() >= 2 && rig->GetBones()[0].ParentID >= 0, "FBX helper ancestors retained without changing cluster indices");
    std::vector<glm::mat4> bind;
    rig->ComputeBindPoseGlobalTransforms(bind);
    ANIMATION_CHECK(AnimationTests::Near(bind[0][3].x,5) && AnimationTests::Near(bind[0]*rig->GetBones()[0].InverseBindMatrix,glm::mat4(1)), "FBX helper/cluster inverse binds share one basis");
    ANIMATION_CHECK(clips.size() == 1, "FBX source animation retained");
    AnimationPose fbxPose; std::vector<glm::mat4> globals;
    ANIMATION_CHECK(clips[0]->SamplePose(0.5f,*rig,fbxPose,false) && fbxPose.ComputeGlobalMatrices(*rig,globals), "FBX keyed helper hierarchy evaluates");
    ANIMATION_CHECK(AnimationTests::Near(globals[0][3].x,6), "FBX evaluated motion includes helper transform");
    const auto lastFbxRig = rig; const auto lastFbxVertices = mesh.Vertices.size();
    ANIMATION_CHECK(!SkeletalMeshImporter::LoadFBX(fixtures / "missing.fbx", mesh, rig, clips, &error) && !error.empty(), "Missing FBX returns diagnostics");
    ANIMATION_CHECK(rig == lastFbxRig && mesh.Vertices.size() == lastFbxVertices, "Failed FBX import keeps last good resource");
    ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "ConvertedSkin.fbx", mesh, rig, clips, &error), "Reflected FBX source basis imports");
    rig->ComputeBindPoseGlobalTransforms(bind);
    ANIMATION_CHECK(AnimationTests::Near(bind[0]*rig->GetBones()[0].InverseBindMatrix,glm::mat4(1)), "FBX canonical mesh basis compensates inverse binds");
    ANIMATION_CHECK(AnimationTests::Near(mesh.Vertices[0].Position.z,-1) && mesh.Vertices[0].Normal.z == -1 && mesh.Vertices[1].Position == glm::vec3(1,1,0), "Reflected FBX converts positions, normals and winding together");
    return true;
}
