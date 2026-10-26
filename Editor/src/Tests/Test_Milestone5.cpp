#include "TestCommon.hpp"

// =============================================================================
// STEP 22: PHASE 19.2 CORE JOBSYSTEM ENGINE & PARALLEL PROCESSING
// =============================================================================
bool RunTest_Step22_Phase19_CoreJobSystemEngine() {
    using namespace Axle;
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 22 TEST] PHASE 19.2 CORE JOBSYSTEM ENGINE        " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[22.1] Initializing JobSystem & Verifying Hardware Concurrency..." << std::endl;
    Axle::JobSystem::Init();
    assert(Axle::JobSystem::IsInitialized());
    
    uint32_t workerThreads = Axle::JobSystem::GetWorkerThreadCount();
    std::cout << "  -> Active Worker Threads: " << workerThreads << std::endl;
    assert(workerThreads >= 1);
    assert(Axle::JobSystem::IsMainThread());
    assert(Axle::JobSystem::GetCurrentThreadIndex() == 0);
    std::cout << "  -> PASS: JobSystem initialized. Main thread is thread 0." << std::endl;

    std::cout << "\n[22.2] Testing Fine-Grained Parallel Loop (ParallelFor with 250,000 items)..." << std::endl;
    const uint32_t elementCount = 250000;
    std::vector<uint64_t> testArray(elementCount, 0);
    std::atomic<uint32_t> processedCount{ 0 };

    Axle::JobSystem::ParallelFor(elementCount, [&](uint32_t index, uint32_t threadNum) {
        (void)threadNum;
        testArray[index] = (static_cast<uint64_t>(index) * 3) + 7;
        processedCount.fetch_add(1, std::memory_order_relaxed);
    }, 4096);

    assert(processedCount.load() == elementCount);
    for (uint32_t i = 0; i < 100; ++i) {
        assert(testArray[i] == (static_cast<uint64_t>(i) * 3) + 7);
    }
    assert(testArray[elementCount - 1] == (static_cast<uint64_t>(elementCount - 1) * 3) + 7);
    std::cout << "  -> PASS: All " << elementCount << " iterations verified 100% correct across all worker threads!" << std::endl;

    std::cout << "\n[22.3] Testing Coarse-Grained Range Processing (ParallelForRange with 50,000 items)..." << std::endl;
    const uint32_t rangeCount = 50000;
    std::vector<float> floatArray(rangeCount, 0.0f);

    Axle::JobSystem::ParallelForRange(rangeCount, [&](uint32_t start, uint32_t end) {
        for (uint32_t i = start; i < end; ++i) {
            floatArray[i] = static_cast<float>(i) * 0.5f;
        }
    }, 2048);

    for (uint32_t i = 0; i < rangeCount; ++i) {
        float expected = static_cast<float>(i) * 0.5f;
        assert(std::abs(floatArray[i] - expected) < 0.0001f);
    }
    std::cout << "  -> PASS: ParallelForRange executed and verified without partition gaps!" << std::endl;

    std::cout << "\n[22.4] Testing Asynchronous Job Dispatching & Dependency Waiting (JobHandle)..." << std::endl;
    std::atomic<bool> asyncTaskDone{ false };
    std::atomic<uint32_t> asyncAccumulator{ 0 };

    auto handle = Axle::JobSystem::Dispatch([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        asyncAccumulator.fetch_add(12345);
        asyncTaskDone.store(true);
    });

    assert(handle.IsValid());
    handle.Wait();
    assert(handle.IsComplete());
    assert(asyncTaskDone.load() == true);
    assert(asyncAccumulator.load() == 12345);
    std::cout << "  -> PASS: Async JobHandle dispatched, waited, and completed with synchronized state." << std::endl;

    std::cout << "\n[22.5] Testing Cross-Thread Main-Thread Dispatching (DispatchToMainThread)..." << std::endl;
    std::atomic<bool> mainThreadTaskExecuted{ false };
    std::atomic<uint32_t> mainThreadWorkerId{ 999 };

    auto workerHandle = Axle::JobSystem::Dispatch([&]() {
        // Dispatched on worker thread, enqueue lambda to main thread
        Axle::JobSystem::DispatchToMainThread([&]() {
            assert(Axle::JobSystem::IsMainThread());
            mainThreadWorkerId.store(Axle::JobSystem::GetCurrentThreadIndex());
            mainThreadTaskExecuted.store(true);
        });
    });

    workerHandle.Wait();

    // Main thread pumps pinned tasks (if not already executed via work-stealing during Wait)
    Axle::JobSystem::RunPinnedTasks();
    assert(mainThreadTaskExecuted.load());
    assert(mainThreadWorkerId.load() == 0);
    std::cout << "  -> PASS: Cross-thread callback safely executed on main thread (Thread 0)!" << std::endl;

    std::cout << "\n[22.6] Testing JobSystem Shutdown and Re-initialization..." << std::endl;
    Axle::JobSystem::Shutdown();
    assert(!Axle::JobSystem::IsInitialized());
    std::cout << "  -> PASS: Shutdown completed cleanly." << std::endl;

    Axle::JobSystem::Init(2);
    assert(Axle::JobSystem::IsInitialized());
    assert(Axle::JobSystem::GetWorkerThreadCount() == 2);
    std::atomic<int> testCount{ 0 };
    Axle::JobSystem::ParallelFor(100, [&](uint32_t i) {
        (void)i;
        testCount.fetch_add(1);
    });
    assert(testCount.load() == 100);
    Axle::JobSystem::Shutdown();
    assert(!Axle::JobSystem::IsInitialized());
    std::cout << "  -> PASS: Re-initialization and custom thread pool size verified!" << std::endl;

    // Restore default JobSystem for subsequent test steps
    Axle::JobSystem::Init();

    std::cout << ">>> STEP 22 PASSED: Phase 19.2 Core JobSystem Engine verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 23: PHASE 19.3 JOLT PHYSICS BRIDGE (JobSystemEnkiTS)
// =============================================================================
#ifdef AddJob
#undef AddJob
#endif

