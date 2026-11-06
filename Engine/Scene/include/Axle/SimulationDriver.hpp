#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/SimulationClock.hpp>
#include <Axle/SimulationInput.hpp>
#include <Axle/ScenePhase.hpp>

namespace Axle {

    class Scene;
    class PhysicsScene;

    class SimulationDriver {
    public:
        explicit SimulationDriver(float fixedTimestep = 1.0f / 60.0f);
        ~SimulationDriver() = default;

        // Advances frame: collects inputs, runs fixed ticks, and executes non-fixed phases
        uint32_t AdvanceFrame(float rawDt, Scene& scene, PhysicsScene* physicsScene);

        // Direct fixed tick execution with active tick input context
        void ExecuteFixedTick(float fixedDt, Scene& scene, PhysicsScene* physicsScene);

        // Clock controls
        SimulationClock& GetClock() { return m_Clock; }
        const SimulationClock& GetClock() const { return m_Clock; }

        SimulationInputAdapter& GetInputAdapter() { return m_InputAdapter; }
        const SimulationInputAdapter& GetInputAdapter() const { return m_InputAdapter; }

        void Pause();
        void Resume();
        void SetPaused(bool paused);
        void RequestSingleStep();
        void Reset();

        bool IsPaused() const { return m_Clock.IsPaused(); }
        void SetSpeed(float speed) { m_Clock.SetSpeed(speed); }
        float GetSpeed() const { return m_Clock.GetSpeed(); }

    private:
        SimulationClock m_Clock;
        SimulationInputAdapter m_InputAdapter;
    };

}
