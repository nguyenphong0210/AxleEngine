#include <Axle/SimulationDriver.hpp>
#include <Axle/Scene.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/ScriptHotReloadManager.hpp>

namespace Axle {
    namespace {
        struct RestoreTickInput {
            SimulationInputAdapter& Input;
            ~RestoreTickInput() {
                Input.DeactivateTickContext();
                ScriptHotReloadManager::Get().SyncInputContext();
            }
        };
    }

    SimulationDriver::SimulationDriver(float fixedTimestep)
        : m_Clock(fixedTimestep) {
    }

    uint32_t SimulationDriver::AdvanceFrame(float rawDt, Scene& scene, PhysicsScene* physicsScene) {
        if (!scene.CanMutate()) return 0;
        RestoreTickInput restore{m_InputAdapter};
        if (scene.IsSimulationFaulted()) {
            Pause();
            return 0;
        }
        m_InputAdapter.PollHardwareState();

        if (m_Clock.IsPaused()) {
            m_InputAdapter.DiscardEdges();
            if (m_Clock.IsSingleStepPending()) {
                uint32_t ticks = m_Clock.Advance(rawDt);
                if (ticks > 0) {
                    m_InputAdapter.PrepareTickSnapshots(1);
                    m_InputAdapter.ActivateTickContext(0);
                    scene.ExecuteFixedTick(m_Clock.GetFixedTimestep(), physicsScene);
                    m_InputAdapter.DeactivateTickContext();
                    scene.ExecuteLatePresentationPhase(m_Clock.GetFixedTimestep(), physicsScene);
                }
                return ticks;
            }
            scene.ExecuteLatePresentationPhase(0.0f, physicsScene);
            return 0;
        }

        uint32_t ticks = m_Clock.Advance(rawDt);
        const float frameDt = m_Clock.GetFrameSimulationDelta();
        if (frameDt <= 0.0f && ticks == 0) {
            m_InputAdapter.DiscardEdges();
            scene.ExecuteLatePresentationPhase(0.0f, physicsScene);
            return 0;
        }

        // 1. Frame gameplay uses the admitted, scaled simulation interval.
        scene.ExecuteCommandBuffer();
        scene.ExecuteFrameScriptPhase(frameDt);

        // 2. Prepare snapshots for all fixed ticks this frame
        m_InputAdapter.PrepareTickSnapshots(ticks);

        // 3. Fixed Ticks (SIM-01 same-tick contract: Controls -> Physics -> PostStepSync -> AnimationVFX)
        float fixedDt = m_Clock.GetFixedTimestep();
        for (uint32_t i = 0; i < ticks; ++i) {
            m_InputAdapter.ActivateTickContext(i);
            scene.ExecuteFixedTick(fixedDt, physicsScene);
        }
        m_InputAdapter.DeactivateTickContext();

        // 4. Late Presentation Phase (LateUpdate camera, spatial audio sync, HUD telemetry)
        scene.ExecuteLatePresentationPhase(frameDt, physicsScene);

        return ticks;
    }

    void SimulationDriver::ExecuteFixedTick(float fixedDt, Scene& scene, PhysicsScene* physicsScene) {
        if (!scene.CanMutate()) return;
        RestoreTickInput restore{m_InputAdapter};
        m_InputAdapter.PrepareTickSnapshots(1);
        m_InputAdapter.ActivateTickContext(0);
        scene.ExecuteFixedTick(fixedDt, physicsScene);
        m_InputAdapter.DeactivateTickContext();
    }

    void SimulationDriver::Pause() {
        if (m_Clock.IsPaused()) return;
        m_Clock.Pause();
        m_InputAdapter.Flush();
    }

    void SimulationDriver::Resume() {
        if (!m_Clock.IsPaused()) return;
        m_Clock.Resume();
        m_InputAdapter.Flush();
    }

    void SimulationDriver::SetPaused(bool paused) {
        if (paused) Pause();
        else Resume();
    }

    void SimulationDriver::RequestSingleStep() {
        m_Clock.RequestSingleStep();
    }

    void SimulationDriver::Reset() {
        m_Clock.Reset();
        m_InputAdapter.Flush();
    }

}