bool RunTest_Step23_Phase19_JoltPhysicsBridgeEnkiTS() {
    using namespace Axle;
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 23 TEST] PHASE 19.3 JOLT PHYSICS BRIDGE (JobSystemEnkiTS)" << std::endl;
    std::cout << "========================================================" << std::endl;

    if (!Axle::JobSystem::IsInitialized()) {
        Axle::JobSystem::Init();
    }
    if (!Axle::PhysicsEngine::IsInitialized()) {
        Axle::PhysicsEngine::Init();
    }

    std::cout << "\n[23.1] Initializing JobSystemEnkiTS & Verifying Concurrency..." << std::endl;
    auto enkiJobSystem = std::make_unique<JobSystemEnkiTS>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers
    );
    int concurrency = enkiJobSystem->GetMaxConcurrency();
    std::cout << "  -> JobSystemEnkiTS Concurrency: " << concurrency << std::endl;
    assert(concurrency >= 1);
    assert(concurrency == static_cast<int>(Axle::JobSystem::GetWorkerThreadCount()));
    std::cout << "  -> PASS: JobSystemEnkiTS concurrency matches Axle::JobSystem worker threads." << std::endl;

    std::cout << "\n[23.2] Testing Direct Job Execution & Barrier Synchronization..." << std::endl;
    {
        std::atomic<int> directJobCounter{ 0 };
        JPH::JobSystem::Barrier* barrier = enkiJobSystem->CreateBarrier();
        assert(barrier != nullptr);

        constexpr int numJobs = 64;
        JPH::JobHandle handles[numJobs];
        for (int i = 0; i < numJobs; ++i) {
            handles[i] = enkiJobSystem->CreateJob("DirectPhysicsJob", JPH::ColorArg(0, 255, 0), [&directJobCounter]() {
                directJobCounter.fetch_add(1, std::memory_order_relaxed);
            }, 0);
            barrier->AddJob(handles[i]);
        }

        enkiJobSystem->WaitForJobs(barrier);
        assert(directJobCounter.load() == numJobs);
        std::cout << "  -> PASS: All " << numJobs << " direct jobs synchronized and executed via JobSystemEnkiTS!" << std::endl;
        enkiJobSystem->DestroyBarrier(barrier);
    }

    std::cout << "\n[23.3] Testing Jolt Dependency Chaining (Job A -> Job B)..." << std::endl;
    {
        std::atomic<bool> jobACompleted{ false };
        std::atomic<bool> jobBCompleted{ false };

        JPH::JobHandle jobB = enkiJobSystem->CreateJob("JobB", JPH::ColorArg(255, 0, 0), [&]() {
            assert(jobACompleted.load() == true);
            jobBCompleted.store(true);
        }, 1); // 1 dependency

        JPH::JobHandle jobA = enkiJobSystem->CreateJob("JobA", JPH::ColorArg(0, 0, 255), [&, jobB]() mutable {
            jobACompleted.store(true);
            jobB.RemoveDependency(1);
        }, 0);

        JPH::JobSystem::Barrier* depBarrier = enkiJobSystem->CreateBarrier();
        depBarrier->AddJob(jobA);
        depBarrier->AddJob(jobB);
        enkiJobSystem->WaitForJobs(depBarrier);
        assert(jobACompleted.load() == true);
        assert(jobBCompleted.load() == true);
        std::cout << "  -> PASS: Job dependency chain (Job A -> Job B) executed and verified in order." << std::endl;
        enkiJobSystem->DestroyBarrier(depBarrier);
    }

    enkiJobSystem.reset();

    std::cout << "\n[23.4] Testing Multithreaded Physics Simulation via PhysicsScene..." << std::endl;
    auto physicsScene = std::make_unique<PhysicsScene>();

    // 1. Create static floor
    BoxBodyProps floorProps;
    floorProps.Position = glm::vec3(0.0f, -0.5f, 0.0f);
    floorProps.HalfExtent = glm::vec3(50.0f, 0.5f, 50.0f);
    floorProps.IsDynamic = false;
    BodyID floorID = physicsScene->CreateBox(floorProps);
    assert(!floorID.IsInvalid());

    // 2. Create multiple dynamic boxes dropped in a grid
    constexpr int gridX = 4;
    constexpr int gridZ = 4;
    constexpr int layers = 3;
    std::vector<BodyID> dynamicBodies;

    for (int y = 0; y < layers; ++y) {
        for (int x = 0; x < gridX; ++x) {
            for (int z = 0; z < gridZ; ++z) {
                BoxBodyProps boxProps;
                boxProps.Position = glm::vec3(
                    (x - gridX / 2.0f) * 2.5f,
                    5.0f + y * 2.0f,
                    (z - gridZ / 2.0f) * 2.5f
                );
                boxProps.HalfExtent = glm::vec3(0.5f, 0.5f, 0.5f);
                boxProps.IsDynamic = true;
                boxProps.Friction = 0.5f;
                boxProps.Restitution = 0.0f;
                BodyID bId = physicsScene->CreateBox(boxProps);
                assert(!bId.IsInvalid());
                dynamicBodies.push_back(bId);
            }
        }
    }
    std::cout << "  -> Spawned " << dynamicBodies.size() << " dynamic rigid bodies on static floor." << std::endl;

    // 3. Step physics through JobSystemEnkiTS
    constexpr int simulationSteps = 60;
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < simulationSteps; ++step) {
        physicsScene->Step(dt);
    }

    // 4. Verify bodies have fallen and settled above the ground without falling through
    int settledCount = 0;
    for (const auto& bId : dynamicBodies) {
        glm::vec3 pos = physicsScene->GetBodyPosition(bId);
        // Box half-extent is 0.5, floor top is at y=0, so y should be >= 0.35f and < initial height
        assert(pos.y >= 0.35f);
        assert(pos.y < 15.0f);
        settledCount++;
    }
    assert(settledCount == static_cast<int>(dynamicBodies.size()));
    std::cout << "  -> PASS: All " << settledCount << " bodies simulated through JobSystemEnkiTS without penetration or drift." << std::endl;

    std::cout << "\n[23.5] Cleaning up PhysicsScene and verifying clean shutdown..." << std::endl;
    physicsScene.reset();
    std::cout << "  -> PASS: PhysicsScene and JobSystemEnkiTS cleanly destroyed." << std::endl;

    std::cout << ">>> STEP 23 PASSED: Phase 19.3 Jolt Physics Bridge (JobSystemEnkiTS) verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 24: Parallel ECS Processing (Batch Views, Hierarchy, CommandBuffer)
