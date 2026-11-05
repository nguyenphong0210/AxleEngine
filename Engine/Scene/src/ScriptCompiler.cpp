#include <Axle/ScriptCompiler.hpp>
#include <Axle/Log.hpp>
#include <chrono>
#include <sstream>
#include <iostream>
#include <regex>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace Axle {

    ScriptCompiler& ScriptCompiler::Get() {
        static ScriptCompiler s_Instance;
        return s_Instance;
    }

    ScriptCompiler::ScriptCompiler() {
        // Auto-detect workspace directory containing the 'build' folder
        std::filesystem::path current = std::filesystem::current_path();
        while (!current.empty() && current != current.root_path()) {
            if (std::filesystem::exists(current / "build") && std::filesystem::exists(current / "CMakeLists.txt")) {
                m_WorkspaceDirectory = current;
                break;
            }
            current = current.parent_path();
        }

        if (m_WorkspaceDirectory.empty()) {
            m_WorkspaceDirectory = std::filesystem::current_path();
        }
    }

    ScriptCompiler::~ScriptCompiler() {
        Cancel();
        WaitForCompletion();
    }

    CompileRequest ScriptCompiler::CreateRequest(const OwnerToken& owner) const {
        CompileRequest request;
        {
            std::lock_guard<std::mutex> lock(m_ConfigMutex);
            request.WorkspaceDirectory = m_WorkspaceDirectory;
            request.BuildConfig = m_BuildConfig;
            request.Target = m_Target;
        }
        request.Owner = owner;
        request.RequestId = const_cast<ScriptCompiler*>(this)->m_NextRequestId.fetch_add(1, std::memory_order_relaxed);
        return request;
    }

    void ScriptCompiler::Cancel() {
        m_CancelRequested.store(true, std::memory_order_release);
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(m_ProcessMutex);
        if (m_ProcessHandle) {
            TerminateProcess((HANDLE)m_ProcessHandle, 1);
        }
#endif
    }

    void ScriptCompiler::WaitForCompletion() {
        if (m_WorkerThread.joinable()) {
            m_WorkerThread.join();
        }

        // Deliver queued completions immediately if called on the engine main thread
        if (JobSystem::IsInitialized() && JobSystem::IsMainThread()) {
            JobSystem::PumpMainThreadCompletions();
        }

        // Flush offline pending callback if JobSystem is not initialized
        if (!JobSystem::IsInitialized()) {
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lock(m_PendingMutex);
                cb = std::move(m_PendingCallback);
            }
            if (cb) {
                cb();
            }
        }
    }

    CompilationResult ScriptCompiler::GetLastResult() const {
        std::lock_guard<std::mutex> lock(m_ResultMutex);
        return m_LastResult;
    }

    std::string ScriptCompiler::GetCurrentOutputLog() const {
        std::lock_guard<std::mutex> lock(m_ResultMutex);
        return m_LiveOutputLog;
    }

    CompilerDiagnostic ScriptCompiler::ParseDiagnosticLine(const std::string& line) {
        CompilerDiagnostic diag;
        diag.RawText = line;
        diag.Level = CompilerDiagnostic::Severity::Info;

        // Pattern 1: path\to\file.cpp(123,45): error C2065: message
        // Pattern 2: path\to\file.cpp(123): warning C4101: message
        // Pattern 3: LINK : fatal error LNK1104: message
        // Pattern 4: CMake Error at path:line: message

        static const std::regex msvcFileDiagRegex(R"((.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(error|fatal error|warning)\s*([A-Za-z0-9]+)?\s*:\s*(.*))");
        static const std::regex linkDiagRegex(R"(LINK\s*:\s*(fatal error|error|warning)\s*([A-Za-z0-9]+)?\s*:\s*(.*))");
        static const std::regex cmakeDiagRegex(R"(CMake (Error|Warning)(?: at (.+?):(\d+))?.*:\s*(.*))");

        std::smatch match;
        if (std::regex_search(line, match, msvcFileDiagRegex)) {
            diag.File = match[1].str();
            diag.Line = std::stoi(match[2].str());
            if (match[3].matched) {
                diag.Column = std::stoi(match[3].str());
            }
            std::string severityStr = match[4].str();
            diag.Level = (severityStr == "warning") ? CompilerDiagnostic::Severity::Warning : CompilerDiagnostic::Severity::Error;
            if (match[5].matched) diag.Code = match[5].str();
            diag.Message = match[6].str();
            return diag;
        }

        if (std::regex_search(line, match, linkDiagRegex)) {
            std::string severityStr = match[1].str();
            diag.Level = (severityStr == "warning") ? CompilerDiagnostic::Severity::Warning : CompilerDiagnostic::Severity::Error;
            if (match[2].matched) diag.Code = match[2].str();
            diag.Message = match[3].str();
            return diag;
        }

        if (std::regex_search(line, match, cmakeDiagRegex)) {
            std::string severityStr = match[1].str();
            diag.Level = (severityStr == "Warning") ? CompilerDiagnostic::Severity::Warning : CompilerDiagnostic::Severity::Error;
            if (match[2].matched) diag.File = match[2].str();
            if (match[3].matched) diag.Line = std::stoi(match[3].str());
            diag.Message = match[4].str();
            return diag;
        }

        if (line.find("error:") != std::string::npos || line.find("error C") != std::string::npos || line.find("fatal error") != std::string::npos) {
            diag.Level = CompilerDiagnostic::Severity::Error;
            diag.Message = line;
        } else if (line.find("warning:") != std::string::npos || line.find("warning C") != std::string::npos) {
            diag.Level = CompilerDiagnostic::Severity::Warning;
            diag.Message = line;
        }

        return diag;
    }

    bool ScriptCompiler::CompileAsync(const CompileRequest& request, OnCompleteCallback onComplete) {
        if (m_Status.load(std::memory_order_acquire) == CompilerStatus::Compiling) {
            AXLE_WARN("ScriptCompiler: CompileAsync called while a build is already in progress!");
            return false;
        }

        WaitForCompletion();

        m_CancelRequested.store(false, std::memory_order_release);
        m_ActiveRequestId.store(request.RequestId, std::memory_order_release);
        m_Status.store(CompilerStatus::Compiling, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_ResultMutex);
            m_LiveOutputLog.clear();
            m_LastResult = CompilationResult();
        }

        m_WorkerThread = std::thread([this, request, onComplete]() {
            CompilationResult result = ExecuteCompileProcess(request);

            const bool wasCancelled = m_CancelRequested.load(std::memory_order_acquire);
            if (wasCancelled) {
                result.Cancelled = true;
                result.Success = false;
            }

            {
                std::lock_guard<std::mutex> lock(m_ResultMutex);
                m_LastResult = result;
                m_Status.store(
                    result.Success ? CompilerStatus::Success :
                    (result.Cancelled ? CompilerStatus::Cancelled : CompilerStatus::Failed),
                    std::memory_order_release
                );
            }

            if (onComplete) {
                if (JobSystem::IsInitialized()) {
                    // Deliver to engine OS owner thread via PostToMainThread with owner token
                    JobSystem::PostToMainThread(request.Owner, [onComplete, result]() {
                        if (onComplete) {
                            onComplete(result);
                        }
                    });
                } else {
                    // JobSystem not initialized: store in pending callback for caller flush
                    std::lock_guard<std::mutex> lock(m_PendingMutex);
                    m_PendingCallback = [onComplete, result]() {
                        if (onComplete) {
                            onComplete(result);
                        }
                    };
                }
            }
        });

        return true;
    }

    bool ScriptCompiler::CompileAsync(const OwnerToken& owner, OnCompleteCallback onComplete) {
        return CompileAsync(CreateRequest(owner), std::move(onComplete));
    }

    bool ScriptCompiler::CompileAsync(OnCompleteCallback onComplete) {
        return CompileAsync(OwnerToken::Global(), std::move(onComplete));
    }

    CompilationResult ScriptCompiler::CompileSync(const CompileRequest& request) {
        if (m_Status.load(std::memory_order_acquire) == CompilerStatus::Compiling) {
            WaitForCompletion();
            return GetLastResult();
        }

        m_CancelRequested.store(false, std::memory_order_release);
        m_ActiveRequestId.store(request.RequestId, std::memory_order_release);
        m_Status.store(CompilerStatus::Compiling, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_ResultMutex);
            m_LiveOutputLog.clear();
            m_LastResult = CompilationResult();
        }

        CompilationResult result = ExecuteCompileProcess(request);
        const bool wasCancelled = m_CancelRequested.load(std::memory_order_acquire);
        if (wasCancelled) {
            result.Cancelled = true;
            result.Success = false;
        }

        {
            std::lock_guard<std::mutex> lock(m_ResultMutex);
            m_LastResult = result;
            m_Status.store(
                result.Success ? CompilerStatus::Success :
                (result.Cancelled ? CompilerStatus::Cancelled : CompilerStatus::Failed),
                std::memory_order_release
            );
        }

        return result;
    }

    CompilationResult ScriptCompiler::CompileSync() {
        return CompileSync(CreateRequest());
    }

    CompilationResult ScriptCompiler::ExecuteCompileProcess(const CompileRequest& request) {
        auto startTime = std::chrono::high_resolution_clock::now();
        CompilationResult result;

#ifdef _WIN32
        if (m_CancelRequested.load(std::memory_order_acquire)) {
            result.Success = false;
            result.Cancelled = true;
            result.ExitCode = 1;
            result.OutputLog = "Compilation cancelled before process creation.";
            return result;
        }

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            result.Success = false;
            result.ExitCode = -1;
            result.OutputLog = "Failed to create I/O pipes for compiler execution.";
            return result;
        }

        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = { 0 };
        si.cb = sizeof(STARTUPINFOA);
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi = { 0 };

        std::string cmd = "cmd.exe /c cmake --build build --config " + request.BuildConfig + " --target " + request.Target;
        std::string workingDir = request.WorkspaceDirectory.string();

        char cmdBuffer[2048];
        strncpy_s(cmdBuffer, cmd.c_str(), sizeof(cmdBuffer) - 1);

        BOOL processCreated = CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            workingDir.empty() ? NULL : workingDir.c_str(),
            &si,
            &pi
        );

        CloseHandle(hWritePipe); // Close write handle in parent so ReadFile hits EOF when process exits

        if (!processCreated) {
            DWORD err = GetLastError();
            CloseHandle(hReadPipe);
            result.Success = false;
            result.ExitCode = (int)err;
            result.OutputLog = "Failed to spawn compile process: error code " + std::to_string(err);
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(m_ProcessMutex);
            if (m_CancelRequested.load(std::memory_order_acquire)) {
                TerminateProcess(pi.hProcess, 1);
            } else {
                m_ProcessHandle = pi.hProcess;
            }
        }

        // Real-time stdout/stderr capture
        char buffer[2048];
        DWORD bytesRead = 0;
        std::string lineAccumulator;

        while (!m_CancelRequested.load(std::memory_order_acquire) &&
               ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string chunk(buffer, bytesRead);

            {
                std::lock_guard<std::mutex> lock(m_ResultMutex);
                m_LiveOutputLog += chunk;
            }

            lineAccumulator += chunk;
            size_t newlinePos = 0;
            while ((newlinePos = lineAccumulator.find('\n')) != std::string::npos) {
                std::string line = lineAccumulator.substr(0, newlinePos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lineAccumulator.erase(0, newlinePos + 1);

                CompilerDiagnostic diag = ParseDiagnosticLine(line);
                if (diag.Level == CompilerDiagnostic::Severity::Error) {
                    result.ErrorCount++;
                    result.Diagnostics.push_back(diag);
                } else if (diag.Level == CompilerDiagnostic::Severity::Warning) {
                    result.WarningCount++;
                    result.Diagnostics.push_back(diag);
                }
            }
        }

        // Flush remaining trailing text in lineAccumulator
        if (!lineAccumulator.empty()) {
            CompilerDiagnostic diag = ParseDiagnosticLine(lineAccumulator);
            if (diag.Level == CompilerDiagnostic::Severity::Error) {
                result.ErrorCount++;
                result.Diagnostics.push_back(diag);
            } else if (diag.Level == CompilerDiagnostic::Severity::Warning) {
                result.WarningCount++;
                result.Diagnostics.push_back(diag);
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(hReadPipe);

        {
            std::lock_guard<std::mutex> lock(m_ProcessMutex);
            m_ProcessHandle = nullptr;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = endTime - startTime;

        result.ExitCode = (int)exitCode;
        result.Success = (exitCode == 0 && !m_CancelRequested.load(std::memory_order_acquire));
        result.Cancelled = m_CancelRequested.load(std::memory_order_acquire);
        result.DurationSeconds = duration.count();

        {
            std::lock_guard<std::mutex> lock(m_ResultMutex);
            result.OutputLog = m_LiveOutputLog;
        }

        if (result.Success) {
            AXLE_INFO("ScriptCompiler: [OK] Compiled target '{0}' successfully in {1:.2f}s ({2} warnings)",
                request.Target, result.DurationSeconds, result.WarningCount);
        } else if (result.Cancelled) {
            AXLE_WARN("ScriptCompiler: [CANCELLED] Target '{0}' compilation was cancelled", request.Target);
        } else {
            AXLE_ERROR("ScriptCompiler: [FAILED] Target '{0}' failed with exit code {1} ({2} errors, {3} warnings)",
                request.Target, result.ExitCode, result.ErrorCount, result.WarningCount);
        }

        return result;
#else
        result.Success = false;
        result.OutputLog = "ScriptCompiler is currently supported on Windows only.";
        return result;
#endif
    }

}
