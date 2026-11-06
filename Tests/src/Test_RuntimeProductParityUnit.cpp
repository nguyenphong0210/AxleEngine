#include <Axle/SimulationDriver.hpp>
#include <Axle/SimulationClock.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/ScenePhysicsBridge.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Components.hpp>
#include <iostream>
#include <vector>
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

namespace {

    class ParityCountingScript : public ScriptableEntity {
    public:
        int UpdateCount = 0;
        int FixedUpdateCount = 0;
        int LateUpdateCount = 0;

        void OnUpdate(Timestep /*ts*/) override {
            ++UpdateCount;
        }

        void OnFixedUpdate(Timestep /*ts*/) override {
            ++FixedUpdateCount;
        }

        void OnLateUpdate(Timestep /*ts*/) override {
            ++LateUpdateCount;
        }
    };

    class ECSJ05TelemetryScript : public ScriptableEntity {
    public:
        PhysicsScene* Physics = nullptr;
        bool ApplyImpulseOnTick1 = false;
        glm::vec3 PreStepPos{ 0.0f };
        glm::vec3 ObservedLatePos{ 0.0f };
        int FixedTicks = 0;
        int LateTicks = 0;

        void OnFixedUpdate(Timestep /*ts*/) override {
            ++FixedTicks;
            if (FixedTicks == 1 && ApplyImpulseOnTick1 && Physics) {
                if (HasComponent<RigidBody3D>()) {
                    auto& rb = GetComponent<RigidBody3D>();
                    PreStepPos = Physics->GetBodyPosition(rb.RuntimeBodyID);
                    // Apply an upward velocity impulse
                    Physics->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f, 20.0f, 0.0f));
                }
            }
        }

        void OnLateUpdate(Timestep /*ts*/) override {
            ++LateTicks;
            if (LateTicks == 1) {
                // Read synchronized transform in the exact same tick
                glm::mat4 worldMat = GetWorldTransform();
                ObservedLatePos = glm::vec3(worldMat[3]);
            }
        }
    };

} // namespace

