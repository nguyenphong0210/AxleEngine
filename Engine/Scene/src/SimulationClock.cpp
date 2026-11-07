#include <Axle/SimulationClock.hpp>
#include <limits>

namespace Axle {

    SimulationClock::SimulationClock(float fixedTimestep, float maxAccumulator) {
        SetFixedTimestep(fixedTimestep);
        SetMaxAccumulator(maxAccumulator);
    }

    uint32_t SimulationClock::Advance(float rawDt) {
        ++m_Diagnostics.TotalFrames;
        m_FrameSimulationDelta = 0.0f;
        // A paused step is independent of wall time and speed.
        if (m_IsPaused && m_SingleStepRequested) {
            m_SingleStepRequested = false;
            ++m_Diagnostics.TotalTicksExecuted;
            m_Diagnostics.TotalSimulationTime += m_FixedTimestep;
            m_FrameSimulationDelta = m_FixedTimestep;
            return 1;
        }
        if (!std::isfinite(rawDt) || rawDt < 0.0f) {
            ++m_Diagnostics.InvalidDtDrops;
            return 0;
        }
        if (m_IsPaused || m_Speed == 0.0f) return 0;

        const double admitted = std::min(rawDt, m_MaxAccumulator);
        m_Diagnostics.TotalRealTime += admitted;
        m_Diagnostics.DroppedWallTime += rawDt - admitted;
        const double interval = admitted * static_cast<double>(m_Speed);
        const double accumulated = m_Accumulator + interval;
        const double fixed = m_FixedTimestep;
        // Tolerance reflects the float dt API, not a fixed microsecond offset.
        // Retain negative debt rather than repeatedly creating time at boundaries.
        const double tolerance = std::min(fixed * 0.001,
            std::max(fixed, m_Diagnostics.TotalRealTime) * std::numeric_limits<float>::epsilon());
        const double scheduled = std::max(0.0, std::floor((accumulated + tolerance) / fixed));
        const bool overloaded = rawDt > m_MaxAccumulator || scheduled > MaxTicksPerFrame;
        if (overloaded) ++m_Diagnostics.OverloadDrops;
        if (overloaded && m_OverloadPolicy == ClockOverloadPolicy::Drop) {
            m_Diagnostics.DroppedSimulationTime += interval;
            m_Diagnostics.DroppedTicks += static_cast<uint64_t>(scheduled);
            return 0;
        }
        const auto ticks = static_cast<uint32_t>(std::min(scheduled, double(MaxTicksPerFrame)));
        const double dropped = (scheduled - ticks) * fixed;
        m_Diagnostics.DroppedTicks += static_cast<uint64_t>(scheduled - ticks);
        m_Diagnostics.DroppedSimulationTime += dropped;
        m_Accumulator = accumulated - scheduled * fixed;
        m_FrameSimulationDelta = static_cast<float>(std::max(0.0, interval - dropped));
        m_Diagnostics.TotalTicksExecuted += ticks;
        m_Diagnostics.TotalSimulationTime += ticks * fixed;
        return ticks;
    }

    void SimulationClock::Pause() {
        if (m_IsPaused) return;
        m_IsPaused = true;
        m_Accumulator = 0.0;
        m_SingleStepRequested = false;
    }

    void SimulationClock::Resume() {
        if (!m_IsPaused) return;
        m_IsPaused = false;
        m_SingleStepRequested = false;
        m_Accumulator = 0.0;
    }

    void SimulationClock::SetPaused(bool paused) {
        if (paused) Pause();
        else Resume();
    }

    void SimulationClock::RequestSingleStep() {
        if (m_IsPaused) m_SingleStepRequested = true;
    }

    void SimulationClock::Reset() {
        m_Accumulator = 0.0;
        m_FrameSimulationDelta = 0.0f;
        m_SingleStepRequested = false;
        m_IsPaused = false;
        ResetDiagnostics();
    }

    void SimulationClock::SetFixedTimestep(float fixedDt) {
        if (std::isfinite(fixedDt) && fixedDt >= 1e-6f) m_FixedTimestep = fixedDt;
    }

    void SimulationClock::SetSpeed(float speed) {
        if (std::isfinite(speed)) m_Speed = std::clamp(speed, 0.0f, 4.0f);
    }

    void SimulationClock::SetMaxAccumulator(float maxAcc) {
        if (std::isfinite(maxAcc) && maxAcc > 0.0f) m_MaxAccumulator = std::min(maxAcc, 0.25f);
    }

    float SimulationClock::GetInterpolationAlpha() const {
        return static_cast<float>(std::clamp(m_Accumulator / m_FixedTimestep, 0.0, 1.0));
    }

    void SimulationClock::ResetDiagnostics() {
        m_Diagnostics = SimulationClockDiagnostics{};
    }
}
