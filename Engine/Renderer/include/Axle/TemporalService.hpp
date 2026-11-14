#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Axle {

    struct TemporalHistorySelection {
        uint8_t ReadSlot = 0;
        uint8_t WriteSlot = 1;
        bool HistoryValid = false;
        bool MotionValid = false;
        bool DisocclusionValid = false;
        glm::vec2 CurrentJitter{ 0.0f };
        glm::vec2 PreviousJitter{ 0.0f };
    };

    // Owns view-local temporal history state. The renderer may borrow the
    // selected slots while recording, but only this service flips or validates
    // history after a successful frame.
    class TemporalService {
    public:
        void Reset(uint32_t resetReasonMask = 0xffffffffu);

        TemporalHistorySelection Begin(uint8_t historyCount, const glm::vec2& currentJitter = {});
        void Commit(const glm::mat4& currentViewProjection, bool historyWritten);
        void Invalidate() { Reset(); }

        bool IsHistoryValid() const { return m_HistoryValid; }
        bool IsMotionValid() const { return m_MotionValid; }
        bool IsDisocclusionValid() const { return m_DisocclusionValid; }
        uint8_t GetReadSlot() const { return m_ReadSlot; }
        uint8_t GetWriteSlot() const { return m_WriteSlot; }
        uint64_t GetCommittedFrameCount() const { return m_CommittedFrameCount; }
        const glm::mat4& GetPreviousViewProjection() const { return m_PreviousViewProjection; }
        const glm::vec2& GetPreviousJitter() const { return m_PreviousJitter; }

    private:
        uint8_t m_ReadSlot = 0;
        uint8_t m_WriteSlot = 1;
        bool m_HistoryValid = false;
        bool m_MotionValid = false;
        bool m_DisocclusionValid = false;
        glm::vec2 m_CurrentJitter{ 0.0f };
        glm::vec2 m_PreviousJitter{ 0.0f };
        glm::mat4 m_PreviousViewProjection{ 1.0f };
        uint64_t m_CommittedFrameCount = 0;
    };
}