// =========================================================================
// 1. Simulation Parity Across Editor and Runtime Drivers
// =========================================================================
void TestSimulationParitySameTickSequence() {
    std::cout << "[RUN] TestSimulationParitySameTickSequence..." << std::endl;

    auto editorScene = std::make_shared<Scene>();
    auto editorPhysics = std::make_unique<PhysicsScene>();
    SimulationDriver editorDriver(1.0f / 60.0f);

    auto runtimeScene = std::make_shared<Scene>();
    auto runtimePhysics = std::make_unique<PhysicsScene>();
    SimulationDriver runtimeDriver(1.0f / 60.0f);

    // Create identical dynamic entities in both scenes
    const glm::vec3 initialPos(5.0f, 30.0f, -2.0f);

    auto eActor = editorScene->CreateEntity("DynamicActor");
    eActor.GetComponent<TransformComponent>().Translation = initialPos;
    editorScene->UpdateWorldTransforms();
    auto& eCs = eActor.AddComponent<CollisionShape3D>();
    eCs.ShapeType = CollisionShapeType::Box;
    eCs.HalfExtent = glm::vec3(0.5f);
    auto& eRb = eActor.AddComponent<RigidBody3D>();
    eRb.MotionType = BodyMotionType::Dynamic;
    eRb.Mass = 2.0f;
    eRb.GravityFactor = 1.0f;
    ScenePhysicsBridge::CreateBodyForEntity(*editorScene, (entt::entity)eActor, editorPhysics.get());
    auto& eNsc = eActor.AddComponent<NativeScriptComponent>();
    auto* eScript = new ParityCountingScript();
    eScript->SetEntity(eActor);
    eNsc.Instance = eScript;
    eNsc.DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };

    auto rActor = runtimeScene->CreateEntity("DynamicActor");
    rActor.GetComponent<TransformComponent>().Translation = initialPos;
    runtimeScene->UpdateWorldTransforms();
    auto& rCs = rActor.AddComponent<CollisionShape3D>();
    rCs.ShapeType = CollisionShapeType::Box;
    rCs.HalfExtent = glm::vec3(0.5f);
    auto& rRb = rActor.AddComponent<RigidBody3D>();
    rRb.MotionType = BodyMotionType::Dynamic;
    rRb.Mass = 2.0f;
    rRb.GravityFactor = 1.0f;
    ScenePhysicsBridge::CreateBodyForEntity(*runtimeScene, (entt::entity)rActor, runtimePhysics.get());
    auto& rNsc = rActor.AddComponent<NativeScriptComponent>();
    auto* rScript = new ParityCountingScript();
    rScript->SetEntity(rActor);
    rNsc.Instance = rScript;
    rNsc.DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };

    // Both drivers start active
    editorDriver.Reset();
    editorDriver.Resume();
    runtimeDriver.Reset();
    runtimeDriver.Resume();

    // Advance both through an identical stream of diverse frame deltas (60Hz, 30Hz, 120Hz, 144Hz)
    const std::vector<float> frameDts = {
        1.0f / 60.0f, 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 120.0f,
        1.0f / 120.0f, 1.0f / 144.0f, 1.0f / 144.0f, 1.0f / 60.0f,
        1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 60.0f
    };

    for (float dt : frameDts) {
        uint32_t eTicks = editorDriver.AdvanceFrame(dt, *editorScene, editorPhysics.get());
        uint32_t rTicks = runtimeDriver.AdvanceFrame(dt, *runtimeScene, runtimePhysics.get());
        EXPECT_EQ(eTicks, rTicks);
    }

    // Diagnostics & tick parity
    EXPECT_EQ(editorDriver.GetClock().GetDiagnostics().TotalTicksExecuted,
              runtimeDriver.GetClock().GetDiagnostics().TotalTicksExecuted);

    // Script callback parity
    EXPECT_EQ(eScript->UpdateCount, rScript->UpdateCount);
    EXPECT_EQ(eScript->FixedUpdateCount, rScript->FixedUpdateCount);
    EXPECT_EQ(eScript->LateUpdateCount, rScript->LateUpdateCount);

    // Scene transform parity
    glm::mat4 eWorld = editorScene->GetWorldTransform((entt::entity)eActor);
    glm::mat4 rWorld = runtimeScene->GetWorldTransform((entt::entity)rActor);
    glm::vec3 ePos(eWorld[3]);
    glm::vec3 rPos(rWorld[3]);
    EXPECT_NEAR(ePos.x, rPos.x, 1e-4f);
    EXPECT_NEAR(ePos.y, rPos.y, 1e-4f);
    EXPECT_NEAR(ePos.z, rPos.z, 1e-4f);

    // Physics body velocity parity
    glm::vec3 eVel = editorPhysics->GetLinearVelocity(eRb.RuntimeBodyID);
    glm::vec3 rVel = runtimePhysics->GetLinearVelocity(rRb.RuntimeBodyID);
    EXPECT_NEAR(eVel.x, rVel.x, 1e-4f);
    EXPECT_NEAR(eVel.y, rVel.y, 1e-4f);
    EXPECT_NEAR(eVel.z, rVel.z, 1e-4f);

    std::cout << "[PASS] TestSimulationParitySameTickSequence (ticks="
              << editorDriver.GetClock().GetDiagnostics().TotalTicksExecuted
              << " ePos.y=" << ePos.y << " rPos.y=" << rPos.y << ")" << std::endl;
}