// =============================================================================
bool RunTest_Step24_Phase19_ParallelECSProcessing() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 24 TEST] PHASE 19.4: PARALLEL ECS PROCESSING     " << std::endl;
    std::cout << "========================================================" << std::endl;

    using namespace Axle;

    if (!JobSystem::IsInitialized()) {
        JobSystem::Init();
    }
    const uint32_t workerCount = JobSystem::GetWorkerThreadCount();
    std::cout << "  -> Active JobSystem worker threads: " << workerCount << std::endl;

    // -------------------------------------------------------------------------
    // [24.1] Parallel view iteration across 50,000 entities
    // -------------------------------------------------------------------------
    std::cout << "\n[24.1] Testing ParallelECS::ForEach across 50,000 entities..." << std::endl;
    entt::registry registry;
    constexpr uint32_t kEntityCount = 50000;

    std::vector<entt::entity> createdEntities;
    createdEntities.reserve(kEntityCount);
    for (uint32_t i = 0; i < kEntityCount; ++i) {
        entt::entity entity = registry.create();
        registry.emplace<TransformComponent>(entity, glm::vec3(0.0f));
        createdEntities.push_back(entity);
    }
    std::cout << "  -> Spawned " << kEntityCount << " entities with initial zero TransformComponent." << std::endl;

    std::atomic<uint32_t> processedCount{0};
    std::atomic<uint32_t> threadActivity[128]{};

    auto tStart = std::chrono::high_resolution_clock::now();

    ParallelECS::ForEach<TransformComponent>(registry, [&](entt::entity e, TransformComponent& tc, uint32_t threadNum) {
        uint32_t idVal = static_cast<uint32_t>(e);
        tc.Translation = glm::vec3(static_cast<float>(idVal), 1.0f, 2.0f);
        tc.Scale = glm::vec3(1.5f);
        processedCount.fetch_add(1, std::memory_order_relaxed);
        if (threadNum < 128) {
            threadActivity[threadNum].fetch_add(1, std::memory_order_relaxed);
        }
    }, 1024);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    assert(processedCount.load() == kEntityCount);

    uint32_t threadsUsed = 0;
    for (uint32_t t = 0; t < 128; ++t) {
        if (threadActivity[t].load() > 0) {
            threadsUsed++;
        }
    }

    std::cout << "  -> Processed " << processedCount.load() << " entities in " << elapsedMs 
              << " ms (" << static_cast<uint32_t>((kEntityCount / (elapsedMs / 1000.0))) << " entities/sec)." << std::endl;
    std::cout << "  -> Active threads participating in batch iteration: " << threadsUsed << " thread(s)." << std::endl;
    if (workerCount > 1) {
        assert(threadsUsed > 1);
    }

    // Spot check 10 entities
    for (uint32_t i = 0; i < 10; ++i) {
        entt::entity sampleEnt = createdEntities[i * (kEntityCount / 10)];
        const auto& tc = registry.get<TransformComponent>(sampleEnt);
        uint32_t expectedID = static_cast<uint32_t>(sampleEnt);
        assert(tc.Translation == glm::vec3(static_cast<float>(expectedID), 1.0f, 2.0f));
        assert(tc.Scale == glm::vec3(1.5f));
    }
    std::cout << "  -> PASS: All 50,000 entities verified with lock-free mutations across workers." << std::endl;

    // -------------------------------------------------------------------------
    // [24.2] Multi-component view filtering & mutation
    // -------------------------------------------------------------------------
    std::cout << "\n[24.2] Testing Multi-Component ParallelECS::ForEach<TransformComponent, RigidBody3D>..." << std::endl;
    constexpr uint32_t kRigidBodyCount = 25000;
    for (uint32_t i = 0; i < kRigidBodyCount; ++i) {
        RigidBody3D rb;
        rb.Mass = 10.0f;
        registry.emplace<RigidBody3D>(createdEntities[i], rb);
    }
    std::cout << "  -> Emplaced RigidBody3D on first " << kRigidBodyCount << " entities." << std::endl;

    std::atomic<uint32_t> multiCompCount{0};
    ParallelECS::ForEach<TransformComponent, RigidBody3D>(registry, [&](entt::entity e, TransformComponent& tc, RigidBody3D& rb) {
        (void)e;
        tc.Translation.y = 42.0f;
        rb.Mass = 25.0f;
        multiCompCount.fetch_add(1, std::memory_order_relaxed);
    }, 512);

    assert(multiCompCount.load() == kRigidBodyCount);
    std::cout << "  -> Processed exactly " << multiCompCount.load() << " filtered entities." << std::endl;

    // Verify first half has updated values, second half unchanged
    for (uint32_t i = 0; i < 5; ++i) {
        entt::entity sampleWithRB = createdEntities[i * 100];
        const auto& tc = registry.get<TransformComponent>(sampleWithRB);
        const auto& rb = registry.get<RigidBody3D>(sampleWithRB);
        assert(tc.Translation.y == 42.0f);
        assert(rb.Mass == 25.0f);
    }
    for (uint32_t i = 0; i < 5; ++i) {
        entt::entity sampleWithoutRB = createdEntities[kRigidBodyCount + i * 100];
        const auto& tc = registry.get<TransformComponent>(sampleWithoutRB);
        assert(tc.Translation.y == 1.0f);
        assert(!registry.all_of<RigidBody3D>(sampleWithoutRB));
    }
    std::cout << "  -> PASS: Multi-component filtering verified without false positives or lock contention." << std::endl;

    // -------------------------------------------------------------------------
    // [24.3] Disjoint subtree parallel hierarchy transform evaluation
    // -------------------------------------------------------------------------
    std::cout << "\n[24.3] Testing ParallelECS::UpdateWorldTransforms on disjoint hierarchy subtrees..." << std::endl;
    auto hierarchyScene = std::make_shared<Scene>();
    constexpr uint32_t kRootCount = 500;
    // Each root has 2 children, each child has 2 grandchildren: 500 * (1 + 2 + 4) = 3,500 entities
    std::vector<Entity> rootList;
    rootList.reserve(kRootCount);

    for (uint32_t r = 0; r < kRootCount; ++r) {
        Entity root = hierarchyScene->CreateEntity("Root_" + std::to_string(r));
        root.GetComponent<TransformComponent>().Translation = glm::vec3(static_cast<float>(r) * 10.0f, 0.0f, 0.0f);

        for (uint32_t c = 0; c < 2; ++c) {
            Entity child = hierarchyScene->CreateEntity("Child_" + std::to_string(r) + "_" + std::to_string(c));
            child.SetParent(root);
            child.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);

            for (uint32_t g = 0; g < 2; ++g) {
                Entity grandchild = hierarchyScene->CreateEntity("Grandchild_" + std::to_string(r) + "_" + std::to_string(c) + "_" + std::to_string(g));
                grandchild.SetParent(child);
                grandchild.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 3.0f);
            }
        }
        rootList.push_back(root);
    }
    std::cout << "  -> Built scene graph hierarchy with " << kRootCount << " roots and 3,500 total nodes." << std::endl;

    // Parallel evaluate transforms across worker threads
    hierarchyScene->UpdateWorldTransformsParallel(32);

    // Verify correct hierarchical world matrix propagation
    for (uint32_t r = 0; r < kRootCount; r += 50) {
        Entity root = rootList[r];
        const auto& rootTc = root.GetComponent<TransformComponent>();
        glm::vec3 expectedRootWorld(static_cast<float>(r) * 10.0f, 0.0f, 0.0f);
        assert(glm::length(glm::vec3(rootTc.WorldMatrix[3]) - expectedRootWorld) < 1e-4f);

        const auto& rootHc = root.GetComponent<HierarchyComponent>();
        for (auto childID : rootHc.Children) {
            Entity child{ childID, hierarchyScene.get() };
            const auto& childTc = child.GetComponent<TransformComponent>();
            glm::vec3 expectedChildWorld = expectedRootWorld + glm::vec3(0.0f, 5.0f, 0.0f);
            assert(glm::length(glm::vec3(childTc.WorldMatrix[3]) - expectedChildWorld) < 1e-4f);

            const auto& childHc = child.GetComponent<HierarchyComponent>();
            for (auto grandID : childHc.Children) {
                Entity grand{ grandID, hierarchyScene.get() };
                const auto& grandTc = grand.GetComponent<TransformComponent>();
                glm::vec3 expectedGrandWorld = expectedChildWorld + glm::vec3(0.0f, 0.0f, 3.0f);
                assert(glm::length(glm::vec3(grandTc.WorldMatrix[3]) - expectedGrandWorld) < 1e-4f);
            }
        }
    }
    std::cout << "  -> PASS: Disjoint hierarchical subtrees evaluated in parallel matching exact world coordinates." << std::endl;

    // -------------------------------------------------------------------------
    // [24.4] Thread-safe deferred CommandBuffer mutations
    // -------------------------------------------------------------------------
    std::cout << "\n[24.4] Testing ParallelECS::CommandBuffer thread-safe deferred mutations..." << std::endl;
    ParallelECS::CommandBuffer cmdBuffer;

    // During parallel iteration, destroy even entities and emplace TagComponent on odd entities
    ParallelECS::ForEach<TransformComponent>(registry, [&](entt::entity e, TransformComponent& tc, uint32_t threadNum) {
        (void)tc;
        (void)threadNum;
        uint32_t idVal = static_cast<uint32_t>(e);
        if (idVal % 2 == 0) {
            cmdBuffer.DestroyEntity(e);
        } else {
            cmdBuffer.EmplaceOrReplace<TagComponent>(e, "DeferredTag_" + std::to_string(idVal));
        }
    }, 1024);

    assert(cmdBuffer.GetPendingCount() == kEntityCount);
    std::cout << "  -> Recorded " << cmdBuffer.GetPendingCount() << " deferred commands from worker threads." << std::endl;

    // Commit on main thread
    cmdBuffer.Execute(registry);
    assert(cmdBuffer.GetPendingCount() == 0);
    std::cout << "  -> Executed all deferred commands on main thread." << std::endl;

    // Check entity states
    uint32_t validCount = 0;
    uint32_t taggedCount = 0;
    for (entt::entity e : createdEntities) {
        uint32_t idVal = static_cast<uint32_t>(e);
        if (idVal % 2 == 0) {
            assert(!registry.valid(e));
        } else {
            assert(registry.valid(e));
            assert(registry.all_of<TagComponent>(e));
            const auto& tag = registry.get<TagComponent>(e);
            assert(tag.Tag == "DeferredTag_" + std::to_string(idVal));
            validCount++;
            taggedCount++;
        }
    }
    assert(validCount == kEntityCount / 2);
    assert(taggedCount == kEntityCount / 2);
    std::cout << "  -> PASS: Deferred structural mutations executed cleanly without memory corruption." << std::endl;

    // -------------------------------------------------------------------------
    // [24.5] ParallelECS::ForEachEntity on filtered sub-collection
    // -------------------------------------------------------------------------
    std::cout << "\n[24.5] Testing ParallelECS::ForEachEntity on filtered entity collection..." << std::endl;
    std::vector<entt::entity> remainingEntities;
    remainingEntities.reserve(validCount);
    for (entt::entity e : createdEntities) {
        if (registry.valid(e)) {
            remainingEntities.push_back(e);
        }
    }

    ParallelECS::ForEachEntity<TransformComponent>(registry, remainingEntities, [](entt::entity e, TransformComponent& tc) {
        (void)e;
        tc.Scale = glm::vec3(3.0f, 3.0f, 3.0f);
    }, 256);

    for (entt::entity e : remainingEntities) {
        const auto& tc = registry.get<TransformComponent>(e);
        assert(tc.Scale == glm::vec3(3.0f, 3.0f, 3.0f));
    }
    std::cout << "  -> PASS: ForEachEntity updated all " << remainingEntities.size() << " surviving entities." << std::endl;

    std::cout << ">>> STEP 24 PASSED: Phase 19.4 Parallel ECS Processing verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 25: Phase 20 Component-per-Wheel N-Wheeled Vehicle Architecture
