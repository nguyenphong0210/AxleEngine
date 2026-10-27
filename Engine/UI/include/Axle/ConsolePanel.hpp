#pragma once
#include <Axle/Types.hpp>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>
#include <mutex>
#include <vector>
#include <string>

namespace Axle {

    struct ConsoleMessage {
        std::string Timestamp;
        std::string LoggerName;
        std::string Message;
        spdlog::level::level_enum Level = spdlog::level::info;
    };

    class EditorConsoleSink : public spdlog::sinks::base_sink<std::mutex> {
    public:
        EditorConsoleSink(size_t maxMessages = 1000);
        ~EditorConsoleSink() override = default;

        std::vector<ConsoleMessage> GetMessages();
        void Clear();

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override;
        void flush_() override {}

    private:
        size_t m_MaxMessages;
        std::vector<ConsoleMessage> m_Messages;
    };

    class ConsolePanel {
    public:
        ConsolePanel();
        ~ConsolePanel() = default;

        void OnImGuiRender(bool* p_open = nullptr);
        void Clear() { if (m_Sink) m_Sink->Clear(); m_SelectedMessageIndex = -1; }
        bool GetClearOnPlay() const { return m_ClearOnPlay; }

    private:
        std::shared_ptr<EditorConsoleSink> m_Sink;
        bool m_AutoScroll = true;
        bool m_ShowTrace = true;
        bool m_ShowInfo = true;
        bool m_ShowWarn = true;
        bool m_ShowError = true;
        bool m_Collapse = false;
        bool m_ClearOnPlay = false;
        int m_SelectedMessageIndex = -1;
        char m_SearchFilter[128] = "";
    };

}
