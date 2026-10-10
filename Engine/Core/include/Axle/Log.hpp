#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace Axle {

    class Log {
    public:
        static void Init();

        static std::shared_ptr<spdlog::logger>& GetCoreLogger();
        static std::shared_ptr<spdlog::logger>& GetClientLogger();

        static void AddSink(const spdlog::sink_ptr& sink);

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };

}

// Core Log Macros
#define AXLE_CORE_TRACE(...)    ::Axle::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define AXLE_CORE_INFO(...)     ::Axle::Log::GetCoreLogger()->info(__VA_ARGS__)
#define AXLE_CORE_WARN(...)     ::Axle::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define AXLE_CORE_ERROR(...)    ::Axle::Log::GetCoreLogger()->error(__VA_ARGS__)
#define AXLE_CORE_CRITICAL(...) ::Axle::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client Log Macros
#define AXLE_TRACE(...)         ::Axle::Log::GetClientLogger()->trace(__VA_ARGS__)
#define AXLE_INFO(...)          ::Axle::Log::GetClientLogger()->info(__VA_ARGS__)
#define AXLE_WARN(...)          ::Axle::Log::GetClientLogger()->warn(__VA_ARGS__)
#define AXLE_ERROR(...)         ::Axle::Log::GetClientLogger()->error(__VA_ARGS__)
#define AXLE_CRITICAL(...)      ::Axle::Log::GetClientLogger()->critical(__VA_ARGS__)

// Assertion Macros
#if defined(_DEBUG) || !defined(NDEBUG)
    #if defined(_MSC_VER)
        #define AXLE_DEBUGBREAK() __debugbreak()
    #else
        #include <csignal>
        #define AXLE_DEBUGBREAK() raise(SIGTRAP)
    #endif
    #define AXLE_CORE_ASSERT(check, ...) { if (!(check)) { AXLE_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); AXLE_DEBUGBREAK(); } }
    #define AXLE_ASSERT(check, ...)      { if (!(check)) { AXLE_ERROR("Assertion Failed: {0}", __VA_ARGS__); AXLE_DEBUGBREAK(); } }
#else
    #define AXLE_CORE_ASSERT(check, ...)
    #define AXLE_ASSERT(check, ...)
#endif