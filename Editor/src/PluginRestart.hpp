#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace Axle {
// Parent owns the candidate process until a healthy child acknowledges startup.
// A failed, timed out or cancelled candidate never closes the current Editor.
class PluginRestart final {
public:
    enum class Result { Idle, Waiting, Committed, Failed };
    PluginRestart();
    ~PluginRestart();
    bool Launch(const std::filesystem::path& executable, std::vector<std::wstring> arguments,
        std::string& error, std::chrono::milliseconds timeout = std::chrono::seconds(90));
    Result Poll(std::string& error, std::function<bool(std::string&)> beforeCommit = {});
    void Cancel();
    bool IsWaiting() const;
    bool CandidateExited(unsigned long& exitCode) const;
private:
    struct State;
    std::unique_ptr<State> m_State;
};
class PluginRestartChild final {
public:
    PluginRestartChild();
    ~PluginRestartChild();
    bool Connect(const std::string& channel, unsigned long parent, std::string& error);
    // Called only after complete product startup and healthy submitted UI frames.
    bool Ready(std::string& error);
    enum class Result { Waiting, Committed, Failed };
    Result Poll(std::string& error);
private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
