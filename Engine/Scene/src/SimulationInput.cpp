#include <Axle/SimulationInput.hpp>

namespace Axle {

    SimulationInputAdapter::SimulationInputAdapter() {
        m_TickInputContext.IsKeyJustPressedFn = [](KeyCode key) { return s_ActiveAdapter && s_ActiveAdapter->GetCurrentSnapshot().IsKeyJustPressed(key); };
        m_TickInputContext.IsKeyJustReleasedFn = [](KeyCode key) { return s_ActiveAdapter && s_ActiveAdapter->GetCurrentSnapshot().IsKeyJustReleased(key); };
        m_TickInputContext.IsMouseButtonJustPressedFn = [](MouseCode button) { return s_ActiveAdapter && s_ActiveAdapter->GetCurrentSnapshot().IsMouseButtonJustPressed(button); };
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
        m_TickInputContext.SetCursorModeFn = [](CursorMode mode) {
            auto* host = s_ActiveAdapter ? s_ActiveAdapter->m_PreviousInputContext : nullptr;
            if (host && host->SetCursorModeFn) host->SetCursorModeFn(mode);
        };
        m_TickInputContext.GetCursorModeFn = []() -> CursorMode {
            auto* host = s_ActiveAdapter ? s_ActiveAdapter->m_PreviousInputContext : nullptr;
            return host && host->GetCursorModeFn ? host->GetCursorModeFn() : CursorMode::Normal;
        };
        m_TickInputContext.SetCursorPositionFn = [](f32 x, f32 y) {
            auto* host = s_ActiveAdapter ? s_ActiveAdapter->m_PreviousInputContext : nullptr;
            if (host && host->SetCursorPositionFn) host->SetCursorPositionFn(x, y);
        };
        m_TickInputContext.IsViewportHoveredFn = []() -> bool {
            auto* host = s_ActiveAdapter ? s_ActiveAdapter->m_PreviousInputContext : nullptr;
            return host && host->IsViewportHoveredFn ? host->IsViewportHoveredFn() : true;
        };
        m_TickInputContext.SetViewportHoveredFn = [](bool hovered) {
            auto* host = s_ActiveAdapter ? s_ActiveAdapter->m_PreviousInputContext : nullptr;
            if (host && host->SetViewportHoveredFn) host->SetViewportHoveredFn(hovered);
        };
    }

    SimulationInputAdapter::~SimulationInputAdapter() {
        DeactivateTickContext();
    }

    void SimulationInputAdapter::PollHardwareState() {
        const auto* host = Input::GetInputContext();
        if (host && host->IsKeyPressedFn) {
            const auto poll = [this, host](int code) {
                const auto key = static_cast<KeyCode>(code);
                if (host->IsKeyPressedFn(key)) InjectKeyPress(key);
                else InjectKeyRelease(key);
            };
            for (int code : {32, 39, 44, 45, 46, 47, 59, 61}) poll(code);
            for (int code = 48; code <= 57; ++code) poll(code);
            for (int code = 65; code <= 90; ++code) poll(code);
            for (int code = 256; code <= 269; ++code) poll(code);
            for (int code = 280; code <= 284; ++code) poll(code);
            for (int code = 290; code <= 301; ++code) poll(code);
            for (int code = 340; code <= 347; ++code) poll(code);
        }
        if (host && host->IsMouseButtonPressedFn) {
            for (int code = 0; code < 8; ++code) {
                const auto button = static_cast<MouseCode>(code);
                if (host->IsMouseButtonPressedFn(button)) InjectMouseButtonPress(button);
                else InjectMouseButtonRelease(button);
            }
        }
        // Sample mouse coordinates if available
        auto [mx, my] = Input::GetMousePosition();
        m_CurrentHardwareState.MousePosition = { mx, my };
        m_CurrentHardwareState.MouseScrollY = Input::GetMouseScrollY();
        m_PendingScroll += m_CurrentHardwareState.MouseScrollY;
        if (m_SuppressHardwareEdges) {
            DiscardEdges();
            m_SuppressHardwareEdges = false;
        }
    }

    void SimulationInputAdapter::InjectKeyPress(KeyCode key) {
        if (m_CurrentHardwareState.KeysDown.find(key) == m_CurrentHardwareState.KeysDown.end()) {
            m_LatchedPresses.insert(key);
        }
        m_CurrentHardwareState.KeysDown.insert(key);
    }

    void SimulationInputAdapter::InjectKeyRelease(KeyCode key) {
        if (m_CurrentHardwareState.KeysDown.find(key) != m_CurrentHardwareState.KeysDown.end()) {
            m_LatchedReleases.insert(key);
        }
        m_CurrentHardwareState.KeysDown.erase(key);
    }

