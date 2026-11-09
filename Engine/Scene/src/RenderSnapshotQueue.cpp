#include <Axle/RenderSnapshotQueue.hpp>

#include <chrono>

namespace Axle {

    static uint64_t NowNs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    RenderSnapshotQueue::RenderSnapshotQueue(uint32_t capacity)
        : m_Capacity(capacity), m_Entries(capacity), m_Scratch() {
        m_Scratch.reserve(capacity);
    }

    RenderSnapshotQueue::~RenderSnapshotQueue() { Drain(); }

    uint32_t RenderSnapshotQueue::LogicalIndex(uint32_t offset) const {
        return (m_ReadIndex + offset) % m_Capacity;
    }

    void RenderSnapshotQueue::RemoveLogical(uint32_t offset) {
        for (uint32_t i = offset; i + 1 < m_Count; ++i)
            m_Entries[LogicalIndex(i)] = std::move(m_Entries[LogicalIndex(i + 1)]);
        m_Entries[LogicalIndex(m_Count - 1)].reset();
        m_WriteIndex = LogicalIndex(m_Count - 1);
        --m_Count;
    }

    RenderSnapshotPushResult RenderSnapshotQueue::Push(RenderSnapshotTicket ticket) {
        std::lock_guard lock(m_Mutex);
        if (m_Stopping) {
            ++m_RejectedStopping;
            return RenderSnapshotPushResult::RejectedStopping;
        }
        if (!m_Capacity) return RenderSnapshotPushResult::InvalidCapacity;

        ticket.Sequence = m_NextSequence++;
        if (m_Count == m_Capacity) {
            uint32_t removed = 0;
            for (uint32_t i = m_Count; i > 0; --i) {
                const auto offset = i - 1;
                if (m_Entries[LogicalIndex(offset)] && !m_Entries[LogicalIndex(offset)]->ProtectedRequest) {
                    RemoveLogical(offset);
                    ++removed;
                    ++m_Replaced;
                    ++m_Dropped;
                    ++m_Consumed;
                }
            }
            if (!removed) {
                ++m_RejectedFull;
                ++m_ProtectedRejected;
                return RenderSnapshotPushResult::RejectedFull;
            }
        }

        m_Entries[m_WriteIndex].emplace(std::move(ticket));
        m_WriteIndex = (m_WriteIndex + 1) % m_Capacity;
        ++m_Count;
        ++m_Accepted;
        if (m_Count > m_HighWaterMark) m_HighWaterMark = m_Count;
        if (m_Count == 1) RefreshOldestAnchor();
        return RenderSnapshotPushResult::Accepted;
    }

    std::optional<RenderSnapshotTicket> RenderSnapshotQueue::Pop() {
        std::lock_guard lock(m_Mutex);
        if (!m_Count) return std::nullopt;
        auto result = std::move(m_Entries[m_ReadIndex]);
        m_Entries[m_ReadIndex].reset();
        m_ReadIndex = (m_ReadIndex + 1) % m_Capacity;
        --m_Count;
        ++m_Consumed;
        if (result) m_LastConsumedSequence = result->Sequence;
        RefreshOldestAnchor();
        return result;
    }

    std::optional<RenderSnapshotTicket> RenderSnapshotQueue::PopLatest() {
        std::lock_guard lock(m_Mutex);
        if (!m_Count) return std::nullopt;

        uint32_t selected = m_Count - 1;
        for (uint32_t i = 0; i < m_Count; ++i) {
            if (m_Entries[LogicalIndex(i)] && m_Entries[LogicalIndex(i)]->ProtectedRequest) {
                selected = i;
                break;
            }
        }

        auto result = std::move(m_Entries[LogicalIndex(selected)]);
        m_Scratch.clear();
        uint64_t discarded = 0;
        for (uint32_t i = 0; i < m_Count; ++i) {
            if (i == selected) continue;
            if (i < selected && m_Entries[LogicalIndex(i)] && !m_Entries[LogicalIndex(i)]->ProtectedRequest) {
                ++discarded;
                continue;
            }
            m_Scratch.push_back(std::move(*m_Entries[LogicalIndex(i)]));
        }
        for (auto& entry : m_Entries) entry.reset();
        m_ReadIndex = 0;
        m_WriteIndex = 0;
        m_Count = 0;
        for (auto& survivor : m_Scratch) {
            m_Entries[m_WriteIndex].emplace(std::move(survivor));
            m_WriteIndex = (m_WriteIndex + 1) % m_Capacity;
            ++m_Count;
        }
        ++m_Consumed;
        m_Consumed += discarded;
        m_Dropped += discarded;
        if (result) m_LastConsumedSequence = result->Sequence;
        RefreshOldestAnchor();
        return result;
    }

    void RenderSnapshotQueue::Stop() {
        std::lock_guard lock(m_Mutex);
        m_Stopping = true;
    }

    bool RenderSnapshotQueue::IsStopping() const {
        std::lock_guard lock(m_Mutex);
        return m_Stopping;
    }

    uint64_t RenderSnapshotQueue::OldestAgeNs() const {
        if (!m_OldestExtractedAtNs) return 0;
        const auto now = NowNs();
        return now >= m_OldestExtractedAtNs ? now - m_OldestExtractedAtNs : 0;
    }

    RenderSnapshotQueueStats RenderSnapshotQueue::GetStats() const {
        std::lock_guard lock(m_Mutex);
        RenderSnapshotQueueStats stats;
        stats.Capacity = m_Capacity;
        stats.Depth = m_Count;
        stats.HighWaterMark = m_HighWaterMark;
        stats.NextSequence = m_NextSequence;
        stats.Accepted = m_Accepted;
        stats.Consumed = m_Consumed;
        stats.RejectedFull = m_RejectedFull;
        stats.RejectedStopping = m_RejectedStopping;
        stats.ProtectedRejected = m_ProtectedRejected;
        stats.Replaced = m_Replaced;
        stats.Dropped = m_Dropped;
        stats.LastConsumedSequence = m_LastConsumedSequence;
        stats.OldestAgeNs = OldestAgeNs();
        stats.Drained = m_Drained;
        return stats;
    }

    void RenderSnapshotQueue::RefreshOldestAnchor() {
        if (!m_Count) {
            m_OldestExtractedAtNs = 0;
            return;
        }
        const auto& oldest = m_Entries[m_ReadIndex];
        m_OldestExtractedAtNs = oldest && oldest->ExtractedAtNs ? oldest->ExtractedAtNs : NowNs();
    }

    uint64_t RenderSnapshotQueue::Drain() {
        std::lock_guard lock(m_Mutex);
        const auto count = m_Count;
        for (auto& entry : m_Entries) entry.reset();
        m_ReadIndex = 0;
        m_WriteIndex = 0;
        m_Count = 0;
        m_Consumed += count;
        m_Drained += count;
        RefreshOldestAnchor();
        return count;
    }

}
