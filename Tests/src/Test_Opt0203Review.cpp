#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/SimulationDriver.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#include <Axle/ScenePhysicsBridge.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace Axle;
namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { ++failures; std::cerr << __LINE__ << ": " #expr "\n"; } } while (0)
struct Observation {
    int Frames = 0, Fixed = 0, Late = 0, Held = 0;
    float FrameTime = 0, LateTime = 0;
};
Observation observed;
class ProbeScript : public ScriptableEntity {
    void OnUpdate(Timestep dt) override { ++observed.Frames; observed.FrameTime += dt; }
    void OnFixedUpdate(Timestep) override { ++observed.Fixed; observed.Held += Input::IsKeyPressed(KeyCode::W); }
    void OnLateUpdate(Timestep dt) override { ++observed.Late; observed.LateTime += dt; }
};
bool held = false;
CursorMode cursor = CursorMode::Normal;
InputContext hardware;

void SimulationRegression() {
    hardware.IsKeyPressedFn = [](KeyCode key) { return key == KeyCode::W && held; };
    hardware.IsMouseButtonPressedFn = [](MouseCode button) { return button == MouseCode::Button0 && held; };
    hardware.SetCursorModeFn = [](CursorMode mode) { cursor = mode; };
    hardware.GetCursorModeFn = [] { return cursor; };
    hardware.IsViewportHoveredFn = [] { return false; };
    Input::SetInputContext(&hardware);
    Scene scene;
    scene.CreateEntity().AddComponent<NativeScriptComponent>().Bind<ProbeScript>();
    scene.OnRuntimeStart();
    SimulationDriver driver;
    observed = {};
    CHECK(!scene.IsSimulationFaulted());
    std::thread invalidCaller([&] {
        scene.ExecuteFrameScriptPhase(1.0f);
        CHECK(driver.AdvanceFrame(1.0f, scene) == 0);
    });
    invalidCaller.join();
    CHECK(observed.Frames == 0);
    CHECK(driver.GetClock().GetDiagnostics().TotalTicksExecuted == 0);
    held = true;
    for (int i = 0; i < 144; ++i) {
        // Exact policy used by both actual product OnUpdate paths.
        driver.SetPaused(false);
        driver.AdvanceFrame(1.0f / 144.0f, scene);
    }
    CHECK(observed.Fixed == 60);
    CHECK(observed.Held == 60);
    CHECK(observed.Frames == 144);
    driver.Pause();
    driver.RequestSingleStep();
    driver.SetPaused(true); // Editor's per-frame state synchronization
    CHECK(driver.AdvanceFrame(0.0f, scene) == 1);
    CHECK(driver.IsPaused());
    CHECK(observed.Fixed == 61);
    CHECK(observed.Frames == 144);
    driver.AdvanceFrame(1.0f, scene);
    CHECK(observed.Fixed == 61);
    driver.Resume();
    driver.SetSpeed(0.0f);
    const auto before = observed;
    driver.AdvanceFrame(1.0f, scene);
    CHECK(observed.Frames == before.Frames);
    CHECK(observed.Fixed == before.Fixed);
    CHECK(observed.LateTime == before.LateTime);
    driver.SetSpeed(2.0f);
    observed = {};
    driver.AdvanceFrame(1.0f / 60.0f, scene);
    CHECK(observed.Fixed == 2);
    CHECK(std::abs(observed.FrameTime - 1.0f / 30.0f) < 1e-7f);
    const auto valid = observed;
    driver.AdvanceFrame(std::numeric_limits<float>::quiet_NaN(), scene);
    CHECK(observed.Frames == valid.Frames);
    CHECK(std::isfinite(observed.LateTime));
    driver.GetInputAdapter().ActivateTickContext(0);
    Input::SetCursorMode(CursorMode::Locked);
    CHECK(cursor == CursorMode::Locked);
    CHECK(!Input::IsViewportHovered());
    driver.GetInputAdapter().DeactivateTickContext();
    CHECK(Input::GetInputContext() == &hardware);
    scene.OnRuntimeStop();
    held = false;
    Input::SetInputContext(nullptr);

    SimulationInputAdapter input;
    input.InjectKeyPress(KeyCode::Space);
    input.InjectKeyRelease(KeyCode::Space); // A short tap between fixed ticks
    input.SetMouseScrollY(2.0f);
    input.PrepareTickSnapshots(0);
    input.PrepareTickSnapshots(2);
    input.ActivateTickContext(0);
    CHECK(input.GetCurrentSnapshot().IsKeyJustPressed(KeyCode::Space));
    CHECK(input.GetCurrentSnapshot().IsKeyJustReleased(KeyCode::Space));
    CHECK(Input::IsKeyJustPressed(KeyCode::Space));
    CHECK(Input::IsKeyJustReleased(KeyCode::Space));
    CHECK(Input::GetMouseScrollY() == 2.0f);
    input.ActivateTickContext(1);
    CHECK(!input.GetCurrentSnapshot().IsKeyJustPressed(KeyCode::Space));
    CHECK(Input::GetMouseScrollY() == 0.0f);
    input.DeactivateTickContext();

    SimulationClock clock;
    CHECK(clock.Advance(2.0f) == 8);
    CHECK(clock.GetDiagnostics().DroppedWallTime > 1.74);
    CHECK(clock.GetDiagnostics().DroppedTicks == 7);
    CHECK(clock.GetAccumulator() < clock.GetFixedTimestep());
    clock.SetSpeed(20.0f);
    CHECK(clock.GetSpeed() == 4.0f);
    clock.Reset();
    clock.SetSpeed(1.0f);
    CHECK(clock.Advance(clock.GetFixedTimestep() - 5e-7f) == 0);
    clock.Pause();
    clock.RequestSingleStep();
    CHECK(clock.Advance(std::numeric_limits<float>::quiet_NaN()) == 1);
    // Long sessions must not grow their roundoff allowance into whole ticks.
    SimulationClock longSession;
    for (int i = 0; i < 400000; ++i) longSession.Advance(0.25f);
    const auto& diagnostics = longSession.GetDiagnostics();
    const double scheduledTime = (diagnostics.TotalTicksExecuted + diagnostics.DroppedTicks) *
                                 static_cast<double>(longSession.GetFixedTimestep());
    CHECK(scheduledTime <= diagnostics.TotalRealTime + longSession.GetFixedTimestep() * 0.0011);
}

void SchedulerRegression() {
    // Queue saturation forces enki AddTaskSetToPipe to execute a nested producer
    // synchronously. Holding the admission mutex across enqueue deadlocks here.
    JobSystem::Shutdown();
    JobSystem::Init(1);
    std::atomic<int> completed = 0;
    for (int i = 0; i < 1024; ++i) {
        auto task = JobSystem::Dispatch([&] {
            auto child = JobSystem::Dispatch([&] { ++completed; });
            child.Wait();
        });
        task.Detach();
    }
    JobSystem::WaitForAll();
    CHECK(completed == 1024);
    bool callback = false;
    JobSystem::PostToMainThread([&] { callback = true; });
    auto task = JobSystem::Dispatch([&] {
        CHECK(JobSystem::PumpMainThreadCompletions() == 0);
        CHECK(!callback);
    });
    task.Wait();
    CHECK(!callback);
    JobSystem::RunPinnedTasks();
    CHECK(!callback);
    CHECK(JobSystem::PumpMainThreadCompletions() == 1);
    int nested = 0;
    JobSystem::PostToMainThread([&] {
        ++nested;
        JobSystem::PostToMainThread([&] { ++nested; });
        CHECK(JobSystem::PumpMainThreadCompletions() == 0);
    });
    JobSystem::PumpMainThreadCompletions();
    CHECK(nested == 1);
    JobSystem::PumpMainThreadCompletions();
    CHECK(nested == 2);

    bool unrelated = false;
    auto other = JobSystem::Dispatch([&] { unrelated = true; });
    {
        JobSystemEnkiTS worldA(16, 4), worldB(16, 4);
        auto barrier = worldA.CreateBarrier();
        bool physics = false;
        auto job = worldA.CreateJob("IsolatedPhysics", JPH::Color::sRed, [&] { physics = true; });
        barrier->AddJob(job);
        job = {};
        worldA.WaitForJobs(barrier);
        worldA.DestroyBarrier(barrier);
        CHECK(physics);
        CHECK(!unrelated);
    }
    CHECK(!unrelated);
    other.Wait();
    CHECK(unrelated);
    JobSystem::RunPinnedTasks();
    JobSystem::Shutdown();
    JobSystem::Init(2);
    std::atomic<bool> blockerStarted{false}, releaseBlocker{false};
    auto blocker = JobSystem::Dispatch([&] {
        blockerStarted = true;
        while (!releaseBlocker.load()) std::this_thread::yield();
    });
    while (!blockerStarted.load()) std::this_thread::yield();
    {
        JobSystemEnkiTS worldA(16, 4), worldB(16, 4);
        for (auto* world : {&worldA, &worldB}) {
            auto barrier = world->CreateBarrier();
            bool physics = false;
            auto job = world->CreateJob("OccupiedWorkerPhysics", JPH::Color::sRed, [&] { physics = true; });
            barrier->AddJob(job);
            job = {};
            world->WaitForJobs(barrier);
            world->DestroyBarrier(barrier);
            CHECK(physics);
            CHECK(world->WaitForLiveTasks());
            CHECK(!releaseBlocker.load());
        }
    }
    releaseBlocker = true;
    blocker.Wait();
}

int scriptDestroyed = 0;
class LifecycleProbe final : public ScriptableEntity {
    void OnDestroy() override { ++scriptDestroyed; }
};
void ComponentLifecycleRegression() {
    {
        JobSystemEnkiTS bridge(8, 4);
        std::vector<JPH::JobHandle> jobs;
        for (int i = 0; i < 8; ++i) jobs.push_back(bridge.CreateJob("Capacity", JPH::Color::sRed, [] {}, 1));
        CHECK(!bridge.CreateJob("Overflow", JPH::Color::sRed, [] {}, 1).IsValid());
        for (auto& job : jobs) job.RemoveDependency();
        JobSystem::WaitForAll();
        jobs.clear();
    }
    PhysicsScene physics;
    Scene scene;
    auto actor = scene.CreateEntity();
    actor.AddComponent<CollisionShape3D>();
    actor.AddComponent<RigidBody3D>();
    actor.AddComponent<NativeScriptComponent>().Bind<LifecycleProbe>();
    scene.OnRuntimeStart(&physics);
    CHECK(physics.GetNumBodies() == 1);
    auto& commands = scene.GetCommandBuffer();
    commands.Remove<NativeScriptComponent>(actor);
    commands.Execute(scene);
    CHECK(scriptDestroyed == 1);
    commands.Remove<RigidBody3D>(actor);
    commands.Execute(scene);
    CHECK(physics.GetNumBodies() == 0);
    commands.EmplaceOrReplace<RigidBody3D>(actor);
    commands.Execute(scene);
    CHECK(physics.GetNumBodies() == 1);
    commands.EmplaceOrReplace<CollisionShape3D>(actor);
    commands.Execute(scene);
    CHECK(physics.GetNumBodies() == 1);
    commands.Remove<CollisionShape3D>(actor);
    commands.Execute(scene);
    CHECK(physics.GetNumBodies() == 0);
    scene.Clear();
    for (bool inheritScale : {false, true}) {
        auto parent = scene.CreateEntity("ParentBody");
        parent.GetComponent<TransformComponent>().Translation.x = 10.0f;
        parent.GetComponent<TransformComponent>().Scale = glm::vec3(2.0f);
        parent.AddComponent<CollisionShape3D>();
        parent.AddComponent<RigidBody3D>().GravityFactor = 0.0f;
        auto child = scene.CreateEntity("ChildBody");
        child.SetParent(parent, false);
        child.GetComponent<HierarchyComponent>().InheritScale = inheritScale;
        child.AddComponent<CollisionShape3D>();
        child.AddComponent<RigidBody3D>().GravityFactor = 0.0f;
        scene.OnRuntimeStart(&physics);
        const float desiredX = inheritScale ? 26.0f : 23.0f;
        physics.SetBodyPositionAndRotation(parent.GetComponent<RigidBody3D>().RuntimeBodyID, glm::vec3(20, 0, 0));
        physics.SetBodyPositionAndRotation(child.GetComponent<RigidBody3D>().RuntimeBodyID, glm::vec3(desiredX, 0, 0));
        scene.ExecutePostStepSyncPhase(&physics);
        CHECK(std::abs(child.GetWorldTransform()[3].x - desiredX) < 1e-4f);
        scene.Clear();
    }
}
}

int main() {
    Log::Init();
    JobSystem::Init(2);
    PhysicsEngine::Init();
    SimulationRegression();
    ComponentLifecycleRegression();
    SchedulerRegression();
    PhysicsEngine::Shutdown();
    JobSystem::Shutdown();
    std::cout << "OPT0203_REVIEW failures=" << failures << '\n';
    return failures ? 1 : 0;
}
