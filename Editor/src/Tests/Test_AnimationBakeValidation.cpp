#include "AnimationTestHarness.hpp"
#include <Axle/FbxCurveBaker.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
#include <filesystem>
#include <limits>
#include <vector>

namespace {
bool SameStats(const Axle::FbxBakeStats& a,const Axle::FbxBakeStats& b) {
    return a.PositionKeys==b.PositionKeys && a.RotationKeys==b.RotationKeys && a.ScaleKeys==b.ScaleKeys &&
        a.SubdivisionsPerformed==b.SubdivisionsPerformed && a.MaxPositionError==b.MaxPositionError &&
        a.MaxRotationError==b.MaxRotationError && a.MaxScaleError==b.MaxScaleError;
}
}

bool RunAnimationBakeValidationTests() {
    using namespace Axle;
    const auto fixtures=std::filesystem::path(AXLE_ANIMATION_SOURCE_DIR)/"Editor/src/Tests/Fixtures/Animation";
    ufbx_load_opts options{}; options.target_axes=ufbx_axes_right_handed_y_up; options.target_unit_meters=1;
    options.inherit_mode_handling=UFBX_INHERIT_MODE_HANDLING_HELPER_NODES; ufbx_error sourceError{};
    std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> source(
        ufbx_load_file((fixtures/"HelperSkin.fbx").string().c_str(),&options,&sourceError),ufbx_free_scene);
    ANIMATION_CHECK(source && source->anim_stacks.count==1,"Budget reference source loads");
    const auto* node=ufbx_find_node(source.get(),"Bone"); ANIMATION_CHECK(node,"Budget reference node exists");
    const auto* anim=source->anim_stacks[0]->anim;
    std::vector<FbxBakeTolerances> rejected;
    const auto add=[&](auto edit) { FbxBakeTolerances settings; edit(settings); rejected.push_back(settings); };
    for (float value : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),0.0f,-1.0f}) {
        add([=](auto& t){t.TranslationTolerance=value;}); add([=](auto& t){t.RotationTolerance=value;});
        add([=](auto& t){t.ScaleTolerance=value;}); add([=](auto& t){t.MinIntervalSeconds=value;});
    }
    add([](auto& t){t.InteriorValidationProbes=0;}); add([](auto& t){t.InteriorValidationProbes=65;});
    add([](auto& t){t.InteriorValidationProbes=std::numeric_limits<uint32_t>::max();});
    add([](auto& t){t.MaxRecursionDepth=25;}); add([](auto& t){t.MaxRecursionDepth=std::numeric_limits<uint32_t>::max();});
    add([](auto& t){t.MaxTotalKeysPerStack=0;}); add([](auto& t){t.MaxTotalKeysPerStack=std::numeric_limits<size_t>::max();});
    add([](auto& t){t.MaxWorkUnitsPerStack=0;}); add([](auto& t){t.MaxWorkUnitsPerStack=std::numeric_limits<size_t>::max();});
    add([](auto& t){t.MinIntervalSeconds=3601;});
    // Valid settings with an intentionally exhausted work or output budget also fail transactionally.
    add([](auto& t){t.MaxWorkUnitsPerStack=1;}); add([](auto& t){t.MaxWorkUnitsPerStack=10;});
    add([](auto& t){t.MaxTotalKeysPerStack=3;});
    for (const auto& settings : rejected) {
        AnimationChannel channel; channel.BoneName="LastGood";
        channel.Positions={{0,glm::vec3(42)}}; channel.ScalesInterpolation=AnimationInterpolation::StepNext;
        const auto before=channel; size_t budget=std::min<size_t>(10000,settings.MaxTotalKeysPerStack);
        const auto previousBudget=budget;
        FbxBakeStats stats{11,12,13,14,0.2f,0.3f,0.4f}; const auto previousStats=stats; std::string diagnostic;
        ANIMATION_CHECK(!FbxCurveBaker::BakeChannel(anim,node,0,1,"Bone",0,channel,settings,budget,&stats,&diagnostic) &&
            !diagnostic.empty(),"Invalid configuration or exhausted work produces a diagnostic");
        ANIMATION_CHECK(channel.BoneName==before.BoneName && channel.Positions.size()==1 &&
            channel.Positions[0].Value==glm::vec3(42) && channel.ScalesInterpolation==before.ScalesInterpolation &&
            budget==previousBudget && SameStats(stats,previousStats),"Failed bake preserves channel, budget and stats");
    }
    {
        AnimationChannel channel; channel.BoneName="LastGood";
        FbxBakeStats stats; stats.PositionKeys=std::numeric_limits<size_t>::max();
        size_t keys=10000; std::string diagnostic;
        ANIMATION_CHECK(!FbxCurveBaker::BakeChannel(anim,node,0,1,"Bone",0,channel,{},keys,&stats,&diagnostic) &&
            stats.PositionKeys==std::numeric_limits<size_t>::max() && keys==10000 && channel.BoneName=="LastGood",
            "Statistics overflow is rejected without mutating output");
    }
    FbxBakeTolerances valid; valid.MaxRecursionDepth=0; // Zero depth is a valid request, not an invalid option.
    AnimationChannel linear; size_t budget=valid.MaxTotalKeysPerStack; std::string error;
    ANIMATION_CHECK(FbxCurveBaker::BakeChannel(anim,node,0,1,"Bone",0,linear,valid,budget,nullptr,&error),
                    "Linear source bakes with valid zero-depth settings");
    ANIMATION_CHECK(AnimationTests::Near(linear.InterpolatePosition(0.25f).x,2.5f),"Valid settings still evaluate the actual source");
    const auto oldChannel=linear; const auto oldBudget=budget;
    ANIMATION_CHECK(!FbxCurveBaker::BakeChannel(anim,node,std::numeric_limits<double>::infinity(),1,"Bone",0,
        linear,valid,budget,nullptr,&error) && linear.Positions.size()==oldChannel.Positions.size() && budget==oldBudget,
        "Nonfinite take times do not publish a channel");
    ANIMATION_CHECK(std::isinf(FbxCurveBaker::QuaternionAngularDistance(glm::quat(0,0,0,0),glm::quat(1,0,0,0))),
                    "Invalid quaternion has nonfinite angular error instead of acceptable finite distance");

    SkeletalMeshData geometry; std::shared_ptr<Skeleton> rig; std::vector<std::shared_ptr<AnimationClip>> clips;
    ANIMATION_CHECK(SkeletalMeshImporter::LoadFBX(fixtures/"HelperSkin.fbx",geometry,rig,clips,&error),"Stack rig fixture imports");
    std::vector<const ufbx_node*> nodes;
    for (const auto& bone : rig->GetBones()) {
        const ufbx_node* found=nullptr;
        for (const auto* candidate : source->nodes) {
            std::string name(candidate->name.data,candidate->name.length);
            if (name.empty()) name="FBX_Node_"+std::to_string(candidate->element_id);
            if (name==bone.Name) { found=candidate; break; }
        }
        ANIMATION_CHECK(found,"All stack nodes map without skipped prerequisites"); nodes.push_back(found);
    }
    FbxBakeStats oneNodeStats;
    auto first=FbxCurveBaker::BakeAnimStack(source->anim_stacks[0],{nodes[0]},*rig,valid,&oneNodeStats,&error);
    ANIMATION_CHECK(first,"First node bakes before stack rollback test");
    FbxBakeTolerances smallStack; smallStack.MaxTotalKeysPerStack=oneNodeStats.PositionKeys*3+3;
    FbxBakeStats stats{11,12,13,14,0.2f,0.3f,0.4f}; const auto previousStats=stats;
    ANIMATION_CHECK(!FbxCurveBaker::BakeAnimStack(source->anim_stacks[0],nodes,*rig,smallStack,&stats,&error) &&
        SameStats(stats,previousStats),"A later-node failure does not publish partial stack stats");
    auto tooMany=nodes; tooMany.push_back(nodes[0]);
    ANIMATION_CHECK(!FbxCurveBaker::BakeAnimStack(source->anim_stacks[0],tooMany,*rig,valid,nullptr,&error),
                    "Oversized node binding fails before indexing rig bones");
    return true;
}