// =========================================================================
// 2. Resolution of ECSJ-05 (Same-Tick Controls-to-Physics Order Parity)
// =========================================================================
void TestECSJ05ControlsToTelemetryParity() {
    std::cout << "[RUN] TestECSJ05ControlsToTelemetryParity..." << std::endl;

    auto editorScene = std::make_shared<Scene>();
    auto editorPhysics = std::make_unique<PhysicsScene>();
    SimulationDriver editorDriver(1.0f / 60.0f);

    auto runtimeScene = std::make_shared<Scene>();
    auto runtimePhysics = std::make_unique<PhysicsScene>();
    SimulationDriver runtimeDriver(1.0f / 60.0f);

    const glm::vec3 startPos(0.0f, 10.0f, 0.0f);

    // Setup Editor entity
    auto eActor = editorScene->CreateEntity("ECSJ05_Editor");
    eActor.GetComponent<TransformComponent>().Translation = startPos;
    editorScene->UpdateWorldTransforms();
    auto& eCs = eActor.AddComponent<CollisionShape3D>();
    eCs.ShapeType = CollisionShapeType::Box;
    eCs.HalfExtent = glm::vec3(0.5f);
    auto& eRb = eActor.AddComponent<RigidBody3D>();
    eRb.MotionType = BodyMotionType::Dynamic;
    eRb.Mass = 1.0f;
    eRb.GravityFactor = 1.0f;
    ScenePhysicsBridge::CreateBodyForEntity(*editorScene, (entt::entity)eActor, editorPhysics.get());
    auto& eNsc = eActor.AddComponent<NativeScriptComponent>();
    auto* eScript = new ECSJ05TelemetryScript();
    eScript->SetEntity(eActor);
    eScript->Physics = editorPhysics.get();
    eScript->ApplyImpulseOnTick1 = true;
    eNsc.Instance = eScript;
    eNsc.DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };

    // Setup Runtime entity
    auto rActor = runtimeScene->CreateEntity("ECSJ05_Runtime");
    rActor.GetComponent<TransformComponent>().Translation = startPos;
    runtimeScene->UpdateWorldTransforms();
    auto& rCs = rActor.AddComponent<CollisionShape3D>();
    rCs.ShapeType = CollisionShapeType::Box;
    rCs.HalfExtent = glm::vec3(0.5f);
    auto& rRb = rActor.AddComponent<RigidBody3D>();
    rRb.MotionType = BodyMotionType::Dynamic;
    rRb.Mass = 1.0f;
    rRb.GravityFactor = 1.0f;
    ScenePhysicsBridge::CreateBodyForEntity(*runtimeScene, (entt::entity)rActor, runtimePhysics.get());
    auto& rNsc = rActor.AddComponent<NativeScriptComponent>();
    auto* rScript = new ECSJ05TelemetryScript();
    rScript->SetEntity(rActor);
    rScript->Physics = runtimePhysics.get();
    rScript->ApplyImpulseOnTick1 = true;
    rNsc.Instance = rScript;
    rNsc.DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };

    // Advance exactly 1 frame at 60 Hz (1 tick)
    editorDriver.Reset();
    editorDriver.Resume();
    runtimeDriver.Reset();
    runtimeDriver.Resume();

    editorDriver.AdvanceFrame(1.0f / 60.0f, *editorScene, editorPhysics.get());
    runtimeDriver.AdvanceFrame(1.0f / 60.0f, *runtimeScene, runtimePhysics.get());

    // In BOTH drivers: FixedControls applied the impulse, Physics stepped,
    // PostStepSync synchronized the transform, and LatePresentation observed the post-step pose in the exact same tick!
    EXPECT_EQ(eScript->FixedTicks, 1);
    EXPECT_EQ(eScript->LateTicks, 1);
    EXPECT_EQ(rScript->FixedTicks, 1);
    EXPECT_EQ(rScript->LateTicks, 1);

    // Pre-step pos was 10.0f; upward impulse of 20 m/s for 1/60s minus gravity moves body upward
    EXPECT_TRUE(eScript->ObservedLatePos.y > eScript->PreStepPos.y + 0.1f);
    EXPECT_TRUE(rScript->ObservedLatePos.y > rScript->PreStepPos.y + 0.1f);

    // Exact parity between Editor and Runtime: 0-tick latency disparity
    EXPECT_NEAR(eScript->ObservedLatePos.y, rScript->ObservedLatePos.y, 1e-4f);

    std::cout << "[PASS] TestECSJ05ControlsToTelemetryParity (Editor y="
              << eScript->ObservedLatePos.y << " Runtime y=" << rScript->ObservedLatePos.y << ")" << std::endl;
}

