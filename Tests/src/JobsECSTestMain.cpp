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
// EJ-01: Thread Identity, Lifecycle Admission and Reinitialization
// ============================================================================
int RunEJ01LifecycleAdmissionProbe() {
    // 1. Stopped state check
    if (Axle::JobSystem::IsInitialized() ||
        Axle::JobSystem::GetState() != Axle::JobSystemState::Stopped ||
        Axle::JobSystem::GetTotalThreadCount() != 0 ||
        Axle::JobSystem::GetDedicatedWorkerThreadCount() != 0 ||
        Axle::JobSystem::GetCurrentThreadIndex() != Axle::InvalidThreadIndex) {
        std::cerr << "FAIL [EJ-01]: Stopped state invariants violated\n";
        return 1;
    }

    // 2. Establish owner on main thread with Init(2)
    Axle::JobSystem::Init(2);
    if (!Axle::JobSystem::IsInitialized() ||
        !Axle::JobSystem::IsMainThread() ||
        Axle::JobSystem::GetState() != Axle::JobSystemState::Running ||
        Axle::JobSystem::GetTotalThreadCount() != 2 ||
        Axle::JobSystem::GetDedicatedWorkerThreadCount() != 1 ||
        Axle::JobSystem::GetCurrentThreadIndex() != 0 ||
        !Axle::JobSystem::IsValidSchedulerContext()) {
        std::cerr << "FAIL [EJ-01]: Main thread initialization invariants violated\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    const Axle::u32 epoch1 = Axle::JobSystem::GetEpoch();

    // 3. Verify external thread identity and rejection of scheduler operations
    std::atomic<bool> externalOk{true};
    std::atomic<bool> externalLambdaRan{false};
    std::thread externalThread([&] {
        if (Axle::JobSystem::IsMainThread()) externalOk = false;
        if (Axle::JobSystem::GetCurrentThreadIndex() != Axle::InvalidThreadIndex) externalOk = false;
        if (Axle::JobSystem::IsValidSchedulerContext()) externalOk = false;

        // External Init / Shutdown calls must be rejected
        Axle::JobSystem::Init(4);
        Axle::JobSystem::Shutdown();

        // External Dispatch must return invalid handle and NOT run lambda inline
        auto handle = Axle::JobSystem::Dispatch([&] {
            externalLambdaRan = true;
        });
        if (handle.IsValid()) externalOk = false;

        // External ParallelFor must not execute or crash
        Axle::JobSystem::ParallelFor(10, [&](Axle::u32) {
            externalLambdaRan = true;
        });

        // External Wait / WaitForAll / RunPinnedTasks must not crash or hang
        Axle::JobSystem::Wait(nullptr);
        Axle::JobSystem::WaitForAll();
        Axle::JobSystem::RunPinnedTasks();
    });
    externalThread.join();

    if (!externalOk.load() || externalLambdaRan.load()) {
        std::cerr << "FAIL [EJ-01]: External thread checks or rejection failed\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 4. Verify worker thread context from within an admitted task
    std::atomic<bool> workerOk{false};
    std::atomic<bool> taskRan{false};
    auto workerTask = Axle::JobSystem::Dispatch([&] {
        const Axle::u32 idx = Axle::JobSystem::GetCurrentThreadIndex();
        const bool main = Axle::JobSystem::IsMainThread();
        const bool validContext = Axle::JobSystem::IsValidSchedulerContext();
        if (!main && validContext && idx != Axle::InvalidThreadIndex && idx < 2) {
            workerOk = true;
        }
        taskRan = true;
    });
    // Yield to let the dedicated worker thread execute the task rather than stealing on main during Wait
    while (!taskRan.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    workerTask.Wait();
    if (!workerOk.load()) {
        std::cerr << "FAIL [EJ-01]: Worker thread context verification failed\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 5. Shutdown and verify stopped state retains owner identity
    Axle::JobSystem::Shutdown();
    if (Axle::JobSystem::IsInitialized() ||
        !Axle::JobSystem::IsMainThread() ||
        Axle::JobSystem::GetState() != Axle::JobSystemState::Stopped ||
        Axle::JobSystem::GetTotalThreadCount() != 0 ||
        Axle::JobSystem::GetDedicatedWorkerThreadCount() != 0 ||
        Axle::JobSystem::GetCurrentThreadIndex() != Axle::InvalidThreadIndex) {
        std::cerr << "FAIL [EJ-01]: Stopped state after shutdown violated\n";
        return 1;
    }

    // 6. Reinitialization with Init(1) -> 1 total thread, 0 dedicated workers, new epoch
    Axle::JobSystem::Init(1);
    const Axle::u32 epoch2 = Axle::JobSystem::GetEpoch();
    if (!Axle::JobSystem::IsInitialized() ||
        epoch2 <= epoch1 ||
        Axle::JobSystem::GetTotalThreadCount() != 1 ||
        Axle::JobSystem::GetDedicatedWorkerThreadCount() != 0) {
        std::cerr << "FAIL [EJ-01]: Reinitialization with Init(1) failed\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    Axle::JobSystem::Shutdown();

    // 7. Barrier-coordinated admission vs shutdown
    Axle::JobSystem::Init(2);

    std::atomic<bool> taskAStarted{false};
    std::atomic<bool> taskARelease{false};
    std::atomic<bool> taskAFinished{false};
    std::atomic<bool> childCRan{false};
    std::atomic<bool> rejectedTaskRan{false};

    auto taskA = Axle::JobSystem::Dispatch([&] {
        taskAStarted.store(true, std::memory_order_release);
        while (!taskARelease.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // Inside task A, dispatch child task C (admitted permit)
        auto childHandle = Axle::JobSystem::Dispatch([&] {
            childCRan.store(true, std::memory_order_release);
        });
        if (childHandle.IsValid()) {
            childHandle.Wait();
        }
        taskAFinished.store(true, std::memory_order_release);
    });

    while (!taskAStarted.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        taskARelease.store(true, std::memory_order_release);
    });

    Axle::JobSystem::Shutdown();
    releaser.join();

    if (!taskAFinished.load() || !childCRan.load()) {
        std::cerr << "FAIL [EJ-01]: Task A or child task C failed to complete during draining\n";
        return 1;
    }

    // After shutdown, dispatch must reject new work
    auto postShutdownHandle = Axle::JobSystem::Dispatch([&] {
        rejectedTaskRan = true;
    });
    if (postShutdownHandle.IsValid() || rejectedTaskRan.load()) {
        std::cerr << "FAIL [EJ-01]: Dispatch after shutdown was not rejected\n";
        return 1;
    }

    // Safe reinitialization
    Axle::JobSystem::Init(2);
    if (!Axle::JobSystem::IsInitialized() || Axle::JobSystem::GetEpoch() <= epoch2) {
        std::cerr << "FAIL [EJ-01]: Safe reinitialization after draining failed\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    Axle::JobSystem::Shutdown();

    std::cout << "RESULT ej01-lifecycle-admission PASS" << std::endl;
    return 0;
}

// ============================================================================
// EJ-02: Always-Deferred Main-Thread Executor and Owner Tokens
// ============================================================================
int RunEJ02DeferredExecutorProbe() {
    Axle::JobSystem::Init(2);
    const std::thread::id mainThreadId = std::this_thread::get_id();

    // 1. Posting from main thread is ALWAYS deferred (never executes inline)
    std::atomic<bool> mainPostRan{false};
    Axle::JobSystem::PostToMainThread([&] {
        mainPostRan = true;
    });
    if (mainPostRan.load()) {
        std::cerr << "FAIL [EJ-02]: PostToMainThread executed inline on main thread\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    Axle::u32 pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (!mainPostRan.load() || pumped != 1) {
        std::cerr << "FAIL [EJ-02]: PumpMainThreadCompletions failed to execute deferred task\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 2. Cross-thread posting and safe FIFO execution on main thread
    std::atomic<bool> workerPostRan{false};
    std::atomic<bool> workerPostOnMain{false};
    auto workerTask = Axle::JobSystem::Dispatch([&] {
        Axle::JobSystem::PostToMainThread([&] {
            workerPostRan = true;
            workerPostOnMain = (std::this_thread::get_id() == mainThreadId);
        });
    });
    workerTask.Wait();
    if (workerPostRan.load()) {
        std::cerr << "FAIL [EJ-02]: Worker post executed prematurely before pump\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (!workerPostRan.load() || !workerPostOnMain.load() || pumped != 1) {
        std::cerr << "FAIL [EJ-02]: Worker post failed to execute on main thread via pump\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 3. Nested posts run in later batches (non-recursive bounded pump)
    std::atomic<bool> nested1Ran{false};
    std::atomic<bool> nested2Ran{false};
    Axle::JobSystem::PostToMainThread([&] {
        nested1Ran = true;
        Axle::JobSystem::PostToMainThread([&] {
            nested2Ran = true;
        });
    });
    // First pump: nested1 runs, nested2 must NOT run yet
    pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (!nested1Ran.load() || nested2Ran.load() || pumped != 1) {
        std::cerr << "FAIL [EJ-02]: Nested post executed in same pump batch\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    // Second pump: nested2 runs
    pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (!nested2Ran.load() || pumped != 1) {
        std::cerr << "FAIL [EJ-02]: Nested post failed to execute in subsequent pump batch\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 4. OperationOwner invalidation & generation invalidation
    std::atomic<bool> validTokenRan{false};
    std::atomic<bool> invalidTokenRan{false};
    std::atomic<bool> destroyedOwnerRan{false};
    std::atomic<bool> generationBumpedRan{false};

    Axle::OperationOwner ownerA;
    Axle::JobSystem::PostToMainThread(ownerA.GetToken(), [&] {
        validTokenRan = true;
    });

    // Invalidate ownerA: pending post must be discarded
    ownerA.Invalidate();

    Axle::JobSystem::PostToMainThread(ownerA.GetToken(), [&] {
        invalidTokenRan = true;
    });

    // Reset / bump generation: token from older generation must be discarded
    ownerA.Reset();
    auto oldToken = ownerA.GetToken();
    ownerA.Invalidate();
    Axle::JobSystem::PostToMainThread(oldToken, [&] {
        generationBumpedRan = true;
    });

    // Destroyed owner
    {
        Axle::OperationOwner localOwner;
        Axle::JobSystem::PostToMainThread(localOwner.GetToken(), [&] {
            destroyedOwnerRan = true;
        });
    }

    pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (validTokenRan.load() || invalidTokenRan.load() || generationBumpedRan.load() || destroyedOwnerRan.load()) {
        std::cerr << "FAIL [EJ-02]: Invalidated / destroyed owner token callback was executed\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 5. Epoch invalidation: completions posted before shutdown do not run in new epoch
    std::atomic<bool> oldEpochRan{false};
    Axle::JobSystem::PostToMainThread([&] {
        oldEpochRan = true;
    });
    Axle::JobSystem::Shutdown();

    Axle::JobSystem::Init(2);
    pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (oldEpochRan.load() || pumped != 0) {
        std::cerr << "FAIL [EJ-02]: Callback from previous scheduler epoch executed in new epoch\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 6. Bounded FIFO order preservation
    std::vector<int> executionOrder;
    for (int i = 0; i < 10; ++i) {
        Axle::JobSystem::PostToMainThread([&executionOrder, i] {
            executionOrder.push_back(i);
        });
    }
    // Pump first 4 items
    pumped = Axle::JobSystem::PumpMainThreadCompletions(4);
    if (pumped != 4 || executionOrder.size() != 4) {
        std::cerr << "FAIL [EJ-02]: Bounded pump batch size mismatch\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    // Pump remaining 6 items
    pumped = Axle::JobSystem::PumpMainThreadCompletions(10);
    if (pumped != 6 || executionOrder.size() != 10) {
        std::cerr << "FAIL [EJ-02]: Bounded pump remainder size mismatch\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    for (int i = 0; i < 10; ++i) {
        if (executionOrder[i] != i) {
            std::cerr << "FAIL [EJ-02]: FIFO execution order violated at index " << i << "\n";
            Axle::JobSystem::Shutdown();
            return 1;
        }
    }

    // 7. DispatchToMainThread unified compatibility
    std::atomic<bool> compatRan{false};
    Axle::JobSystem::DispatchToMainThread([&] {
        compatRan = true;
    });
    if (compatRan.load()) {
        std::cerr << "FAIL [EJ-02]: DispatchToMainThread ran inline instead of being deferred\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }
    pumped = Axle::JobSystem::PumpMainThreadCompletions();
    if (!compatRan.load() || pumped != 1) {
        std::cerr << "FAIL [EJ-02]: DispatchToMainThread failed to execute via pump\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    Axle::JobSystem::Shutdown();
    std::cout << "RESULT ej02-deferred-executor PASS" << std::endl;
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
                  << "  ej01-lifecycle-admission\n"
                  << "  ej02-deferred-executor\n"
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
        if (scenario == "ej01-lifecycle-admission") return finish(RunEJ01LifecycleAdmissionProbe());
        if (scenario == "ej02-deferred-executor") return finish(RunEJ02DeferredExecutorProbe());
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
