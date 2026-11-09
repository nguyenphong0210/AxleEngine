#include "RenderTestCommon.hpp"
#include <Axle/RenderWorker.hpp>
#include <chrono>
#include <thread>

namespace Axle::RenderTests {

    bool RunRenderWorkerUnitTests() {
        std::cout << "  [RenderWorker] Running worker ownership/lifecycle tests...\n";
        RenderWorker worker;
        RenderWorkerConfig config;
        config.Device.Headless = true;
        config.Device.AllowFallback = false;
        config.Device.PreferredBackend = RHI::RHIType::Direct3D12;
        config.QueueCapacity = 2;
        RENDER_CHECK(worker.Start(config), "worker thread starts once");
        for (int i = 0; i < 5000 && worker.GetState() == RenderWorkerState::Starting; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto state = worker.GetState();
        RENDER_CHECK(state == RenderWorkerState::Running || state == RenderWorkerState::Failed,
            "worker reaches running or explicit startup failure");

        RenderWorkerFrame frame;
        frame.RequestId = 1;
        frame.Request.Width = 1;
        frame.Request.Height = 1;
        const bool submitted = worker.Submit(std::move(frame));
        if (state == RenderWorkerState::Running) {
            RENDER_CHECK(submitted, "running worker admits an immutable frame");
            RenderWorkerResult result;
            bool polled = false;
            for (int i = 0; i < 500 && !polled; ++i) {
                polled = worker.Poll(result);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            RENDER_CHECK(polled, "worker publishes a bounded result for every accepted frame");
            RENDER_CHECK(result.RequestId == 1 && result.Result.Success,
                "native worker executes and reports a successful offscreen render");
            RENDER_CHECK(result.StateEpoch != 0 && RHI::RHIDevice::Get().GetCapabilities().SupportsRenderWorkerOwnership,
                "successful native worker probe publishes ownership capability and epoch");
        } else {
            RENDER_CHECK(!submitted, "failed worker rejects frames without native ownership");
        }
        worker.Stop();
        RENDER_CHECK(worker.GetState() == RenderWorkerState::Stopped, "worker stop joins and returns to stopped");
        RENDER_CHECK(!worker.Submit(RenderWorkerFrame{}), "stopped worker rejects new frames");
        std::cout << "  [RenderWorker] All worker lifecycle tests passed.\n";
        return true;
    }

}