// =========================================================================
// 3. Paused State and Input Isolation Parity
// =========================================================================
void TestRuntimePauseResumeInputIsolation() {
    std::cout << "[RUN] TestRuntimePauseResumeInputIsolation..." << std::endl;

    auto scene = std::make_shared<Scene>();
    auto physics = std::make_unique<PhysicsScene>();
    SimulationDriver driver(1.0f / 60.0f);

    auto actor = scene->CreateEntity("StaticDuringPause");
    actor.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 15.0f, 0.0f);
    scene->UpdateWorldTransforms();
    auto& cs = actor.AddComponent<CollisionShape3D>();
    cs.ShapeType = CollisionShapeType::Box;
    cs.HalfExtent = glm::vec3(0.5f);
    auto& rb = actor.AddComponent<RigidBody3D>();
    rb.MotionType = BodyMotionType::Dynamic;
    rb.Mass = 1.0f;
    ScenePhysicsBridge::CreateBodyForEntity(*scene, (entt::entity)actor, physics.get());

    // Start simulation
    driver.Reset();
    driver.Resume();

    // Advance 10 frames
    for (int i = 0; i < 10; ++i) {
        driver.AdvanceFrame(1.0f / 60.0f, *scene, physics.get());
    }

    glm::mat4 midWorld = scene->GetWorldTransform((entt::entity)actor);
    float midY = midWorld[3].y;
    EXPECT_TRUE(midY < 15.0f);

    // Pause simulation (mimicking Escape key or pause menu in RuntimeLayer)
    driver.Pause();
    EXPECT_TRUE(driver.IsPaused());

    // Advance 30 frames while paused
    for (int i = 0; i < 30; ++i) {
        uint32_t ticks = driver.AdvanceFrame(1.0f / 60.0f, *scene, physics.get());
        EXPECT_EQ(ticks, 0u);
    }

    // Body did not move during pause
    glm::mat4 pauseWorld = scene->GetWorldTransform((entt::entity)actor);
    EXPECT_NEAR(pauseWorld[3].y, midY, 1e-5f);

    // Resume simulation
    driver.Resume();
    EXPECT_FALSE(driver.IsPaused());

    // First frame after resume should execute exactly 1 tick, no catch-up burst!
    uint32_t resumeTicks = driver.AdvanceFrame(1.0f / 60.0f, *scene, physics.get());
    EXPECT_EQ(resumeTicks, 1u);

    glm::mat4 resumeWorld = scene->GetWorldTransform((entt::entity)actor);
    EXPECT_TRUE(resumeWorld[3].y < midY);

    std::cout << "[PASS] TestRuntimePauseResumeInputIsolation" << std::endl;
}

// =========================================================================
// 4. Speed Scaling Parity Across Editor and Runtime
// =========================================================================
void TestRuntimeSpeedScalingParity() {
    std::cout << "[RUN] TestRuntimeSpeedScalingParity..." << std::endl;

    const std::vector<float> speeds = { 0.5f, 1.0f, 2.0f };

    for (float speed : speeds) {
        SimulationDriver editorDriver(1.0f / 60.0f);
        SimulationDriver runtimeDriver(1.0f / 60.0f);

        editorDriver.SetSpeed(speed);
        runtimeDriver.SetSpeed(speed);

        auto sceneA = std::make_shared<Scene>();
        auto sceneB = std::make_shared<Scene>();

        editorDriver.Resume();
        runtimeDriver.Resume();

        uint32_t totalTicksA = 0;
        uint32_t totalTicksB = 0;

        for (int i = 0; i < 60; ++i) {
            totalTicksA += editorDriver.AdvanceFrame(1.0f / 60.0f, *sceneA, nullptr);
            totalTicksB += runtimeDriver.AdvanceFrame(1.0f / 60.0f, *sceneB, nullptr);
        }

        EXPECT_EQ(totalTicksA, totalTicksB);

        if (speed == 0.5f) {
            EXPECT_EQ(totalTicksA, 30u);
        } else if (speed == 1.0f) {
            EXPECT_EQ(totalTicksA, 60u);
        } else if (speed == 2.0f) {
            EXPECT_EQ(totalTicksA, 120u);
        }
    }

    std::cout << "[PASS] TestRuntimeSpeedScalingParity" << std::endl;
}

