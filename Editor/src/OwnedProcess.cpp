#include "OwnedProcess.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace Axle {
#ifdef _WIN32
namespace {
struct Handle {
    HANDLE Value;
    explicit Handle(HANDLE value) : Value(value) {}
    ~Handle() { if (Value && Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
    Handle(const Handle&) = delete;
};
std::wstring Quote(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        quoted.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        quoted += c; slashes = 0;
    }
    quoted.append(2 * slashes, L'\\'); return quoted + L'"';
}
}
#endif
OwnedProcessResult RunOwnedProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& workingDirectory,
    const std::filesystem::path& log, const std::atomic_bool& cancel) {
    OwnedProcessResult result;
    if (cancel.load()) { result.Cancelled = true; result.Error = "Process cancelled"; return result; }
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle output(CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (output.Value == INVALID_HANDLE_VALUE || input.Value == INVALID_HANDLE_VALUE || !job.Value) {
        result.Error = "Cannot create owned process handles"; return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.Value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        result.Error = "Cannot configure process ownership"; return result;
    }
    auto command = Quote(executable.wstring());
    for (const auto& argument : arguments) command += L" " + Quote(argument);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(0);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output.Value; startup.hStdInput = input.Value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup, &process)) {
        result.Error = "Cannot launch process (Windows error " + std::to_string(GetLastError()) + ")"; return result;
    }
    Handle processHandle(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.Value, processHandle.Value)) {
        TerminateProcess(processHandle.Value, 1); WaitForSingleObject(processHandle.Value, 5000);
        result.Error = "Cannot attach process to its owner"; return result;
    }
    if (ResumeThread(thread.Value) == static_cast<DWORD>(-1)) { result.Error = "Cannot resume process"; return result; }
    for (;;) {
        const auto wait = WaitForSingleObject(processHandle.Value, 100);
        if (cancel.load()) {
            TerminateJobObject(job.Value, 1); WaitForSingleObject(processHandle.Value, 5000);
            result.Cancelled = true; result.Error = "Process cancelled"; return result;
        }
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_TIMEOUT) { result.Error = "Process wait failed"; return result; }
    }
    DWORD code;
    if (!GetExitCodeProcess(processHandle.Value, &code)) { result.Error = "Cannot read process exit status"; return result; }
    result.ExitCode = static_cast<int>(code);
    if (code != 0) result.Error = "Process failed (exit " + std::to_string(code) + ")";
#else
    result.Error = "Owned native plugin processes require Windows";
#endif
    return result;
}
}
