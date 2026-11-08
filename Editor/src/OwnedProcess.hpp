#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
namespace Axle {
struct OwnedProcessResult { int ExitCode = -1; bool Cancelled = false; std::string Error; };
OwnedProcessResult RunOwnedProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& workingDirectory,
    const std::filesystem::path& log, const std::atomic_bool& cancel);
}
