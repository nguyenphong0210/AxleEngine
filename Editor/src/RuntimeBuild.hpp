#pragma once
#include <atomic>
#include <filesystem>
#include <string>

namespace Axle {
    struct RuntimeBuildRequest {
        std::filesystem::path CMake;
        std::filesystem::path BuildDirectory;
        std::filesystem::path ExecutableDirectory;
        std::string Configuration;
    };

    struct RuntimeBuildResult {
        bool Success = false;
        int ExitCode = -1;
        std::filesystem::path ExecutableDirectory;
        std::filesystem::path LogPath;
        // Written only after both player artifacts pass validation.  Consumers
        // may use this as the last-known-good marker after a failed build.
        std::filesystem::path ReceiptPath;
        std::string Error;
    };

    RuntimeBuildRequest GetRuntimeBuildRequest();
    // Run on a worker. Cancellation terminates the owned process tree on Windows.
    RuntimeBuildResult BuildRuntime(const RuntimeBuildRequest& request, const std::atomic_bool& cancel);
}
