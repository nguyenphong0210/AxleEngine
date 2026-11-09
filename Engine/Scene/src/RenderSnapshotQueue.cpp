#include <Axle/RenderSnapshotQueue.hpp>

#include <algorithm>

namespace Axle {

    RenderSnapshotQueue::RenderSnapshotQueue(uint32_t capacity)
        : m_Capacity(capacity), m_Slots(capacity) {
        for (uint64_t i = 0; i < m_Slots.size(); ++i) m_Slots[i].Sequence.store(i, std::memory_order_relaxed);
    }
    RenderSnapshotQueue::~RenderSnapshotQueue() { Drain(); }

    bool RenderSnapshotQueue::TryReserveProducer(uint64_t& position, Slot*& slot) {
        if (!m_Capacity) return false;
        position = m_EnqueuePosition.load(std::memory_order_relaxed);
        for (;;) {
            slot = &m_Slots[position % m_Capacity];
            const auto sequence = slot->Sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<int64_t>(sequence - position);
            if (difference == 0) {
                if (m_EnqueuePosition.compare_exchange_weak(position, position + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) return true;
            } else if (difference < 0) return false;
            else position = m_EnqueuePosition.load(std::memory_order_relaxed);
        }
    }

    bool RenderSnapshotQueue::TryReserveConsumer(uint64_t& position, Slot*& slot) {
        if (!m_Capacity) return false;
        position = m_DequeuePosition.load(std::memory_order_relaxed);
        for (;;) {
            slot = &m_Slots[position % m_Capacity];
            const auto sequence = slot->Sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<int64_t>(sequence - (position + 1));
            if (difference == 0) {
                if (m_DequeuePosition.compare_exchange_weak(position, position + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) return true;
            } else if (difference < 0) return false;
            else position = m_DequeuePosition.load(std::memory_order_relaxed);
        }
    }

    uint64_t RenderSnapshotQueue::Depth() const {
        const auto write = m_EnqueuePosition.load(std::memory_order_acquire);
        const auto read = m_DequeuePosition.load(std::memory_order_acquire);
        return write >= read ? write - read : 0;
    }

    RenderSnapshotPushResult RenderSnapshotQueue::Push(RenderSnapshotTicket ticket) {
        if (m_Stopping.load(std::memory_order_acquire)) {
            m_RejectedStopping.fetch_add(1, std::memory_order_relaxed);
            return RenderSnapshotPushResult::RejectedStopping;
        }
        if (!m_Capacity) return RenderSnapshotPushResult::InvalidCapacity;
        uint64_t position = 0; Slot* slot = nullptr;
        if (!TryReserveProducer(position, slot)) {
            m_RejectedFull.fetch_add(1, std::memory_order_relaxed);
            return RenderSnapshotPushResult::RejectedFull;
        }
        if (!ticket.Sequence) ticket.Sequence = m_NextSequence.fetch_add(1, std::memory_order_relaxed);
        else {
            auto next = m_NextSequence.load(std::memory_order_relaxed);
            while (next <= ticket.Sequence && !m_NextSequence.compare_exchange_weak(next, ticket.Sequence + 1,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {}
        }
        slot->Ticket.emplace(std::move(ticket));
        slot->Sequence.store(position + 1, std::memory_order_release);
        m_Accepted.fetch_add(1, std::memory_order_relaxed);
        const auto depth = Depth();
        auto high = m_HighWaterMark.load(std::memory_order_relaxed);
        while (high < depth && !m_HighWaterMark.compare_exchange_weak(high, depth,
                std::memory_order_relaxed, std::memory_order_relaxed)) {}
        return RenderSnapshotPushResult::Accepted;
    }

    std::optional<RenderSnapshotTicket> RenderSnapshotQueue::Pop() {
        uint64_t position = 0; Slot* slot = nullptr;
        if (!TryReserveConsumer(position, slot)) return std::nullopt;
        std::optional<RenderSnapshotTicket> result = std::move(slot->Ticket);
        slot->Ticket.reset();
        slot->Sequence.store(position + m_Capacity, std::memory_order_release);
        if (result) m_Consumed.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    std::optional<RenderSnapshotTicket> RenderSnapshotQueue::PopLatest() {
        std::optional<RenderSnapshotTicket> latest;
        while (auto next = Pop()) {
            if (next->ProtectedRequest) {
                if (!latest || !latest->ProtectedRequest) latest = std::move(next);
            } else if (!latest || !latest->ProtectedRequest) {
                latest = std::move(next);
            }
        }
        return latest;
    }

    void RenderSnapshotQueue::Stop() { m_Stopping.store(true, std::memory_order_release); }

    RenderSnapshotQueueStats RenderSnapshotQueue::GetStats() const {
        RenderSnapshotQueueStats stats;
        stats.Capacity = m_Capacity;
        stats.Depth = Depth();
        stats.HighWaterMark = m_HighWaterMark.load(std::memory_order_acquire);
        stats.NextSequence = m_NextSequence.load(std::memory_order_acquire);
        stats.Accepted = m_Accepted.load(std::memory_order_acquire);
        stats.Consumed = m_Consumed.load(std::memory_order_acquire);
        stats.RejectedFull = m_RejectedFull.load(std::memory_order_acquire);
        stats.RejectedStopping = m_RejectedStopping.load(std::memory_order_acquire);
        stats.Drained = m_Drained.load(std::memory_order_acquire);
        return stats;
    }

    uint64_t RenderSnapshotQueue::Drain() {
        uint64_t count = 0;
        while (Pop()) ++count;
        m_Drained.fetch_add(count, std::memory_order_relaxed);
        return count;
    }

}
