#pragma once

#include <Axle/RenderWorld.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace Axle {

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
        uint64_t ProtectedRejected = 0;
        uint64_t BlockedProducers = 0;
        uint64_t Replaced = 0;
        uint64_t Dropped = 0;
        uint64_t LastConsumedSequence = 0;
        uint64_t OldestAgeNs = 0;
        uint64_t Drained = 0;
    };

    // A fixed-capacity, mutex-protected ring. Push, Pop and Stop share one
    // admission boundary, so stop cannot race a producer and protected-ticket
    // replacement remains atomic. Ring/survivor storage is reserved during
    // construction; discard paths use bounded temporary retirement storage so
    // payload destructors execute after unlocking (and may query the queue).
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
        bool IsStopping() const;
        uint32_t Capacity() const { return m_Capacity; }
        RenderSnapshotQueueStats GetStats() const;
        uint64_t Drain();

    private:
        uint32_t LogicalIndex(uint32_t offset) const;
        void RemoveLogical(uint32_t offset);
        void RefreshOldestAnchor();
        uint64_t OldestAgeNs() const;

        const uint32_t m_Capacity;
        std::vector<std::optional<RenderSnapshotTicket>> m_Entries;
        std::vector<RenderSnapshotTicket> m_Scratch;
        uint32_t m_ReadIndex = 0;
        uint32_t m_WriteIndex = 0;
        uint32_t m_Count = 0;
        uint64_t m_NextSequence = 1;
        uint64_t m_HighWaterMark = 0;
        uint64_t m_Accepted = 0;
        uint64_t m_Consumed = 0;
        uint64_t m_RejectedFull = 0;
        uint64_t m_RejectedStopping = 0;
        uint64_t m_ProtectedRejected = 0;
        uint64_t m_Replaced = 0;
        uint64_t m_Dropped = 0;
        uint64_t m_LastConsumedSequence = 0;
        uint64_t m_OldestExtractedAtNs = 0;
        uint64_t m_Drained = 0;
        bool m_Stopping = false;
        mutable std::mutex m_Mutex;
    };

}
