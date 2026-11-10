#include "RenderTestCommon.hpp"
#include <Axle/RenderSnapshotQueue.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <atomic>
#include <algorithm>
#include <barrier>
#include <filesystem>
#include <thread>
#include <vector>

using namespace Axle;
extern thread_local bool FailRenderOwnerAllocation;
bool RunRenderTeardownAllocationStress();

namespace {
    struct Payload {
        RenderSnapshotQueue& Queue;
        std::atomic<uint64_t>& Released;
        ~Payload() { (void)Queue.GetStats(); ++Released; }
    };
    RenderSnapshotTicket Ticket(RenderSnapshotQueue& queue, std::atomic<uint64_t>& released, bool protect = false) {
        RenderSnapshotTicket ticket;
        ticket.ProtectedRequest = protect;
        ticket.LeaseAnchor = std::shared_ptr<Payload>(new Payload{queue, released});
        return ticket;
    }
    bool SnapshotStress() {
        std::atomic<uint64_t> released{0};
        {
            RenderSnapshotQueue queue(3);
            for (int i = 0; i < 3; ++i) queue.Push(Ticket(queue, released));
            RENDER_CHECK(queue.Push(Ticket(queue, released)) == RenderSnapshotPushResult::Accepted && released == 3,
                "full replacement releases payloads outside the queue mutex");
            queue.Push(Ticket(queue, released)); queue.Push(Ticket(queue, released));
            auto latest = queue.PopLatest();
            RENDER_CHECK(latest && released == 5, "PopLatest discard permits reentrant payload cleanup");
            latest.reset();
            queue.Push(Ticket(queue, released, true)); queue.Push(Ticket(queue, released, true));
            RENDER_CHECK(queue.Drain() == 2 && released == 8, "explicit drain releases each payload exactly once outside lock");
            queue.Push(Ticket(queue, released));
        }
        RENDER_CHECK(released == 9, "queue destruction also permits destructor queries");
        std::cerr << "Snapshot stress: teardown allocation guard\n";
        auto dying = std::make_unique<RenderSnapshotQueue>(2);
        dying->Push(Ticket(*dying, released)); dying->Push(Ticket(*dying, released));
        FailRenderOwnerAllocation = true;
        dying.reset();
        const bool allocationFree = std::exchange(FailRenderOwnerAllocation, false);
        std::cerr << "Snapshot stress: queue teardown complete\n";
        RENDER_CHECK(allocationFree && released == 11 && RunRenderTeardownAllocationStress(), "queue and stopped worker destructors allocate no retirement scratch under memory pressure");
        for (unsigned run = 0; run < 16; ++run) {
            RenderSnapshotQueue queue(7);
            std::atomic<uint64_t> created{0}, destroyed{0}, popped{0}, attempted{0};
            std::atomic<unsigned> finished{0};
            std::barrier start(5);
            std::vector<std::jthread> producers;
            for (unsigned p = 0; p < 4; ++p) producers.emplace_back([&, p] {
                start.arrive_and_wait();
                for (unsigned i = 0; i < 1200; ++i) {
                    ++created; ++attempted;
                    queue.Push(Ticket(queue, destroyed, (i + p) % 5 == 0));
                    if (i == 600 && p == run % 4) queue.Stop();
                }
                ++finished;
            });
            start.arrive_and_wait();
            while (finished != 4 || queue.GetStats().Depth) {
                if ((popped.load() & 1) ? queue.Pop().has_value() : queue.PopLatest().has_value()) ++popped;
                else std::this_thread::yield();
            }
            producers.clear(); queue.Stop(); queue.Drain();
            const auto stats = queue.GetStats();
            RENDER_CHECK(destroyed == created && stats.Depth == 0 && stats.HighWaterMark <= 7,
                "all concurrent payloads release exactly once within fixed capacity");
            RENDER_CHECK(stats.Accepted + stats.RejectedFull + stats.RejectedStopping == attempted &&
                stats.Accepted == popped + stats.Dropped + stats.Drained,
                "producer/consumer/stop race reconciles every accepted and rejected snapshot");
        }
        return true;
    }
}
bool RunRenderWorkerStress(RHI::RHIType backend, bool exception);
bool RunRenderPluginStress(const std::filesystem::path& library, const std::string& config, RHI::RHIType backend);
bool RunRenderTransientStress(RHI::RHIType backend, const std::string& terminal);
bool RunRenderSyncStress(RHI::RHIType backend);
int main(int argc, char** argv) {
    RenderTests::ConfigureFailureReporting(); Log::Init();
    bool ok = false;
    try {
        const std::string mode = argc > 1 ? argv[1] : "policy";
        const std::vector<std::string> modes{"policy", "plugin", "dx12_plugin", "vulkan_plugin", "dx12_native", "vulkan_native",
            "dx12_submit", "vulkan_submit", "dx12_lost", "vulkan_lost", "dx12_worker", "vulkan_worker", "dx12_exception", "vulkan_exception", "dx12_sync", "vulkan_sync"};
        if (std::find(modes.begin(), modes.end(), mode) == modes.end()) throw std::runtime_error("Unknown stress mode: " + mode);
        if (mode.ends_with("plugin")) ok = argc == 4 && RunRenderPluginStress(argv[2], argv[3], mode == "plugin" ? RHI::RHIType::None :
            mode.starts_with("vulkan") ? RHI::RHIType::Vulkan : RHI::RHIType::Direct3D12);
        else if (mode.ends_with("sync")) ok = RunRenderSyncStress(mode.starts_with("vulkan") ? RHI::RHIType::Vulkan : RHI::RHIType::Direct3D12);
        else if (mode.ends_with("native") || mode.ends_with("lost") || mode.ends_with("submit"))
            ok = RunRenderTransientStress(mode.starts_with("vulkan") ? RHI::RHIType::Vulkan : RHI::RHIType::Direct3D12,
                mode.ends_with("lost") ? "lost" : mode.ends_with("submit") ? "submit" : "normal");
        else ok = mode == "policy" ? SnapshotStress() : RunRenderWorkerStress(
            mode.starts_with("vulkan") ? RHI::RHIType::Vulkan : RHI::RHIType::Direct3D12, mode.ends_with("exception"));
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    RHI::RHIDevice::Get().Shutdown();
    ok = RenderTests::Check(RHI::GetRHIValidationErrorCount() == 0, "validation including stress teardown", __FILE__, __LINE__) && ok;
    std::cout << "Render lifetime stress: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
