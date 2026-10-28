#include <Axle/UICanvasDesignerPanel.hpp>
#include <Axle/UICanvasImGuiRenderer.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
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
        m_MockContext.Set("Vehicle.SpeedKmh", m_MockSpeed);
        m_MockContext.Set("Vehicle.SpeedUnit", std::string("KM/H"));
        m_MockContext.Set("Vehicle.RPMNormalized", m_MockRPM);

        const bool redline = m_MockIsRedline || (m_MockRPM >= 0.85f);
        m_MockContext.Set("Vehicle.IsRedline", redline);

        const char* gears[] = { "R", "N", "1", "2", "3", "4", "5", "6" };
        std::string gearStr = gears[std::clamp(m_MockGearIndex, 0, 7)];
        m_MockContext.Set("Vehicle.GearString", gearStr);

        m_MockContext.Set("Vehicle.Throttle", m_MockThrottle);
        m_MockContext.Set("Vehicle.Brake", m_MockBrake);

        char latBuf[32], longBuf[32];
        std::snprintf(latBuf, sizeof(latBuf), "%+.2f", m_MockLatG);
        std::snprintf(longBuf, sizeof(longBuf), "%+.2f", m_MockLongG);
        m_MockContext.Set("Vehicle.LateralG", std::string(latBuf));
        m_MockContext.Set("Vehicle.LongitudinalG", std::string(longBuf));
        m_MockContext.Set("Vehicle.CameraMode", std::string("CHASE"));
        m_MockContext.Set("Game.IsPaused", false);
    }

    void UICanvasDesignerPanel::OpenDocument(const std::filesystem::path& path) {
        m_FilePath = path;
        m_IsOpen = true;
        ReloadDocument();
    }

    void UICanvasDesignerPanel::ReloadDocument() {
        if (m_FilePath.empty()) return;

        std::filesystem::path fullPath = m_FilePath;
        if (!std::filesystem::exists(fullPath)) {
            fullPath = FileSystem::ResolveAssetPath(m_FilePath.string());
        }

        std::ifstream file(fullPath);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            m_TextBuffer = ss.str();

            std::strncpy(m_TextEditBuffer, m_TextBuffer.c_str(), sizeof(m_TextEditBuffer) - 1);
            m_TextEditBuffer[sizeof(m_TextEditBuffer) - 1] = '\0';

            RecompileFromBuffer();
            m_StatusMessage = "Loaded: " + fullPath.filename().string();
        } else {
            m_StatusMessage = "Failed to open: " + fullPath.string();
            m_HasParseError = true;
            m_ErrorMessage = "File could not be opened";
        }
    }

    void UICanvasDesignerPanel::SaveDocument() {
        if (m_FilePath.empty()) return;

        std::filesystem::path fullPath = m_FilePath;
        if (!std::filesystem::exists(fullPath)) {
            fullPath = FileSystem::ResolveAssetPath(m_FilePath.string());
        }

        if (fullPath.has_parent_path() && !std::filesystem::exists(fullPath.parent_path())) {
            std::filesystem::create_directories(fullPath.parent_path());
        }

        std::ofstream file(fullPath);
        if (file.is_open()) {
            file << m_TextEditBuffer;
            m_StatusMessage = "Saved: " + fullPath.filename().string();
            m_TextBuffer = m_TextEditBuffer;
            RecompileFromBuffer();
        } else {
            m_StatusMessage = "Failed to save file!";
        }
    }

    void UICanvasDesignerPanel::RecompileFromBuffer() {
        std::string err;
        auto newDoc = std::make_shared<UIDocument>();
        bool ok = UIParser::ParseSource(m_TextEditBuffer, *newDoc, &err);
        if (ok) {
            m_Document = newDoc;
            m_Document->SetFilePath(m_FilePath.string());
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

            // 2. Mock Vehicle Telemetry Simulator Strip
            ImGui::TextDisabled("LIVE SIMULATION CONTROLS (MOCK TELEMETRY):");
            ImGui::PushItemWidth(110.0f);
            bool valueChanged = false;

            if (ImGui::SliderInt("Speed##Mock", &m_MockSpeed, 0, 360, "%d KM/H")) valueChanged = true;
            ImGui::SameLine();
            if (ImGui::SliderFloat("RPM##Mock", &m_MockRPM, 0.0f, 1.0f, "%.2f")) valueChanged = true;
            ImGui::SameLine();
            const char* gearLabels[] = { "R", "N", "1", "2", "3", "4", "5", "6" };
            if (ImGui::Combo("Gear##Mock", &m_MockGearIndex, gearLabels, IM_ARRAYSIZE(gearLabels))) valueChanged = true;
            ImGui::SameLine();
            if (ImGui::SliderFloat("THR##Mock", &m_MockThrottle, 0.0f, 1.0f, "%.2f")) valueChanged = true;
            ImGui::SameLine();
            if (ImGui::SliderFloat("BRK##Mock", &m_MockBrake, 0.0f, 1.0f, "%.2f")) valueChanged = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Redline##Mock", &m_MockIsRedline)) valueChanged = true;
            ImGui::SameLine();
            if (ImGui::SliderFloat("Lat G##Mock", &m_MockLatG, -2.0f, 2.0f, "%+.2f")) valueChanged = true;

            ImGui::PopItemWidth();

            if (valueChanged) {
                SyncMockContext();
            }

            ImGui::Separator();

            // 3. Dual Split Workspace: Left = Live Canvas Preview, Right = Source Code Editor
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
