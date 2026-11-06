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

} // anonymous namespace

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "AxleSceneMutationUnitTests (EJ-09)" << std::endl;
    std::cout << "========================================" << std::endl;

    if (!TestParentChildRecursiveDestroy()) return 1;
    if (!TestChildUnparentingAndDestruction()) return 1;
    if (!TestScriptAndPhysicsTeardownOnDestroy()) return 1;
    if (!TestCommandBufferDestroyThenEditConflict()) return 1;
    if (!TestWrongSceneCollisionRejection()) return 1;
    if (!TestMultiThreadedRecordingAndPostJoinCommit()) return 1;
    if (!TestCommandBufferMerge()) return 1;

    std::cout << "========================================" << std::endl;
    std::cout << "ALL SCENE MUTATION UNIT TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
