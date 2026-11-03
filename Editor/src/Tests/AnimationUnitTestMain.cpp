#include "AnimationTestHarness.hpp"
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>

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

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-failure")
        return Axle::AnimationTests::Check(false, "Intentional harness failure", __FILE__, __LINE__) ? 0 : 1;
    std::cout << "Animation unit tests: device-free CPU evaluation\n";
    if (!TestBaselineSampling()) return 1;
    std::cout << "Animation unit tests PASS\n";
    return 0;
}
