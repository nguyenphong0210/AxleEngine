#include <Axle/SimulationClock.hpp>

namespace Axle {

    SimulationClock::SimulationClock(float fixedTimestep, float maxAccumulator)
        : m_FixedTimestep(fixedTimestep > 0.0f ? fixedTimestep : 1.0f / 60.0f),
          m_MaxAccumulator(maxAccumulator > 0.0f ? maxAccumulator : 0.2f) {
    }

    uint32_t SimulationClock::Advance(float rawDt) {
        m_Diagnostics.TotalFrames++;

        // 1. Validate raw delta time (reject non-finite and negative dt)
        if (std::isnan(rawDt) || std::isinf(rawDt) || rawDt < 0.0f) {
            m_Diagnostics.InvalidDtDrops++;
            return 0;
        }

        if (rawDt == 0.0f) {
            return 0;
        }

        // 2. Paused handling
        if (m_IsPaused) {
            if (m_SingleStepRequested) {
                m_SingleStepRequested = false;
                m_Diagnostics.TotalTicksExecuted++;
                m_Diagnostics.TotalSimulationTime += m_FixedTimestep;
                return 1;
            }
            return 0;
        }

        // 3. Speed handling (0 speed produces zero ticks)
        if (m_Speed <= 0.0f) {
            return 0;
        }

        float effectiveDt = rawDt * m_Speed;
        m_Diagnostics.TotalRealTime += rawDt;
        m_Accumulator += static_cast<double>(effectiveDt);

        // 4. Overload cap to prevent simulation spiral of death
        if (m_Accumulator > static_cast<double>(m_MaxAccumulator)) {
            m_Diagnostics.OverloadDrops++;
            if (m_OverloadPolicy == ClockOverloadPolicy::Clamp) {
                m_Accumulator = static_cast<double>(m_MaxAccumulator);
            } else {
                m_Accumulator = 0.0;
                return 0;
            }
        }

        // 5. Compute whole ticks (with 1e-6 epsilon to avoid float rounding edge drops)
        double fixedDt = static_cast<double>(m_FixedTimestep);
        uint32_t ticks = static_cast<uint32_t>((m_Accumulator + 1e-6) / fixedDt);
        if (ticks > 0) {
            m_Accumulator -= static_cast<double>(ticks) * fixedDt;
            if (m_Accumulator < 0.0) m_Accumulator = 0.0;

            m_Diagnostics.TotalTicksExecuted += ticks;
            m_Diagnostics.TotalSimulationTime += static_cast<float>(ticks) * m_FixedTimestep;
        }

        return ticks;
    }

    void SimulationClock::Pause() {
        m_IsPaused = true;
        // Clear accumulator on pause to prevent catch-up burst when resuming
        m_Accumulator = 0.0f;
        m_SingleStepRequested = false;
    }

    void SimulationClock::Resume() {
        m_IsPaused = false;
        m_SingleStepRequested = false;
        m_Accumulator = 0.0f;
    }

    void SimulationClock::SetPaused(bool paused) {
        if (paused) Pause();
        else Resume();
    }

    void SimulationClock::RequestSingleStep() {
        m_SingleStepRequested = true;
    }

    void SimulationClock::Reset() {
        m_Accumulator = 0.0f;
        m_SingleStepRequested = false;
        m_IsPaused = false;
        ResetDiagnostics();
    }

    void SimulationClock::SetFixedTimestep(float fixedDt) {
        if (fixedDt > 0.0f && !std::isnan(fixedDt) && !std::isinf(fixedDt)) {
            m_FixedTimestep = fixedDt;
        }
    }

    void SimulationClock::SetSpeed(float speed) {
        if (!std::isnan(speed) && !std::isinf(speed)) {
            m_Speed = std::max(0.0f, speed);
        }
    }

    void SimulationClock::SetMaxAccumulator(float maxAcc) {
        if (maxAcc > 0.0f && !std::isnan(maxAcc) && !std::isinf(maxAcc)) {
            m_MaxAccumulator = maxAcc;
        }
    }

    float SimulationClock::GetInterpolationAlpha() const {
        if (m_FixedTimestep <= 0.0f) return 0.0f;
        float alpha = m_Accumulator / m_FixedTimestep;
        return std::clamp(alpha, 0.0f, 1.0f);
    }

    void SimulationClock::ResetDiagnostics() {
        m_Diagnostics = SimulationClockDiagnostics{};
    }

}
