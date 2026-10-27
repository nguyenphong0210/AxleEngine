#include <Axle/ConsolePanel.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Axle {

    EditorConsoleSink::EditorConsoleSink(size_t maxMessages)
        : m_MaxMessages(maxMessages) {
    }

    void EditorConsoleSink::sink_it_(const spdlog::details::log_msg& msg) {
        ConsoleMessage consoleMsg;
        consoleMsg.Level = msg.level;
        consoleMsg.LoggerName = std::string(msg.logger_name.data(), msg.logger_name.size());
        consoleMsg.Message = std::string(msg.payload.data(), msg.payload.size());

        // Format timestamp: HH:MM:SS.mmm
        auto duration = msg.time.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000;
        std::time_t timeT = std::chrono::system_clock::to_time_t(msg.time);
        std::tm tmTime{};
#if defined(_WIN32)
        localtime_s(&tmTime, &timeT);
#else
        localtime_r(&timeT, &tmTime);
#endif
        std::ostringstream ss;
        ss << std::setfill('0')
           << std::setw(2) << tmTime.tm_hour << ":"
           << std::setw(2) << tmTime.tm_min << ":"
           << std::setw(2) << tmTime.tm_sec << "."
           << std::setw(3) << millis;
        consoleMsg.Timestamp = ss.str();

        if (m_Messages.size() >= m_MaxMessages) {
            m_Messages.erase(m_Messages.begin(), m_Messages.begin() + (m_MaxMessages / 4));
        }
        m_Messages.push_back(std::move(consoleMsg));
    }

    std::vector<ConsoleMessage> EditorConsoleSink::GetMessages() {
        std::lock_guard<std::mutex> lock(mutex_);
        return m_Messages;
    }

    void EditorConsoleSink::Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        m_Messages.clear();
    }

    ConsolePanel::ConsolePanel() {
        m_Sink = std::make_shared<EditorConsoleSink>(1000);
        Log::AddSink(m_Sink);
    }

    static bool CaseInsensitiveFind(const std::string& str, const std::string& query) {
        if (query.empty()) return true;
        auto it = std::search(
            str.begin(), str.end(),
            query.begin(), query.end(),
            [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); }
        );
        return it != str.end();
    }

    void ConsolePanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::SetNextWindowSize(ImVec2(680.0f, 280.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Console", p_open)) {
            ImGui::End();
            return;
        }

        const auto messages = m_Sink->GetMessages();

        // 1. Calculate category counts
        size_t infoCount = 0;
        size_t warnCount = 0;
        size_t errorCount = 0;
        for (const auto& msg : messages) {
            if (msg.Level == spdlog::level::warn) {
                warnCount++;
            } else if (msg.Level == spdlog::level::err || msg.Level == spdlog::level::critical) {
                errorCount++;
            } else {
                infoCount++;
            }
        }

        // Helper lambda for Unity-style toggle buttons
        auto DrawToggleButton = [](const char* label, bool* state, ImVec2 size = ImVec2(0.0f, 24.0f)) -> bool {
            bool active = *state;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.44f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.76f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.38f, 0.60f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.26f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            bool clicked = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (clicked) {
                *state = !(*state);
            }
            return clicked;
        };

        // --- 2. UNITY CONSOLE TOOLBAR ---
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("Clear", ImVec2(50.0f, 24.0f))) {
            m_Sink->Clear();
            m_SelectedMessageIndex = -1;
        }
        ImGui::PopStyleVar();

        ImGui::SameLine();
        DrawToggleButton("Collapse", &m_Collapse, ImVec2(66.0f, 24.0f));

        ImGui::SameLine();
        DrawToggleButton("Clear on Play", &m_ClearOnPlay, ImVec2(88.0f, 24.0f));

        ImGui::SameLine();
        DrawToggleButton("Auto-Scroll", &m_AutoScroll, ImVec2(78.0f, 24.0f));

        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputTextWithHint("##ConsoleSearch", "Search...", m_SearchFilter, sizeof(m_SearchFilter));
        if (m_SearchFilter[0] != '\0') {
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                m_SearchFilter[0] = '\0';
            }
        }

        // Right side: Unity Icon Pill Toggles with live counters
        const float rightClusterWidth = 220.0f;
        float rightTargetX = ImGui::GetWindowWidth() - rightClusterWidth;
        if (ImGui::GetCursorPosX() < rightTargetX) {
            ImGui::SameLine(rightTargetX);
        } else {
            ImGui::SameLine();
        }

        std::string infoLabel = "Info " + std::to_string(infoCount);
        bool infoState = m_ShowInfo;
        if (DrawToggleButton(infoLabel.c_str(), &infoState, ImVec2(68.0f, 24.0f))) {
            m_ShowInfo = infoState;
            m_ShowTrace = infoState;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Information Logs (%zu)", infoCount);

        ImGui::SameLine();
        std::string warnLabel = "Warn " + std::to_string(warnCount);
        if (DrawToggleButton(warnLabel.c_str(), &m_ShowWarn, ImVec2(68.0f, 24.0f))) {
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Warning Logs (%zu)", warnCount);

        ImGui::SameLine();
        std::string errLabel = "Error " + std::to_string(errorCount);
        if (DrawToggleButton(errLabel.c_str(), &m_ShowError, ImVec2(70.0f, 24.0f))) {
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Error Logs (%zu)", errorCount);

        ImGui::Separator();

        // --- 3. PREPARE DISPLAY LIST (Handle Collapse & Filters) ---
        struct DisplayEntry {
            size_t OriginalIndex;
            std::string Timestamp;
            std::string LoggerName;
            std::string Message;
            spdlog::level::level_enum Level;
            int Count;
        };

        std::vector<DisplayEntry> displayEntries;
        displayEntries.reserve(messages.size());

        for (size_t i = 0; i < messages.size(); ++i) {
            const auto& msg = messages[i];

            // Filter check
            if ((msg.Level == spdlog::level::trace || msg.Level == spdlog::level::info) && !m_ShowInfo) continue;
            if (msg.Level == spdlog::level::warn && !m_ShowWarn) continue;
            if ((msg.Level == spdlog::level::err || msg.Level == spdlog::level::critical) && !m_ShowError) continue;

            if (m_SearchFilter[0] != '\0' && !CaseInsensitiveFind(msg.Message, m_SearchFilter)) {
                continue;
            }

            if (m_Collapse && !displayEntries.empty()) {
                auto& last = displayEntries.back();
                if (last.Level == msg.Level && last.Message == msg.Message) {
                    last.Count++;
                    last.Timestamp = msg.Timestamp; // latest timestamp
                    continue;
                }
            }

            displayEntries.push_back({ i, msg.Timestamp, msg.LoggerName, msg.Message, msg.Level, 1 });
        }

        // --- 4. MESSAGE LIST & DETAIL SPLIT PANE ---
        float totalHeight = ImGui::GetContentRegionAvail().y;
        bool hasSelection = (m_SelectedMessageIndex >= 0 && m_SelectedMessageIndex < (int)messages.size());
        float detailPaneHeight = hasSelection ? 92.0f : 0.0f;
        float listHeight = totalHeight - detailPaneHeight;
        if (listHeight < 50.0f) listHeight = 50.0f;

        ImGui::BeginChild("LogScrollingRegion", ImVec2(0.0f, listHeight), false);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

        for (size_t i = 0; i < displayEntries.size(); ++i) {
            const auto& entry = displayEntries[i];
            bool isSelected = (m_SelectedMessageIndex == (int)entry.OriginalIndex);

            // Alternating Unity row background
            ImVec4 rowBg = (i % 2 == 0) ? ImVec4(0.15f, 0.15f, 0.15f, 0.50f) : ImVec4(0.18f, 0.18f, 0.18f, 0.50f);
            if (isSelected) {
                rowBg = ImVec4(0.18f, 0.36f, 0.58f, 0.90f); // Unity selection blue
            }

            ImGui::PushStyleColor(ImGuiCol_Header, rowBg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.24f, 0.24f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.40f, 0.65f, 1.0f));

            ImVec4 levelCol = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            const char* levelIcon = "[i]";
            switch (entry.Level) {
                case spdlog::level::trace:
                    levelCol = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
                    levelIcon = "[.]";
                    break;
                case spdlog::level::info:
                    levelCol = ImVec4(0.35f, 0.82f, 0.95f, 1.0f);
                    levelIcon = "[i]";
                    break;
                case spdlog::level::warn:
                    levelCol = ImVec4(0.98f, 0.78f, 0.15f, 1.0f);
                    levelIcon = "[!]";
                    break;
                case spdlog::level::err:
                case spdlog::level::critical:
                    levelCol = ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
                    levelIcon = "[X]";
                    break;
                default:
                    break;
            }

            // Render selectable row
            std::string selectableID = "##LogEntry_" + std::to_string(i);
            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
            if (ImGui::Selectable(selectableID.c_str(), isSelected, flags, ImVec2(0.0f, 20.0f))) {
                m_SelectedMessageIndex = (int)entry.OriginalIndex;
            }

            ImGui::SameLine(6.0f);
            ImGui::TextColored(levelCol, "%s", levelIcon);
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", entry.Timestamp.c_str());
            ImGui::SameLine();

            // Truncate first line of message for concise list display
            std::string singleLine = entry.Message;
            auto newlinePos = singleLine.find('\n');
            if (newlinePos != std::string::npos) {
                singleLine = singleLine.substr(0, newlinePos) + "...";
            }
            ImGui::TextColored(levelCol, "%s", singleLine.c_str());

            // Right side count badge if collapsed
            if (entry.Count > 1) {
                std::string countStr = std::to_string(entry.Count);
                float countW = ImGui::CalcTextSize(countStr.c_str()).x + 12.0f;
                ImGui::SameLine(ImGui::GetWindowWidth() - countW - 20.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
                ImGui::TextDisabled("[%s]", countStr.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::PopStyleColor(3);
        }

        ImGui::PopStyleVar();

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        // --- 5. BOTTOM DETAIL PANE (Selected Message Stack/Payload) ---
        if (hasSelection) {
            ImGui::Separator();
            const auto& selMsg = messages[m_SelectedMessageIndex];

            ImGui::BeginChild("LogDetailRegion", ImVec2(0.0f, detailPaneHeight - 6.0f), false);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
            ImGui::Text("Timestamp: %s | Logger: %s", selMsg.Timestamp.c_str(), selMsg.LoggerName.c_str());
            ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
            if (ImGui::SmallButton("x")) {
                m_SelectedMessageIndex = -1;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
            ImGui::TextWrapped("%s", selMsg.Message.c_str());
            ImGui::PopStyleColor();

            ImGui::EndChild();
        }

        ImGui::End();
    }

}
