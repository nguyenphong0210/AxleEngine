#include <Axle/SimulationClock.hpp>
#include <Axle/SimulationInput.hpp>
#include <Axle/SimulationDriver.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/ScenePhysicsBridge.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Components.hpp>
#include <iostream>
#include <cassert>
#include <cmath>
#include <limits>

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

void TestClockDtValidation() {
    std::cout << "[RUN] TestClockDtValidation..." << std::endl;
    SimulationClock clock(1.0f / 60.0f, 0.2f);

    // Negative dt
    uint32_t ticks1 = clock.Advance(-0.016f);
    EXPECT_EQ(ticks1, 0u);
    EXPECT_EQ(clock.GetDiagnostics().InvalidDtDrops, 1ull);

    // NaN dt
    uint32_t ticks2 = clock.Advance(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(ticks2, 0u);
    EXPECT_EQ(clock.GetDiagnostics().InvalidDtDrops, 2ull);

    // Inf dt
    uint32_t ticks3 = clock.Advance(std::numeric_limits<float>::infinity());
    EXPECT_EQ(ticks3, 0u);
    EXPECT_EQ(clock.GetDiagnostics().InvalidDtDrops, 3ull);

    // Zero dt
    uint32_t ticks4 = clock.Advance(0.0f);
    EXPECT_EQ(ticks4, 0u);
    EXPECT_EQ(clock.GetDiagnostics().InvalidDtDrops, 3ull); // Zero is valid idle dt, not an invalid drop

    // Valid dt
    uint32_t ticks5 = clock.Advance(1.0f / 60.0f);
    EXPECT_EQ(ticks5, 1u);
    EXPECT_EQ(clock.GetDiagnostics().TotalTicksExecuted, 1ull);
    EXPECT_EQ(clock.GetDiagnostics().TotalFrames, 5ull);

    std::cout << "[PASS] TestClockDtValidation" << std::endl;
}

void TestClockOverloadCap() {
    std::cout << "[RUN] TestClockOverloadCap..." << std::endl;
    // 60Hz fixed tick = ~0.0166667s, MaxAccumulator = 0.2s (~12 ticks)
    SimulationClock clock(1.0f / 60.0f, 0.2f);

    // Massive 2.0s hitch
    uint32_t ticks = clock.Advance(2.0f);
    EXPECT_EQ(clock.GetDiagnostics().OverloadDrops, 1ull);
    // Bounded work: eight ticks, excess whole ticks dropped with a retained remainder.
    EXPECT_EQ(ticks, 8u);
    EXPECT_EQ(clock.GetDiagnostics().TotalTicksExecuted, 8ull);
    EXPECT_EQ(clock.GetDiagnostics().DroppedTicks, 4ull);

    // Test Drop policy
    clock.Reset();
    clock.SetOverloadPolicy(ClockOverloadPolicy::Drop);
    uint32_t dropTicks = clock.Advance(1.5f);
    EXPECT_EQ(dropTicks, 0u);
    EXPECT_EQ(clock.GetDiagnostics().OverloadDrops, 1ull);
    EXPECT_EQ(clock.GetAccumulator(), 0.0f);

    std::cout << "[PASS] TestClockOverloadCap" << std::endl;
}

void TestClockFramePartitions() {
    std::cout << "[RUN] TestClockFramePartitions (SIM-02 30/60/144 Hz)..." << std::endl;
    const float fixedDt = 1.0f / 60.0f;

    // 1. 60 Hz frame rate (60 frames of 1/60s)
    {
        SimulationClock clock60(fixedDt, 0.5f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 60; ++i) {
            uint32_t t = clock60.Advance(1.0f / 60.0f);
            EXPECT_EQ(t, 1u);
            totalTicks += t;
        }
        EXPECT_EQ(totalTicks, 60u);
        EXPECT_EQ(clock60.GetDiagnostics().TotalTicksExecuted, 60ull);
    }

    // 2. 30 Hz frame rate (30 frames of 1/30s)
    {
        SimulationClock clock30(fixedDt, 0.5f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 30; ++i) {
            uint32_t t = clock30.Advance(1.0f / 30.0f);
            EXPECT_EQ(t, 2u);
            totalTicks += t;
        }
        EXPECT_EQ(totalTicks, 60u);
        EXPECT_EQ(clock30.GetDiagnostics().TotalTicksExecuted, 60ull);
    }

    // 3. 144 Hz frame rate (144 frames of 1/144s)
    {
        SimulationClock clock144(fixedDt, 0.5f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 144; ++i) {
            uint32_t t = clock144.Advance(1.0f / 144.0f);
            EXPECT_TRUE(t == 0u || t == 1u);
            totalTicks += t;
        }
        EXPECT_EQ(totalTicks, 60u);
        EXPECT_EQ(clock144.GetDiagnostics().TotalTicksExecuted, 60ull);
    }

    std::cout << "[PASS] TestClockFramePartitions" << std::endl;
}

void TestClockSpeedScaling() {
    std::cout << "[RUN] TestClockSpeedScaling..." << std::endl;
    const float fixedDt = 1.0f / 60.0f;

    // Speed 0.0x
    {
        SimulationClock clock(fixedDt, 1.0f);
        clock.SetSpeed(0.0f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 60; ++i) totalTicks += clock.Advance(1.0f / 60.0f);
        EXPECT_EQ(totalTicks, 0u);
    }

    // Speed 0.5x
    {
        SimulationClock clock(fixedDt, 1.0f);
        clock.SetSpeed(0.5f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 60; ++i) totalTicks += clock.Advance(1.0f / 60.0f);
        EXPECT_EQ(totalTicks, 30u);
    }

    // Speed 1.0x
    {
        SimulationClock clock(fixedDt, 1.0f);
        clock.SetSpeed(1.0f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 60; ++i) totalTicks += clock.Advance(1.0f / 60.0f);
        EXPECT_EQ(totalTicks, 60u);
    }

    // Speed 2.0x
    {
        SimulationClock clock(fixedDt, 1.0f);
        clock.SetSpeed(2.0f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 60; ++i) totalTicks += clock.Advance(1.0f / 60.0f);
        EXPECT_EQ(totalTicks, 120u);
    }

    // Speed 4.0x
    {
        SimulationClock clock(fixedDt, 1.0f);
        clock.SetSpeed(4.0f);
        uint32_t totalTicks = 0;
        for (int i = 0; i < 60; ++i) totalTicks += clock.Advance(1.0f / 60.0f);
        EXPECT_EQ(totalTicks, 240u);
    }

    std::cout << "[PASS] TestClockSpeedScaling" << std::endl;
}

void TestClockPauseResumeAndSingleStep() {
    std::cout << "[RUN] TestClockPauseResumeAndSingleStep (SIM-03)..." << std::endl;
    SimulationClock clock(1.0f / 60.0f, 0.5f);

    clock.Pause();
    EXPECT_TRUE(clock.IsPaused());

    // Advancing while paused returns 0 ticks
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(clock.Advance(1.0f / 60.0f), 0u);
    }
    EXPECT_EQ(clock.GetDiagnostics().TotalTicksExecuted, 0ull);

    // Single step request
    clock.RequestSingleStep();
    EXPECT_TRUE(clock.IsSingleStepPending());

    // Next frame executes exactly 1 tick
    uint32_t stepTick = clock.Advance(1.0f / 60.0f);
    EXPECT_EQ(stepTick, 1u);
    EXPECT_FALSE(clock.IsSingleStepPending());
    EXPECT_TRUE(clock.IsPaused());

    // Following frame returns 0 ticks
    EXPECT_EQ(clock.Advance(1.0f / 60.0f), 0u);

    // Resume clock: starts accumulating fresh without catch-up burst
    clock.Resume();
    EXPECT_FALSE(clock.IsPaused());
    EXPECT_EQ(clock.Advance(1.0f / 60.0f), 1u);

    // Reset
    clock.Reset();
    EXPECT_EQ(clock.GetDiagnostics().TotalTicksExecuted, 0ull);
    EXPECT_EQ(clock.GetAccumulator(), 0.0f);

    std::cout << "[PASS] TestClockPauseResumeAndSingleStep" << std::endl;
}

