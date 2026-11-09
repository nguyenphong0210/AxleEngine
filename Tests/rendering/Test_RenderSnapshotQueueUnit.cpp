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
        RENDER_CHECK(queue.Push(Ticket(2)) == RenderSnapshotPushResult::Accepted, "second snapshot accepted");
        RENDER_CHECK(queue.Push(Ticket(3)) == RenderSnapshotPushResult::Accepted, "full queue replaces stale unprotected snapshots");
        auto stats = queue.GetStats();
        RENDER_CHECK(stats.Depth == 1 && stats.HighWaterMark == 2 && stats.Replaced == 2 && stats.Dropped == 2,
            "queue pressure replacement is bounded and observable");

        auto latest = queue.PopLatest();
        RENDER_CHECK(latest && latest->Sequence == 3 && !latest->ProtectedRequest && queue.GetStats().Depth == 0,
            "PopLatest returns the newest replacement");
        RenderSnapshotQueue protectedFull(2);
        protectedFull.Push(Ticket(20, true));
        protectedFull.Push(Ticket(21, true));
        RENDER_CHECK(protectedFull.Push(Ticket(22)) == RenderSnapshotPushResult::RejectedFull &&
            protectedFull.GetStats().ProtectedRejected == 1 && protectedFull.GetStats().Depth == 2,
            "full protected queue rejects without dropping protected work");
        RenderSnapshotQueue protectedQueue(4);
        protectedQueue.Push(Ticket(10, true));
        protectedQueue.Push(Ticket(11, true));
        auto firstProtected = protectedQueue.PopLatest();
        auto secondProtected = protectedQueue.PopLatest();
        RENDER_CHECK(firstProtected && secondProtected && firstProtected->Sequence == 10 && secondProtected->Sequence == 11,
            "PopLatest never discards a second protected request");
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
        RENDER_CHECK(stats.Accepted == accepted.load() && stats.Consumed == consumed.load() + stats.Dropped,
            "concurrent producer/consumer accounting is exact");
        RENDER_CHECK(stats.Depth == 0 && stats.Accepted + stats.RejectedFull == total,
            "concurrent queue remains bounded with no lost accounting");
        std::cout << "  [RenderSnapshotQueue] All bounded queue tests passed.\n";
        return true;
    }

}
