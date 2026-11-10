#include "RenderTestCommon.hpp"
#include <Axle/RenderSnapshotQueue.hpp>
#include <Axle/Log.hpp>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

using namespace Axle;

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
int main() {
    RenderTests::ConfigureFailureReporting(); Log::Init();
    bool ok = false;
    try { ok = SnapshotStress(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    std::cout << "Render lifetime stress: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
