#include <Axle/JobSystem.hpp>
#include <Axle/ScriptCompiler.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/Log.hpp>

#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <cstdlib>

namespace {

// ============================================================================
// ECSJ-01: Script Compiler Async Completion Thread Probe
// ============================================================================
int RunCompilerThreadProbe(const std::filesystem::path& workspace) {
    const auto fixtureDir = workspace / "compiler_probe";
    std::filesystem::create_directories(fixtureDir);

    // Write minimal CMake fixture
    const auto cmakePath = fixtureDir / "CMakeLists.txt";
    std::ofstream cmakeFile(cmakePath);
    cmakeFile << "cmake_minimum_required(VERSION 3.22)\n"
              << "project(CompilerProbe NONE)\n"
              << "add_custom_target(CompilerProbeTarget\n"
              << "    COMMAND \"${CMAKE_COMMAND}\" -E echo \"CompilerProbeTargetExecuted\"\n"
              << "    VERBATIM)\n";
    cmakeFile.close();

    // Configure fixture once
    std::string configureCmd = "cmake -S \"" + fixtureDir.string() + "\" -B \"" + (fixtureDir / "build").string() + "\"";
    if (std::system(configureCmd.c_str()) != 0) {
        std::cerr << "ERROR: Compiler fixture configure failed\n";
        return 2;
    }

    auto& compiler = Axle::ScriptCompiler::Get();
    compiler.SetWorkspaceDirectory(fixtureDir);
    compiler.SetTarget("CompilerProbeTarget");
#ifdef NDEBUG
    compiler.SetBuildConfig("Release");
#else
    compiler.SetBuildConfig("Debug");
#endif

    const std::thread::id mainThreadId = std::this_thread::get_id();
    std::atomic<bool> callbackRan{false};
    std::atomic<bool> callbackOnMain{false};
    std::atomic<bool> compilationSucceeded{false};

    const bool started = compiler.CompileAsync([&](const Axle::CompilationResult& res) {
        callbackRan = true;
        compilationSucceeded = res.Success;
        callbackOnMain = (std::this_thread::get_id() == mainThreadId);
    });

    compiler.WaitForCompletion();

    std::cout << "started=" << (started ? 1 : 0)
              << " callbackRan=" << (callbackRan.load() ? 1 : 0)
              << " compilationSucceeded=" << (compilationSucceeded.load() ? 1 : 0)
              << " callbackOnMain=" << (callbackOnMain.load() ? 1 : 0) << std::endl;

    if (!started || !callbackRan.load() || !compilationSucceeded.load()) {
        std::cerr << "ERROR: Successful compiler fixture prerequisite did not complete\n";
        return 2;
    }

    // Architectural assertion: completion callback must run on the owner / main thread
    if (!callbackOnMain.load()) {
        std::cerr << "DEFECT REPRODUCED [ECSJ-01]: ScriptCompiler callback executed on background worker thread\n";
        return 1;
    }

    std::cout << "RESULT ecsj-01-compiler-thread PASS" << std::endl;
    return 0;
}

// ============================================================================
// ECSJ-02: External Thread Identity and Main-Thread Delivery Probe
// ============================================================================
int RunThreadIdentityProbe(bool registeredProducer = false) {
    Axle::JobSystem::Init(2);
    const std::thread::id mainThreadId = std::this_thread::get_id();
    std::atomic<uint32_t> rawEnki{0};
    std::atomic<uint32_t> wrapperIndex{0};
    std::atomic<bool> incorrectlyMain{false};
    std::atomic<bool> callbackOnMain{false};
    std::atomic<uint32_t> callbackCount{0};

    auto produce = [&] {
        rawEnki = Axle::JobSystem::GetScheduler().GetThreadNum();
        wrapperIndex = Axle::JobSystem::GetCurrentThreadIndex();
        incorrectlyMain = Axle::JobSystem::IsMainThread();
        Axle::JobSystem::DispatchToMainThread([&] {
            callbackOnMain = (std::this_thread::get_id() == mainThreadId);
            callbackCount.fetch_add(1);
        });
    };
    if (registeredProducer) {
        // This already-correct production path proves the probe accepts queued delivery.
        auto task = Axle::JobSystem::Dispatch(produce);
        while (!task.IsComplete()) std::this_thread::yield();
        task.Wait();
    } else {
        std::thread producer(produce);
        producer.join();
    }

    const auto callbacksBeforePump = callbackCount.load();
    Axle::JobSystem::RunPinnedTasks();
    const auto callbacksAfterPump = callbackCount.load();
    const bool deliveredOnMain = callbackOnMain.load();
    Axle::JobSystem::Shutdown();

    std::cout << "raw_index=" << rawEnki.load() << " wrapper_index=" << wrapperIndex.load()
              << " incorrectlyMain=" << incorrectlyMain.load()
              << " callbacks_before_pump=" << callbacksBeforePump
              << " callbacks_after_pump=" << callbacksAfterPump
              << " callbackOnMain=" << deliveredOnMain << std::endl;
    if (incorrectlyMain.load() || wrapperIndex.load() == 0 || callbacksBeforePump != 0 ||
        callbacksAfterPump != 1 || !deliveredOnMain) {
        std::cerr << "DEFECT REPRODUCED [ECSJ-02]: Thread identity or exactly-once owning-thread delivery failed\n";
        return 1;
    }
    return 0;
}

// ============================================================================
// ECSJ-03: Single-Thread Jolt Bridge Destruction Hang & Passing Control
// ============================================================================
int RunBridgeProbe(bool useControlDrain) {
    Axle::JobSystem::Init(1);
    Axle::PhysicsEngine::Init();

    std::atomic<bool> ran{false};
    {
        auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(16, 8);
        auto barrier = bridge->CreateBarrier();
        auto job = bridge->CreateJob("ProbeJob", JPH::Color::sWhite, [&] {
            ran = true;
        });
        barrier->AddJob(job);
        job = {};
        bridge->WaitForJobs(barrier);

        std::cout << "Jolt barrier returned; ran=" << (ran.load() ? 1 : 0) << std::endl;

        bridge->DestroyBarrier(barrier);
        barrier = nullptr;

        if (useControlDrain) {
            std::cout << "Control: calling JobSystem::WaitForAll() before bridge destruction" << std::endl;
            Axle::JobSystem::WaitForAll();
        }

        std::cout << "Destroying bridge" << std::endl;
        if (!useControlDrain && ran.load())
            std::cout << "AXLE_TEST_READY ecsj-03-bridge-hang" << std::endl;
        bridge.reset();
        if (!useControlDrain)
            std::cout << "AXLE_TEST_COMPLETED ecsj-03-bridge-hang" << std::endl;
        std::cout << "Bridge destruction returned" << std::endl;
    }

    Axle::PhysicsEngine::Shutdown();
    Axle::JobSystem::Shutdown();

    if (!ran.load()) {
        std::cerr << "ERROR: Jolt job did not execute\n";
        return 1;
    }

    return 0;
}

// ============================================================================
// ECSJ-04: EnTT Stable Storage Tombstone Iteration Probe
// ============================================================================
struct StableTombstoneComponent {
    static constexpr bool in_place_delete = true;
    int Value = 0;
};

int RunEnTTTombstoneProbe() {
    Axle::JobSystem::Init(2);
    entt::registry registry;

    const auto e1 = registry.create();
    const auto e2 = registry.create();
    registry.emplace<StableTombstoneComponent>(e1, 100);
    registry.emplace<StableTombstoneComponent>(e2, 200);

    // Remove component from e1, leaving a tombstone in in_place_delete packed storage
    registry.remove<StableTombstoneComponent>(e1);

    size_t sequentialCount = 0;
    for (auto e : registry.view<StableTombstoneComponent>()) {
        (void)e;
        sequentialCount++;
    }

    std::atomic<size_t> parallelCount{0};
    std::atomic<size_t> invalidCallbacks{0};

    // Force parallel branch with minBatchSize=1
    Axle::ParallelECS::ForEach<StableTombstoneComponent>(registry, [&](entt::entity e) {
        parallelCount.fetch_add(1);
        if (!registry.valid(e) || !registry.all_of<StableTombstoneComponent>(e) || e == entt::tombstone) {
            invalidCallbacks.fetch_add(1);
        }
    }, 1);

    Axle::JobSystem::Shutdown();

    std::cout << "sequential=" << sequentialCount
              << " parallel=" << parallelCount.load()
              << " invalidCallbacks=" << invalidCallbacks.load() << std::endl;

    // Architectural assertion: Parallel iteration must match sequential view and skip tombstones
    if (sequentialCount != parallelCount.load() || invalidCallbacks.load() != 0) {
        std::cerr << "DEFECT REPRODUCED [ECSJ-04]: ParallelECS::ForEach raw packed iteration visited tombstones\n";
        return 1;
    }

    std::cout << "RESULT ecsj-04-entt-tombstone PASS" << std::endl;
    return 0;
}

Axle::Entity FindEntity(Axle::Scene& scene, const std::string& tag) {
    for (auto handle : scene.GetRegistry().view<Axle::TagComponent>()) {
        if (scene.GetRegistry().get<Axle::TagComponent>(handle).Tag == tag) {
            return {handle, &scene};
        }
    }
    return {};
}

// ============================================================================
// Current-Format Scene/Assembly & Ordinary Physics Lifecycle Probe
// ============================================================================
int RunCurrentFormatLifecycleProbe(const std::filesystem::path& workspace) {
    Axle::JobSystem::Init(4);
    Axle::PhysicsEngine::Init();

    const auto source = std::make_shared<Axle::Scene>();
    auto ball = source->CreateEntity("OrdinaryBody");
    ball.GetComponent<Axle::TransformComponent>().Translation = {0.0f, 10.0f, 0.0f};
    auto& rb = ball.AddComponent<Axle::RigidBody3D>();
    rb.MotionType = Axle::BodyMotionType::Dynamic;
    rb.IsDynamic = true;
    ball.AddComponent<Axle::CollisionShape3D>();

    // 1. Scene copy
    auto copy = Axle::Scene::Copy(source);
    if (!copy || !FindEntity(*copy, "OrdinaryBody")) {
        std::cerr << "FAIL: Scene::Copy failed\n";
        return 1;
    }

    // 2. Scene serialization round trip
    Axle::Scene target;
    Axle::PhysicsScene targetPhysics;
    Axle::SceneSerializer writer(source.get());
    Axle::SceneSerializer reader(&target, &targetPhysics);
    const std::string serialized = writer.SerializeToString();
    if (!reader.DeserializeFromString(serialized)) {
        std::cerr << "FAIL: SceneSerializer round-trip failed\n";
        return 1;
    }

    auto targetBody = FindEntity(target, "OrdinaryBody");
    if (!targetBody || targetPhysics.GetNumBodies() != 1) {
        std::cerr << "FAIL: Target body not found or wrong body count after deserialize\n";
        return 1;
    }

    // 3. Duplicate entity with physics scene tracking
    auto duplicate = target.DuplicateEntity(targetBody, &targetPhysics);
    if (!duplicate || targetPhysics.GetNumBodies() != 2) {
        std::cerr << "FAIL: DuplicateEntity with physics failed\n";
        return 1;
    }
    target.DestroyEntity(duplicate, &targetPhysics);
    if (targetPhysics.GetNumBodies() != 1) {
        std::cerr << "FAIL: DestroyEntity did not clean up physics body\n";
        return 1;
    }

    // 4. Assembly save and load
    const auto asmPath = workspace / "OrdinaryBody.axle";
    if (!Axle::AssemblySerializer::Serialize(asmPath, targetBody)) {
        std::cerr << "FAIL: AssemblySerializer::Serialize failed\n";
        return 1;
    }
    Axle::Scene asmScene;
    Axle::PhysicsScene asmPhysics;
    auto asmSpawned = Axle::AssemblySerializer::Deserialize(asmPath, &asmScene, &asmPhysics);
    if (!asmSpawned || !asmSpawned.HasComponent<Axle::RigidBody3D>() || asmPhysics.GetNumBodies() != 1) {
        std::cerr << "FAIL: AssemblySerializer::Deserialize failed or body not created\n";
        return 1;
    }

    // 5. Ordinary physics Play/Stop lifecycle
    const auto bodyID = targetBody.GetComponent<Axle::RigidBody3D>().RuntimeBodyID;
    const float beforeY = targetPhysics.GetBodyPosition(bodyID).y;

    target.OnRuntimeStart(&targetPhysics);
    for (int i = 0; i < 12; ++i) {
        targetPhysics.Step(1.0f / 60.0f);
        target.OnUpdateRuntime(1.0f / 60.0f, &targetPhysics);
    }
    const float afterY = targetPhysics.GetBodyPosition(bodyID).y;
    target.OnRuntimeStop(&targetPhysics);
    Axle::JobSystem::WaitForAll();

    std::cout << "ordinary_before=" << beforeY
              << " ordinary_after=" << afterY
              << " bodies=" << targetPhysics.GetNumBodies() << std::endl;

    if (afterY >= beforeY || targetPhysics.GetNumBodies() != 1 || target.GetPhysicsScene() != nullptr) {
        std::cerr << "FAIL: Physics lifecycle verification failed (gravity or body count mismatch)\n";
        return 1;
    }

    Axle::PhysicsEngine::Shutdown();
    Axle::JobSystem::Shutdown();

    std::cout << "RESULT current-format-lifecycle PASS" << std::endl;
    return 0;
}

// ============================================================================
// Intentional Failure & Watchdog Probes
// ============================================================================
int RunDeliberateFailure() {
    std::cout << "Executing intentional harness failure probe" << std::endl;
    return 1;
}

int RunWatchdogHang() {
    std::cout << "Executing watchdog hang probe: spinning indefinitely" << std::endl;
    std::cout << "AXLE_TEST_READY watchdog-hang" << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Axle::Log::Init();

    if (argc < 2) {
        std::cout << "Usage: AxleJobsECSTests <scenario> [workspace]\n"
                  << "Scenarios:\n"
                  << "  ecsj-01-compiler-thread\n"
                  << "  ecsj-02-thread-identity\n"
                  << "  ecsj-03-bridge-hang\n"
                  << "  ecsj-03-bridge-control\n"
                  << "  ecsj-04-entt-tombstone\n"
                  << "  queued-delivery-control\n"
                  << "  current-format-lifecycle\n"
                  << "  deliberate-failure\n"
                  << "  watchdog-hang\n"
                  << "  --passing\n";
        return 2;
    }

    const std::string_view scenario = argv[1];
    const std::filesystem::path workspace = (argc >= 3)
        ? std::filesystem::absolute(argv[2])
        : std::filesystem::current_path() / "JobsECSTestWorkspace";

    auto finish = [&](int result) {
        const char* event = result == 0 ? "AXLE_TEST_PASS" :
                            result == 1 ? "AXLE_TEST_DEFECT" : "AXLE_TEST_ERROR";
        if (scenario == "deliberate-failure" && result == 1)
            event = "AXLE_TEST_EXPECTED_FAILURE";
        std::cout << event << " " << scenario << std::endl;
        return result;
    };
    try {
        if (scenario == "ecsj-01-compiler-thread") return finish(RunCompilerThreadProbe(workspace));
        if (scenario == "ecsj-02-thread-identity") return finish(RunThreadIdentityProbe());
        if (scenario == "queued-delivery-control") return finish(RunThreadIdentityProbe(true));
        if (scenario == "ecsj-03-bridge-hang") return finish(RunBridgeProbe(false));
        if (scenario == "ecsj-03-bridge-control" || scenario == "--passing" || scenario == "passing-control")
            return finish(RunBridgeProbe(true));
        if (scenario == "ecsj-04-entt-tombstone") return finish(RunEnTTTombstoneProbe());
        if (scenario == "deliberate-failure") return finish(RunDeliberateFailure());
        if (scenario == "watchdog-hang") return finish(RunWatchdogHang());
        if (scenario == "current-format-lifecycle") return finish(RunCurrentFormatLifecycleProbe(workspace));
        std::cerr << "Unknown scenario: " << scenario << std::endl;
        return finish(2);
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << std::endl;
        return finish(2);
    }
}
