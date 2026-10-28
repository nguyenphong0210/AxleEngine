#pragma once
#include <Axle/Types.hpp>
#include <Axle/UI/UIDocument.hpp>
#include <Axle/UI/UIDataContext.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Axle {

    class UICanvasDesignerPanel {
    public:
        UICanvasDesignerPanel();
        ~UICanvasDesignerPanel() = default;

        void OpenDocument(const std::filesystem::path& path);
        void SaveDocument();
        void ReloadDocument();

        void OnImGuiRender(bool* p_open = nullptr);

        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }

        const std::filesystem::path& GetCurrentPath() const { return m_FilePath; }

    private:
        void InitializeMockContext();
        void SyncMockContext();
        void RecompileFromBuffer();

    private:
        bool m_IsOpen = false;
        std::filesystem::path m_FilePath;
        std::shared_ptr<UIDocument> m_Document;
        std::string m_TextBuffer;
        char m_TextEditBuffer[65536] = { 0 };
        std::string m_StatusMessage = "Ready";
        bool m_HasParseError = false;
        std::string m_ErrorMessage;

        // Mock Telemetry Tunables
        UIDataContext m_MockContext;
        int m_MockSpeed = 168;
        float m_MockRPM = 0.72f;
        int m_MockGearIndex = 4; // 0: R, 1: N, 2: 1, 3: 2, 4: 3, 5: 4, 6: 5, 7: 6
        float m_MockThrottle = 0.85f;
        float m_MockBrake = 0.0f;
        float m_MockLatG = 0.42f;
        float m_MockLongG = 0.18f;
        bool m_MockIsRedline = false;
        bool m_AutoSyncCode = false;

        // Resolution Preview Preset
        int m_ResolutionPreset = 0; // 0: 1080p, 1: 4K, 2: 720p, 3: Fit Window
    };

} // namespace Axle
