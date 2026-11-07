#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Axle {

    enum class ClockOverloadPolicy : uint8_t {
        Clamp = 0,   // Drop excess whole ticks, retaining the fractional remainder
        Drop = 1     // Discard the overloaded frame's simulation interval
    };

    struct SimulationClockDiagnostics {
        uint64_t TotalTicksExecuted = 0;
        uint64_t TotalFrames = 0;
        uint64_t OverloadDrops = 0;
        uint64_t InvalidDtDrops = 0;
        double TotalSimulationTime = 0.0;
        double TotalRealTime = 0.0;
        double DroppedSimulationTime = 0.0;
        double DroppedWallTime = 0.0;
        uint64_t DroppedTicks = 0;
    };

    class SimulationClock {
    public:
        explicit SimulationClock(float fixedTimestep = 1.0f / 60.0f, float maxAccumulator = 0.25f);

        // Core tick step calculation
        uint32_t Advance(float rawDt);
        float GetFrameSimulationDelta() const { return m_FrameSimulationDelta; }

        // Control APIs
        void Pause();
        void Resume();
        void SetPaused(bool paused);
        bool IsPaused() const { return m_IsPaused; }

        void RequestSingleStep();
        bool IsSingleStepPending() const { return m_SingleStepRequested; }

        void Reset();

        // Parameters
        void SetFixedTimestep(float fixedDt);
        float GetFixedTimestep() const { return m_FixedTimestep; }

        void SetSpeed(float speed);
        float GetSpeed() const { return m_Speed; }

        void SetMaxAccumulator(float maxAcc);
        float GetMaxAccumulator() const { return m_MaxAccumulator; }

        void SetOverloadPolicy(ClockOverloadPolicy policy) { m_OverloadPolicy = policy; }
        ClockOverloadPolicy GetOverloadPolicy() const { return m_OverloadPolicy; }

        // Fractional remainder for interpolation [0.0, 1.0)
        float GetInterpolationAlpha() const;

        float GetAccumulator() const { return static_cast<float>(m_Accumulator); }

        // Telemetry
        const SimulationClockDiagnostics& GetDiagnostics() const { return m_Diagnostics; }
        void ResetDiagnostics();

    private:
        float m_FixedTimestep = 1.0f / 60.0f;
        double m_Accumulator = 0.0;
        float m_MaxAccumulator = 0.25f; // Maximum wall interval admitted per frame
        static constexpr uint32_t MaxTicksPerFrame = 8;
        float m_FrameSimulationDelta = 0.0f;
        float m_Speed = 1.0f;
        bool m_IsPaused = false;
        bool m_SingleStepRequested = false;
        ClockOverloadPolicy m_OverloadPolicy = ClockOverloadPolicy::Clamp;
        SimulationClockDiagnostics m_Diagnostics;
    };

}
