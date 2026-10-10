#pragma once
#include <Axle/Types.hpp>

namespace Axle {

    class Timestep {
    public:
        Timestep(f32 time = 0.0f) : m_Time(time) {}

        operator f32() const { return m_Time; }

        f32 GetSeconds() const { return m_Time; }
        f32 GetMilliseconds() const { return m_Time * 1000.0f; }

    private:
        f32 m_Time = 0.0f;
    };

}