#include "AnimationTestHarness.hpp"
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/FbxCurveBaker.hpp>
#include <ufbx.h>
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

    // A2: Test stepped track preservation and exact hold boundary evaluation
    ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "SteppedSkin.fbx", mesh, rig, clips, &error), "Stepped FBX imports");
    ANIMATION_CHECK(clips.size() == 1, "Stepped FBX clip validated");
    const auto* steppedChannel = clips[0]->FindChannel(rig->GetBones()[0].Name);
    ANIMATION_CHECK(steppedChannel != nullptr, "Stepped channel exists");
    ANIMATION_CHECK(steppedChannel->PositionsInterpolation == AnimationInterpolation::Step, "Stepped channel marked as Step interpolation");
    AnimationPose stepPose;
    ANIMATION_CHECK(clips[0]->SamplePose(0.0f, *rig, stepPose, false) && AnimationTests::Near(stepPose.Transforms[0].Translation.x, 2.0f), "Hold start exact value");
    ANIMATION_CHECK(clips[0]->SamplePose(0.25f, *rig, stepPose, false) && AnimationTests::Near(stepPose.Transforms[0].Translation.x, 2.0f), "Hold interior off-grid before step");
    ANIMATION_CHECK(clips[0]->SamplePose(0.4999f, *rig, stepPose, false) && AnimationTests::Near(stepPose.Transforms[0].Translation.x, 2.0f), "Hold just before boundary");
    ANIMATION_CHECK(clips[0]->SamplePose(0.5000f, *rig, stepPose, false) && AnimationTests::Near(stepPose.Transforms[0].Translation.x, 5.0f), "Hold exact boundary switch");
    ANIMATION_CHECK(clips[0]->SamplePose(0.75f, *rig, stepPose, false) && AnimationTests::Near(stepPose.Transforms[0].Translation.x, 5.0f), "Hold interior off-grid after step");
    ANIMATION_CHECK(clips[0]->SamplePose(1.0f, *rig, stepPose, false) && AnimationTests::Near(stepPose.Transforms[0].Translation.x, 8.0f), "Hold clip end value");

    // R4-R6: Close steps, CONSTANT_NEXT holds, and final key linear interpolation regressions
    for (const auto& [fixtureName, queryTime] : {
        std::pair<std::string, double>{"CloseSteps.fbx", 0.5001},
        std::pair<std::string, double>{"ConstantNext.fbx", 0.25},
        std::pair<std::string, double>{"FinalKeyLinear.fbx", 0.25}
    }) {
        SkeletalMeshData stepData; std::shared_ptr<Skeleton> stepRig;
        std::vector<std::shared_ptr<AnimationClip>> stepClips; std::string stepErr;
        const auto fixturePath = fixtures / fixtureName;
        ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixturePath, stepData, stepRig, stepClips, &stepErr),
                        (fixtureName + " imports successfully: " + stepErr).c_str());
        ANIMATION_CHECK(stepClips.size() == 1, (fixtureName + " has 1 clip").c_str());
        ufbx_load_opts opts{}; opts.target_axes = ufbx_axes_right_handed_y_up; opts.target_unit_meters = 1;
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES; ufbx_error uErr{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> sc(
            ufbx_load_file(fixturePath.string().c_str(), &opts, &uErr), ufbx_free_scene);
        ANIMATION_CHECK(sc && sc->anim_stacks.count > 0, (fixtureName + " scene loaded by ufbx").c_str());
        auto* boneNode = ufbx_find_node(sc.get(), "Bone");
        ANIMATION_CHECK(boneNode != nullptr, (fixtureName + " has Bone node").c_str());
        const auto expected = ufbx_evaluate_transform(sc->anim_stacks[0]->anim, boneNode,
                                                     sc->anim_stacks[0]->time_begin + queryTime);
        AnimationPose pose;
        ANIMATION_CHECK(stepClips[0]->SamplePose(static_cast<float>(queryTime), *stepRig, pose, false),
                        (fixtureName + " sampled at query time").c_str());
        float diffX = std::abs(static_cast<float>(expected.translation.x) - pose.Transforms[0].Translation.x);
        ANIMATION_CHECK(diffX < 0.0011f, (fixtureName + " hold value matches source at query time").c_str());
    }

    // A2: Fast rotation that naive 30 Hz grid would undersample/miss
    ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "FastRotationSkin.fbx", mesh, rig, clips, &error), "Fast rotation FBX imports");
    ANIMATION_CHECK(clips.size() == 1 && AnimationTests::Near(clips[0]->GetDuration(), 0.1f), "Fast rotation duration 0.1s");
    AnimationPose rotPose;
    ANIMATION_CHECK(clips[0]->SamplePose(0.05f, *rig, rotPose, false), "Fast rotation samples at peak");
    glm::quat expectedPeak = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    float distToPeak = FbxCurveBaker::QuaternionAngularDistance(rotPose.Transforms[0].Rotation, expectedPeak);
    ANIMATION_CHECK(distToPeak < 1e-3f, "Fast rotation peak 90 deg captured accurately");
    ANIMATION_CHECK(clips[0]->SamplePose(0.025f, *rig, rotPose, false), "Off-grid interior sample at 0.025s");
    glm::quat expected45 = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));
    ANIMATION_CHECK(FbxCurveBaker::QuaternionAngularDistance(rotPose.Transforms[0].Rotation, expected45) < 1e-3f, "Off-grid 0.025s rotation within tolerance");

    // R3: 0-to-2160 deg rotation over 1/30s (AliasedSpin.fbx)
    {
        SkeletalMeshData spinMesh; std::shared_ptr<Skeleton> spinRig;
        std::vector<std::shared_ptr<AnimationClip>> spinClips; std::string spinErr;
        const auto spinPath = fixtures / "AliasedSpin.fbx";
        ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(spinPath, spinMesh, spinRig, spinClips, &spinErr),
                        ("AliasedSpin.fbx imports successfully: " + spinErr).c_str());
        ANIMATION_CHECK(spinClips.size() == 1, "AliasedSpin.fbx has 1 clip");
        ufbx_load_opts opts{}; opts.target_axes = ufbx_axes_right_handed_y_up; opts.target_unit_meters = 1;
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES; ufbx_error uErr{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> sc(
            ufbx_load_file(spinPath.string().c_str(), &opts, &uErr), ufbx_free_scene);
        ANIMATION_CHECK(sc && sc->anim_stacks.count > 0, "AliasedSpin.fbx loaded by ufbx");
        auto* boneNode = ufbx_find_node(sc.get(), "Bone");
        ANIMATION_CHECK(boneNode != nullptr, "AliasedSpin.fbx has Bone node");
        double queryTime = 1.0 / 360.0;
        const auto expected = ufbx_evaluate_transform(sc->anim_stacks[0]->anim, boneNode,
                                                     sc->anim_stacks[0]->time_begin + queryTime);
        AnimationPose pose;
        ANIMATION_CHECK(spinClips[0]->SamplePose(static_cast<float>(queryTime), *spinRig, pose, false),
                        "AliasedSpin sampled at query time");
        glm::quat ref(static_cast<float>(expected.rotation.w),
                      static_cast<float>(expected.rotation.x),
                      static_cast<float>(expected.rotation.y),
                      static_cast<float>(expected.rotation.z));
        float dist = FbxCurveBaker::QuaternionAngularDistance(ref, pose.Transforms[0].Rotation);
        ANIMATION_CHECK(dist <= 0.0011f, "AliasedSpin off-probe angular error within declared 0.001 rad tolerance");
    }

    // A2: Signed scale and reflection preservation
    ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "SignedScaleSkin.fbx", mesh, rig, clips, &error), "Signed scale FBX imports");
    AnimationPose scalePose;
    ANIMATION_CHECK(clips[0]->SamplePose(1.0f, *rig, scalePose, false), "Sample signed scale at clip end");
    ANIMATION_CHECK(AnimationTests::Near(scalePose.Transforms[0].Scale.x, -2.0f), "Preserves negative signed scale (-2.0)");
    ANIMATION_CHECK(clips[0]->SamplePose(0.5f, *rig, scalePose, false) && AnimationTests::Near(scalePose.Transforms[0].Scale.x, -1.5f), "Interior scale sample evaluates");

    // A2: Non-zero take start time handling
    ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "NonZeroTakeStartSkin.fbx", mesh, rig, clips, &error), "Non-zero take start FBX imports");
    ANIMATION_CHECK(AnimationTests::Near(clips[0]->GetDuration(), 1.0f), "Clip duration relative to take bounds");
    AnimationPose takePose;
    ANIMATION_CHECK(clips[0]->SamplePose(0.0f, *rig, takePose, false) && AnimationTests::Near(takePose.Transforms[0].Translation.x, 10.0f), "Start local time maps to take start");
    ANIMATION_CHECK(clips[0]->SamplePose(1.0f, *rig, takePose, false) && AnimationTests::Near(takePose.Transforms[0].Translation.x, 20.0f), "End local time maps to take stop");
    ANIMATION_CHECK(clips[0]->SamplePose(0.4f, *rig, takePose, false) && AnimationTests::Near(takePose.Transforms[0].Translation.x, 14.0f), "Interior local time maps correctly to source take");

    // A2: Mixed interpolation rejection and last-good preservation
    const auto lastGoodRig = rig;
    const auto lastGoodMeshVertices = mesh.Vertices.size();
    const auto lastGoodClips = clips.size();
    ANIMATION_CHECK(!SkeletalMeshImporter::LoadFBX(fixtures / "MixedInterpolationSkin.fbx", mesh, rig, clips, &error), "Mixed interpolation FBX is rejected");
    ANIMATION_CHECK(error.find("unsupported mixed step and continuous interpolation") != std::string::npos, "Diagnostic identifies mixed step/continuous error");
    ANIMATION_CHECK(rig == lastGoodRig && mesh.Vertices.size() == lastGoodMeshVertices && clips.size() == lastGoodClips, "Failed bake retains previous good resources");

    // R8: failed public BakeChannel retains last-good caller output
    {
        ufbx_load_opts opts{}; opts.target_axes = ufbx_axes_right_handed_y_up; opts.target_unit_meters = 1;
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES; ufbx_error uErr{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> sc(
            ufbx_load_file((fixtures / "MixedInterpolationSkin.fbx").string().c_str(), &opts, &uErr), ufbx_free_scene);
        ANIMATION_CHECK(sc && sc->anim_stacks.count > 0, "Mixed scene loaded for BakeChannel transactional test");
        AnimationChannel existing;
        existing.BoneName = "LastGood";
        existing.Positions.push_back({ 0.0f, glm::vec3(42.0f) });
        size_t budget = 1000;
        std::string bakeErr;
        const bool baked = FbxCurveBaker::BakeChannel(sc->anim_stacks[0]->anim, ufbx_find_node(sc.get(), "Bone"),
                                                      0, 1, "Bone", 0, existing, {}, budget, nullptr, &bakeErr);
        ANIMATION_CHECK(!baked && existing.BoneName == "LastGood" && existing.Positions.size() == 1,
                        "failed public BakeChannel retains last-good caller output");
        ANIMATION_CHECK(budget == 1000, "failed public BakeChannel preserves key budget");
    }

    // R7: Budget exhaustion and explicit helper node mapping
    {
        SkeletalMeshData helperMesh; std::shared_ptr<Skeleton> helperRig;
        std::vector<std::shared_ptr<AnimationClip>> helperClips; std::string helperErr;
        ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "HelperSkin.fbx", helperMesh, helperRig, helperClips, &helperErr),
                        "HelperSkin.fbx imports for budget test");
        ufbx_load_opts opts{}; opts.target_axes = ufbx_axes_right_handed_y_up; opts.target_unit_meters = 1;
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        ufbx_error ufbxErr{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> sc(ufbx_load_file((fixtures / "HelperSkin.fbx").string().c_str(), &opts, &ufbxErr), ufbx_free_scene);
        ANIMATION_CHECK(sc && sc->anim_stacks.count > 0, "UFBX loads scene for budget test");
        std::vector<const ufbx_node*> fbxNodes;
        for (size_t b = 0; b < helperRig->GetBoneCount(); ++b) {
            const auto& bone = helperRig->GetBones()[b];
            for (auto n : sc->nodes) {
                std::string nName(n->name.data, n->name.length);
                if (nName.empty()) nName = "FBX_Node_" + std::to_string(n->element_id);
                if (nName == bone.Name) {
                    fbxNodes.push_back(n);
                    break;
                }
            }
        }
        ANIMATION_CHECK(fbxNodes.size() == helperRig->GetBoneCount(), "All rig bones mapped to FBX nodes");

        FbxBakeTolerances budgetTols;
        budgetTols.MaxTotalKeysPerStack = 3; // Intentionally starved budget
        std::string budgetErr;
        auto starvedClip = FbxCurveBaker::BakeAnimStack(sc->anim_stacks[0], fbxNodes, *helperRig, budgetTols, nullptr, &budgetErr);
        ANIMATION_CHECK(starvedClip == nullptr, "Starved budget rejects bake");
        ANIMATION_CHECK(budgetErr.find("FBX sampled key budget exceeded") != std::string::npos, "Diagnostic identifies key budget exhaustion");
    }

    // R7: Nonlinear curve adaptive refinement and precision limit
    {
        SkeletalMeshData nlMesh; std::shared_ptr<Skeleton> nlRig;
        std::vector<std::shared_ptr<AnimationClip>> nlClips; std::string nlErr;
        ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures / "NonlinearSkin.fbx", nlMesh, nlRig, nlClips, &nlErr),
                        "NonlinearSkin.fbx imports successfully");
        ANIMATION_CHECK(nlClips.size() == 1, "NonlinearSkin has 1 clip");

        ufbx_load_opts opts{}; opts.target_axes = ufbx_axes_right_handed_y_up; opts.target_unit_meters = 1;
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        ufbx_error ufbxErr{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> sc(
            ufbx_load_file((fixtures / "NonlinearSkin.fbx").string().c_str(), &opts, &ufbxErr), ufbx_free_scene);
        ANIMATION_CHECK(sc && sc->anim_stacks.count > 0, "NonlinearSkin loaded by ufbx");
        auto* boneNode = ufbx_find_node(sc.get(), "Bone");
        ANIMATION_CHECK(boneNode != nullptr, "NonlinearSkin has Bone node");

        // Verify adaptive baking matches continuous ufbx evaluation at off-grid query points
        for (double queryTime : { 0.125, 0.25, 0.375, 0.625, 0.75, 0.875 }) {
            const auto expected = ufbx_evaluate_transform(sc->anim_stacks[0]->anim, boneNode,
                                                         sc->anim_stacks[0]->time_begin + queryTime);
            AnimationPose pose;
            ANIMATION_CHECK(nlClips[0]->SamplePose(static_cast<float>(queryTime), *nlRig, pose, false),
                            "NonlinearSkin sampled at off-grid query time");
            float diffX = std::abs(static_cast<float>(expected.translation.x) - pose.Transforms[0].Translation.x);
            ANIMATION_CHECK(diffX < 0.0011f, "Nonlinear adaptive bake matches continuous ufbx evaluation within tolerance");
        }

        // Test that MaxRecursionDepth = 0 on this nonlinear curve fails with precision limit error
        std::vector<const ufbx_node*> nlNodes = { boneNode };
        FbxBakeTolerances tightTols;
        tightTols.TranslationTolerance = 0.01f; // Macroscopic tolerance (1 cm)
        tightTols.MaxRecursionDepth = 0;        // Refinement forbidden
        std::string tightErr;
        auto unrefinableClip = FbxCurveBaker::BakeAnimStack(sc->anim_stacks[0], nlNodes, *nlRig, tightTols, nullptr, &tightErr);
        ANIMATION_CHECK(unrefinableClip == nullptr, "Unrefinable nonlinear curve rejects bake when refinement is forbidden");
        ANIMATION_CHECK(tightErr.find("cannot be refined within limit") != std::string::npos, "Diagnostic identifies precision limit failure");
    }

    // A2: Quaternion angular distance handles antipodal equivalence
    glm::quat qA(1, 0, 0, 0);
    glm::quat qNegA(-1, 0, 0, 0);
    ANIMATION_CHECK(AnimationTests::Near(FbxCurveBaker::QuaternionAngularDistance(qA, qNegA), 0.0f), "q and -q have zero angular distance");
    glm::quat qRot90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    ANIMATION_CHECK(AnimationTests::Near(FbxCurveBaker::QuaternionAngularDistance(qA, qRot90), glm::radians(90.0f)), "90 deg rotation angular distance matches");
    ANIMATION_CHECK(AnimationTests::Near(FbxCurveBaker::QuaternionAngularDistance(qNegA, qRot90), glm::radians(90.0f)), "-q and 90 deg rotation angular distance matches");

    return true;
}
