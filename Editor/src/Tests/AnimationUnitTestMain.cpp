#include "AnimationTestHarness.hpp"
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
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

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test-failure")
        return Axle::AnimationTests::Check(false, "Intentional harness failure", __FILE__, __LINE__) ? 0 : 1;
    std::cout << "Animation unit tests: device-free CPU evaluation\n";
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
    std::cout << "Animation unit tests PASS\n";
    return 0;
}
