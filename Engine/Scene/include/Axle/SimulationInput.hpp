#pragma once
#include <Axle/Types.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/Input.hpp>
#include <unordered_set>
#include <vector>
#include <utility>

namespace Axle {

    struct SimulationInputSnapshot {
        std::unordered_set<KeyCode> KeysDown;
        std::unordered_set<KeyCode> KeysJustPressed;
        std::unordered_set<KeyCode> KeysJustReleased;

        std::unordered_set<MouseCode> MouseButtonsDown;
        std::unordered_set<MouseCode> MouseButtonsJustPressed;
        std::unordered_set<MouseCode> MouseButtonsJustReleased;

        std::pair<float, float> MousePosition{ 0.0f, 0.0f };
        float MouseScrollY = 0.0f;

        bool IsKeyDown(KeyCode key) const {
            return KeysDown.find(key) != KeysDown.end();
        }

        bool IsKeyJustPressed(KeyCode key) const {
            return KeysJustPressed.find(key) != KeysJustPressed.end();
        }

        bool IsKeyJustReleased(KeyCode key) const {
            return KeysJustReleased.find(key) != KeysJustReleased.end();
        }

        bool IsMouseButtonDown(MouseCode button) const {
            return MouseButtonsDown.find(button) != MouseButtonsDown.end();
        }

        bool IsMouseButtonJustPressed(MouseCode button) const {
            return MouseButtonsJustPressed.find(button) != MouseButtonsJustPressed.end();
        }

        bool IsMouseButtonJustReleased(MouseCode button) const {
            return MouseButtonsJustReleased.find(button) != MouseButtonsJustReleased.end();
        }
    };

    class SimulationInputAdapter {
    public:
        SimulationInputAdapter();
        ~SimulationInputAdapter();

        // Hardware state polling / latching
        void PollHardwareState();

        // Synthetic injection (for automated unit testing and headless tests)
        void InjectKeyPress(KeyCode key);
        void InjectKeyRelease(KeyCode key);
        void InjectMouseButtonPress(MouseCode button);
        void InjectMouseButtonRelease(MouseCode button);
        void SetMousePosition(float x, float y);
        void SetMouseScrollY(float scrollY);

        // Prepares snapshots for the fixed ticks in the current frame
        void PrepareTickSnapshots(uint32_t tickCount);

        // Activates the snapshot for tick index [0 .. tickCount - 1]
        void ActivateTickContext(uint32_t tickIndex);

        // Restores host input context
        void DeactivateTickContext();

        // Discards all pending edge triggers and held keys (e.g. on pause / resume)
        void Flush();

        const SimulationInputSnapshot& GetCurrentSnapshot() const;
        const SimulationInputSnapshot& GetHardwareState() const { return m_CurrentHardwareState; }

        static SimulationInputAdapter* GetActiveAdapter() { return s_ActiveAdapter; }

    private:
        SimulationInputSnapshot m_CurrentHardwareState;
        std::unordered_set<KeyCode> m_LatchedPresses;
        std::unordered_set<KeyCode> m_LatchedReleases;
        std::unordered_set<MouseCode> m_LatchedMousePresses;
        std::unordered_set<MouseCode> m_LatchedMouseReleases;

        std::vector<SimulationInputSnapshot> m_TickSnapshots;
        size_t m_ActiveSnapshotIndex = 0;
        bool m_ContextActive = false;

        const InputContext* m_PreviousInputContext = nullptr;
        InputContext m_TickInputContext;

        inline static SimulationInputAdapter* s_ActiveAdapter = nullptr;
    };

}
