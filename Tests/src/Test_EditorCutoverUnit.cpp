#include <Axle/SimulationDriver.hpp>
#include <Axle/SimulationClock.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/ScenePhysicsBridge.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Components.hpp>
#include <iostream>
#include <cassert>
#include <cmath>

using namespace Axle;

static int g_PassCount = 0;
static int g_FailCount = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            g_FailCount++; \
        } else { \
            g_PassCount++; \
        } \
    } while(0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_NEAR(a, b, eps) EXPECT_TRUE(std::abs((a) - (b)) <= (eps))

void TestEditorSimulationDriverIntegration() {
    std::cout << "[RUN] TestEditorSimulationDriverIntegration..." << std::endl;

    auto editorScene = std::make_shared<Scene>();
    auto physicsScene = std::make_unique<PhysicsScene>();
    SimulationDriver driver(1.0f / 60.0f);

    auto actor = editorScene->CreateEntity("CubeActor");
    auto& tc = actor.GetComponent<TransformComponent>();
    tc.Translation = glm::vec3(0.0f, 10.0f, 0.0f);

    auto& cs = actor.AddComponent<CollisionShape3D>();
    cs.ShapeType = CollisionShapeType::Box;
    cs.HalfExtent = glm::vec3(0.5f);

    auto& rb = actor.AddComponent<RigidBody3D>();
    rb.MotionType = BodyMotionType::Dynamic;
    rb.Mass = 1.0f;
    rb.GravityFactor = 1.0f;

    ScenePhysicsBridge::CreateBodyForEntity(*editorScene, (entt::entity)actor, physicsScene.get());

    // 1. Edit Mode: driver is paused/reset; AdvanceFrame does not run fixed ticks
    driver.Pause();
    EXPECT_TRUE(driver.IsPaused());
    uint32_t editTicks = driver.AdvanceFrame(1.0f / 60.0f, *editorScene, physicsScene.get());
    EXPECT_EQ(editTicks, 0u);
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 0ull);

    // 2. Play Mode: clone ephemeral runtime scene and resume driver
    auto runtimeScene = Scene::Copy(editorScene);
    driver.Reset();
    driver.Resume();
    EXPECT_FALSE(driver.IsPaused());

    // Run 30 frames at 60 Hz (0.5 seconds)
    for (int i = 0; i < 30; ++i) {
        uint32_t t = driver.AdvanceFrame(1.0f / 60.0f, *runtimeScene, physicsScene.get());
        EXPECT_EQ(t, 1u);
    }
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 30ull);

    // 3. Pause Mode: pause simulation
    driver.Pause();
    EXPECT_TRUE(driver.IsPaused());
    for (int i = 0; i < 10; ++i) {
        uint32_t pt = driver.AdvanceFrame(1.0f / 60.0f, *runtimeScene, physicsScene.get());
        EXPECT_EQ(pt, 0u);
    }
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 30ull);

    // 4. Step 1 Frame: single-step execution
    driver.RequestSingleStep();
    uint32_t st = driver.AdvanceFrame(1.0f / 60.0f, *runtimeScene, physicsScene.get());
    EXPECT_EQ(st, 1u);
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 31ull);
    EXPECT_TRUE(driver.IsPaused());

    // Next frame remains paused
    uint32_t nextPt = driver.AdvanceFrame(1.0f / 60.0f, *runtimeScene, physicsScene.get());
    EXPECT_EQ(nextPt, 0u);
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 31ull);

    // 5. Stop Mode: teardown runtime scene, restore editor scene, reset driver
    runtimeScene->OnRuntimeStop(physicsScene.get());
    runtimeScene.reset();
    driver.Reset();
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 0ull);

    std::cout << "[PASS] TestEditorSimulationDriverIntegration" << std::endl;
}