void TestInputSnapshotZeroOneMultipleTicks() {
    std::cout << "[RUN] TestInputSnapshotZeroOneMultipleTicks (SIM-05)..." << std::endl;
    SimulationInputAdapter adapter;

    // Case 1: Key press during 0-tick frame (e.g. 144Hz)
    adapter.InjectKeyPress(KeyCode::Space);
    adapter.PrepareTickSnapshots(0); // 0 ticks this frame
    EXPECT_TRUE(adapter.GetHardwareState().IsKeyDown(KeyCode::Space));

    // Next frame has 1 tick: key press edge must be consumed here
    adapter.PrepareTickSnapshots(1);
    adapter.ActivateTickContext(0);
    const auto& snap0 = adapter.GetCurrentSnapshot();
    EXPECT_TRUE(snap0.IsKeyDown(KeyCode::Space));
    EXPECT_TRUE(snap0.IsKeyJustPressed(KeyCode::Space));
    EXPECT_TRUE(Input::IsKeyPressed(KeyCode::Space)); // Verified via scoped InputContext!
    adapter.DeactivateTickContext();

    // Next frame with 1 tick: key is still down, but JustPressed is false
    adapter.PrepareTickSnapshots(1);
    adapter.ActivateTickContext(0);
    const auto& snapNext = adapter.GetCurrentSnapshot();
    EXPECT_TRUE(snapNext.IsKeyDown(KeyCode::Space));
    EXPECT_FALSE(snapNext.IsKeyJustPressed(KeyCode::Space));
    adapter.DeactivateTickContext();

    // Case 2: Multi-tick frame (30Hz, 2 ticks)
    adapter.InjectKeyPress(KeyCode::W);
    adapter.PrepareTickSnapshots(2);

    // Tick 0 sees JustPressed == true
    adapter.ActivateTickContext(0);
    EXPECT_TRUE(adapter.GetCurrentSnapshot().IsKeyDown(KeyCode::W));
    EXPECT_TRUE(adapter.GetCurrentSnapshot().IsKeyJustPressed(KeyCode::W));
    adapter.DeactivateTickContext();

    // Tick 1 (catch-up tick in same frame) sees JustPressed == false (edge consumed once!)
    adapter.ActivateTickContext(1);
    EXPECT_TRUE(adapter.GetCurrentSnapshot().IsKeyDown(KeyCode::W));
    EXPECT_FALSE(adapter.GetCurrentSnapshot().IsKeyJustPressed(KeyCode::W));
    adapter.DeactivateTickContext();

    std::cout << "[PASS] TestInputSnapshotZeroOneMultipleTicks" << std::endl;
}