    void SimulationInputAdapter::InjectMouseButtonPress(MouseCode button) {
        if (m_CurrentHardwareState.MouseButtonsDown.find(button) == m_CurrentHardwareState.MouseButtonsDown.end()) {
            m_LatchedMousePresses.insert(button);
        }
        m_CurrentHardwareState.MouseButtonsDown.insert(button);
    }

    void SimulationInputAdapter::InjectMouseButtonRelease(MouseCode button) {
        if (m_CurrentHardwareState.MouseButtonsDown.find(button) != m_CurrentHardwareState.MouseButtonsDown.end()) {
            m_LatchedMouseReleases.insert(button);
        }
        m_CurrentHardwareState.MouseButtonsDown.erase(button);
    }

    void SimulationInputAdapter::SetMousePosition(float x, float y) {
        m_CurrentHardwareState.MousePosition = { x, y };
    }

    void SimulationInputAdapter::SetMouseScrollY(float scrollY) {
        m_CurrentHardwareState.MouseScrollY = scrollY;
        m_PendingScroll += scrollY;
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
        m_TickSnapshots[0].MouseScrollY = m_PendingScroll;
        m_PendingScroll = 0.0f;
        m_TickSnapshots[0].KeysJustPressed = m_LatchedPresses;
        m_TickSnapshots[0].KeysJustReleased = m_LatchedReleases;
        m_TickSnapshots[0].MouseButtonsJustPressed = m_LatchedMousePresses;
        m_TickSnapshots[0].MouseButtonsJustReleased = m_LatchedMouseReleases;

        m_LatchedPresses.clear();
        m_LatchedReleases.clear();
        m_LatchedMousePresses.clear();
        m_LatchedMouseReleases.clear();
        m_CurrentHardwareState.MouseScrollY = 0.0f;

        // Ticks 1 .. N (catch-up ticks in same frame): keys remain held, but edge triggers are empty
        for (uint32_t i = 1; i < tickCount; ++i) {
            m_TickSnapshots[i] = m_CurrentHardwareState;
            m_TickSnapshots[i].KeysJustPressed.clear();
            m_TickSnapshots[i].KeysJustReleased.clear();
            m_TickSnapshots[i].MouseButtonsJustPressed.clear();
            m_TickSnapshots[i].MouseButtonsJustReleased.clear();
            m_TickSnapshots[i].MouseScrollY = 0.0f;
        }
        m_ActiveSnapshotIndex = 0;
    }

    void SimulationInputAdapter::ActivateTickContext(uint32_t tickIndex) {
        if (tickIndex < m_TickSnapshots.size()) {
            m_ActiveSnapshotIndex = tickIndex;
        }
        if (!m_ContextActive) {
            m_PreviousAdapter = s_ActiveAdapter;
            s_ActiveAdapter = this;
            m_PreviousInputContext = Input::GetInputContext();
            m_TickInputContext.Window = m_PreviousInputContext ? m_PreviousInputContext->Window : nullptr;
            Input::SetInputContext(&m_TickInputContext);
            m_ContextActive = true;
        }
    }

    void SimulationInputAdapter::DeactivateTickContext() {
        if (m_ContextActive) {
            Input::SetInputContext(m_PreviousInputContext);
            m_PreviousInputContext = nullptr;
            s_ActiveAdapter = m_PreviousAdapter;
            m_PreviousAdapter = nullptr;
            m_ContextActive = false;
        }
    }

    void SimulationInputAdapter::Flush() {
        DeactivateTickContext();
        DiscardEdges();
        m_SuppressHardwareEdges = true;
        m_CurrentHardwareState = SimulationInputSnapshot{};
        m_TickSnapshots.clear();
        m_ActiveSnapshotIndex = 0;
    }

    void SimulationInputAdapter::DiscardEdges() {
        m_PendingScroll = 0.0f;
        m_LatchedPresses.clear();
        m_LatchedReleases.clear();
        m_LatchedMousePresses.clear();
        m_LatchedMouseReleases.clear();

        m_CurrentHardwareState.KeysJustPressed.clear();
        m_CurrentHardwareState.KeysJustReleased.clear();
        m_CurrentHardwareState.MouseButtonsJustPressed.clear();
        m_CurrentHardwareState.MouseButtonsJustReleased.clear();

        m_CurrentHardwareState.MouseScrollY = 0.0f;
    }

    const SimulationInputSnapshot& SimulationInputAdapter::GetCurrentSnapshot() const {
        if (!m_TickSnapshots.empty() && m_ActiveSnapshotIndex < m_TickSnapshots.size()) {
            return m_TickSnapshots[m_ActiveSnapshotIndex];
        }
        return m_CurrentHardwareState;
    }

}
