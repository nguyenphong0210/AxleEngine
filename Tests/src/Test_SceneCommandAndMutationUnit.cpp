#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/ScriptableEntity.hpp>

#include <iostream>
#include <cassert>
#include <vector>
#include <atomic>
#include <thread>
#include <cmath>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

    class MockTestScript : public Axle::ScriptableEntity {
    public:
        static inline std::atomic<int> s_DestroyCalls{0};
        static inline std::atomic<int> s_CreateCalls{0};

        void OnCreate() override {
            s_CreateCalls++;
        }

        void OnDestroy() override {
            s_DestroyCalls++;
        }
    };

    bool TestParentChildRecursiveDestroy() {
        std::cout << "--- TestParentChildRecursiveDestroy ---" << std::endl;
        Axle::Scene scene;

        Axle::Entity root = scene.CreateEntity("Root");
        Axle::Entity child1 = scene.CreateEntity("Child1");
        Axle::Entity child2 = scene.CreateEntity("Child2");
        Axle::Entity child3 = scene.CreateEntity("Child3");
        Axle::Entity grandchild1 = scene.CreateEntity("Grandchild1");
        Axle::Entity grandchild2 = scene.CreateEntity("Grandchild2");

        child1.SetParent(root);
        child2.SetParent(root);
        child3.SetParent(root);
        grandchild1.SetParent(child2);
        grandchild2.SetParent(child2);

        TEST_ASSERT(root.GetChildCount() == 3, "Root must have 3 children");
        TEST_ASSERT(child2.GetChildCount() == 2, "Child2 must have 2 children");

        entt::entity rootID = root;
        entt::entity c1ID = child1;
        entt::entity c2ID = child2;
        entt::entity c3ID = child3;
        entt::entity gc1ID = grandchild1;
        entt::entity gc2ID = grandchild2;

        // Destroy root entity
        scene.DestroyEntity(root);

        // Entire subtree must be destroyed
        TEST_ASSERT(!scene.GetRegistry().valid(rootID), "Root entity must be destroyed");
        TEST_ASSERT(!scene.GetRegistry().valid(c1ID), "Child1 must be destroyed");
        TEST_ASSERT(!scene.GetRegistry().valid(c2ID), "Child2 must be destroyed");
        TEST_ASSERT(!scene.GetRegistry().valid(c3ID), "Child3 must be destroyed");
        TEST_ASSERT(!scene.GetRegistry().valid(gc1ID), "Grandchild1 must be destroyed");
        TEST_ASSERT(!scene.GetRegistry().valid(gc2ID), "Grandchild2 must be destroyed");
        TEST_ASSERT(scene.GetRegistry().view<Axle::IDComponent>().empty(), "Registry must be empty");

        std::cout << "PASS: TestParentChildRecursiveDestroy" << std::endl;
        return true;
    }

    bool TestChildUnparentingAndDestruction() {
        std::cout << "--- TestChildUnparentingAndDestruction ---" << std::endl;
        Axle::Scene scene;

        Axle::Entity parent = scene.CreateEntity("Parent");
        Axle::Entity childA = scene.CreateEntity("ChildA");
        Axle::Entity childB = scene.CreateEntity("ChildB");

        childA.SetParent(parent);
        childB.SetParent(parent);

        TEST_ASSERT(parent.GetChildCount() == 2, "Parent must have 2 children");

        entt::entity childAID = childA;
        scene.DestroyEntity(childA);

        TEST_ASSERT(!scene.GetRegistry().valid(childAID), "ChildA must be destroyed");
        TEST_ASSERT(scene.GetRegistry().valid(parent), "Parent must remain valid");
        TEST_ASSERT(scene.GetRegistry().valid(childB), "ChildB must remain valid");
        TEST_ASSERT(parent.GetChildCount() == 1, "Parent must now have exactly 1 child");

        auto remainingChildren = parent.GetChildren();
        TEST_ASSERT(remainingChildren.size() == 1, "Remaining children list size must be 1");
        TEST_ASSERT(remainingChildren[0] == childB, "Remaining child must be ChildB");
        TEST_ASSERT(childB.GetParent() == parent, "ChildB parent must still be parent");

        std::cout << "PASS: TestChildUnparentingAndDestruction" << std::endl;
        return true;
    }

    bool TestScriptAndPhysicsTeardownOnDestroy() {
        std::cout << "--- TestScriptAndPhysicsTeardownOnDestroy ---" << std::endl;
        Axle::PhysicsEngine::Init();

        auto physicsScene = std::make_unique<Axle::PhysicsScene>();

        Axle::Scene scene;
        Axle::Entity entity = scene.CreateEntity("PhysicsAndScriptEntity");

        // Attach script
        MockTestScript::s_DestroyCalls = 0;
        MockTestScript::s_CreateCalls = 0;
        auto& nsc = entity.AddComponent<Axle::NativeScriptComponent>();
        nsc.Bind<MockTestScript>();
        nsc.Instance = nsc.InstantiateScript();
        nsc.Instance->OnCreate();
        TEST_ASSERT(MockTestScript::s_CreateCalls.load() == 1, "Script OnCreate must be called");

        // Attach physics body
        auto& rb = entity.AddComponent<Axle::RigidBody3D>();
        rb.IsDynamic = true;
        rb.Mass = 10.0f;

        auto& shape = entity.AddComponent<Axle::CollisionShape3D>();
        shape.ShapeType = Axle::CollisionShapeType::Box;
        shape.HalfExtent = glm::vec3(0.5f);

        Axle::BoxBodyProps bProps;
        bProps.Position = glm::vec3(0.0f);
        bProps.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        bProps.HalfExtent = shape.HalfExtent;
        bProps.IsDynamic = rb.IsDynamic;
        bProps.Mass = rb.Mass;
        rb.RuntimeBodyID = physicsScene->CreateBox(bProps);

        TEST_ASSERT(!rb.RuntimeBodyID.IsInvalid(), "Physics body must be valid");
        TEST_ASSERT(physicsScene->IsBodyValid(rb.RuntimeBodyID), "Body must be valid in physicsScene");

        Axle::BodyID savedBodyID = rb.RuntimeBodyID;

        // Destroy entity through scene with physicsScene
        scene.DestroyEntity(entity, physicsScene.get());

        TEST_ASSERT(MockTestScript::s_DestroyCalls.load() == 1, "Script OnDestroy must be called on destruction");
        TEST_ASSERT(nsc.Instance == nullptr, "Script instance must be nullified");
        TEST_ASSERT(!physicsScene->IsBodyValid(savedBodyID), "Physics body must be destroyed from physicsScene");

        physicsScene.reset();
        Axle::PhysicsEngine::Shutdown();
        Axle::JobSystem::Shutdown();

        std::cout << "PASS: TestScriptAndPhysicsTeardownOnDestroy" << std::endl;
        return true;
    }

    bool TestCommandBufferDestroyThenEditConflict() {
        std::cout << "--- TestCommandBufferDestroyThenEditConflict ---" << std::endl;
        Axle::Scene scene;
        Axle::Entity entity = scene.CreateEntity("ConflictTarget");
        entt::entity handle = entity;

        Axle::SceneCommandBuffer cmdBuffer;

        // Command 1: Destroy entity
        cmdBuffer.DestroyEntity(entity);

        // Command 2: Attempt to edit the same entity (should be rejected by conflict rule)
        cmdBuffer.EmplaceOrReplace<Axle::TagComponent>(entity, "MutatedAfterDestroy");

        TEST_ASSERT(cmdBuffer.GetPendingCount() == 2, "Command buffer should have 2 pending commands");

        // Execute against scene
        cmdBuffer.Execute(scene);

        TEST_ASSERT(cmdBuffer.GetLastExecutedCount() == 1, "Only Destroy command should have executed");
        TEST_ASSERT(cmdBuffer.GetLastRejectedCount() == 1, "Edit after destroy must be rejected");
        TEST_ASSERT(!scene.GetRegistry().valid(handle), "Entity must be destroyed");

        std::cout << "PASS: TestCommandBufferDestroyThenEditConflict" << std::endl;
        return true;
    }

    bool TestWrongSceneCollisionRejection() {
        std::cout << "--- TestWrongSceneCollisionRejection ---" << std::endl;
        Axle::Scene sceneA;
        Axle::Scene sceneB;

        Axle::Entity entityA = sceneA.CreateEntity("EntityA");
        Axle::Entity entityB = sceneB.CreateEntity("EntityB");

        // 1. Direct reparent across scenes must be rejected
        entityA.SetParent(entityB);
        TEST_ASSERT(!entityA.GetParent(), "Cross-scene SetParent must be rejected");

        // 2. Command buffer executing on Scene A with target Entity B from Scene B must be rejected
        Axle::SceneCommandBuffer cmdBufferA;
        cmdBufferA.DestroyEntity(entityB);
        cmdBufferA.EmplaceOrReplace<Axle::TagComponent>(entityB, "WrongSceneTag");
        cmdBufferA.SetParent(entityA, entityB);

        cmdBufferA.Execute(sceneA);

        TEST_ASSERT(cmdBufferA.GetLastExecutedCount() == 0, "No commands should execute for wrong-scene targets");
        TEST_ASSERT(cmdBufferA.GetLastRejectedCount() == 3, "All 3 commands targeting wrong scene must be rejected");

        // Entity B in Scene B must remain completely valid and unchanged
        TEST_ASSERT(sceneB.GetRegistry().valid(entityB), "EntityB in Scene B must remain valid");
        TEST_ASSERT(entityB.GetName() == "EntityB", "EntityB tag must not be mutated");

        std::cout << "PASS: TestWrongSceneCollisionRejection" << std::endl;
        return true;
    }

    bool TestMultiThreadedRecordingAndPostJoinCommit() {
        std::cout << "--- TestMultiThreadedRecordingAndPostJoinCommit ---" << std::endl;
        Axle::JobSystem::Init(4);

        Axle::Scene scene;
        Axle::SceneCommandBuffer cmdBuffer;

        constexpr uint32_t taskCount = 32;
        constexpr uint32_t entitiesPerTask = 20;

        // Record commands across multiple worker threads
        Axle::JobSystem::ParallelFor(taskCount, [&](uint32_t taskIdx) {
            for (uint32_t i = 0; i < entitiesPerTask; ++i) {
                std::string name = "WorkerEntity_" + std::to_string(taskIdx) + "_" + std::to_string(i);
                cmdBuffer.CreateEntity(name, [](Axle::Entity e) {
                    e.AddComponent<Axle::TagComponent>("Initialized");
                });
            }
        });

        // After all worker threads have joined, execute on the main thread
        cmdBuffer.Execute(scene);

        size_t createdCount = 0;
        for (auto e : scene.GetRegistry().view<Axle::IDComponent>()) {
            (void)e;
            createdCount++;
        }

        TEST_ASSERT(createdCount == taskCount * entitiesPerTask, "All parallel recorded entities must be created");
        TEST_ASSERT(cmdBuffer.GetLastExecutedCount() == taskCount * entitiesPerTask, "All commands must execute");
        TEST_ASSERT(cmdBuffer.GetLastRejectedCount() == 0, "Zero commands should be rejected");

        Axle::JobSystem::Shutdown();
        std::cout << "PASS: TestMultiThreadedRecordingAndPostJoinCommit" << std::endl;
        return true;
    }

    bool TestCommandBufferMerge() {
        std::cout << "--- TestCommandBufferMerge ---" << std::endl;
        Axle::Scene scene;

        Axle::SceneCommandBuffer bufferA;
        Axle::SceneCommandBuffer bufferB;

        for (int i = 0; i < 5; ++i) {
            bufferA.CreateEntity("EntityA_" + std::to_string(i));
        }
        for (int i = 0; i < 7; ++i) {
            bufferB.CreateEntity("EntityB_" + std::to_string(i));
        }

        TEST_ASSERT(bufferA.GetPendingCount() == 5, "Buffer A must have 5 pending");
        TEST_ASSERT(bufferB.GetPendingCount() == 7, "Buffer B must have 7 pending");

        // Merge buffer B into buffer A
        bufferA.Merge(std::move(bufferB));

        TEST_ASSERT(bufferA.GetPendingCount() == 12, "Merged buffer A must have 12 pending");
        TEST_ASSERT(bufferB.GetPendingCount() == 0, "Buffer B must be empty after move merge");

        bufferA.Execute(scene);

        TEST_ASSERT(bufferA.GetLastExecutedCount() == 12, "All 12 merged commands must execute");

        size_t total = 0;
        for (auto e : scene.GetRegistry().view<Axle::IDComponent>()) {
            (void)e;
            total++;
        }
        TEST_ASSERT(total == 12, "Scene must contain 12 entities");

        std::cout << "PASS: TestCommandBufferMerge" << std::endl;
        return true;
    }

    // =========================================================================
    // EJ-10 TRANSFORM EVALUATION OWNERSHIP TESTS
    // =========================================================================

    bool MatrixEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (std::abs(a[c][r] - b[c][r]) > eps) {
                    return false;
                }
            }
        }
        return true;
    }

    bool TestTransformEvaluationSerialVsParallel() {
        std::cout << "--- TestTransformEvaluationSerialVsParallel ---" << std::endl;
        if (!Axle::JobSystem::IsInitialized()) {
            Axle::JobSystem::Init();
        }

        Axle::Scene scene;
        std::vector<Axle::Entity> entities;

        // Build a forest of 120 entities across 20 distinct trees
        for (int t = 0; t < 20; ++t) {
            auto root = scene.CreateEntity("Root_" + std::to_string(t));
            auto& rtc = root.GetComponent<Axle::TransformComponent>();
            rtc.Translation = glm::vec3(t * 10.0f, static_cast<float>(t % 3), static_cast<float>(t % 5));
            rtc.Rotation = glm::angleAxis(glm::radians(static_cast<float>(t * 15)), glm::vec3(0.0f, 1.0f, 0.0f));
            rtc.Scale = glm::vec3(1.0f + 0.1f * t);
            entities.push_back(root);

            for (int c = 0; c < 3; ++c) {
                auto child = scene.CreateEntity("Child_" + std::to_string(t) + "_" + std::to_string(c));
                auto& ctc = child.GetComponent<Axle::TransformComponent>();
                ctc.Translation = glm::vec3(c * 2.0f, 1.0f, 0.5f);
                ctc.Rotation = glm::angleAxis(glm::radians(static_cast<float>(c * 30)), glm::vec3(1.0f, 0.0f, 0.0f));
                ctc.Scale = glm::vec3(0.8f);
                child.SetParent(root, false);
                entities.push_back(child);

                for (int gc = 0; gc < 2; ++gc) {
                    auto grandChild = scene.CreateEntity("GC_" + std::to_string(t) + "_" + std::to_string(c) + "_" + std::to_string(gc));
                    auto& gctc = grandChild.GetComponent<Axle::TransformComponent>();
                    gctc.Translation = glm::vec3(0.0f, gc * 1.5f, -1.0f);
                    grandChild.SetParent(child, false);
                    entities.push_back(grandChild);
                }
            }
        }

        // 1. Compute serial transforms using batch size > total roots (forces serial fallback)
        scene.UpdateWorldTransformsParallel(100000);
        std::vector<glm::mat4> serialMatrices;
        serialMatrices.reserve(entities.size());
        for (const auto& e : entities) {
            serialMatrices.push_back(e.GetWorldTransform());
        }

        // 2. Clear world matrices to verify parallel re-evaluates them from scratch
        for (auto& e : entities) {
            e.GetComponent<Axle::TransformComponent>().WorldMatrix = glm::mat4(0.0f);
        }

        // 3. Compute parallel transforms using small batch size to exercise JobSystem
        scene.UpdateWorldTransformsParallel(2);

        // 4. Compare all matrices
        for (size_t i = 0; i < entities.size(); ++i) {
            glm::mat4 parMat = entities[i].GetWorldTransform();
            TEST_ASSERT(MatrixEqual(serialMatrices[i], parMat), "Serial and parallel matrices must match exactly");
        }

        std::cout << "PASS: TestTransformEvaluationSerialVsParallel" << std::endl;
        return true;
    }

    bool TestTransformScaleInheritance() {
        std::cout << "--- TestTransformScaleInheritance ---" << std::endl;
        Axle::Scene scene;

        auto parent = scene.CreateEntity("ScaleParent");
        auto& ptc = parent.GetComponent<Axle::TransformComponent>();
        ptc.Translation = glm::vec3(10.0f, 5.0f, 0.0f);
        ptc.Scale = glm::vec3(2.0f, 3.0f, 4.0f);

        auto childInherit = scene.CreateEntity("ChildInheritScale");
        childInherit.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(1.0f, 1.0f, 1.0f);
        childInherit.GetComponent<Axle::HierarchyComponent>().InheritScale = true;
        childInherit.SetParent(parent, false);

        auto childNoInherit = scene.CreateEntity("ChildNoInheritScale");
        childNoInherit.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(1.0f, 1.0f, 1.0f);
        childNoInherit.GetComponent<Axle::HierarchyComponent>().InheritScale = false;
        childNoInherit.SetParent(parent, false);

        scene.UpdateWorldTransforms();

        glm::mat4 inheritMat = childInherit.GetWorldTransform();
        glm::mat4 noInheritMat = childNoInherit.GetWorldTransform();

        // With scale inheritance, child's translation in world space is (10 + 2*1, 5 + 3*1, 0 + 4*1) = (12, 8, 4)
        glm::vec3 inheritPos = glm::vec3(inheritMat[3]);
        TEST_ASSERT(std::abs(inheritPos.x - 12.0f) < 1e-4f, "InheritScale position X must be 12");
        TEST_ASSERT(std::abs(inheritPos.y - 8.0f) < 1e-4f, "InheritScale position Y must be 8");
        TEST_ASSERT(std::abs(inheritPos.z - 4.0f) < 1e-4f, "InheritScale position Z must be 4");

        // Without scale inheritance, parent scale is NOT applied to child, only rigid rotation & translation:
        // Position is (10 + 1, 5 + 1, 0 + 1) = (11, 6, 1)
        glm::vec3 noInheritPos = glm::vec3(noInheritMat[3]);
        TEST_ASSERT(std::abs(noInheritPos.x - 11.0f) < 1e-4f, "NoInheritScale position X must be 11");
        TEST_ASSERT(std::abs(noInheritPos.y - 6.0f) < 1e-4f, "NoInheritScale position Y must be 6");
        TEST_ASSERT(std::abs(noInheritPos.z - 1.0f) < 1e-4f, "NoInheritScale position Z must be 1");

        std::cout << "PASS: TestTransformScaleInheritance" << std::endl;
        return true;
    }

    bool TestTransformCycleDetectionAndRepair() {
        std::cout << "--- TestTransformCycleDetectionAndRepair ---" << std::endl;
        Axle::Scene scene;

        auto a = scene.CreateEntity("EntityA");
        auto b = scene.CreateEntity("EntityB");

        // Manually introduce a 2-node cycle in HierarchyComponent
        a.GetComponent<Axle::HierarchyComponent>().Parent = b;
        b.GetComponent<Axle::HierarchyComponent>().Parent = a;
        a.GetComponent<Axle::HierarchyComponent>().Children.push_back(b);
        b.GetComponent<Axle::HierarchyComponent>().Children.push_back(a);

        // UpdateWorldTransforms must detect cycle, repair it without hanging or crashing
        scene.UpdateWorldTransforms();

        // At least one must be promoted to root (Parent == entt::null)
        auto& ahc = a.GetComponent<Axle::HierarchyComponent>();
        auto& bhc = b.GetComponent<Axle::HierarchyComponent>();
        TEST_ASSERT(ahc.Parent == entt::null || bhc.Parent == entt::null, "Cycle must be broken with at least one root");

        // Self-parenting test
        auto c = scene.CreateEntity("EntityC");
        c.GetComponent<Axle::HierarchyComponent>().Parent = c;
        scene.UpdateWorldTransforms();
        TEST_ASSERT(c.GetComponent<Axle::HierarchyComponent>().Parent == entt::null, "Self-parenting must be repaired to null");

        std::cout << "PASS: TestTransformCycleDetectionAndRepair" << std::endl;
        return true;
    }

    bool TestTransformRevisionProgression() {
        std::cout << "--- TestTransformRevisionProgression ---" << std::endl;
        Axle::Scene scene;

        uint64_t rev0 = scene.GetTransformRevision();
        TEST_ASSERT(rev0 > 0, "Initial revision must be non-zero");

        auto entity = scene.CreateEntity("TestEntity");
        scene.UpdateWorldTransforms();
        uint64_t rev1 = scene.GetTransformRevision();
        TEST_ASSERT(rev1 > rev0, "UpdateWorldTransforms must increment revision");

        entity.RecomputeWorldTransform();
        uint64_t rev2 = scene.GetTransformRevision();
        TEST_ASSERT(rev2 > rev1, "RecomputeWorldTransform must increment revision");

        scene.Clear();
        uint64_t rev3 = scene.GetTransformRevision();
        TEST_ASSERT(rev3 > rev2, "Clear must increment revision");

        std::cout << "PASS: TestTransformRevisionProgression" << std::endl;
        return true;
    }

    bool TestTransformReadOnlyQueryConcurrency() {
        std::cout << "--- TestTransformReadOnlyQueryConcurrency ---" << std::endl;
        Axle::Scene scene;

        auto parent = scene.CreateEntity("Parent");
        parent.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(10.0f, 20.0f, 30.0f);

        auto child = scene.CreateEntity("Child");
        child.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);
        child.SetParent(parent, false);

        scene.UpdateWorldTransforms();

        glm::mat4 expectedParent = parent.GetWorldTransform();
        glm::mat4 expectedChild = child.GetWorldTransform();

        std::atomic<bool> failed{false};
        std::vector<std::thread> readers;
        const int numThreads = 8;
        const int iterations = 50000;

        for (int t = 0; t < numThreads; ++t) {
            readers.emplace_back([&scene, parent, child, expectedParent, expectedChild, &failed]() {
                for (int i = 0; i < iterations; ++i) {
                    glm::mat4 pMat = scene.GetWorldTransform(parent);
                    glm::mat4 cMat = scene.GetWorldTransform(child);
                    if (!MatrixEqual(pMat, expectedParent) || !MatrixEqual(cMat, expectedChild)) {
                        failed = true;
                        break;
                    }
                }
            });
        }

        for (auto& th : readers) {
            th.join();
        }

        TEST_ASSERT(!failed.load(), "Concurrent GetWorldTransform queries must not race or return inconsistent values");

        std::cout << "PASS: TestTransformReadOnlyQueryConcurrency" << std::endl;
        return true;
    }

} // anonymous namespace

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "AxleSceneMutationUnitTests (EJ-09 / EJ-10)" << std::endl;
    std::cout << "========================================" << std::endl;

    if (!TestParentChildRecursiveDestroy()) return 1;
    if (!TestChildUnparentingAndDestruction()) return 1;
    if (!TestScriptAndPhysicsTeardownOnDestroy()) return 1;
    if (!TestCommandBufferDestroyThenEditConflict()) return 1;
    if (!TestWrongSceneCollisionRejection()) return 1;
    if (!TestMultiThreadedRecordingAndPostJoinCommit()) return 1;
    if (!TestCommandBufferMerge()) return 1;

    // EJ-10 Tests
    if (!TestTransformEvaluationSerialVsParallel()) return 1;
    if (!TestTransformScaleInheritance()) return 1;
    if (!TestTransformCycleDetectionAndRepair()) return 1;
    if (!TestTransformRevisionProgression()) return 1;
    if (!TestTransformReadOnlyQueryConcurrency()) return 1;

    std::cout << "========================================" << std::endl;
    std::cout << "ALL SCENE MUTATION & TRANSFORM UNIT TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