// =========================================================================
// 5. Clean Teardown and Physics Body Deregistration Parity
// =========================================================================
void TestRuntimeCleanTeardownAndPhysicsDeregistration() {
    std::cout << "[RUN] TestRuntimeCleanTeardownAndPhysicsDeregistration..." << std::endl;

    auto scene = std::make_shared<Scene>();
    auto physics = std::make_unique<PhysicsScene>();
    SimulationDriver driver(1.0f / 60.0f);

    std::vector<Entity> createdActors;
    std::vector<BodyID> createdBodies;

    for (int i = 0; i < 5; ++i) {
        auto actor = scene->CreateEntity("TeardownActor_" + std::to_string(i));
        actor.GetComponent<TransformComponent>().Translation = glm::vec3((float)i, 10.0f, 0.0f);
        scene->UpdateWorldTransforms();
        auto& cs = actor.AddComponent<CollisionShape3D>();
        cs.ShapeType = CollisionShapeType::Box;
        cs.HalfExtent = glm::vec3(0.5f);
        auto& rb = actor.AddComponent<RigidBody3D>();
        rb.MotionType = BodyMotionType::Dynamic;
        rb.Mass = 1.0f;
        ScenePhysicsBridge::CreateBodyForEntity(*scene, (entt::entity)actor, physics.get());
        EXPECT_TRUE(physics->IsBodyValid(rb.RuntimeBodyID));
        createdBodies.push_back(rb.RuntimeBodyID);
        createdActors.push_back(actor);
    }

    // Run simulation
    driver.Reset();
    driver.Resume();
    for (int i = 0; i < 10; ++i) {
        driver.AdvanceFrame(1.0f / 60.0f, *scene, physics.get());
    }

    // Teardown simulation: stop runtime and destroy entities with physics teardown
    driver.Reset();
    scene->OnRuntimeStop(physics.get());
    for (auto actor : createdActors) {
        scene->DestroyEntity(actor, physics.get());
    }

    // Verify all created bodies are now released/deregistered from physics scene
    for (const auto& bodyID : createdBodies) {
        EXPECT_FALSE(physics->IsBodyValid(bodyID));
    }

    std::cout << "[PASS] TestRuntimeCleanTeardownAndPhysicsDeregistration (5 bodies verified destroyed)" << std::endl;
}

int main(int /*argc*/, char** /*argv*/) {
    std::cout << "===========================================" << std::endl;
    std::cout << "   Axle Runtime Product Parity Test Suite  " << std::endl;
    std::cout << "===========================================" << std::endl;

    JobSystem::Init(2);
    PhysicsEngine::Init();

    TestSimulationParitySameTickSequence();
    TestECSJ05ControlsToTelemetryParity();
    TestRuntimePauseResumeInputIsolation();
    TestRuntimeSpeedScalingParity();
    TestRuntimeCleanTeardownAndPhysicsDeregistration();

    PhysicsEngine::Shutdown();
    JobSystem::Shutdown();

    std::cout << "===========================================" << std::endl;
    if (g_FailCount == 0) {
        std::cout << "   ALL PRODUCT PARITY TESTS PASSED (100%)  " << std::endl;
        std::cout << "===========================================" << std::endl;
        return 0;
    } else {
        std::cout << "   FAILED: " << g_FailCount << " test(s) failed.   " << std::endl;
        std::cout << "===========================================" << std::endl;
        return 1;
    }
}
