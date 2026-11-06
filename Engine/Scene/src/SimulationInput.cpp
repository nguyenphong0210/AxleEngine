#include <Axle/SimulationInput.hpp>

namespace Axle {

    static SimulationInputSnapshot s_FallbackSnapshot;

    SimulationInputAdapter::SimulationInputAdapter() {
        m_TickInputContext.Window = nullptr;
        m_TickInputContext.IsKeyPressedFn = [](KeyCode key) -> bool {
            if (s_ActiveAdapter) {
                return s_ActiveAdapter->GetCurrentSnapshot().IsKeyDown(key);
            }
            return false;
        };
        m_TickInputContext.IsMouseButtonPressedFn = [](MouseCode button) -> bool {
            if (s_ActiveAdapter) {
                return s_ActiveAdapter->GetCurrentSnapshot().IsMouseButtonDown(button);
            }
            return false;
        };
        m_TickInputContext.GetMousePositionFn = []() -> std::pair<f32, f32> {
            if (s_ActiveAdapter) {
                return s_ActiveAdapter->GetCurrentSnapshot().MousePosition;
            }
            return { 0.0f, 0.0f };
        };
        m_TickInputContext.GetMouseScrollYFn = []() -> f32 {
            if (s_ActiveAdapter) {
                return s_ActiveAdapter->GetCurrentSnapshot().MouseScrollY;
            }
            return 0.0f;
        };
        m_TickInputContext.SetCursorModeFn = [](CursorMode /*mode*/) {};
        m_TickInputContext.GetCursorModeFn = []() -> CursorMode { return CursorMode::Normal; };
        m_TickInputContext.SetCursorPositionFn = [](f32 /*x*/, f32 /*y*/) {};
        m_TickInputContext.IsViewportHoveredFn = []() -> bool { return true; };
        m_TickInputContext.SetViewportHoveredFn = [](bool /*hovered*/) {};
    }

    SimulationInputAdapter::~SimulationInputAdapter() {
        DeactivateTickContext();
    }

    void SimulationInputAdapter::PollHardwareState() {
        // Sample mouse coordinates if available
        auto [mx, my] = Input::GetMousePosition();
        m_CurrentHardwareState.MousePosition = { mx, my };
        m_CurrentHardwareState.MouseScrollY = Input::GetMouseScrollY();
    }

    void SimulationInputAdapter::InjectKeyPress(KeyCode key) {
        if (m_CurrentHardwareState.KeysDown.find(key) == m_CurrentHardwareState.KeysDown.end()) {
            m_LatchedPresses.insert(key);
        }
        m_CurrentHardwareState.KeysDown.insert(key);
        m_LatchedReleases.erase(key);
    }

    void SimulationInputAdapter::InjectKeyRelease(KeyCode key) {
        if (m_CurrentHardwareState.KeysDown.find(key) != m_CurrentHardwareState.KeysDown.end()) {
            m_LatchedReleases.insert(key);
        }
        m_CurrentHardwareState.KeysDown.erase(key);
        m_LatchedPresses.erase(key);
    }

    void SimulationInputAdapter::InjectMouseButtonPress(MouseCode button) {
        if (m_CurrentHardwareState.MouseButtonsDown.find(button) == m_CurrentHardwareState.MouseButtonsDown.end()) {
            m_LatchedMousePresses.insert(button);
        }
        m_CurrentHardwareState.MouseButtonsDown.insert(button);
        m_LatchedMouseReleases.erase(button);
    }

    void SimulationInputAdapter::InjectMouseButtonRelease(MouseCode button) {
        if (m_CurrentHardwareState.MouseButtonsDown.find(button) != m_CurrentHardwareState.MouseButtonsDown.end()) {
            m_LatchedMouseReleases.insert(button);
        }
        m_CurrentHardwareState.MouseButtonsDown.erase(button);
        m_LatchedMousePresses.erase(button);
    }

    void SimulationInputAdapter::SetMousePosition(float x, float y) {
        m_CurrentHardwareState.MousePosition = { x, y };
    }

    void SimulationInputAdapter::SetMouseScrollY(float scrollY) {
        m_CurrentHardwareState.MouseScrollY = scrollY;
    }

    void SimulationInputAdapter::PrepareTickSnapshots(uint32_t tickCount) {
        if (tickCount == 0) {
            // No ticks this frame; preserve latched triggers until next frame
            return;
        }

        m_TickSnapshots.clear();
        m_TickSnapshots.resize(tickCount);

        // Tick 0 consumes all pending latched triggers
        m_TickSnapshots[0] = m_CurrentHardwareState;
        m_TickSnapshots[0].KeysJustPressed = m_LatchedPresses;
        m_TickSnapshots[0].KeysJustReleased = m_LatchedReleases;
        m_TickSnapshots[0].MouseButtonsJustPressed = m_LatchedMousePresses;
        m_TickSnapshots[0].MouseButtonsJustReleased = m_LatchedMouseReleases;

        m_LatchedPresses.clear();
        m_LatchedReleases.clear();
        m_LatchedMousePresses.clear();
        m_LatchedMouseReleases.clear();

        // Ticks 1 .. N (catch-up ticks in same frame): keys remain held, but edge triggers are empty
        for (uint32_t i = 1; i < tickCount; ++i) {
            m_TickSnapshots[i] = m_CurrentHardwareState;
            m_TickSnapshots[i].KeysJustPressed.clear();
            m_TickSnapshots[i].KeysJustReleased.clear();
            m_TickSnapshots[i].MouseButtonsJustPressed.clear();
            m_TickSnapshots[i].MouseButtonsJustReleased.clear();
        }
        m_ActiveSnapshotIndex = 0;
    }

    void SimulationInputAdapter::ActivateTickContext(uint32_t tickIndex) {
        if (tickIndex < m_TickSnapshots.size()) {
            m_ActiveSnapshotIndex = tickIndex;
        }
        s_ActiveAdapter = this;
        if (!m_ContextActive) {
            m_PreviousInputContext = Input::GetInputContext();
            Input::SetInputContext(&m_TickInputContext);
            m_ContextActive = true;
        }
    }

    void SimulationInputAdapter::DeactivateTickContext() {
        if (m_ContextActive) {
            Input::SetInputContext(m_PreviousInputContext);
            m_PreviousInputContext = nullptr;
            s_ActiveAdapter = nullptr;
            m_ContextActive = false;
        }
    }

    void SimulationInputAdapter::Flush() {
        m_LatchedPresses.clear();
        m_LatchedReleases.clear();
        m_LatchedMousePresses.clear();
        m_LatchedMouseReleases.clear();

        m_CurrentHardwareState.KeysDown.clear();
        m_CurrentHardwareState.KeysJustPressed.clear();
        m_CurrentHardwareState.KeysJustReleased.clear();
        m_CurrentHardwareState.MouseButtonsDown.clear();
        m_CurrentHardwareState.MouseButtonsJustPressed.clear();
        m_CurrentHardwareState.MouseButtonsJustReleased.clear();

        m_TickSnapshots.clear();
        m_ActiveSnapshotIndex = 0;
    }

    const SimulationInputSnapshot& SimulationInputAdapter::GetCurrentSnapshot() const {
        if (!m_TickSnapshots.empty() && m_ActiveSnapshotIndex < m_TickSnapshots.size()) {
            return m_TickSnapshots[m_ActiveSnapshotIndex];
        }
        return m_CurrentHardwareState;
    }

}