void TestEditorZeroDriftRestoration() {
    std::cout << "[RUN] TestEditorZeroDriftRestoration..." << std::endl;

    auto editorScene = std::make_shared<Scene>();
    auto physicsScene = std::make_unique<PhysicsScene>();
    SimulationDriver driver(1.0f / 60.0f);

    const glm::vec3 initialPos(12.5f, 25.0f, -8.0f);
    const glm::quat initialRot = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));

    auto entity = editorScene->CreateEntity("ZeroDriftBox");
    auto& tc = entity.GetComponent<TransformComponent>();
    tc.Translation = initialPos;
    tc.Rotation = initialRot;
    editorScene->UpdateWorldTransforms();

    auto& cs = entity.AddComponent<CollisionShape3D>();
    cs.ShapeType = CollisionShapeType::Box;
    cs.HalfExtent = glm::vec3(0.5f);

    auto& rb = entity.AddComponent<RigidBody3D>();
    rb.MotionType = BodyMotionType::Dynamic;
    rb.Mass = 5.0f;
    rb.GravityFactor = 1.0f;

    ScenePhysicsBridge::CreateBodyForEntity(*editorScene, (entt::entity)entity, physicsScene.get());

    // Record initial poses
    editorScene->RestorePhysicsBodies(physicsScene.get());

    // Start simulation
    auto runtimeScene = Scene::Copy(editorScene);
    driver.Reset();
    driver.Resume();

    // Advance 60 frames (1.0 second): body falls under gravity
    for (int i = 0; i < 60; ++i) {
        driver.AdvanceFrame(1.0f / 60.0f, *runtimeScene, physicsScene.get());
    }

    // Runtime scene entity has moved
    auto runtimeEntity = runtimeScene->GetEntityByUUID(entity.GetUUID());
    EXPECT_TRUE(bool(runtimeEntity));
    glm::mat4 movedWorld = runtimeScene->GetWorldTransform((entt::entity)runtimeEntity);
    float movedY = movedWorld[3].y;
    EXPECT_TRUE(movedY < initialPos.y - 1.0f);

    // Stop simulation: restore pristine editor scene and restore bodies
    runtimeScene->OnRuntimeStop(physicsScene.get());
    runtimeScene.reset();
    driver.Reset();

    // Zero-drift restoration to pristine editor transforms
    editorScene->RestorePhysicsBodies(physicsScene.get());
    editorScene->UpdateWorldTransforms();

    glm::mat4 restoredWorld = editorScene->GetWorldTransform((entt::entity)entity);
    glm::vec3 restoredPos = glm::vec3(restoredWorld[3]);
    EXPECT_NEAR(restoredPos.x, initialPos.x, 1e-4f);
    EXPECT_NEAR(restoredPos.y, initialPos.y, 1e-4f);
    EXPECT_NEAR(restoredPos.z, initialPos.z, 1e-4f);

    glm::vec3 restoredPhysicsPos = physicsScene->GetBodyPosition(rb.RuntimeBodyID);
    EXPECT_NEAR(restoredPhysicsPos.x, initialPos.x, 1e-3f);
    EXPECT_NEAR(restoredPhysicsPos.y, initialPos.y, 1e-3f);
    EXPECT_NEAR(restoredPhysicsPos.z, initialPos.z, 1e-3f);

    std::cout << "[PASS] TestEditorZeroDriftRestoration (restored=" << restoredPos.y << " initial=" << initialPos.y << ")" << std::endl;
}

void TestEditorSpeedScaling() {
    std::cout << "[RUN] TestEditorSpeedScaling..." << std::endl;

    auto scene = std::make_shared<Scene>();
    auto physicsScene = std::make_unique<PhysicsScene>();
    SimulationDriver driver(1.0f / 60.0f);

    // Test 0.5x speed: 60 frames of 1/60s produces 30 fixed ticks
    driver.Reset();
    driver.SetSpeed(0.5f);
    driver.Resume();
    for (int i = 0; i < 60; ++i) {
        driver.AdvanceFrame(1.0f / 60.0f, *scene, physicsScene.get());
    }
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 30ull);

    // Test 2.0x speed: 60 frames of 1/60s produces 120 fixed ticks
    driver.Reset();
    driver.SetSpeed(2.0f);
    driver.Resume();
    for (int i = 0; i < 60; ++i) {
        driver.AdvanceFrame(1.0f / 60.0f, *scene, physicsScene.get());
    }
    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 120ull);

    std::cout << "[PASS] TestEditorSpeedScaling" << std::endl;
}

void TestEditorPrivatePreviewClockIsolation() {
    std::cout << "[RUN] TestEditorPrivatePreviewClockIsolation..." << std::endl;

    SimulationDriver editorDriver(1.0f / 60.0f);
    SimulationClock vfxPreviewClock(1.0f / 60.0f);

    // Editor main simulation is paused
    editorDriver.Pause();
    EXPECT_TRUE(editorDriver.IsPaused());

    // Advance 60 frames
    uint32_t editorTicks = 0;
    uint32_t previewTicks = 0;
    auto scene = std::make_shared<Scene>();
    for (int i = 0; i < 60; ++i) {
        editorTicks += editorDriver.AdvanceFrame(1.0f / 60.0f, *scene, nullptr);
        previewTicks += vfxPreviewClock.Advance(1.0f / 60.0f);
    }

    EXPECT_EQ(editorTicks, 0u);
    EXPECT_EQ(previewTicks, 60u);

    std::cout << "[PASS] TestEditorPrivatePreviewClockIsolation" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "===========================================" << std::endl;
    std::cout << "   Axle Editor Cutover Unit Test Suite     " << std::endl;
    std::cout << "===========================================" << std::endl;

    JobSystem::Init(2);
    PhysicsEngine::Init();

    TestEditorSimulationDriverIntegration();
    TestEditorZeroDriftRestoration();
    TestEditorSpeedScaling();
    TestEditorPrivatePreviewClockIsolation();

    PhysicsEngine::Shutdown();
    JobSystem::Shutdown();

    std::cout << "===========================================" << std::endl;
    if (g_FailCount == 0) {
        std::cout << "   ALL EDITOR CUTOVER TESTS PASSED (100%)  " << std::endl;
        std::cout << "===========================================" << std::endl;
        return 0;
    } else {
        std::cerr << "   FAILED: " << g_FailCount << " test(s) failed." << std::endl;
        std::cout << "===========================================" << std::endl;
        return 1;
    }
}
