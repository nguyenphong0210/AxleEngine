#include <Axle/JobSystem.hpp>
#include <Axle/ScriptCompiler.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/ScriptRegistry.hpp>
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
    Axle::JobSystem::Init(2);
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
        Axle::JobSystem::Shutdown();
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
        Axle::JobSystem::Shutdown();
        return 2;
    }

    // Architectural assertion: completion callback must run on the owner / main thread
    if (!callbackOnMain.load()) {
        std::cerr << "DEFECT REPRODUCED [ECSJ-01]: ScriptCompiler callback executed on background worker thread\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    Axle::JobSystem::Shutdown();
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
        auto* sched = Axle::JobSystem::GetScheduler();
        rawEnki = sched ? sched->GetThreadNum() : Axle::InvalidThreadIndex;
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
// EJ-01 Review Fixes: Direct Access, Shutdown Race, Forgery & Quiescing Probe
// ============================================================================
int RunEJ01ReviewFixProbe() {
    std::cout << "[EJ01-Fix] Running review fixes verification probe..." << std::endl;

    // --- Subtest 1: EJ01-R1 External Direct Access Rejection ---
    Axle::JobSystem::Init(2);
    std::atomic<bool> externalDirectAccessRejected{false};
    std::atomic<bool> externalDispatchRejected{false};
    std::atomic<bool> externalTryAdmitRejected{false};

    std::thread externalTester([&] {
        // 1. External thread calling GetScheduler() must return nullptr and be diagnosed
        enki::TaskScheduler* sched = Axle::JobSystem::GetScheduler();
        if (sched == nullptr) {
            externalDirectAccessRejected.store(true);
        }

        // 2. External thread calling Dispatch directly must be rejected
        auto handle = Axle::JobSystem::Dispatch([] {});
        if (!handle.IsValid()) {
            externalDispatchRejected.store(true);
        }

        // 3. External thread calling TryAdmitTaskSet must be rejected
        struct DummyTask : public enki::ITaskSet {
            DummyTask() : enki::ITaskSet(1) {}
            void ExecuteRange(enki::TaskSetPartition, uint32_t) override {}
        } dummy;
        if (!Axle::JobSystem::TryAdmitTaskSet(&dummy)) {
            externalTryAdmitRejected.store(true);
        }
    });
    externalTester.join();

    if (!externalDirectAccessRejected.load() || !externalDispatchRejected.load() || !externalTryAdmitRejected.load()) {
        std::cerr << "[EJ01-Fix] FAILED: External thread direct access / dispatch was not cleanly rejected!\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // --- Subtest 2: EJ01-R3 Unforgeable Task Permit & Context Protection ---
    std::atomic<bool> forgeryRejected{false};
    std::thread forgeryTester([&] {
        // Attempting to instantiate Detail::TaskExecutionScope from an external thread
        {
            Axle::Detail::TaskExecutionScope scope;
            // GetActiveTaskDepth() must remain 0 because constructor rejected invalid context
            if (Axle::JobSystem::GetActiveTaskDepth() == 0 && !Axle::JobSystem::IsValidSchedulerContext()) {
                // And dispatch still rejected
                auto h = Axle::JobSystem::Dispatch([] {});
                if (!h.IsValid()) {
                    forgeryRejected.store(true);
                }
            }
        }
    });
    forgeryTester.join();

    if (!forgeryRejected.load()) {
        std::cerr << "[EJ01-Fix] FAILED: External permit forgery was not rejected!\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // --- Subtest 3: Submission Rejection during Quiescing & Child Admission ---
    std::atomic<bool> parentStarted{false};
    std::atomic<bool> releaseParent{false};
    std::atomic<bool> childAdmittedInQuiescing{false};
    std::atomic<bool> rootRejectedInQuiescing{false};

    // Dispatch a root task that holds worker until main thread initiates shutdown
    auto parentJob = Axle::JobSystem::Dispatch([&] {
        parentStarted.store(true);
        while (!releaseParent.load()) {
            std::this_thread::yield();
        }

        // At this point, main thread is blocked inside Shutdown() (Quiescing / Draining)
        // As an already-admitted task executing on a worker, child dispatch MUST succeed:
        auto childJob = Axle::JobSystem::Dispatch([&] {
            childAdmittedInQuiescing.store(true);
        });
        if (childJob.IsValid()) {
            childJob.Wait();
        }
    });

    while (!parentStarted.load()) {
        std::this_thread::yield();
    }

    std::thread backgroundTester([&] {
        // Sleep briefly to ensure main thread enters Shutdown()
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // External or unadmitted work while quiescing/draining must be rejected
        auto rejectedRoot = Axle::JobSystem::Dispatch([] {});
        if (!rejectedRoot.IsValid()) {
            rootRejectedInQuiescing.store(true);
        }

        // Release parent task so draining can finish
        releaseParent.store(true);
    });

    // Main thread executes Shutdown() cleanly
    Axle::JobSystem::Shutdown();
    backgroundTester.join();

    if (!childAdmittedInQuiescing.load() || !rootRejectedInQuiescing.load()) {
        std::cerr << "[EJ01-Fix] FAILED: Quiescing admission boundary violated (child="
                  << childAdmittedInQuiescing.load() << ", rootRejected=" << rootRejectedInQuiescing.load() << ")\n";
        return 1;
    }

    // --- Subtest 4: EJ01-R2 Shutdown Race with External Producers ---
    // Reinitialize JobSystem to test aggressive concurrent producer racing with Shutdown()
    Axle::JobSystem::Init(4);

    constexpr int kProducerCount = 8;
    std::atomic<bool> runProducers{true};
    std::atomic<uint32_t> successfulPosts{0};
    std::atomic<uint32_t> rejectedPosts{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducerCount);

    for (int i = 0; i < kProducerCount; ++i) {
        producers.emplace_back([&] {
            while (runProducers.load(std::memory_order_relaxed)) {
                auto res = Axle::JobSystem::PostToMainThread([] {});
                if (res == Axle::PostResult::Success) {
                    successfulPosts.fetch_add(1, std::memory_order_relaxed);
                } else if (res == Axle::PostResult::RejectedStopped) {
                    rejectedPosts.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Let producers run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Shutdown concurrently while producers are actively posting
    Axle::JobSystem::Shutdown();

    // Signal producers to finish and join
    runProducers.store(false, std::memory_order_release);
    for (auto& t : producers) {
        t.join();
    }

    // Any subsequent post after shutdown must return RejectedStopped
    auto postAfterShutdown = Axle::JobSystem::PostToMainThread([] {});
    if (postAfterShutdown != Axle::PostResult::RejectedStopped) {
        std::cerr << "[EJ01-Fix] FAILED: PostToMainThread after shutdown returned unexpected code\n";
        return 1;
    }

    std::cout << "[EJ01-Fix] PASS: All review fixes verified (successful_posts="
              << successfulPosts.load() << ", rejected_posts=" << rejectedPosts.load() << ")\n";
    std::cout << "RESULT ej01-review-fixes PASS" << std::endl;
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
// EJ-03: Compiler Request/Result Handoff & Safe Owner Detach
// ============================================================================
int RunEJ03CompilerHandoffProbe(const std::filesystem::path& workspace) {
    Axle::JobSystem::Init(2);
    const std::thread::id mainThreadId = std::this_thread::get_id();

    const auto fixtureDir = workspace / "ej03_compiler_probe";
    std::filesystem::create_directories(fixtureDir);

    const auto cmakePath = fixtureDir / "CMakeLists.txt";
    std::ofstream cmakeFile(cmakePath);
    cmakeFile << "cmake_minimum_required(VERSION 3.22)\n"
              << "project(EJ03CompilerProbe NONE)\n"
              << "add_custom_target(EJ03ValidTarget\n"
              << "    COMMAND \"${CMAKE_COMMAND}\" -E echo \"EJ03TargetExecuted\"\n"
              << "    VERBATIM)\n";
    cmakeFile.close();

    std::string configureCmd = "cmake -S \"" + fixtureDir.string() + "\" -B \"" + (fixtureDir / "build").string() + "\"";
    if (std::system(configureCmd.c_str()) != 0) {
        std::cerr << "FAIL [EJ-03]: Compiler fixture configure failed\n";
        Axle::JobSystem::Shutdown();
        return 2;
    }

    auto& compiler = Axle::ScriptCompiler::Get();
    compiler.SetWorkspaceDirectory(fixtureDir);
    compiler.SetTarget("EJ03ValidTarget");
#ifdef NDEBUG
    compiler.SetBuildConfig("Release");
#else
    compiler.SetBuildConfig("Debug");
#endif

    // 1. Immutable Request Snapshot: mutating compiler after submission does not corrupt in-flight build
    auto request = compiler.CreateRequest();
    compiler.SetTarget("NonExistentInvalidTargetName");
    compiler.SetBuildConfig("BogusInvalidConfig");

    std::atomic<bool> immutableCallbackRan{false};
    std::atomic<bool> immutableSuccess{false};
    std::atomic<bool> immutableOnMain{false};

    bool started = compiler.CompileAsync(request, [&](const Axle::CompilationResult& res) {
        immutableCallbackRan = true;
        immutableSuccess = res.Success;
        immutableOnMain = (std::this_thread::get_id() == mainThreadId);
    });
    if (!started) {
        std::cerr << "FAIL [EJ-03]: CompileAsync failed to start with valid request\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    compiler.WaitForCompletion();
    if (!immutableCallbackRan.load() || !immutableSuccess.load() || !immutableOnMain.load()) {
        std::cerr << "FAIL [EJ-03]: Request immutability violated or completion failed on main thread\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // Restore compiler valid target for remaining tests
    compiler.SetTarget("EJ03ValidTarget");
#ifdef NDEBUG
    compiler.SetBuildConfig("Release");
#else
    compiler.SetBuildConfig("Debug");
#endif

    // 2. Owner Token Invalidation: Invalidation during in-flight compilation safely discards completion
    Axle::OperationOwner ownerA;
    std::atomic<bool> invalidatedCallbackRan{false};

    started = compiler.CompileAsync(ownerA.GetToken(), [&](const Axle::CompilationResult&) {
        invalidatedCallbackRan = true;
    });
    if (!started) {
        std::cerr << "FAIL [EJ-03]: CompileAsync failed to start for owner invalidation test\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // Invalidate owner while worker is compiling
    ownerA.Invalidate();
    compiler.WaitForCompletion();

    if (invalidatedCallbackRan.load()) {
        std::cerr << "FAIL [EJ-03]: Invalidated owner received completion callback\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 3. Cooperative / Safe Process Cancellation
    std::atomic<bool> cancelledCallbackRan{false};
    std::atomic<bool> wasCancelled{false};

    started = compiler.CompileAsync([&](const Axle::CompilationResult& res) {
        cancelledCallbackRan = true;
        wasCancelled = res.Cancelled;
    });
    if (!started) {
        std::cerr << "FAIL [EJ-03]: CompileAsync failed to start for cancellation test\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    compiler.Cancel();
    compiler.WaitForCompletion();

    if (compiler.IsCompiling()) {
        std::cerr << "FAIL [EJ-03]: Compiler still running after Cancel() and WaitForCompletion()\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    Axle::JobSystem::Shutdown();
    std::cout << "RESULT ej03-compiler-handoff PASS" << std::endl;
    return 0;
}

// ============================================================================
// EJ-04: Reload Commit, Staging Rollback and Scene/DLL Teardown
// ============================================================================
int RunEJ04ReloadCommitProbe(const std::filesystem::path& workspace) {
    Axle::JobSystem::Init(2);

    auto& manager = Axle::ScriptHotReloadManager::Get();
    if (!manager.Initialize()) {
        std::cerr << "FAIL [EJ-04]: ScriptHotReloadManager failed initial module load\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    if (!manager.IsLoaded()) {
        std::cerr << "FAIL [EJ-04]: ScriptHotReloadManager module not loaded after Initialize\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    const uint64_t initialGen = manager.GetModuleGeneration();
    auto testScene = std::make_shared<Axle::Scene>();
    manager.SetActiveScene(testScene);

    auto ent = testScene->CreateEntity("ReloadTestBeacon");
    ent.AddComponent<Axle::NativeScriptComponent>();

    // 1. Successful reload and module generation advancement
    auto reloadRes = manager.Reload(testScene, false);
    if (!reloadRes.Success || manager.GetModuleGeneration() <= initialGen) {
        std::cerr << "FAIL [EJ-04]: Reload failed or module generation did not increment\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 2. Destructor-before-unload guarantee & clean teardown ordering
    const uint64_t genBeforeShutdown = manager.GetModuleGeneration();
    manager.Shutdown();

    if (manager.IsLoaded()) {
        std::cerr << "FAIL [EJ-04]: Manager still reports loaded after Shutdown()\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    if (manager.GetModuleGeneration() <= genBeforeShutdown) {
        std::cerr << "FAIL [EJ-04]: Shutdown did not increment module generation lease\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // Verify entity instance is nullptr and dynamic scripts are cleared
    auto& nsc = ent.GetComponent<Axle::NativeScriptComponent>();
    if (nsc.Instance != nullptr) {
        std::cerr << "FAIL [EJ-04]: Scene script instance was not destroyed before DLL unload\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // 3. Re-initialization recovery
    if (!manager.Initialize()) {
        std::cerr << "FAIL [EJ-04]: Manager failed re-initialization after shutdown\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    manager.Shutdown();
    manager.SetActiveScene(nullptr);

    Axle::JobSystem::Shutdown();
    std::cout << "RESULT ej04-reload-commit PASS" << std::endl;
    return 0;
}

// ============================================================================
// EJ-05: Stable Physics Submission Lifetime Probe
// ============================================================================
int RunEJ05PhysicsLifetimeProbe() {
    std::cout << "[EJ05] Running stable physics submission lifetime probe..." << std::endl;

    if (!Axle::JobSystem::IsInitialized()) {
        Axle::JobSystem::Init(4);
    }
    if (!Axle::PhysicsEngine::IsInitialized()) {
        Axle::PhysicsEngine::Init();
    }

    // 1. Constrained Jolt pool rapid reuse (BRIDGE-02)
    // Bridge configured with small pool (4 jobs, 2 barriers) to force aggressive slot reuse
    {
        auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(4, 2);
        constexpr int kTotalJobs = 32;
        std::atomic<int> completedCount{0};

        for (int i = 0; i < kTotalJobs; ++i) {
            auto barrier = bridge->CreateBarrier();
            auto job = bridge->CreateJob("ConstrainedReuseJob", JPH::Color::sGreen, [&completedCount] {
                completedCount.fetch_add(1, std::memory_order_relaxed);
            });
            barrier->AddJob(job);
            // Invalidate handle immediately so JPH job reference is released
            job = {};

            bridge->WaitForJobs(barrier);
            bridge->DestroyBarrier(barrier);
        }

        if (completedCount.load() != kTotalJobs) {
            std::cerr << "[EJ05] FAILED: Constrained pool reuse missed jobs! (" << completedCount.load() << "/" << kTotalJobs << ")\n";
            return 1;
        }

        // Live task count must return to 0 once all enki tasks complete
        if (!bridge->WaitForLiveTasks(std::chrono::milliseconds(2000)) || bridge->GetLiveTaskCount() != 0) {
            std::cerr << "[EJ05] FAILED: Live task count non-zero after wait (" << bridge->GetLiveTaskCount() << ")\n";
            return 1;
        }

        std::cout << "[EJ05] Subtest 1 PASS: Constrained pool rapid reuse completed (high_water="
                  << bridge->GetAllocatedHighWaterMark() << ", total_allocated="
                  << bridge->GetAllocatedTaskCount() << ")\n";
    }

    // 2. Concurrent execution and reclamation under load (BRIDGE-05)
    {
        auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(128, 16);
        constexpr int kTotalJobs = 100;
        std::atomic<int> concurrentJobsRun{0};

        auto barrier = bridge->CreateBarrier();

        for (int j = 0; j < kTotalJobs; ++j) {
            auto job = bridge->CreateJob("ConcurrentJob", JPH::Color::sBlue, [&concurrentJobsRun] {
                concurrentJobsRun.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            });
            barrier->AddJob(job);
        }

        bridge->WaitForJobs(barrier);
        bridge->DestroyBarrier(barrier);

        if (concurrentJobsRun.load() != kTotalJobs) {
            std::cerr << "[EJ05] FAILED: Concurrent queueing missed jobs ("
                      << concurrentJobsRun.load() << "/" << kTotalJobs << ")\n";
            return 1;
        }

        if (!bridge->WaitForLiveTasks(std::chrono::milliseconds(2000)) || bridge->GetLiveTaskCount() != 0) {
            std::cerr << "[EJ05] FAILED: Concurrent live task count non-zero (" << bridge->GetLiveTaskCount() << ")\n";
            return 1;
        }

        if (bridge->GetAllocatedHighWaterMark() == 0 || bridge->GetAllocatedTaskCount() == 0) {
            std::cerr << "[EJ05] FAILED: High-water mark not recorded\n";
            return 1;
        }

        std::cout << "[EJ05] Subtest 2 PASS: Concurrent queueing and reclamation (high_water="
                  << bridge->GetAllocatedHighWaterMark() << ", total_allocated="
                  << bridge->GetAllocatedTaskCount() << ")\n";
    }

    // 3. Queue-full / synchronous execution fallback (BRIDGE-03)
    {
        // Test with stopped JobSystem: TryAdmitTaskSet will reject admission
        Axle::JobSystem::Shutdown();

        auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(8, 4);
        std::atomic<bool> fallbackRan{false};

        auto barrier = bridge->CreateBarrier();
        auto job = bridge->CreateJob("FallbackJob", JPH::Color::sYellow, [&fallbackRan] {
            fallbackRan.store(true);
        });
        barrier->AddJob(job);
        job = {};

        // In Jolt, AddJob with 0 dependencies queues the job.
        // Because JobSystem is stopped, QueueJobs executes the job synchronously as fallback.
        if (!fallbackRan.load()) {
            std::cerr << "[EJ05] FAILED: Synchronous fallback job did not execute\n";
            return 1;
        }

        bridge->DestroyBarrier(barrier);
        if (bridge->GetLiveTaskCount() != 0) {
            std::cerr << "[EJ05] FAILED: Fallback job record leaked (" << bridge->GetLiveTaskCount() << ")\n";
            return 1;
        }

        std::cout << "[EJ05] Subtest 3 PASS: Synchronous execution fallback verified\n";

        // Reinitialize JobSystem for subsequent tests
        Axle::JobSystem::Init(2);
    }

    std::cout << "RESULT ej05-physics-lifetime PASS" << std::endl;
    return 0;
}

// ============================================================================
// EJ-06: Physics Waits, 1-Thread Forward Progress & Disposal Protection
// ============================================================================
int RunEJ06PhysicsWaitsProbe() {
    std::cout << "[EJ06] Running physics waits and forward progress probe..." << std::endl;

    // 1. Single-Thread Forward Progress & Destruction (Resolves ECSJ-03)
    {
        if (Axle::JobSystem::IsInitialized()) {
            Axle::JobSystem::Shutdown();
        }
        Axle::JobSystem::Init(1);
        if (!Axle::PhysicsEngine::IsInitialized()) {
            Axle::PhysicsEngine::Init();
        }

        std::atomic<bool> jobARan{false};
        std::atomic<bool> jobBRan{false};
        {
            auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(16, 4);
            auto barrier = bridge->CreateBarrier();

            // Job B depends on Job A
            auto jobB = bridge->CreateJob("SingleThreadJobB", JPH::Color::sRed, [&] {
                if (!jobARan.load()) {
                    std::cerr << "[EJ06] Error: Job B ran before Job A!\n";
                }
                jobBRan.store(true);
            }, 1);

            auto jobA = bridge->CreateJob("SingleThreadJobA", JPH::Color::sGreen, [&, jobB] () mutable {
                jobARan.store(true);
                jobB.RemoveDependency(1);
            }, 0);

            barrier->AddJob(jobA);
            barrier->AddJob(jobB);
            jobA = {};
            jobB = {};

            bridge->WaitForJobs(barrier);
            bridge->DestroyBarrier(barrier);

            if (!jobARan.load() || !jobBRan.load()) {
                std::cerr << "[EJ06] FAILED: Dependent jobs failed to execute on 1-thread scheduler\n";
                return 1;
            }

            // Direct destruction without manual drain - MUST NOT HANG
            bridge.reset();
        }
        std::cout << "[EJ06] Subtest 1 PASS: 1-thread forward progress and direct destruction\n";
        Axle::JobSystem::Shutdown();
    }

    // 2. Non-Owner / In-Job Disposal Rejection
    {
        Axle::JobSystem::Init(4);
        auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(16, 4);

        // Attempt shutdown from background non-owner thread
        std::atomic<bool> bgShutdownAttempted{false};
        std::thread nonOwner([&] {
            bridge->Shutdown();
            bgShutdownAttempted.store(true);
        });
        nonOwner.join();

        if (!bgShutdownAttempted.load()) {
            std::cerr << "[EJ06] FAILED: Non-owner thread failed to run\n";
            return 1;
        }

        if (bridge->IsShutdown()) {
            std::cerr << "[EJ06] FAILED: Non-owner thread succeeded in shutting down bridge!\n";
            return 1;
        }

        // Attempt shutdown from within a worker job
        std::atomic<bool> inJobShutdownAttempted{false};
        auto barrier = bridge->CreateBarrier();
        auto job = bridge->CreateJob("InJobDisposal", JPH::Color::sYellow, [&] {
            bridge->Shutdown();
            inJobShutdownAttempted.store(true);
        });
        barrier->AddJob(job);
        job = {};
        bridge->WaitForJobs(barrier);
        bridge->DestroyBarrier(barrier);

        if (!inJobShutdownAttempted.load()) {
            std::cerr << "[EJ06] FAILED: In-job disposal check failed to run\n";
            return 1;
        }

        if (bridge->IsShutdown()) {
            std::cerr << "[EJ06] FAILED: In-job worker succeeded in shutting down bridge!\n";
            return 1;
        }

        // Owner thread shutdown MUST succeed
        bridge->Shutdown();
        if (!bridge->IsShutdown()) {
            std::cerr << "[EJ06] FAILED: Owner thread shutdown failed!\n";
            return 1;
        }

        std::cout << "[EJ06] Subtest 2 PASS: Non-owner and in-job disposal properly prohibited\n";
    }

    // 3. Constrained Scheduler Forward Progress Under Chaining
    {
        Axle::JobSystem::Shutdown();
        Axle::JobSystem::Init(2);

        auto bridge = std::make_unique<Axle::JobSystemEnkiTS>(32, 8);
        constexpr int kChainLength = 10;
        std::atomic<int> chainProgress{0};

        auto barrier = bridge->CreateBarrier();
        std::vector<JPH::JobHandle> chainJobs(kChainLength);

        // Create chain in reverse so each job has 1 dependency (except root)
        for (int i = kChainLength - 1; i >= 0; --i) {
            uint32_t deps = (i == 0) ? 0 : 1;
            JPH::JobHandle nextHandle = (i + 1 < kChainLength) ? chainJobs[i + 1] : JPH::JobHandle();
            chainJobs[i] = bridge->CreateJob("ChainJob", JPH::Color::sCyan, [&, i, nextHandle] () mutable {
                int expected = i;
                if (chainProgress.load() != expected) {
                    std::cerr << "[EJ06] Chaining ordering error: expected " << expected << ", got " << chainProgress.load() << "\n";
                }
                chainProgress.fetch_add(1, std::memory_order_relaxed);
                if (nextHandle.IsValid()) {
                    nextHandle.RemoveDependency(1);
                    nextHandle = {};
                }
            }, deps);
        }

        for (int i = 0; i < kChainLength; ++i) {
            barrier->AddJob(chainJobs[i]);
            chainJobs[i] = {};
        }

        bridge->WaitForJobs(barrier);
        bridge->DestroyBarrier(barrier);

        if (chainProgress.load() != kChainLength) {
            std::cerr << "[EJ06] FAILED: Chained jobs failed (" << chainProgress.load() << "/" << kChainLength << ")\n";
            return 1;
        }

        bridge->Shutdown();
        std::cout << "[EJ06] Subtest 3 PASS: Constrained scheduler forward progress under chaining\n";
    }

    std::cout << "RESULT ej06-physics-waits PASS" << std::endl;
    return 0;
}

// ============================================================================
// EJ-07: Handles, Draining and Shutdown Ordering Probe
// ============================================================================
int RunEJ07HandlesShutdownProbe() {
    std::cout << "[EJ07] Running handles, draining and shutdown probe..." << std::endl;

    // 1. Owning Asynchronous Handles Retain Scheduler State Across Shutdown
    {
        if (Axle::JobSystem::IsInitialized()) {
            Axle::JobSystem::Shutdown();
        }
        Axle::JobSystem::Init(4);
        const Axle::u32 epoch1 = Axle::JobSystem::GetEpoch();

        std::atomic<bool> task1Done{false};
        auto handle1 = Axle::JobSystem::Dispatch([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            task1Done.store(true);
        });

        if (!handle1.IsValid() || handle1.GetEpoch() != epoch1) {
            std::cerr << "[EJ07] FAILED: Handle1 invalid or epoch mismatch\n";
            return 1;
        }

        // Ensure all scheduler worker threads are occupied so handleCancel is queued
        std::atomic<bool> blocker2Done{false};
        auto blocker2 = Axle::JobSystem::Dispatch([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            blocker2Done.store(true);
        });
        std::atomic<bool> blocker3Done{false};
        auto blocker3 = Axle::JobSystem::Dispatch([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            blocker3Done.store(true);
        });

        // Test Cancellation
        std::atomic<bool> cancelledTaskRan{false};
        auto handleCancel = Axle::JobSystem::Dispatch([&] {
            cancelledTaskRan.store(true);
        });
        bool didCancel = handleCancel.Cancel();
        handleCancel.Wait();
        blocker2.Wait();
        blocker3.Wait();
        if (didCancel && cancelledTaskRan.load()) {
            std::cerr << "[EJ07] FAILED: Cancelled task ran despite cancellation\n";
            return 1;
        }
        if (didCancel && !handleCancel.IsCancelled()) {
            std::cerr << "[EJ07] FAILED: Cancelled handle does not report IsCancelled\n";
            return 1;
        }

        // Test Detach
        std::atomic<bool> detachTaskDone{false};
        auto handleDetach = Axle::JobSystem::Dispatch([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            detachTaskDone.store(true);
        });
        handleDetach.Detach();
        if (handleDetach.IsValid()) {
            std::cerr << "[EJ07] FAILED: Detached handle still valid\n";
            return 1;
        }

        // Call Shutdown() while handle1 is still outstanding and held
        Axle::JobSystem::Shutdown();

        if (Axle::JobSystem::IsInitialized()) {
            std::cerr << "[EJ07] FAILED: JobSystem still reported initialized after Shutdown\n";
            return 1;
        }

        // Handle1 must report complete and Wait() must return safely without crash
        if (!handle1.IsComplete()) {
            std::cerr << "[EJ07] FAILED: Handle1 not complete after Shutdown\n";
            return 1;
        }
        if (!task1Done.load()) {
            std::cerr << "[EJ07] FAILED: Task1 did not finish during draining\n";
            return 1;
        }
        handle1.Wait(); // Must return safely without crash

        // Detached task must have finished safely during draining
        for (int i = 0; i < 50 && !detachTaskDone.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!detachTaskDone.load()) {
            std::cerr << "[EJ07] FAILED: Detached task did not complete during draining\n";
            return 1;
        }

        std::cout << "[EJ07] Subtest 1 PASS: Asynchronous handles retain scheduler state across shutdown\n";
    }

    // 2. Repeated Startup/Shutdown Cycles & Reentrancy / Non-Owner Rejection
    {
        for (int cycle = 0; cycle < 3; ++cycle) {
            Axle::JobSystem::Init(2);

            Axle::OperationOwner owner;
            std::atomic<int> completedTasks{0};

            for (int j = 0; j < 8; ++j) {
                Axle::JobSystem::Dispatch([&, j] {
                    Axle::JobSystem::PostToMainThread(owner.GetToken(), [&, j] {
                        completedTasks.fetch_add(1, std::memory_order_relaxed);
                    });
                });
            }

            // Wait a moment for workers to post completions
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Axle::JobSystem::RunPinnedTasks();

            // Attempt shutdown from background non-owner thread (MUST FAIL)
            std::atomic<bool> nonOwnerRan{false};
            std::thread bgThread([&] {
                Axle::JobSystem::Shutdown();
                nonOwnerRan.store(true);
            });
            bgThread.join();

            if (!nonOwnerRan.load() || !Axle::JobSystem::IsInitialized()) {
                std::cerr << "[EJ07] FAILED: Non-owner thread shutdown succeeded or hung in cycle " << cycle << "\n";
                return 1;
            }

            // Attempt shutdown from inside an admitted task callback (MUST FAIL)
            std::atomic<bool> inTaskShutdownAttempted{false};
            auto inTaskHandle = Axle::JobSystem::Dispatch([&] {
                Axle::JobSystem::Shutdown();
                inTaskShutdownAttempted.store(true);
            });
            inTaskHandle.Wait();

            if (!inTaskShutdownAttempted.load() || !Axle::JobSystem::IsInitialized()) {
                std::cerr << "[EJ07] FAILED: In-task shutdown succeeded or hung in cycle " << cycle << "\n";
                return 1;
            }

            // Proper owner shutdown
            Axle::JobSystem::Shutdown();

            if (Axle::JobSystem::IsInitialized()) {
                std::cerr << "[EJ07] FAILED: JobSystem not stopped after cycle " << cycle << "\n";
                return 1;
            }
        }
        std::cout << "[EJ07] Subtest 2 PASS: Repeated startup/shutdown cycles and rejection protections verified\n";
    }

    // 3. Full Teardown Sequencing Order (PhysicsScene -> Deferred Drainage -> JobSystem)
    {
        Axle::JobSystem::Init(4);
        if (!Axle::PhysicsEngine::IsInitialized()) {
            Axle::PhysicsEngine::Init();
        }

        Axle::OperationOwner sceneOwner;
        std::atomic<int> sceneCompletionsRan{0};

        {
            auto physicsScene = std::make_unique<Axle::PhysicsScene>();
            Axle::BoxBodyProps boxProps;
            boxProps.Position = glm::vec3(0.0f, 5.0f, 0.0f);
            boxProps.HalfExtent = glm::vec3(0.5f);
            physicsScene->CreateBox(boxProps);

            // Step simulation to run Jolt jobs through JobSystemEnkiTS
            physicsScene->Step(Axle::Timestep(0.016f));

            // Queue deferred completions
            Axle::JobSystem::PostToMainThread(sceneOwner.GetToken(), [&] {
                sceneCompletionsRan.fetch_add(1);
            });

            // Destroy PhysicsScene: verifies m_JobSystem->Shutdown() inside destructor drains all physics jobs cleanly
            physicsScene.reset();
        }

        // Invalidate scene owner and pump completions: ensures no callback runs on destroyed scene
        sceneOwner.Invalidate();
        Axle::u32 pumped = Axle::JobSystem::PumpMainThreadCompletions();
        (void)pumped;
        if (sceneCompletionsRan.load() > 0) {
            std::cerr << "[EJ07] FAILED: Completion ran after owner invalidation!\n";
            return 1;
        }

        // Shut down physics engine and job system in order
        Axle::PhysicsEngine::Shutdown();
        Axle::JobSystem::Shutdown();

        if (Axle::JobSystem::IsInitialized() || Axle::PhysicsEngine::IsInitialized()) {
            std::cerr << "[EJ07] FAILED: Subsystems still report initialized after final teardown!\n";
            return 1;
        }

        std::cout << "[EJ07] Subtest 3 PASS: Full teardown sequencing verified without leaks or errors\n";
    }

    std::cout << "RESULT ej07-handles-shutdown PASS" << std::endl;
    return 0;
}

// ============================================================================
// ECSJ-04: EnTT Stable Storage Tombstone Iteration Probe
// ============================================================================
struct StableTombstoneComponent {
    static constexpr bool in_place_delete = true;
    int Value = 0;
};

struct ExtraComponent {
    int Tag = 0;
};

int RunEnTTTombstoneProbe() {
    Axle::JobSystem::Init(4);
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

    // Architectural assertion: Parallel iteration must match sequential view and skip tombstones
    if (sequentialCount != parallelCount.load() || invalidCallbacks.load() != 0) {
        std::cerr << "DEFECT REPRODUCED [ECSJ-04]: ParallelECS::ForEach raw packed iteration visited tombstones\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // High-volume multi-component iteration test with interleaved tombstones
    constexpr size_t totalTestEntities = 2000;
    std::vector<entt::entity> allEntities;
    allEntities.reserve(totalTestEntities);
    for (size_t i = 0; i < totalTestEntities; ++i) {
        auto ent = registry.create();
        allEntities.push_back(ent);
        registry.emplace<StableTombstoneComponent>(ent, static_cast<int>(i));
        if (i % 2 == 0) {
            registry.emplace<ExtraComponent>(ent, static_cast<int>(i * 10));
        }
    }

    // Delete 1 in 3 to create numerous tombstones in StableTombstoneComponent pool
    for (size_t i = 0; i < totalTestEntities; i += 3) {
        registry.remove<StableTombstoneComponent>(allEntities[i]);
    }

    // Collect expected entities sequentially for multi-component view
    std::unordered_set<entt::entity> expectedMulti;
    for (auto ent : registry.view<StableTombstoneComponent, ExtraComponent>()) {
        expectedMulti.insert(ent);
    }

    std::mutex collectedMutex;
    std::unordered_set<entt::entity> parallelMulti;
    std::atomic<size_t> multiInvalid{0};

    Axle::ParallelECS::ForEach<StableTombstoneComponent, ExtraComponent>(registry,
        [&](entt::entity ent, StableTombstoneComponent& stc, ExtraComponent& ec, uint32_t threadIndex) {
            (void)threadIndex;
            if (!registry.valid(ent) || ent == entt::tombstone) {
                multiInvalid.fetch_add(1);
                return;
            }
            if (stc.Value * 10 != ec.Tag) {
                multiInvalid.fetch_add(1);
                return;
            }
            std::lock_guard<std::mutex> lock(collectedMutex);
            parallelMulti.insert(ent);
        }, 16);

    if (multiInvalid.load() != 0 || expectedMulti != parallelMulti) {
        std::cerr << "DEFECT [ECSJ-04]: ParallelECS::ForEach multi-component mismatch or invalid callbacks. Expected: "
                  << expectedMulti.size() << ", got: " << parallelMulti.size() << ", invalid: " << multiInvalid.load() << "\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    // ForEachEntity deduplication and tombstone filtering test
    std::vector<entt::entity> duplicateQueryList;
    duplicateQueryList.reserve(allEntities.size() * 2 + 10);
    // Add valid and invalid entities multiple times
    for (size_t i = 0; i < 100; ++i) {
        duplicateQueryList.push_back(allEntities[i]);
        duplicateQueryList.push_back(allEntities[i]); // duplicate
    }
    duplicateQueryList.push_back(entt::null);
    duplicateQueryList.push_back(entt::tombstone);

    std::atomic<size_t> forEachEntityCount{0};
    std::atomic<size_t> forEachEntityInvalid{0};
    std::mutex seenEntitiesMutex;
    std::unordered_set<entt::entity> seenInForEachEntity;

    Axle::ParallelECS::ForEachEntity<StableTombstoneComponent>(registry, duplicateQueryList,
        [&](entt::entity ent, StableTombstoneComponent& comp, uint32_t threadIndex) {
            (void)comp;
            (void)threadIndex;
            if (!registry.valid(ent) || ent == entt::tombstone || ent == entt::null) {
                forEachEntityInvalid.fetch_add(1);
                return;
            }
            forEachEntityCount.fetch_add(1);
            std::lock_guard<std::mutex> lock(seenEntitiesMutex);
            if (!seenInForEachEntity.insert(ent).second) {
                // duplicate visited!
                forEachEntityInvalid.fetch_add(1);
            }
        }, 8);

    if (forEachEntityInvalid.load() != 0) {
        std::cerr << "DEFECT [ECSJ-04]: ParallelECS::ForEachEntity visited duplicate or invalid entities\n";
        Axle::JobSystem::Shutdown();
        return 1;
    }

    Axle::JobSystem::Shutdown();

    std::cout << "sequential=" << sequentialCount
              << " parallel=" << parallelCount.load()
              << " multiCount=" << parallelMulti.size()
              << " dedupCount=" << forEachEntityCount.load()
              << " invalidCallbacks=0" << std::endl;

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
                  << "  ej01-review-fixes\n"
                  << "  ej02-deferred-executor\n"
                  << "  ej03-compiler-handoff\n"
                  << "  ej04-reload-commit\n"
                  << "  ej05-physics-lifetime\n"
                  << "  ej06-physics-waits\n"
                  << "  ej07-handles-shutdown\n"
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
        if (scenario == "ej01-review-fixes") return finish(RunEJ01ReviewFixProbe());
        if (scenario == "ej02-deferred-executor") return finish(RunEJ02DeferredExecutorProbe());
        if (scenario == "ej03-compiler-handoff") return finish(RunEJ03CompilerHandoffProbe(workspace));
        if (scenario == "ej04-reload-commit") return finish(RunEJ04ReloadCommitProbe(workspace));
        if (scenario == "ej05-physics-lifetime") return finish(RunEJ05PhysicsLifetimeProbe());
        if (scenario == "ej06-physics-waits") return finish(RunEJ06PhysicsWaitsProbe());
        if (scenario == "ej07-handles-shutdown") return finish(RunEJ07HandlesShutdownProbe());
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
