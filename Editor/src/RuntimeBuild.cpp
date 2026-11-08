#include "RuntimeBuild.hpp"
#include "RuntimeBuildConfig.hpp"
#include <chrono>
#include <fstream>
#include <system_error>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axle {
    RuntimeBuildRequest GetRuntimeBuildRequest() {
        return { AXLE_RUNTIME_CMAKE, AXLE_RUNTIME_BUILD_DIRECTORY,
                 AXLE_RUNTIME_EXECUTABLE_DIRECTORY, AXLE_RUNTIME_CONFIGURATION };
    }

#ifdef _WIN32
    namespace {
        struct Handle {
            HANDLE Value = nullptr;
            ~Handle() { if (Value && Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            explicit Handle(HANDLE value = nullptr) : Value(value) {}
        };

        // CreateProcess receives arguments directly, without cmd.exe or shell expansion.
        std::wstring Quote(const std::wstring& value) {
            std::wstring result = L"\"";
            size_t slashes = 0;
            for (wchar_t ch : value) {
                if (ch == L'\\') { ++slashes; continue; }
                result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                result += ch;
                slashes = 0;
            }
            result.append(slashes * 2, L'\\');
            return result + L'"';
        }
    }
#endif

    RuntimeBuildResult BuildRuntime(const RuntimeBuildRequest& request, const std::atomic_bool& cancel) {
        RuntimeBuildResult result;
        result.ExecutableDirectory = request.ExecutableDirectory;
        result.ReceiptPath = request.ExecutableDirectory / "AxleRuntime.build.receipt";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(request.CMake, ec) ||
            !std::filesystem::is_regular_file(request.BuildDirectory / "CMakeCache.txt", ec) ||
            request.ExecutableDirectory.empty()) {
            result.Error = "Runtime build tools or configured build directory are unavailable. Use a configured engine checkout.";
            return result;
        }
        if (cancel.load()) { result.Error = "Runtime build cancelled."; return result; }
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        result.LogPath = request.BuildDirectory / ("runtime-build-" + std::to_string(stamp) + ".log");
#ifdef _WIN32
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle output(CreateFileW(result.LogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                   &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle job(CreateJobObjectW(nullptr, nullptr));
        if (output.Value == INVALID_HANDLE_VALUE || input.Value == INVALID_HANDLE_VALUE || !job.Value) {
            result.Error = "Could not create Runtime build log/process handles.";
            return result;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.Value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            result.Error = "Could not configure Runtime build process ownership.";
            return result;
        }
        auto command = Quote(request.CMake.wstring()) + L" --build " + Quote(request.BuildDirectory.wstring());
        if (!request.Configuration.empty())
            command += L" --config " + Quote(std::filesystem::path(request.Configuration).wstring());
        command += L" --target AxleRuntime";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = output.Value;
        startup.hStdInput = input.Value;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(request.CMake.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, request.BuildDirectory.c_str(), &startup, &process)) {
            result.Error = "Could not start CMake Runtime build (Windows error " + std::to_string(GetLastError()) + ").";
            return result;
        }
        Handle processHandle(process.hProcess), thread(process.hThread);
        if (!AssignProcessToJobObject(job.Value, processHandle.Value)) {
            TerminateProcess(processHandle.Value, 1);
            result.Error = "Could not attach Runtime build to its process owner.";
            return result;
        }
        if (ResumeThread(thread.Value) == static_cast<DWORD>(-1)) {
            result.Error = "Could not resume Runtime build.";
            return result;
        }
        for (;;) {
            const DWORD wait = WaitForSingleObject(processHandle.Value, 100);
            if (cancel.load()) {
                TerminateJobObject(job.Value, 1);
                WaitForSingleObject(processHandle.Value, 5000);
                result.Error = "Runtime build cancelled.";
                return result;
            }
            if (wait == WAIT_OBJECT_0) break;
            if (wait != WAIT_TIMEOUT) { result.Error = "Runtime build process wait failed."; return result; }
        }
        DWORD exitCode = 1;
        if (!GetExitCodeProcess(processHandle.Value, &exitCode)) {
            result.Error = "Could not read Runtime build result.";
            return result;
        }
        result.ExitCode = static_cast<int>(exitCode);
        if (exitCode != 0) {
            result.Error = "Runtime build failed (exit " + std::to_string(exitCode) + "). See " + result.LogPath.string();
            return result;
        }
        if (!std::filesystem::is_regular_file(request.ExecutableDirectory / "AxleRuntime.exe", ec) ||
            !std::filesystem::is_regular_file(request.ExecutableDirectory / "AxleScripts.dll", ec)) {
            result.Error = "Runtime build finished without the expected player and scripts module.";
            return result;
        }
        // Publish a last-known-good marker only after the complete artifact set
        // has been validated.  Write and rename so a cancelled/failed build can
        // never leave a truncated receipt that looks usable to the Editor.
        const auto receiptTemp = result.ReceiptPath.string() + ".tmp";
        {
            std::ofstream receipt(receiptTemp, std::ios::trunc);
            if (!receipt) {
                result.Error = "Runtime build succeeded but its receipt could not be written.";
                return result;
            }
            receipt << "schema=1\n"
                    << "configuration=" << request.Configuration << "\n"
                    << "player=AxleRuntime.exe\n"
                    << "scripts=AxleScripts.dll\n";
            receipt.flush();
            if (!receipt) {
                std::error_code removeEc;
                std::filesystem::remove(receiptTemp, removeEc);
                result.Error = "Runtime build succeeded but its receipt could not be completed.";
                return result;
            }
        }
        std::error_code renameEc;
#ifdef _WIN32
        // std::filesystem::rename does not replace an existing file on
        // Windows. MoveFileEx provides an atomic replacement on the same
        // volume and keeps the old receipt if the replacement cannot happen.
        if (!MoveFileExW(std::filesystem::path(receiptTemp).wstring().c_str(),
                         result.ReceiptPath.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            renameEc = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }
#else
        std::filesystem::rename(receiptTemp, result.ReceiptPath, renameEc);
#endif
        if (renameEc) {
            std::error_code removeEc;
            std::filesystem::remove(receiptTemp, removeEc);
            result.Error = "Runtime build succeeded but its receipt could not be published.";
            return result;
        }
        result.Success = true;
#else
        result.Error = "Building the Windows standalone player is currently supported only on Windows.";
#endif
        return result;
    }
}
