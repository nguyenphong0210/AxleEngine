#pragma once
#include <Axle/Types.hpp>
#include <Axle/JobSystem.hpp>
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
        Failed,
        Cancelled
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
        bool Cancelled = false;
        int ExitCode = -1;
        double DurationSeconds = 0.0;
        std::string OutputLog;
        std::vector<CompilerDiagnostic> Diagnostics;
        int ErrorCount = 0;
        int WarningCount = 0;
    };

    // Immutable snapshot of compile parameters submitted to the compiler
    struct CompileRequest {
        std::filesystem::path WorkspaceDirectory;
        std::string BuildConfig = "Release";
        std::string Target = "AxleScripts";
        OwnerToken Owner = OwnerToken::Global();
        u64 RequestId = 0;
    };

    /**
     * @brief Asynchronous background compiler for C++ script modules (AxleScripts.dll).
     *
     * Snapshots compile requests immutably, manages background child processes safely under Windows,
     * and delivers completion results strictly to the engine main thread via JobSystem::PostToMainThread
     * with OperationOwner invalidation tokens (resolving ECSJ-01).
     */
    class ScriptCompiler {
    public:
        using OnCompleteCallback = std::function<void(const CompilationResult&)>;

        static ScriptCompiler& Get();

        void SetWorkspaceDirectory(const std::filesystem::path& path) {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            m_WorkspaceDirectory = path;
        }
        std::filesystem::path GetWorkspaceDirectory() const {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            return m_WorkspaceDirectory;
        }

        void SetBuildConfig(const std::string& config) {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            m_BuildConfig = config;
        }
        std::string GetBuildConfig() const {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            return m_BuildConfig;
        }

        void SetTarget(const std::string& target) {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            m_Target = target;
        }
        std::string GetTarget() const {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            return m_Target;
        }

        // Snapshot current configuration into an immutable CompileRequest
        CompileRequest CreateRequest(const OwnerToken& owner = OwnerToken::Global()) const;

        /**
         * @brief Starts background asynchronous compilation with an explicit immutable request.
         * Returns false if a compilation is already in progress.
         */
        bool CompileAsync(const CompileRequest& request, OnCompleteCallback onComplete = nullptr);

        /**
         * @brief Starts background asynchronous compilation with owner token (snapshots configuration).
         */
        bool CompileAsync(const OwnerToken& owner, OnCompleteCallback onComplete = nullptr);

        /**
         * @brief Starts background asynchronous compilation (backward compatibility using OwnerToken::Global()).
         */
        bool CompileAsync(OnCompleteCallback onComplete = nullptr);

        /**
         * @brief Compiles synchronously on the calling thread (blocks until finished).
         */
        CompilationResult CompileSync(const CompileRequest& request);
        CompilationResult CompileSync();

        /**
         * @brief Checks if a background compile is currently running.
         */
        bool IsCompiling() const { return m_Status.load(std::memory_order_acquire) == CompilerStatus::Compiling; }

        CompilerStatus GetStatus() const { return m_Status.load(std::memory_order_acquire); }
        CompilationResult GetLastResult() const;
        std::string GetCurrentOutputLog() const;

        u64 GetActiveRequestId() const { return m_ActiveRequestId.load(std::memory_order_acquire); }

        /**
         * @brief Parses a single line of MSVC compiler output into structured diagnostic.
         */
        static CompilerDiagnostic ParseDiagnosticLine(const std::string& line);

        void Cancel();
        void WaitForCompletion();
        // Owner polling is also the recovery path when the bounded executor rejects a post.
        void PollCompletion();

    private:
        ScriptCompiler();
        ~ScriptCompiler();

        ScriptCompiler(const ScriptCompiler&) = delete;
        ScriptCompiler& operator=(const ScriptCompiler&) = delete;

        CompilationResult ExecuteCompileProcess(const CompileRequest& request);

    private:
        mutable std::mutex m_ConfigMutex;
        std::filesystem::path m_WorkspaceDirectory;
        std::string m_BuildConfig = "Release";
        std::string m_Target = "AxleScripts";

        std::atomic<u64> m_NextRequestId{ 1 };
        std::atomic<u64> m_ActiveRequestId{ 0 };

        std::atomic<CompilerStatus> m_Status{ CompilerStatus::Idle };
        mutable std::mutex m_ResultMutex;
        CompilationResult m_LastResult;
        std::string m_LiveOutputLog;

        std::thread m_WorkerThread;
        std::atomic<bool> m_CancelRequested{ false };

        std::mutex m_ProcessMutex;
        void* m_ProcessHandle = nullptr;
        void* m_ProcessJobHandle = nullptr;
        std::thread::id m_OwnerThread = std::this_thread::get_id();

        std::mutex m_PendingMutex;
        std::function<void()> m_PendingCallback;
    };

}
