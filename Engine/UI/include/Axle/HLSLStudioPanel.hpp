#pragma once
#include <Axle/Types.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

namespace Axle {

    enum class HLSLTargetStage : uint8_t {
        ForwardPipeline = 0, // Vertex (VSMain) + Pixel (PSMain)
        VertexOnly      = 1, // Vertex (VSMain)
        PixelOnly       = 2, // Pixel (PSMain)
        ComputeOnly     = 3  // Compute (CSMain)
    };

    class HLSLStudioPanel {
    public:
        HLSLStudioPanel();
        ~HLSLStudioPanel();

        // File I/O
        void OpenFile(const std::filesystem::path& filepath);
        bool SaveFile();
        bool CompileShader();

        // Template generation
        void CreateNewShaderTemplate(const std::string& shaderName = "CustomForwardPBR");

        // Panel state
        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }
        void Focus() { m_RequestFocus = true; }

        // Source buffer access
        const std::filesystem::path& GetCurrentFilePath() const { return m_CurrentFilePath; }
        const std::string& GetSourceCode() const { return m_SourceCode; }
        void SetSourceCode(const std::string& code);

        bool IsDirty() const { return m_IsDirty; }
        bool LastCompileSuccess() const { return m_LastCompileSuccess; }
        const std::string& GetCompileMessage() const { return m_CompileMessage; }
        const std::string& GetWarningMessage() const { return m_WarningMessage; }

        // Main ImGui rendering entry point
        void OnImGuiRender(bool* p_open = nullptr);

        // Pre-defined template generator
        static std::string GetDefaultPBRTemplate(const std::string& shaderName = "CustomForwardPBR");

    private:
        void DrawToolbar();
        void DrawEditorArea();
        void DrawDiagnosticsPanel();
        void DrawStatusBar();

    private:
        bool m_IsOpen = false;
        bool m_RequestFocus = false;
        std::filesystem::path m_CurrentFilePath;
        std::string m_SourceCode;
        std::string m_SavedSourceCode;
        bool m_IsDirty = false;

        // Compile settings
        HLSLTargetStage m_TargetStage = HLSLTargetStage::ForwardPipeline;
        char m_VSEntryPoint[64] = "VSMain";
        char m_PSEntryPoint[64] = "PSMain";
        char m_CSEntryPoint[64] = "CSMain";

        // Diagnostics
        bool m_HasCompiled = false;
        bool m_LastCompileSuccess = true;
        std::string m_CompileMessage;
        std::string m_WarningMessage;
        size_t m_CompiledBytecodeSize = 0;
        float m_LastCompileTimeMs = 0.0f;

        // Layout state
        float m_DiagnosticsHeight = 130.0f;
        float m_ScrollY = 0.0f;
    };

} // namespace Axle

