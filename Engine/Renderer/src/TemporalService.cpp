#include <Axle/TemporalService.hpp>

namespace Axle {

    void TemporalService::Reset(uint32_t) {
        m_ReadSlot = 0;
        m_WriteSlot = 1;
        m_HistoryValid = false;
        m_MotionValid = false;
        m_DisocclusionValid = false;
        m_CurrentJitter = {};
        m_PreviousJitter = {};
        m_PreviousViewProjection = glm::mat4(1.0f);
        m_CommittedFrameCount = 0;
    }

    TemporalHistorySelection TemporalService::Begin(uint8_t historyCount, const glm::vec2& currentJitter) {
        m_CurrentJitter = currentJitter;
        TemporalHistorySelection selection;
        selection.ReadSlot = m_ReadSlot;
        selection.WriteSlot = m_WriteSlot;
        selection.HistoryValid = historyCount >= 2 && m_HistoryValid;
        selection.MotionValid = selection.HistoryValid && m_MotionValid;
        selection.DisocclusionValid = selection.HistoryValid && m_DisocclusionValid;
        selection.CurrentJitter = m_CurrentJitter;
        selection.PreviousJitter = m_PreviousJitter;
        return selection;
    }

    void TemporalService::Commit(const glm::mat4& currentViewProjection, bool historyWritten) {
        if (!historyWritten) return;
        m_PreviousViewProjection = currentViewProjection;
        m_PreviousJitter = m_CurrentJitter;
        m_HistoryValid = true;
        m_MotionValid = true;
        m_DisocclusionValid = true;
        const uint8_t oldRead = m_ReadSlot;
        m_ReadSlot = m_WriteSlot;
        m_WriteSlot = oldRead;
        ++m_CommittedFrameCount;
    }
}