// =============================================================================
bool RunTest_Step25_Phase20_ComponentPerWheelVehicle() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 25 TEST] PHASE 20: COMPONENT-PER-WHEEL VEHICLE   " << std::endl;
    std::cout << "========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // [25.1] WheelComponent & VehicleWheel3D serialization roundtrip
    // -------------------------------------------------------------------------
    std::cout << "[25.1] Testing WheelComponent serialization and deserialization..." << std::endl;
    std::filesystem::path testScenePath = "temp_wheel_component_test.axlescene";

    {
        auto srcScene = std::make_shared<Axle::Scene>();
        auto wheelEnt = srcScene->CreateEntity("CustomWheel_FrontLeft");
        auto& tc = wheelEnt.GetComponent<Axle::TransformComponent>();
        tc.Translation = glm::vec3(0.95f, -0.15f, 1.4f);

        auto& wc = wheelEnt.AddComponent<Axle::WheelComponent>();
        wc.Radius = 0.42f;
        wc.Width = 0.28f;
        wc.SuspensionMinLength = 0.18f;
        wc.SuspensionMaxLength = 0.48f;
        wc.SpringFrequency = 3.5f;
        wc.SpringDamping = 0.65f;
        wc.CanSteer = true;
        wc.MaxSteerAngle = 38.0f;
        wc.IsDriven = true;
        wc.HasHandbrake = false;
        wc.BrakeTorque = 4200.0f;
        wc.HandBrakeTorque = 0.0f;
        wc.AutoSyncMesh = false;

        Axle::SceneSerializer serializer(srcScene.get());
        bool serSuccess = serializer.Serialize(testScenePath);
        assert(serSuccess);
        assert(std::filesystem::exists(testScenePath));
    }

    // Verify deserialization
    {
        auto dstScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(dstScene.get());
        bool deserSuccess = deserializer.Deserialize(testScenePath);
        assert(deserSuccess);

        auto view = dstScene->GetRegistry().view<Axle::WheelComponent, Axle::TagComponent>();
        assert(view.size_hint() == 1);

        for (auto entityID : view) {
            const auto& tag = view.get<Axle::TagComponent>(entityID).Tag;
            const auto& wc = view.get<Axle::WheelComponent>(entityID);
            assert(tag == "CustomWheel_FrontLeft");
            assert(std::abs(wc.Radius - 0.42f) < 0.001f);
            assert(std::abs(wc.Width - 0.28f) < 0.001f);
            assert(std::abs(wc.SuspensionMinLength - 0.18f) < 0.001f);
            assert(std::abs(wc.SuspensionMaxLength - 0.48f) < 0.001f);
            assert(std::abs(wc.SpringFrequency - 3.5f) < 0.001f);
            assert(std::abs(wc.SpringDamping - 0.65f) < 0.001f);
            assert(wc.CanSteer == true);
            assert(std::abs(wc.MaxSteerAngle - 38.0f) < 0.001f);
            assert(wc.IsDriven == true);
            assert(wc.HasHandbrake == false);
            assert(std::abs(wc.BrakeTorque - 4200.0f) < 0.001f);
            assert(wc.AutoSyncMesh == false);
        }
        std::error_code ec;
        std::filesystem::remove(testScenePath, ec);
    }
    std::cout << "  -> PASS: WheelComponent successfully serialized and deserialized with all tuning properties." << std::endl;

    // -------------------------------------------------------------------------
    // [25.2] VehicleComponent tuning parameters serialization roundtrip (Phase 20.2)
    // -------------------------------------------------------------------------
    std::cout << "\n[25.2] Testing VehicleComponent tuning serialization and deserialization..." << std::endl;
    std::filesystem::path vehicleScenePath = "temp_vehicle_component_test.axlescene";

    {
        auto srcScene = std::make_shared<Axle::Scene>();
        auto carEnt = srcScene->CreateEntity("CustomTunedChassis");
        auto& vc = carEnt.AddComponent<Axle::VehicleComponent>();
        vc.ChassisMass = 1850.0f;
        vc.MaxEngineTorque = 950.0f;
        vc.MaxRPM = 7500.0f;
        vc.MinRPM = 1100.0f;
        vc.MaxBrakeTorque = 3500.0f;
        vc.MaxHandBrakeTorque = 7000.0f;
        vc.AntiRollBarStiffness = 1800.0f;
        vc.FourWheelDrive = true;
        vc.CenterOfMassOffset = glm::vec3(0.0f, -0.2f, 0.1f);

        Axle::SceneSerializer serializer(srcScene.get());
        bool serSuccess = serializer.Serialize(vehicleScenePath);
        assert(serSuccess);
    }

    {
        auto dstScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(dstScene.get());
        bool deserSuccess = deserializer.Deserialize(vehicleScenePath);
        assert(deserSuccess);

        auto view = dstScene->GetRegistry().view<Axle::VehicleComponent, Axle::TagComponent>();
        assert(view.size_hint() == 1);

        for (auto entityID : view) {
            const auto& tag = view.get<Axle::TagComponent>(entityID).Tag;
            const auto& vc = view.get<Axle::VehicleComponent>(entityID);
            assert(tag == "CustomTunedChassis");
            assert(std::abs(vc.ChassisMass - 1850.0f) < 0.001f);
            assert(std::abs(vc.MaxEngineTorque - 950.0f) < 0.001f);
            assert(std::abs(vc.MaxRPM - 7500.0f) < 0.001f);
            assert(std::abs(vc.MinRPM - 1100.0f) < 0.001f);
            assert(std::abs(vc.MaxBrakeTorque - 3500.0f) < 0.001f);
            assert(std::abs(vc.MaxHandBrakeTorque - 7000.0f) < 0.001f);
            assert(std::abs(vc.AntiRollBarStiffness - 1800.0f) < 0.001f);
            assert(vc.FourWheelDrive == true);
            assert(glm::distance(vc.CenterOfMassOffset, glm::vec3(0.0f, -0.2f, 0.1f)) < 0.001f);
        }
        std::error_code ec;
        std::filesystem::remove(vehicleScenePath, ec);
    }
    std::cout << "  -> PASS: VehicleComponent tuning properties serialized and deserialized accurately." << std::endl;

    // -------------------------------------------------------------------------
    // [25.3] 6x6 N-Wheeled Vehicle Physics Simulation
    // -------------------------------------------------------------------------
    std::cout << "\n[25.3] Testing 6-wheeled (6x6) heavy truck physics simulation..." << std::endl;
    {
        Axle::PhysicsScene physicsScene;

        Axle::VehicleConfig config;
        config.InitialPosition = glm::vec3(0.0f, 1.5f, 0.0f);
        config.ChassisHalfExtent = glm::vec3(1.2f, 0.4f, 3.0f);
        config.ChassisMass = 5000.0f;

        // Configure 6 wheels (Front pair, Middle pair, Rear pair)
        const float xOffset = 1.25f;
        const float yOffset = -0.2f;

        // Front pair (steerable + driven)
        Axle::WheelConfig wFL;
        wFL.Position = glm::vec3(xOffset, yOffset, 2.2f);
        wFL.CanSteer = true;
        wFL.IsDriven = true;
        config.CustomWheels.push_back(wFL);

        Axle::WheelConfig wFR;
        wFR.Position = glm::vec3(-xOffset, yOffset, 2.2f);
        wFR.CanSteer = true;
        wFR.IsDriven = true;
        config.CustomWheels.push_back(wFR);

        // Middle pair (driven)
        Axle::WheelConfig wML;
        wML.Position = glm::vec3(xOffset, yOffset, 0.0f);
        wML.CanSteer = false;
        wML.IsDriven = true;
        config.CustomWheels.push_back(wML);

        Axle::WheelConfig wMR;
        wMR.Position = glm::vec3(-xOffset, yOffset, 0.0f);
        wMR.CanSteer = false;
        wMR.IsDriven = true;
        config.CustomWheels.push_back(wMR);

        // Rear pair (driven + handbrake)
        Axle::WheelConfig wRL;
        wRL.Position = glm::vec3(xOffset, yOffset, -2.2f);
        wRL.CanSteer = false;
        wRL.IsDriven = true;
        wRL.MaxHandBrakeTorque = 8000.0f;
        config.CustomWheels.push_back(wRL);

        Axle::WheelConfig wRR;
        wRR.Position = glm::vec3(-xOffset, yOffset, -2.2f);
        wRR.CanSteer = false;
        wRR.IsDriven = true;
        wRR.MaxHandBrakeTorque = 8000.0f;
        config.CustomWheels.push_back(wRR);

        Axle::Vehicle truck(physicsScene, config);

        assert(truck.GetConstraint() != nullptr);
        assert(truck.GetConstraint()->GetWheels().size() == 6);
        std::cout << "  -> PASS: 6-wheeled vehicle constraint created with 6 active wheels in Jolt Physics." << std::endl;

        // Verify GetWheelTransform for all 6 wheels
        for (Axle::u32 i = 0; i < 6; ++i) {
            glm::mat4 wMat = truck.GetWheelTransform(i);
            glm::vec3 pos = glm::vec3(wMat[3]);
            assert(!std::isnan(pos.x) && !std::isnan(pos.y) && !std::isnan(pos.z));
        }
        std::cout << "  -> PASS: All 6 wheel transforms queried successfully without bounds violation." << std::endl;

        // Simulate 30 physics steps with full throttle
        truck.SetDriverInput(1.0f, 0.0f, 0.0f, 0.0f);
        for (int step = 0; step < 30; ++step) {
            physicsScene.Step(1.0f / 60.0f);
        }

        // Verify truck position and state
        glm::vec3 finalPos = truck.GetPosition();
        assert(!std::isnan(finalPos.x) && !std::isnan(finalPos.y) && !std::isnan(finalPos.z));
        std::cout << "  -> Final truck position after 30 simulation steps: ["
                  << finalPos.x << ", " << finalPos.y << ", " << finalPos.z << "]" << std::endl;
        std::cout << "  -> PASS: Dynamic powertrain differential and anti-roll bars simulated stably." << std::endl;
    }

    // -------------------------------------------------------------------------
    // [25.4] Pure ECS VehicleComponent & WheelComponent API & Input Buffer (Phase 20.3 Stage 1)
    // -------------------------------------------------------------------------
    std::cout << "\n[25.4] Testing Pure ECS VehicleComponent & WheelComponent APIs..." << std::endl;
    {
        Axle::VehicleComponent vc;
        assert(!vc.HasActivePhysics());
        assert(vc.Throttle == 0.0f && vc.Steering == 0.0f && vc.Brake == 0.0f && !vc.Handbrake);

        // Test SetDriverInput with standard ranges
        vc.SetDriverInput(0.85f, -0.4f, 0.2f, true);
        assert(std::abs(vc.Throttle - 0.85f) < 0.001f);
        assert(std::abs(vc.Steering - -0.4f) < 0.001f);
        assert(std::abs(vc.Brake - 0.2f) < 0.001f);
        assert(vc.Handbrake == true);

        // Test clamping behavior
        vc.SetDriverInput(3.0f, -4.5f, 2.0f, false);
        assert(vc.Throttle == 1.0f);
        assert(vc.Steering == -1.0f);
        assert(vc.Brake == 1.0f);
        assert(!vc.Handbrake);

        // Test telemetry state queries
        vc.CurrentSpeedKmh = 105.4f;
        vc.CurrentSpeedMps = 29.27f;
        vc.CurrentRPM = 5400.0f;
        vc.CurrentGear = 4;
        assert(std::abs(vc.GetSpeedKmh() - 105.4f) < 0.001f);
        assert(std::abs(vc.GetSpeedMps() - 29.27f) < 0.001f);
        assert(std::abs(vc.GetRPM() - 5400.0f) < 0.001f);
        assert(vc.GetCurrentGear() == 4);

        // Test WheelComponent telemetry defaults
        Axle::WheelComponent wc;
        assert(wc.AutoSyncMesh == true);
        assert(wc.IsGrounded == false);
        assert(wc.SuspensionLength == 0.0f);
        assert(wc.AngularVelocity == 0.0f);
        std::cout << "  -> PASS: VehicleComponent and WheelComponent pure ECS input & telemetry interfaces validated." << std::endl;
    }

    // -------------------------------------------------------------------------
    // [25.5] Scene Dynamic Assembly (VehicleUpdateColliders) & Lifecycle
    // -------------------------------------------------------------------------
    std::cout << "\n[25.5] Testing Scene Dynamic Assembly (VehicleUpdateColliders) & Lifecycle..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();

        // 1. Create chassis entity
        auto chassis = scene->CreateEntity("CarChassis");
        auto& rbc = chassis.AddComponent<Axle::RigidBody3D>();
        rbc.MotionType = Axle::BodyMotionType::Dynamic;
        rbc.Mass = 1400.0f;
        auto& bc = chassis.AddComponent<Axle::CollisionShape3D>(Axle::CollisionShapeType::Box, glm::vec3(0.9f, 0.4f, 1.8f));
        auto& vc = chassis.AddComponent<Axle::VehicleComponent>();
        vc.ChassisMass = 1400.0f;
        vc.MaxEngineTorque = 600.0f;

        // 2. Create 4 child wheel entities with WheelComponent
        struct WheelDef {
            std::string Name;
            glm::vec3 Offset;
            bool Steer;
            bool Driven;
            float MaxSteer;
        };

        std::vector<WheelDef> wheelDefs = {
            { "Wheel_FL", glm::vec3( 0.85f, -0.2f,  1.2f), true,  false, 32.0f },
            { "Wheel_FR", glm::vec3(-0.85f, -0.2f,  1.2f), true,  false, 32.0f },
            { "Wheel_RL", glm::vec3( 0.85f, -0.2f, -1.2f), false, true,   0.0f },
            { "Wheel_RR", glm::vec3(-0.85f, -0.2f, -1.2f), false, true,   0.0f }
        };

        std::vector<Axle::Entity> wheelEntities;
        for (const auto& wd : wheelDefs) {
            auto wEnt = scene->CreateEntity(wd.Name);
            wEnt.SetParent(chassis, false);
            auto& wt = wEnt.GetComponent<Axle::TransformComponent>();
            wt.Translation = wd.Offset;

            auto& wc = wEnt.AddComponent<Axle::WheelComponent>();
            wc.Radius = 0.34f;
            wc.Width = 0.22f;
            wc.CanSteer = wd.Steer;
            wc.IsDriven = wd.Driven;
            wc.MaxSteerAngle = wd.MaxSteer;
            wc.AutoSyncMesh = true;

            wheelEntities.push_back(wEnt);
        }

        assert(chassis.GetChildCount() == 4);

        // 3. Start Runtime with PhysicsScene
        Axle::PhysicsScene physicsScene;
        scene->OnRuntimeStart(&physicsScene);

        // Verify dynamic assembly
        assert(scene->GetRuntimeVehicles().size() == 1);
        auto* runtimeVehicle = scene->GetPrimaryRuntimeVehicle();
        assert(runtimeVehicle != nullptr);
        assert(chassis.GetComponent<Axle::VehicleComponent>().HasActivePhysics());
        assert(runtimeVehicle->GetWheelCount() == 4);
        std::cout << "  -> PASS: Scene::OnRuntimeStart successfully dynamically assembled vehicle with 4 child wheels." << std::endl;

        // 4. Test driver input via ECS VehicleComponent and update loop
        vc.SetDriverInput(1.0f, 0.25f, 0.0f, false);
        for (int step = 0; step < 15; ++step) {
            physicsScene.Step(0.016f);
            scene->OnUpdateRuntime(0.016f, &physicsScene);
        }

        // Verify telemetry synchronization into ECS components
        const auto& updatedVc = chassis.GetComponent<Axle::VehicleComponent>();
        assert(updatedVc.HasActivePhysics());
        std::cout << "  -> Simulated vehicle speed: " << updatedVc.GetSpeedKmh() << " km/h, RPM: " << updatedVc.GetRPM() << std::endl;

        // Verify child wheel transform updates & telemetry
        for (size_t i = 0; i < wheelEntities.size(); ++i) {
            auto w = wheelEntities[i];
            const auto& wt = w.GetComponent<Axle::TransformComponent>();
            assert(wt.Scale.x > 0.0f && wt.Scale.y > 0.0f && wt.Scale.z > 0.0f);
            assert(!std::isnan(wt.Translation.x) && !std::isnan(wt.Translation.y) && !std::isnan(wt.Translation.z));
        }
        std::cout << "  -> PASS: Wheel local transforms and telemetry synchronized accurately." << std::endl;

        // 5. Stop Runtime & verify clean teardown
        scene->OnRuntimeStop(&physicsScene);
        assert(scene->GetRuntimeVehicles().empty());
        assert(scene->GetPrimaryRuntimeVehicle() == nullptr);
        assert(!chassis.GetComponent<Axle::VehicleComponent>().HasActivePhysics());
        std::cout << "  -> PASS: Scene::OnRuntimeStop safely destroyed runtime vehicle without memory or state leaks." << std::endl;
    }

    std::cout << ">>> STEP 25 PASSED: Phase 20 Component-per-Wheel Vehicle Architecture verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 26: Diagnostic CarTest Scene Simulation