void TestPausedInputIsolation() {
    std::cout << "[RUN] TestPausedInputIsolation (SIM-05)..." << std::endl;
    SimulationDriver driver;

    driver.Pause();
    EXPECT_TRUE(driver.IsPaused());

    // Inject UI click/key during pause
    driver.GetInputAdapter().InjectKeyPress(KeyCode::Enter);
    driver.GetInputAdapter().InjectMouseButtonPress(MouseCode::Button0);

    // Flush or Resume simulation
    driver.Resume();
    EXPECT_FALSE(driver.IsPaused());

    // Verify flushed state: gameplay receives no stale paused UI inputs
    driver.GetInputAdapter().PrepareTickSnapshots(1);
    driver.GetInputAdapter().ActivateTickContext(0);
    EXPECT_FALSE(driver.GetInputAdapter().GetCurrentSnapshot().IsKeyDown(KeyCode::Enter));
    EXPECT_FALSE(driver.GetInputAdapter().GetCurrentSnapshot().IsKeyJustPressed(KeyCode::Enter));
    EXPECT_FALSE(driver.GetInputAdapter().GetCurrentSnapshot().IsMouseButtonDown(MouseCode::Button0));
    driver.GetInputAdapter().DeactivateTickContext();

    std::cout << "[PASS] TestPausedInputIsolation" << std::endl;
}

