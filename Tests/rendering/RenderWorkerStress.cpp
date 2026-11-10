#include "RenderTestCommon.hpp"
#include <Axle/RenderWorker.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <new>
#include <set>

// Test-only, one-shot allocation failure on the actual render owner. It is
// armed by a completed frame's payload, never by timing or a producer thread.
thread_local bool FailRenderOwnerAllocation = false;
void* operator new(std::size_t size) {
    if (std::exchange(FailRenderOwnerAllocation, false)) throw std::bad_alloc();
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace Axle;
using namespace std::chrono_literals;
namespace {
    template<class F> bool Until(F&& ready) {
        const auto end = std::chrono::steady_clock::now() + 15s;
        while (!ready()) { if (std::chrono::steady_clock::now() >= end) return false; std::this_thread::sleep_for(1ms); }
        return true;
    }
    struct Gate {
        std::promise<void> Entered, Release;
        std::shared_future<void> Open = Release.get_future().share();
        std::atomic<bool> Released{false};
        void Unblock() { if (!Released.exchange(true)) Release.set_value(); }
        ~Gate() { Unblock(); }
    };
    RenderWorkerFrame Frame(uint64_t id) {
        RenderWorkerFrame frame; frame.RequestId = id; frame.Request.Width = frame.Request.Height = 8;
        return frame;
    }
}
bool RunRenderWorkerStress(RHI::RHIType backend, bool exception) {
    for (unsigned cycle = 0; cycle < (exception ? 2u : 6u); ++cycle) {
        RenderWorker worker;
        auto gate = std::make_shared<Gate>();
        struct OpenOnExit { std::shared_ptr<Gate> Value; ~OpenOnExit() { Value->Unblock(); } } open{gate};
        RenderWorkerConfig config; config.QueueCapacity = 4;
        config.Device.PreferredBackend = backend; config.Device.AllowFallback = false; config.Device.Headless = true;
        config.Device.EnableNRIValidation = true;
        config.Device.EnableValidation = RHI::RHIDevice::IsGraphicsAPIValidationAvailable(backend);
        RENDER_CHECK(config.Device.EnableValidation, "native worker stress requires API validation");
        RENDER_CHECK(worker.Start(config) && Until([&] { return worker.GetState() != RenderWorkerState::Starting; }) &&
            worker.GetState() == RenderWorkerState::Running, "strict worker startup reaches real native renderer");
        const auto epoch = worker.GetStateEpoch();
        auto first = Frame(1);
        if (exception) first.World.ErrorMessage = "first deterministic frame error";
        first.LeaseAnchor = std::shared_ptr<void>(new int, [&, gate](void* p) {
            delete static_cast<int*>(p);
            (void)worker.GetState(); gate->Entered.set_value(); gate->Open.wait();
            if (exception) FailRenderOwnerAllocation = true;
        });
        RENDER_CHECK(worker.Submit(std::move(first)) && gate->Entered.get_future().wait_for(15s) == std::future_status::ready,
            "gate proves active payload cleanup occurs on worker outside its mutex");
        std::atomic<unsigned> retired{0};
        for (uint64_t id = 2; id <= 4; ++id) {
            auto frame = Frame(id);
            frame.LeaseAnchor = std::shared_ptr<void>(new int, [&](void* p) {
                delete static_cast<int*>(p); (void)worker.GetState(); ++retired;
            });
            RENDER_CHECK(worker.Submit(std::move(frame)), "gated worker admits bounded pending requests");
        }
        RENDER_CHECK(!worker.Submit(Frame(5)), "in-flight/result pressure rejects over-capacity admission");
        if (exception) {
            gate->Unblock();
            RENDER_CHECK(Until([&] { return worker.GetState() == RenderWorkerState::Stopped; }), "allocation exception cannot escape native thread entry");
            RENDER_CHECK(worker.GetLastError() == "first deterministic frame error", "first error survives later worker exception");
            worker.Stop();
        } else {
            std::vector<std::jthread> producers;
            for (unsigned p = 0; p < 3; ++p) producers.emplace_back([&, p] {
                for (unsigned n = 0; n < 300; ++n) worker.Submit(Frame(100 + p * 300 + n));
            });
            auto stopped = std::async(std::launch::async, [&] { worker.Stop(); });
            RENDER_CHECK(Until([&] { return retired == 3; }), "Stop cancels queued payloads outside mutex while active work is gated");
            gate->Unblock();
            RENDER_CHECK(stopped.wait_for(15s) == std::future_status::ready, "stop join finishes after active CPU cleanup releases");
            stopped.get(); producers.clear();
        }
        RENDER_CHECK(!worker.Start(config), "restart cannot discard unpolled terminal results");
        std::set<uint64_t> ids; unsigned successful = 0, cancelled = 0;
        RenderWorkerResult result;
        while (worker.Poll(result)) {
            RENDER_CHECK(ids.insert(result.RequestId).second && result.StateEpoch >= epoch, "unique terminal result and monotonic state epoch");
            if (result.Result.Success) ++successful; else ++cancelled;
        }
        RENDER_CHECK(ids == std::set<uint64_t>({1, 2, 3, 4}) && worker.GetAcceptedFrames() == 4 && retired == 3 &&
            successful == (exception ? 0u : 1u) && cancelled == (exception ? 4u : 3u),
            "every accepted request reconciles to success, failure or cancellation exactly once");
        RENDER_CHECK(worker.Start(config) && Until([&] { return worker.GetState() != RenderWorkerState::Starting; }) &&
            worker.GetState() == RenderWorkerState::Running && worker.GetStateEpoch() > epoch, "result drain permits clean native restart");
        worker.Stop();
    }
    return true;
}
