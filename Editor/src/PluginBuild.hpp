#pragma once
#include <Axle/Plugin.hpp>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
namespace Axle {
struct PluginBuildRequest {
    std::filesystem::path CMake;
    std::filesystem::path ProjectDirectory;
    std::filesystem::path BuildDirectory; // generated root, never active artifacts
    std::filesystem::path SdkDirectory;
    std::string Generator;
    std::string GeneratorToolset;
    std::filesystem::path GeneratorInstance;
    Plugin::BuildPlan Plan;
};
struct PluginBuildResult {
    bool Success = false;
    bool UpToDate = false;
    bool Cancelled = false;
    int ExitCode = -1;
    std::filesystem::path LogPath;
    std::filesystem::path GenerationDirectory;
    std::vector<std::filesystem::path> PublishedReceipts;
    std::string Error;
};
bool IsPluginBuildInProgress();
PluginBuildResult BuildPlugins(const PluginBuildRequest& request, const std::atomic_bool& cancel);
}
