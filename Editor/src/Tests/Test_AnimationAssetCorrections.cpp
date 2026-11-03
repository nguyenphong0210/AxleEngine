#include "AnimationTestHarness.hpp"
#include <Axle/AnimationAssetCodec.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

static bool TestLosslessBindAndInvalidOutputs() {
    using namespace Axle;
    SkeletonDefinition definition; definition.Id = "rig"; definition.Name = "Rig";
    Bone bone; bone.Name = "Root";
    bone.LocalTransform = glm::translate(glm::mat4(1), {0, 2, 0});
    bone.InverseBindMatrix = glm::translate(glm::mat4(1), {0, -2, 0});
    definition.Bones.push_back(bone);
    AssetDocument encoded;
    ANIMATION_CHECK(AnimationAssetCodec::EncodeSkeleton(definition, encoded), "Encode complete bind data");
    const auto text = AssetDSL::SerializeDocument(encoded);
    AssetDocument parsed; std::string error;
    ANIMATION_CHECK(AssetDSL::ParseSource(text, parsed, &error), "Actual skeleton text parses");
    SkeletonDefinition loaded;
    ANIMATION_CHECK(AnimationAssetCodec::DecodeSkeleton(parsed, loaded, &error), "Decode complete bind data");
    ANIMATION_CHECK(AnimationTests::Near(loaded.Bones[0].InverseBindMatrix, bone.InverseBindMatrix), "Inverse bind survives text round trip");
    ANIMATION_CHECK(AnimationTests::Near(loaded.Bones[0].LocalTransform * loaded.Bones[0].InverseBindMatrix, glm::mat4(1)), "Bind skin palette remains identity");
    auto rotated = definition;
    rotated.Bones[0].LocalTransform = glm::rotate(bone.LocalTransform, 0.7f, glm::vec3(0, 1, 0));
    ANIMATION_CHECK(SkeletonDefinition::ComputeSignature(rotated.Bones) != SkeletonDefinition::ComputeSignature(definition.Bones), "Compatibility includes rotation");
    rotated = definition; rotated.Bones[0].InverseBindMatrix[3].x = 3;
    ANIMATION_CHECK(SkeletonDefinition::ComputeSignature(rotated.Bones) != SkeletonDefinition::ComputeSignature(definition.Bones), "Compatibility includes inverse binds");
    parsed.Root->SetUInt("version", 99);
    ANIMATION_CHECK(!AnimationAssetCodec::DecodeSkeleton(parsed, loaded, &error) && !error.empty(), "Future schemas produce diagnostics");
    ANIMATION_CHECK(AnimationTests::Near(loaded.Bones[0].InverseBindMatrix, bone.InverseBindMatrix), "Failed decode retains last good output");

    Skeleton first("First"), second("Second"); first.AddBone(bone); second.AddBone(bone);
    AnimationPose a, b, result;
    a.InitFromSkeletonBindPose(first); b.InitFromSkeletonBindPose(second);
    ANIMATION_CHECK(!AnimationPose::Blend(a, b, 0.5f, result), "Equal-length poses from different rigs reject blending");
    b.InitFromSkeletonBindPose(first);
    b.Transforms[0].Translation.x = std::numeric_limits<float>::quiet_NaN();
    ANIMATION_CHECK(!AnimationPose::Blend(a, b, 0.5f, result), "Nonfinite poses reject blending");
    b.InitFromSkeletonBindPose(first);
    ANIMATION_CHECK(!AnimationPose::Blend(a, b, std::numeric_limits<float>::quiet_NaN(), result), "Nonfinite blend weight rejected");
    AnimationClip clip("Clip", 1);
    AnimationChannel valid; valid.BoneName = "Root"; valid.Positions = {{0, glm::vec3(0)}, {1, glm::vec3(1)}};
    ANIMATION_CHECK(clip.ReplaceChannels({valid}, &first), "Publish valid channels");
    auto invalid = valid; invalid.Positions[1].TimeStamp = 0;
    ANIMATION_CHECK(!clip.ReplaceChannels({invalid}, &first) && clip.GetChannels()[0].Positions[1].TimeStamp == 1, "Rejected edit keeps previous channels");
    return true;
}

bool RunAnimationAssetCorrectionTests() { return TestLosslessBindAndInvalidOutputs(); }
