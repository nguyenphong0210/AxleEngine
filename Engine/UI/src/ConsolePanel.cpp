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

    void ConsolePanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::SetNextWindowSize(ImVec2(600.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Console", p_open)) {
            ImGui::End();
            return;
        }

        // Toolbar
        if (ImGui::Button("Clear")) {
            m_Sink->Clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-Scroll", &m_AutoScroll);

        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::Checkbox("Trace", &m_ShowTrace);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_ShowWarn);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError);

        ImGui::Separator();

        // Message log view child window
        ImGui::BeginChild("LogScrollingRegion", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

        const auto messages = m_Sink->GetMessages();
        for (size_t i = 0; i < messages.size(); ++i) {
            const auto& msg = messages[i];

            // Filter check
            if (msg.Level == spdlog::level::trace && !m_ShowTrace) continue;
            if (msg.Level == spdlog::level::info && !m_ShowInfo) continue;
            if (msg.Level == spdlog::level::warn && !m_ShowWarn) continue;
            if ((msg.Level == spdlog::level::err || msg.Level == spdlog::level::critical) && !m_ShowError) continue;

            ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            const char* levelStr = "INFO";
            switch (msg.Level) {
                case spdlog::level::trace:
                    col = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
                    levelStr = "TRACE";
                    break;
                case spdlog::level::info:
                    col = ImVec4(0.20f, 0.85f, 0.95f, 1.0f);
                    levelStr = "INFO";
                    break;
                case spdlog::level::warn:
                    col = ImVec4(0.98f, 0.75f, 0.12f, 1.0f);
                    levelStr = "WARN";
                    break;
                case spdlog::level::err:
                case spdlog::level::critical:
                    col = ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
                    levelStr = "ERROR";
                    break;
                default:
                    break;
            }

            ImGui::TextDisabled("[%s] [%s]", msg.Timestamp.c_str(), levelStr);
            ImGui::SameLine();
            ImGui::TextColored(col, "%s", msg.Message.c_str());
        }

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::End();
    }

}
