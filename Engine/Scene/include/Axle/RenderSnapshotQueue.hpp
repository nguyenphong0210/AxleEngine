#pragma once

#include <Axle/RenderWorld.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Axle {

    // An immutable scene snapshot plus the metadata needed to make queue
    // pressure and latency observable. LeaseAnchor keeps any external resource
    // generations alive until the consumer releases the ticket.
    struct RenderSnapshotTicket {
        RenderWorld World;
        uint64_t Sequence = 0;
        uint64_t SimulationTick = 0;
        uint64_t SceneRevision = 0;
        uint64_t ResourceEpoch = 0;
        uint64_t ExtractedAtNs = 0;
        bool ProtectedRequest = false;
        std::shared_ptr<const void> LeaseAnchor;
    };

    enum class RenderSnapshotPushResult {
        Accepted,
        RejectedFull,
        RejectedStopping,
        InvalidCapacity
    };

    struct RenderSnapshotQueueStats {
        uint64_t Capacity = 0;
        uint64_t Depth = 0;
        uint64_t HighWaterMark = 0;
        uint64_t NextSequence = 0;
        uint64_t Accepted = 0;
        uint64_t Consumed = 0;
        uint64_t RejectedFull = 0;
        uint64_t RejectedStopping = 0;
        uint64_t Drained = 0;
    };

    // Bounded sequence-number queue. It is safe for the normal one-producer /
    // one-consumer path and uses the same per-slot sequence protocol for a
    // producer and consumer that overlap during stop/drain. It never allocates
    // after construction and never silently drops a protected ticket.
    class RenderSnapshotQueue {
    public:
        explicit RenderSnapshotQueue(uint32_t capacity = 3);
        ~RenderSnapshotQueue();

        RenderSnapshotQueue(const RenderSnapshotQueue&) = delete;
        RenderSnapshotQueue& operator=(const RenderSnapshotQueue&) = delete;

        RenderSnapshotPushResult Push(RenderSnapshotTicket ticket);
        std::optional<RenderSnapshotTicket> Pop();
        std::optional<RenderSnapshotTicket> PopLatest();

        void Stop();
        bool IsStopping() const { return m_Stopping.load(std::memory_order_acquire); }
        uint32_t Capacity() const { return m_Capacity; }
        RenderSnapshotQueueStats GetStats() const;
        uint64_t Drain();

    private:
        struct Slot {
            std::atomic<uint64_t> Sequence{0};
            std::optional<RenderSnapshotTicket> Ticket;
        };

        bool TryReserveProducer(uint64_t& position, Slot*& slot);
        bool TryReserveConsumer(uint64_t& position, Slot*& slot);
        uint64_t Depth() const;

        const uint32_t m_Capacity;
        std::vector<Slot> m_Slots;
        std::atomic<uint64_t> m_EnqueuePosition{0};
        std::atomic<uint64_t> m_DequeuePosition{0};
        std::atomic<uint64_t> m_NextSequence{1};
        std::atomic<uint64_t> m_HighWaterMark{0};
        std::atomic<uint64_t> m_Accepted{0};
        std::atomic<uint64_t> m_Consumed{0};
        std::atomic<uint64_t> m_RejectedFull{0};
        std::atomic<uint64_t> m_RejectedStopping{0};
        std::atomic<uint64_t> m_Drained{0};
        std::atomic<bool> m_Stopping{false};
    };

}
