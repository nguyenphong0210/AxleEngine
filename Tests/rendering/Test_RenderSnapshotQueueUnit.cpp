#include "RenderTestCommon.hpp"
#include <Axle/RenderSnapshotQueue.hpp>
#include <atomic>
#include <thread>

namespace Axle::RenderTests {

    static RenderSnapshotTicket Ticket(uint64_t sequence, bool protectedRequest = false) {
        RenderSnapshotTicket ticket;
        ticket.Sequence = sequence;
        ticket.SimulationTick = sequence * 10;
        ticket.SceneRevision = sequence;
        ticket.ProtectedRequest = protectedRequest;
        ticket.World.SceneRevision = sequence;
        return ticket;
    }

    bool RunRenderSnapshotQueueUnitTests() {
        std::cout << "  [RenderSnapshotQueue] Running bounded snapshot queue tests...\n";
        RenderSnapshotQueue queue(2);
        RENDER_CHECK(queue.Capacity() == 2 && queue.GetStats().Depth == 0, "fixed queue capacity and empty start");
        RENDER_CHECK(queue.Push(Ticket(1)) == RenderSnapshotPushResult::Accepted, "first snapshot accepted");
        RENDER_CHECK(queue.Push(Ticket(2, true)) == RenderSnapshotPushResult::Accepted, "protected snapshot accepted");
        RENDER_CHECK(queue.Push(Ticket(3)) == RenderSnapshotPushResult::RejectedFull, "full queue rejects unprotected snapshot");
        auto stats = queue.GetStats();
        RENDER_CHECK(stats.Depth == 2 && stats.HighWaterMark == 2 && stats.RejectedFull == 1,
            "queue pressure is bounded and observable");

        auto latest = queue.PopLatest();
        RENDER_CHECK(latest && latest->Sequence == 2 && latest->ProtectedRequest && queue.GetStats().Depth == 0,
            "PopLatest preserves the admitted protected snapshot");
        RENDER_CHECK(queue.Push(Ticket(4)) == RenderSnapshotPushResult::Accepted, "queue accepts after consumption");
        queue.Stop();
        RENDER_CHECK(queue.Push(Ticket(5)) == RenderSnapshotPushResult::RejectedStopping,
            "stopped queue rejects new snapshots");
        RENDER_CHECK(queue.Drain() == 1 && queue.GetStats().Drained == 1, "stop drain releases pending snapshot");

        RenderSnapshotQueue concurrent(64);
        constexpr uint64_t total = 2000;
        std::atomic<uint64_t> accepted{0};
        std::atomic<uint64_t> consumed{0};
        std::atomic<bool> producerDone{false};
        std::thread producer([&] {
            for (uint64_t i = 1; i <= total; ++i) {
                if (concurrent.Push(Ticket(i)) == RenderSnapshotPushResult::Accepted) accepted.fetch_add(1);
                std::this_thread::yield();
            }
            producerDone.store(true, std::memory_order_release);
        });
        std::thread consumer([&] {
            while (!producerDone.load(std::memory_order_acquire) || concurrent.GetStats().Depth) {
                if (concurrent.Pop()) consumed.fetch_add(1);
                else std::this_thread::yield();
            }
        });
        producer.join(); consumer.join();
        concurrent.Drain();
        stats = concurrent.GetStats();
        RENDER_CHECK(stats.Accepted == accepted.load() && stats.Consumed == consumed.load(),
            "concurrent producer/consumer accounting is exact");
        RENDER_CHECK(stats.Depth == 0 && stats.Accepted + stats.RejectedFull == total,
            "concurrent queue remains bounded with no lost accounting");
        std::cout << "  [RenderSnapshotQueue] All bounded queue tests passed.\n";
        return true;
    }

}