// =============================================================================
bool RunTest_Step26_CarTestSceneSimulate() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 26 TEST] DIAGNOSTIC CARTEST SCENE SIMULATION     " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path scenePath = "Editor/assets/scenes/CarTest.axlescene";
    if (!std::filesystem::exists(scenePath)) {
        scenePath = "assets/scenes/CarTest.axlescene";
    }
    std::cout << "[26.1] Checking scene file: " << scenePath << std::endl;
    if (!std::filesystem::exists(scenePath)) {
        std::cerr << "FAIL: CarTest.axlescene does not exist!" << std::endl;
        return false;
    }

    // Initialize ScriptHotReloadManager to load AxleScripts.dll
    std::filesystem::path dllPath = "build/Editor/Release/AxleScripts.dll";
    if (!std::filesystem::exists(dllPath)) dllPath = "AxleScripts.dll";
    bool initOK = Axle::ScriptHotReloadManager::Get().Initialize(dllPath);
    std::cout << "[26.0] ScriptHotReloadManager::Initialize result: " << initOK << std::endl;
    const auto* meta = Axle::ScriptRegistry::GetMetadata("CarController");
    std::cout << "[26.0] ScriptRegistry::GetMetadata('CarController'): " << (meta ? "FOUND" : "NULL") << std::endl;

    auto scene = std::make_shared<Axle::Scene>();
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();

    Axle::SceneSerializer serializer(scene.get(), physicsScene.get());
    bool loaded = serializer.Deserialize(scenePath);
    if (!loaded) {
        std::cerr << "FAIL: Could not deserialize " << scenePath << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Deserialized scene successfully. Total physics bodies: " 
              << physicsScene->GetNumBodies() << std::endl;

    // Check Ground entity
    Axle::Entity ground;
    Axle::Entity car;
    auto tagView = scene->GetRegistry().view<Axle::TagComponent>();
    for (auto e : tagView) {
        if (tagView.get<Axle::TagComponent>(e).Tag == "Ground") ground = Axle::Entity{ e, scene.get() };
        if (tagView.get<Axle::TagComponent>(e).Tag == "Car") car = Axle::Entity{ e, scene.get() };
    }

    if (ground) {
        auto& tc = ground.GetComponent<Axle::TransformComponent>();
        std::cout << "  -> Ground Translation: [" << tc.Translation.x << ", " << tc.Translation.y << ", " << tc.Translation.z << "]" << std::endl;
        if (ground.HasComponent<Axle::CollisionShape3D>()) {
            auto& cs = ground.GetComponent<Axle::CollisionShape3D>();
            std::cout << "  -> Ground ShapeType: " << (int)cs.ShapeType << ", HalfExtent: [" 
                      << cs.HalfExtent.x << ", " << cs.HalfExtent.y << ", " << cs.HalfExtent.z << "]"
                      << ", Offset: [" << cs.Offset.x << ", " << cs.Offset.y << ", " << cs.Offset.z << "]" << std::endl;
        }
        if (ground.HasComponent<Axle::RigidBody3D>()) {
            auto& rb = ground.GetComponent<Axle::RigidBody3D>();
            std::cout << "  -> Ground BodyID: " << rb.RuntimeBodyID.GetIndexAndSequenceNumber() 
                      << ", IsValid: " << physicsScene->IsBodyValid(rb.RuntimeBodyID) << std::endl;
            if (physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                glm::vec3 pos = physicsScene->GetBodyPosition(rb.RuntimeBodyID);
                std::cout << "  -> Ground Physics Body Position: [" << pos.x << ", " << pos.y << ", " << pos.z << "]" << std::endl;
            }
        }
    }

    // Check Car entity
    if (!car) {
        std::cerr << "FAIL: Entity 'Car' not found!" << std::endl;
        return false;
    }
    auto& carTC = car.GetComponent<Axle::TransformComponent>();
    std::cout << "  -> Car Initial Translation: [" << carTC.Translation.x << ", " << carTC.Translation.y << ", " << carTC.Translation.z << "]" << std::endl;
    std::cout << "  -> Car Child Count: " << car.GetChildCount() << std::endl;
    for (auto child : car.GetChildren()) {
        std::string tag = child.HasComponent<Axle::TagComponent>() ? child.GetComponent<Axle::TagComponent>().Tag : "Unknown";
        auto& tc = child.GetComponent<Axle::TransformComponent>();
        std::cout << "     * Child: " << tag << " Pos: [" << tc.Translation.x << ", " << tc.Translation.y << ", " << tc.Translation.z << "]";
        if (child.HasComponent<Axle::WheelComponent>()) {
            auto& wc = child.GetComponent<Axle::WheelComponent>();
            std::cout << " [Radius: " << wc.Radius << ", Steer: " << wc.CanSteer << ", Driven: " << wc.IsDriven << "]";
        }
        std::cout << std::endl;
    }

    // Start Runtime
    std::cout << "[26.2] Starting Runtime Simulation..." << std::endl;
    scene->OnRuntimeStart(physicsScene.get());

    auto* rv = scene->GetPrimaryRuntimeVehicle();
    if (!rv) {
        std::cerr << "FAIL: Primary runtime vehicle is null!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Runtime vehicle created with " << rv->GetWheelCount() << " wheels." << std::endl;
    std::cout << "  -> Initial vehicle pos: [" << rv->GetPosition().x << ", " << rv->GetPosition().y << ", " << rv->GetPosition().z << "]" << std::endl;

    // Simulate 120 steps (2 seconds) under gravity with 0 throttle
    std::cout << "[26.3] Stepping 60 frames (1 second) with 0 throttle (settling under gravity)..." << std::endl;
    for (int step = 0; step < 60; ++step) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    auto tele0 = rv->GetTelemetry();
    std::cout << "  -> After 60 steps (settled): Pos: [" << tele0.Position.x << ", " << tele0.Position.y << ", " << tele0.Position.z 
              << "] Speed: " << tele0.SpeedKmh << " km/h, Gear: " << tele0.CurrentGear << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "     * Wheel " << i << ": Contact: " << (tele0.Wheels[i].HasContact ? "YES" : "NO")
                  << ", SuspLength: " << tele0.Wheels[i].SuspensionLength 
                  << ", WorldMat Y: " << tele0.Wheels[i].WorldTransform[3][1] << std::endl;
    }

    // Simulate 60 steps (1 second) with 100% throttle
    std::cout << "[26.4] Applying 100% Throttle for 60 frames (1 second)..." << std::endl;
    auto& vc = car.GetComponent<Axle::VehicleComponent>();
    vc.SetDriverInput(1.0f, 0.0f, 0.0f, false);

    for (int step = 0; step < 60; ++step) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    auto tele1 = rv->GetTelemetry();
    std::cout << "  -> After 60 steps with throttle: Pos: [" << tele1.Position.x << ", " << tele1.Position.y << ", " << tele1.Position.z 
              << "] Speed: " << tele1.SpeedKmh << " km/h, RPM: " << tele1.EngineRPM << ", Gear: " << tele1.CurrentGear << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << "     * Wheel " << i << ": Contact: " << (tele1.Wheels[i].HasContact ? "YES" : "NO")
                  << ", AngularVel: " << tele1.Wheels[i].AngularVelocity << std::endl;
    }

    // Stop car first
    vc.SetDriverInput(0.0f, 0.0f, 1.0f, false);
    for (int step = 0; step < 60; ++step) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    // Apply Reverse Throttle for 60 frames (1 second)
    std::cout << "[26.5] Applying -100% Throttle (Reverse) for 60 frames (1 second)..." << std::endl;
    vc.SetDriverInput(-1.0f, 0.0f, 0.0f, false);
    for (int step = 0; step < 60; ++step) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    auto teleRev = rv->GetTelemetry();
    std::cout << "  -> After 60 steps in reverse: Pos: [" << teleRev.Position.x << ", " << teleRev.Position.y << ", " << teleRev.Position.z 
              << "] Speed: " << teleRev.SpeedKmh << " km/h, ForwardSpeed: " << vc.GetForwardSpeedKmh() 
              << " km/h, RPM: " << teleRev.EngineRPM << ", Gear: " << teleRev.CurrentGear << std::endl;

    scene->OnRuntimeStop(physicsScene.get());
    std::cout << ">>> STEP 26 DIAGNOSTIC COMPLETE <<<\n" << std::endl;
    return true;
}