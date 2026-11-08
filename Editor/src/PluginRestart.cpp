#include "PluginRestart.hpp"
#include <atomic>
#include <utility>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace Axle {
namespace {
struct Handle {
    HANDLE Value = nullptr;
    ~Handle() { if (Value) CloseHandle(Value); }
};
std::wstring Quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (auto c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
std::wstring EventName(const std::string& channel, const wchar_t* suffix) {
    return L"Local\\AxleRestart-" + std::wstring(channel.begin(), channel.end()) + suffix;
}
bool ValidChannel(const std::string& channel) {
    if (channel.empty() || channel.size() > 100) return false;
    for (auto c : channel) if (!(c >= '0' && c <= '9') && c != '-') return false;
    return true;
}
std::string WinError(const char* operation) { return std::string(operation) + " (Windows error " + std::to_string(GetLastError()) + ")"; }
}
struct PluginRestart::State {
    Handle Ready, Commit, Process, Job;
    std::chrono::steady_clock::time_point Deadline;
    bool Committed = false;
    ~State() {
        // Cancellation terminates only the owned candidate and its descendants.
        if (!Committed && Job.Value) TerminateJobObject(Job.Value, 1);
    }
};
PluginRestart::PluginRestart() = default;
PluginRestart::~PluginRestart() = default;
bool PluginRestart::IsWaiting() const { return m_State && !m_State->Committed; }
bool PluginRestart::CandidateExited(unsigned long& exitCode) const {
    return m_State && WaitForSingleObject(m_State->Process.Value, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(m_State->Process.Value, &exitCode);
}
void PluginRestart::Cancel() { m_State.reset(); }
bool PluginRestart::Launch(const std::filesystem::path& executable, std::vector<std::wstring> arguments, std::string& error, std::chrono::milliseconds timeout) {
    if (IsWaiting()) { error = "An Editor restart is already pending"; return false; }
    m_State.reset(); error.clear();
    auto state = std::make_unique<State>();
    static std::atomic_ulong sequence{0};
    const auto channel = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(++sequence);
    state->Ready.Value = CreateEventW(nullptr, TRUE, FALSE, EventName(channel, L"-ready").c_str());
    if (!state->Ready.Value || GetLastError() == ERROR_ALREADY_EXISTS) { error = WinError("Create restart readiness event"); return false; }
    state->Commit.Value = CreateEventW(nullptr, TRUE, FALSE, EventName(channel, L"-commit").c_str());
    if (!state->Commit.Value || GetLastError() == ERROR_ALREADY_EXISTS) { error = WinError("Create restart commit event"); return false; }
    state->Job.Value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!state->Job.Value || !SetInformationJobObject(state->Job.Value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { error = WinError("Create restart job"); return false; }
    arguments.push_back(L"--restart-channel=" + std::wstring(channel.begin(), channel.end()));
    arguments.push_back(L"--restart-parent=" + std::to_wstring(GetCurrentProcessId()));
    std::wstring command = Quote(executable.wstring());
    for (const auto& argument : arguments) command += L" " + Quote(argument);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
        nullptr, executable.parent_path().c_str(), &startup, &process)) { error = WinError("Launch candidate Editor"); return false; }
    state->Process.Value = process.hProcess; Handle thread; thread.Value = process.hThread;
    if (!AssignProcessToJobObject(state->Job.Value, process.hProcess)) {
        error = WinError("Own candidate Editor process"); TerminateProcess(process.hProcess, 1); return false;
    }
    if (ResumeThread(thread.Value) == static_cast<DWORD>(-1)) { error = WinError("Resume candidate Editor"); return false; }
    state->Deadline = std::chrono::steady_clock::now() + timeout;
    m_State = std::move(state); return true;
}
PluginRestart::Result PluginRestart::Poll(std::string& error, std::function<bool(std::string&)> beforeCommit) {
    if (!m_State) return Result::Idle;
    if (m_State->Committed) return Result::Committed;
    if (WaitForSingleObject(m_State->Process.Value, 0) == WAIT_OBJECT_0) {
        DWORD code = 1; GetExitCodeProcess(m_State->Process.Value, &code);
        error = "Candidate Editor exited before startup acknowledgement (exit " + std::to_string(code) + ")"; m_State.reset(); return Result::Failed;
    }
    if (WaitForSingleObject(m_State->Ready.Value, 0) == WAIT_OBJECT_0) {
        try {
            if (beforeCommit && !beforeCommit(error)) { m_State.reset(); return Result::Failed; }
        } catch (const std::exception& exception) { error = exception.what(); m_State.reset(); return Result::Failed; }
        catch (...) { error = "Could not validate the current Editor before restart"; m_State.reset(); return Result::Failed; }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (!SetInformationJobObject(m_State->Job.Value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !SetEvent(m_State->Commit.Value)) {
            error = WinError("Commit candidate Editor"); m_State.reset(); return Result::Failed;
        }
        m_State->Committed = true; return Result::Committed;
    }
    if (std::chrono::steady_clock::now() >= m_State->Deadline) { error = "Candidate Editor startup timed out; current session was kept"; m_State.reset(); return Result::Failed; }
    return Result::Waiting;
}
struct PluginRestartChild::State {
    Handle Ready, Commit, Parent;
    std::chrono::steady_clock::time_point Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    bool Signalled = false;
};
PluginRestartChild::PluginRestartChild() = default;
PluginRestartChild::~PluginRestartChild() = default;
bool PluginRestartChild::Connect(const std::string& channel, unsigned long parent, std::string& error) {
    if (!ValidChannel(channel) || !parent || m_State) { error = "Invalid restart handoff"; return false; }
    auto state = std::make_unique<State>();
    state->Ready.Value = OpenEventW(EVENT_MODIFY_STATE, FALSE, EventName(channel, L"-ready").c_str());
    state->Commit.Value = OpenEventW(SYNCHRONIZE, FALSE, EventName(channel, L"-commit").c_str());
    state->Parent.Value = OpenProcess(SYNCHRONIZE, FALSE, parent);
    if (!state->Ready.Value || !state->Commit.Value || !state->Parent.Value) { error = WinError("Open restart handoff"); return false; }
    m_State = std::move(state); return true;
}
bool PluginRestartChild::Ready(std::string& error) {
    if (!m_State) { error = "No restart handoff"; return false; }
    if (m_State->Signalled) return true;
    if (!SetEvent(m_State->Ready.Value)) { error = WinError("Acknowledge Editor startup"); return false; }
    m_State->Signalled = true; return true;
}
PluginRestartChild::Result PluginRestartChild::Poll(std::string& error) {
    if (!m_State) { error = "No restart handoff"; return Result::Failed; }
    if (m_State->Signalled && WaitForSingleObject(m_State->Commit.Value, 0) == WAIT_OBJECT_0) return Result::Committed;
    if (WaitForSingleObject(m_State->Parent.Value, 0) == WAIT_OBJECT_0 || std::chrono::steady_clock::now() >= m_State->Deadline) {
        error = "Launching Editor ended or timed out before restart commit"; return Result::Failed;
    }
    return Result::Waiting;
}
}
