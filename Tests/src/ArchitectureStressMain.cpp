#include <Axle/JobSystem.hpp>
#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/SceneCommandBuffer.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/Log.hpp>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Axle;
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Counter { unsigned Value = 0; };

void Run(unsigned threads) {
    JobSystem::Init(threads);
    Require(JobSystem::GetTotalThreadCount() == threads, "requested scheduler size");
    if (!PhysicsEngine::IsInitialized()) PhysicsEngine::Init();
    const auto ownerThread = std::this_thread::get_id();
    unsigned delivered = 0;
    bool wrongOwner = false;
    for (unsigned generation = 0; generation < 32; ++generation) {
        OperationOwner owner;
        const auto old = owner.GetToken();
        std::atomic<unsigned> admitted{0};
        std::thread producer([&] {
            for (unsigned item = 0; item < 16; ++item)
                admitted += JobSystem::PostToMainThread(old, [&] { ++delivered; }) == PostResult::Success;
        });
        producer.join();
        Require(admitted == 16 && delivered == generation, "external completion stays deferred");
        owner.Reset();
        Require(JobSystem::PostToMainThread(owner.GetToken(), [&] {
            wrongOwner |= std::this_thread::get_id() != ownerThread;
            ++delivered;
        }) == PostResult::Success, "fresh generation admitted");
        JobSystem::PumpMainThreadCompletions(0);
        Require(delivered == generation + 1 && !wrongOwner, "stale generation discarded on owner");
    }
    {
        JobSystemEnkiTS bridge(4, 2);
        std::atomic<unsigned> completed{0};
        for (unsigned batch = 0; batch < 512; ++batch) {
            auto* barrier = bridge.CreateBarrier();
            Require(barrier != nullptr, "constrained barrier capacity reclaimed");
            for (unsigned item = 0; item < 4; ++item) {
                auto job = bridge.CreateJob("OPT10 reuse", JPH::Color::sGreen, [&] { ++completed; });
                Require(job.IsValid(), "constrained job capacity reclaimed");
                barrier->AddJob(job);
            }
            bridge.WaitForJobs(barrier);
            bridge.DestroyBarrier(barrier);
        }
        Require(bridge.WaitForLiveTasks() && bridge.GetLiveTaskCount() == 0 && completed == 2048,
                "actual enki completion before reclamation");
        Require(bridge.GetAllocatedTaskCount() <= 8, "submission storage stays bounded across 512 batches");
        std::cout << "STRESS threads=" << threads << " jobs=" << completed
                  << " high_water=" << bridge.GetAllocatedHighWaterMark()
                  << " records=" << bridge.GetAllocatedTaskCount() << '\n';
    }
    {
        SceneData scene;
        for (unsigned index = 0; index < 2048; ++index) scene.CreateEntity().AddComponent<Counter>();
        for (unsigned batch = 0; batch < 8; ++batch) {
            ParallelECS::ForEach<Counter>(scene.GetRegistry(), [](Counter& counter) { ++counter.Value; }, 8);
            unsigned live = 0;
            for (auto handle : scene.GetRegistry().view<Counter>()) {
                Require(scene.GetRegistry().get<Counter>(handle).Value == batch + 1, "exact entity visit set");
                if (live++ % 3 == 0) scene.GetCommandBuffer().DestroyEntity(Entity(handle, &scene));
            }
            scene.ExecuteCommandBuffer();
            scene.UpdateWorldTransformsParallel(8);
        }
        auto old = scene.CreateEntity();
        SceneCommandBuffer commands;
        commands.DestroyEntity(old);
        scene.Clear();
        auto fresh = scene.CreateEntity();
        commands.Execute(scene);
        Require(bool(fresh) && commands.GetLastRejectedCount() == 1, "stale scene command cannot destroy replacement");
    }
    {
        PhysicsScene first, second;
        const auto a = first.CreateBox({0, 12, 0}, {0.5f, 0.5f, 0.5f});
        const auto b = second.CreateBox({0, 25, 0}, {0.5f, 0.5f, 0.5f});
        Require(!a.IsInvalid() && !b.IsInvalid(), "two real physics worlds");
        for (unsigned tick = 0; tick < 120; ++tick) {
            auto unrelated = JobSystem::Dispatch([] {});
            first.Step(1.0f / 60.0f);
            second.Step(1.0f / 60.0f);
            unrelated.Wait();
        }
        const float y1 = first.GetBodyPosition(a).y, y2 = second.GetBodyPosition(b).y;
        Require(std::isfinite(y1) && std::isfinite(y2) && y1 < 12 && y2 < 25 && std::abs(y2-y1-13) < 0.001f,
                "two worlds progress independently while unrelated work completes");
    }
    JobSystem::Shutdown();
}
}
int main() {
    Log::Init();
    try {
        for (unsigned threads : {1u, 2u, std::max(1u, std::thread::hardware_concurrency())}) Run(threads);
        PhysicsEngine::Shutdown();
        std::cout << "ARCHITECTURE_STRESS PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ARCHITECTURE_STRESS FAIL: " << error.what() << '\n';
        JobSystem::Shutdown(); PhysicsEngine::Shutdown();
        return 1;
    }
}
