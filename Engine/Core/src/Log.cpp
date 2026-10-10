#include <Axle/Log.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Axle {

    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

    void Log::Init() {
        if (s_CoreLogger && s_ClientLogger) return;

        // Pattern format: [Hour:Min:Sec.Millis] [LoggerName] Message
        spdlog::set_pattern("%^[%T.%e] [%n] %v%$");

        if (!s_CoreLogger) {
            s_CoreLogger = spdlog::stdout_color_mt("AXLE");
            s_CoreLogger->set_level(spdlog::level::trace);
        }

        if (!s_ClientLogger) {
            s_ClientLogger = spdlog::stdout_color_mt("APP");
            s_ClientLogger->set_level(spdlog::level::trace);
        }
    }

    std::shared_ptr<spdlog::logger>& Log::GetCoreLogger() {
        if (!s_CoreLogger) {
            Init();
        }
        return s_CoreLogger;
    }

    std::shared_ptr<spdlog::logger>& Log::GetClientLogger() {
        if (!s_ClientLogger) {
            Init();
        }
        return s_ClientLogger;
    }

    void Log::AddSink(const spdlog::sink_ptr& sink) {
        if (!s_CoreLogger || !s_ClientLogger) {
            Init();
        }
        s_CoreLogger->sinks().push_back(sink);
        s_ClientLogger->sinks().push_back(sink);
    }

}