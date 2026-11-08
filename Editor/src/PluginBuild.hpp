#pragma once

#include <Axle/Plugin.hpp>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace Axle {

struct PluginBuildItem {
    std::string PluginId;
    std::string ModuleName;
    Plugin::ModuleKind Kind = Plugin::ModuleKind::Runtime;
    std::filesystem::path SourceRoot;
    std::string Fingerprint;
    std::string SdkFingerprint;
};

struct PluginBuildRequest {
    std::filesystem::path CMake;
    std::filesystem::path ProjectDirectory;
    std::filesystem::path BuildDirectory;
    std::filesystem::path BinaryDirectory;
    std::string Configuration = "Debug";
    std::string TargetName;
    std::vector<PluginBuildItem> Items;
    Plugin::BuildPlan Plan;
};

struct PluginBuildResult {
    bool Success = false;
    bool UpToDate = false;
    int ExitCode = -1;
    std::filesystem::path LogPath;
    std::vector<std::filesystem::path> PublishedReceipts;
    std::string Error;
};

bool IsPluginBuildInProgress();

// Run on a worker or synchronously.
// Rejects simultaneous calls with an atomic build lock.
// Checks if all items are up-to-date with existing matching receipts and DLLs (prebuilt no-op).
// If build is needed, launches CMake process with job object cancellation and log capture.
// Atomically publishes build receipts (schema=1) only on build success.
// Failed/cancelled builds never replace or corrupt existing receipts.
PluginBuildResult BuildPlugins(const PluginBuildRequest& request, const std::atomic_bool& cancel);

} // namespace Axle
