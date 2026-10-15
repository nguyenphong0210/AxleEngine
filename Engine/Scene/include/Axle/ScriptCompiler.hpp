#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>

namespace Axle {

    enum class CompilerStatus {
        Idle,
        Compiling,
        Success,
        Failed
    };

    struct CompilerDiagnostic {
        enum class Severity { Info, Warning, Error };
        Severity Level = Severity::Info;
        std::string File;
        int Line = 0;
        int Column = 0;
        std::string Code;    // e.g. "C2065" or "LNK1104"
        std::string Message; // e.g. "'myVar': undeclared identifier"
        std::string RawText;
    };

    struct CompilationResult {
        bool Success = false;
        int ExitCode = -1;
        double DurationSeconds = 0.0;
        std::string OutputLog;
        std::vector<CompilerDiagnostic> Diagnostics;
        int ErrorCount = 0;
        int WarningCount = 0;
    };

    /**
     * @brief Asynchronous background compiler for C++ script modules (AxleScripts.dll).
     *
     * Spawns a dedicated worker thread invoking the build system without freezing the editor UI,
     * captures stdout/stderr in real-time, parses MSVC error/warning diagnostics, and invokes callbacks.
     */
    class ScriptCompiler {
    public:
        using OnCompleteCallback = std::function<void(const CompilationResult&)>;

        static ScriptCompiler& Get();

        void SetWorkspaceDirectory(const std::filesystem::path& path) { m_WorkspaceDirectory = path; }
        const std::filesystem::path& GetWorkspaceDirectory() const { return m_WorkspaceDirectory; }

        void SetBuildConfig(const std::string& config) { m_BuildConfig = config; }
        const std::string& GetBuildConfig() const { return m_BuildConfig; }

        void SetTarget(const std::string& target) { m_Target = target; }
        const std::string& GetTarget() const { return m_Target; }

        /**
         * @brief Starts background asynchronous compilation on a dedicated worker thread.
         * Returns false if a compilation is already in progress.
         */
        bool CompileAsync(OnCompleteCallback onComplete = nullptr);

        /**
         * @brief Compiles synchronously on the calling thread (blocks until finished).
         */
        CompilationResult CompileSync();

        /**
         * @brief Checks if a background compile is currently running.
         */
        bool IsCompiling() const { return m_Status.load() == CompilerStatus::Compiling; }

        CompilerStatus GetStatus() const { return m_Status.load(); }
        CompilationResult GetLastResult() const;
        std::string GetCurrentOutputLog() const;

        /**
         * @brief Parses a single line of MSVC compiler output into structured diagnostic.
         */
        static CompilerDiagnostic ParseDiagnosticLine(const std::string& line);

        void Cancel();
        void WaitForCompletion();

    private:
        ScriptCompiler();
        ~ScriptCompiler();

        ScriptCompiler(const ScriptCompiler&) = delete;
        ScriptCompiler& operator=(const ScriptCompiler&) = delete;

        CompilationResult ExecuteCompileProcess();

    private:
        std::filesystem::path m_WorkspaceDirectory;
        std::string m_BuildConfig = "Release";
        std::string m_Target = "AxleScripts";

        std::atomic<CompilerStatus> m_Status{ CompilerStatus::Idle };
        mutable std::mutex m_ResultMutex;
        CompilationResult m_LastResult;
        std::string m_LiveOutputLog;

        std::thread m_WorkerThread;
        std::atomic<bool> m_CancelRequested{ false };
        void* m_ProcessHandle = nullptr;
    };

}
