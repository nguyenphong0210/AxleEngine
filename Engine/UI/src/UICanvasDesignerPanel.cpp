#include <Axle/UICanvasDesignerPanel.hpp>
#include <Axle/UICanvasImGuiRenderer.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Path.hpp>
#include <Axle/PlatformUtils.hpp>
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace Axle {

    UICanvasDesignerPanel::UICanvasDesignerPanel() {
        m_Document = std::make_shared<UIDocument>();
        InitializeMockContext();
    }

    void UICanvasDesignerPanel::InitializeMockContext() {
        SyncMockContext();
    }

    void UICanvasDesignerPanel::SyncMockContext() {
        m_MockContext.Set("Engine.FPS", 144);
        m_MockContext.Set("Game.IsPaused", false);
    }

    void UICanvasDesignerPanel::OpenDocument(const std::filesystem::path& path) {
        const auto previous = m_FilePath;
        m_FilePath = path;
        m_IsOpen = true;
        if (!ReloadDocument()) m_FilePath = previous;
    }

    bool UICanvasDesignerPanel::ReloadDocument() {
        try {
        if (m_FilePath.empty()) return false;

        std::filesystem::path fullPath = m_FilePath;
        if (!std::filesystem::exists(fullPath)) {
            fullPath = FileSystem::ResolveAssetPath(m_FilePath);
        }

        std::ifstream file(fullPath);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            const auto text = ss.str();
            if (file.bad() || text.size() >= sizeof(m_TextEditBuffer)) { m_StatusMessage = "Document cannot be loaded completely into the source editor."; return false; }
            m_TextBuffer = text;

            std::strncpy(m_TextEditBuffer, m_TextBuffer.c_str(), sizeof(m_TextEditBuffer) - 1);
            m_TextEditBuffer[sizeof(m_TextEditBuffer) - 1] = '\0';

            RecompileFromBuffer();
            m_StatusMessage = "Loaded: " + PathToUtf8(fullPath.filename());
            return true;
        } else {
            m_StatusMessage = "Failed to open: " + PathToUtf8(fullPath);
            m_HasParseError = true;
            m_ErrorMessage = "File could not be opened";
            return false;
        }
        } catch (const std::exception& error) { m_StatusMessage = error.what(); return false; }
    }

    bool UICanvasDesignerPanel::SaveDocument(const std::filesystem::path& path) {
        try {
        std::filesystem::path fullPath = path.empty() ? m_FilePath : path;
        if (fullPath.empty()) {
            const auto root = PathToUtf8(FileSystem::GetAssetDirectory());
            const auto selected = FileDialogs::SaveFile("UI Canvas (*.axleui)\0*.axleui\0All Files (*.*)\0*.*\0", root.c_str(), "axleui");
            if (selected.empty()) return false;
            fullPath = PathFromUtf8(selected);
        }
        if (!std::filesystem::exists(fullPath)) {
            fullPath = FileSystem::ResolveAssetPath(fullPath);
        }

        if (fullPath.has_parent_path() && !std::filesystem::exists(fullPath.parent_path())) {
            std::filesystem::create_directories(fullPath.parent_path());
        }

        std::string error;
        if (FileSystem::WriteIdentifiedTextFile(fullPath, m_TextEditBuffer, &error)) {
            m_FilePath = fullPath;
            m_StatusMessage = "Saved: " + PathToUtf8(fullPath.filename());
            m_TextBuffer = m_TextEditBuffer;
            RecompileFromBuffer();
            return true;
        } else {
            m_StatusMessage = "Failed to save: " + error;
            return false;
        }
        } catch (const std::exception& error) { m_StatusMessage = error.what(); return false; }
    }
    void UICanvasDesignerPanel::DiscardChanges() {
        std::strncpy(m_TextEditBuffer, m_TextBuffer.c_str(), sizeof(m_TextEditBuffer) - 1);
        m_TextEditBuffer[sizeof(m_TextEditBuffer) - 1] = '\0'; RecompileFromBuffer();
    }

    void UICanvasDesignerPanel::RecompileFromBuffer() {
        std::string err;
        auto newDoc = std::make_shared<UIDocument>();
        bool ok = UIParser::ParseSource(m_TextEditBuffer, *newDoc, &err);
        if (ok) {
            m_Document = newDoc;
            m_Document->SetFilePath(PathToUtf8(m_FilePath));
            m_HasParseError = false;
            m_ErrorMessage.clear();
            m_StatusMessage = "Parsed successfully (" + std::to_string(m_Document->GetNodeCount()) + " nodes)";
        } else {
            m_HasParseError = true;
            m_ErrorMessage = err;
            m_StatusMessage = "Syntax Error in .axleui source";
        }
    }

    void UICanvasDesignerPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) {
            m_IsOpen = false;
            return;
        }

        if (!m_IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(1060.0f, 680.0f), ImGuiCond_FirstUseEver);
        std::string windowTitle = "UI Canvas Designer: " + (m_FilePath.empty() ? std::string("[No File]") : m_FilePath.filename().string()) + "###UICanvasDesigner";

        if (ImGui::Begin(windowTitle.c_str(), p_open ? p_open : &m_IsOpen, ImGuiWindowFlags_MenuBar)) {

            // Menu Bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Save", "Ctrl+S")) {
                        SaveDocument();
                    }
                    if (ImGui::MenuItem("Reload from Disk", "Ctrl+R")) {
                        ReloadDocument();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Close Designer")) {
                        m_IsOpen = false;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            // Keyboard shortcut Ctrl+S
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                SaveDocument();
            }

            // 1. Top Action Toolbar
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("💾 Save Document (Ctrl+S)", ImVec2(180.0f, 26.0f))) {
                SaveDocument();
            }
            ImGui::SameLine();
            if (ImGui::Button("🔄 Reload", ImVec2(90.0f, 26.0f))) {
                ReloadDocument();
            }
            ImGui::SameLine();
            if (ImGui::Button("⚡ Recompile Live", ImVec2(130.0f, 26.0f))) {
                RecompileFromBuffer();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            const char* presets[] = { "1080p (1920x1080)", "4K (3840x2160)", "720p (1280x720)", "Fit Window" };
            ImGui::Combo("##ResolutionPreset", &m_ResolutionPreset, presets, IM_ARRAYSIZE(presets));

            ImGui::SameLine();
            if (m_HasParseError) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[!] %s", m_ErrorMessage.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[OK] %s", m_StatusMessage.c_str());
            }
            ImGui::PopStyleVar();

            ImGui::Separator();

            // 2. Dual Split Workspace: Left = Live Canvas Preview, Right = Source Code Editor
            float availW = ImGui::GetContentRegionAvail().x;
            float availH = ImGui::GetContentRegionAvail().y;
            float previewW = std::max(360.0f, availW * 0.58f);
            float editorW = availW - previewW - 12.0f;

            // --- Left: Live Canvas Viewport ---
            ImGui::BeginChild("CanvasPreviewViewport", ImVec2(previewW, availH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            {
                ImVec2 pMin = ImGui::GetCursorScreenPos();
                ImVec2 pSize = ImGui::GetContentRegionAvail();
                ImDrawList* drawList = ImGui::GetWindowDrawList();

                // Dark checkerboard background for precision transparency inspection
                drawList->AddRectFilled(pMin, ImVec2(pMin.x + pSize.x, pMin.y + pSize.y), IM_COL32(11, 14, 20, 255));
                const float checkSize = 18.0f;
                for (float y = 0; y < pSize.y; y += checkSize) {
                    for (float x = 0; x < pSize.x; x += checkSize) {
                        int ix = static_cast<int>(x / checkSize);
                        int iy = static_cast<int>(y / checkSize);
                        if ((ix + iy) % 2 == 1) {
                            drawList->AddRectFilled(
                                ImVec2(pMin.x + x, pMin.y + y),
                                ImVec2(std::min(pMin.x + x + checkSize, pMin.x + pSize.x), std::min(pMin.y + y + checkSize, pMin.y + pSize.y)),
                                IM_COL32(16, 20, 28, 255)
                            );
                        }
                    }
                }

                // Aspect ratio viewport fitting (16:9 reference resolution)
                glm::vec2 targetRes(1920.0f, 1080.0f);
                if (m_ResolutionPreset == 1) targetRes = glm::vec2(3840.0f, 2160.0f);
                else if (m_ResolutionPreset == 2) targetRes = glm::vec2(1280.0f, 720.0f);

                float aspect = (m_ResolutionPreset == 3) ? (pSize.x / std::max(1.0f, pSize.y)) : (targetRes.x / targetRes.y);
                float fitW = pSize.x;
                float fitH = fitW / aspect;
                if (fitH > pSize.y) {
                    fitH = pSize.y;
                    fitW = fitH * aspect;
                }

                ImVec2 cMin(pMin.x + (pSize.x - fitW) * 0.5f, pMin.y + (pSize.y - fitH) * 0.5f);
                ImVec2 cMax(cMin.x + fitW, cMin.y + fitH);

                // Draw Canvas Boundary Frame
                drawList->AddRect(cMin, cMax, IM_COL32(56, 189, 248, 120), 2.0f, 0, 1.5f);

                // Render Canvas UI Live Document
                if (m_Document && m_Document->GetRootNode()) {
                    m_Document->EvaluateBindings(m_MockContext);
                    m_Document->UpdateLayout(glm::vec2(fitW, fitH));
                    UICanvasImGuiRenderer::Render(m_Document.get(), drawList, glm::vec2(cMin.x, cMin.y), glm::vec2(fitW, fitH));
                }

                // Resolution badge in corner
                char badgeStr[64];
                std::snprintf(badgeStr, sizeof(badgeStr), "Preview: %.0fx%.0f (Scale: %.2fx)", fitW, fitH, fitW / targetRes.x);
                drawList->AddText(ImVec2(cMin.x + 8.0f, cMin.y + 8.0f), IM_COL32(148, 163, 184, 180), badgeStr);
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // --- Right: Source Code Text Editor ---
            ImGui::BeginChild("SourceCodeEditorPanel", ImVec2(editorW, availH), true);
            {
                ImGui::Text("📝 .axleui Declarative Source:");
                ImGui::Separator();

                ImVec2 editSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 36.0f);
                ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
                if (ImGui::InputTextMultiline("##SourceCode", m_TextEditBuffer, sizeof(m_TextEditBuffer), editSize, editFlags)) {
                    // Auto recompile when typing if desired
                }

                if (ImGui::Button("⚡ Recompile & Update Preview", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
                    RecompileFromBuffer();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

} // namespace Axle