void TestPrivatePreviewClockIndependence() {
    std::cout << "[RUN] TestPrivatePreviewClockIndependence..." << std::endl;
    SimulationClock mainGameClock(1.0f / 60.0f);
    SimulationClock previewClock(1.0f / 60.0f);

    mainGameClock.Pause();

    // Main clock produces 0 ticks
    uint32_t mainTicks = mainGameClock.Advance(1.0f / 60.0f);
    EXPECT_EQ(mainTicks, 0u);

    // Preview clock runs independently in real time
    uint32_t previewTicks = previewClock.Advance(1.0f / 60.0f);
    EXPECT_EQ(previewTicks, 1u);

    std::cout << "[PASS] TestPrivatePreviewClockIndependence" << std::endl;
}

void TestSimulationDriverEndToEnd() {
    std::cout << "[RUN] TestSimulationDriverEndToEnd..." << std::endl;
    Scene scene;
    scene.EnablePhaseTracing(true);

    PhysicsScene physicsScene;
    SimulationDriver driver(1.0f / 60.0f);

    auto entity = scene.CreateEntity("DynamicBox");
    auto& tc = entity.GetComponent<TransformComponent>();
    tc.Translation = glm::vec3(0.0f, 10.0f, 0.0f);

    auto& cs = entity.AddComponent<CollisionShape3D>();
    cs.ShapeType = CollisionShapeType::Box;
    cs.HalfExtent = glm::vec3(0.5f);

    auto& rb = entity.AddComponent<RigidBody3D>();
    rb.MotionType = BodyMotionType::Dynamic;
    rb.Mass = 1.0f;
    rb.GravityFactor = 1.0f;

    ScenePhysicsBridge::CreateBodyForEntity(scene, (entt::entity)entity, &physicsScene);

    // Advance 60 frames of 1/60s (1.0 second simulation)
    for (int i = 0; i < 60; ++i) {
        uint32_t ticks = driver.AdvanceFrame(1.0f / 60.0f, scene, &physicsScene);
        EXPECT_EQ(ticks, 1u);
    }

    EXPECT_EQ(driver.GetClock().GetDiagnostics().TotalTicksExecuted, 60ull);
    EXPECT_NEAR(driver.GetClock().GetDiagnostics().TotalSimulationTime, 1.0f, 1e-4f);

    // Body should have fallen under gravity
    glm::mat4 finalWorld = scene.GetWorldTransform((entt::entity)entity);
    float finalY = finalWorld[3].y;
    EXPECT_TRUE(finalY < 10.0f);

    std::cout << "[PASS] TestSimulationDriverEndToEnd (finalY=" << finalY << ")" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "===========================================" << std::endl;
    std::cout << "  Axle Simulation Clock & Input Unit Tests " << std::endl;
    std::cout << "===========================================" << std::endl;

    JobSystem::Init(2);
    PhysicsEngine::Init();

    TestClockDtValidation();
    TestClockOverloadCap();
    TestClockFramePartitions();
    TestClockSpeedScaling();
    TestClockPauseResumeAndSingleStep();
    TestInputSnapshotZeroOneMultipleTicks();
    TestPausedInputIsolation();
    TestPrivatePreviewClockIndependence();
    TestSimulationDriverEndToEnd();

    PhysicsEngine::Shutdown();
    JobSystem::Shutdown();

    std::cout << "===========================================" << std::endl;
    if (g_FailCount == 0) {
        std::cout << "  ALL CLOCK & INPUT TESTS PASSED (100%)    " << std::endl;
        std::cout << "===========================================" << std::endl;
        return 0;
    } else {
        std::cerr << "  FAILED: " << g_FailCount << " test(s) failed." << std::endl;
        std::cout << "===========================================" << std::endl;
        return 1;
    }
}
