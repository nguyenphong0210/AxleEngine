#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <memory>
#include <functional>

namespace Axle {
    struct RuntimeProductSnapshot;
    struct CookSettings;
    struct RuntimeBuildRequest {
        std::filesystem::path CMake;
        std::filesystem::path BuildDirectory;
        std::filesystem::path ExecutableDirectory;
        std::string Configuration;
        std::filesystem::path EngineSourceDirectory;
        std::filesystem::path ProjectPath;
        std::string Generator;
        std::string GeneratorToolset;
        std::filesystem::path Compiler;
        std::string CompilerVersion, WindowsSDK;
        std::filesystem::path GeneratorInstance;
        std::shared_ptr<const RuntimeProductSnapshot> Snapshot;
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
        std::filesystem::path GenerationDirectory;
        std::filesystem::path FrozenSourceDirectory, ProductBuildDirectory;
        std::shared_ptr<const RuntimeBuildRequest> Request;
        std::string GeneratedGraph, GeneratedRegistration;
        bool CookOnly = false;
    };

    RuntimeBuildRequest GetRuntimeBuildRequest();
    // Captures authored project, selected code, engine and assets before worker dispatch.
    bool FreezeRuntimeBuildRequest(RuntimeBuildRequest& request, std::string& error);
    bool ValidateRuntimeBuildRequest(const RuntimeBuildRequest& request, std::string& error);
    bool ValidateRuntimeBuildDocuments(const RuntimeBuildRequest& request,
        const std::function<bool(const std::filesystem::path&, std::string&)>& validator, std::string& error);
    bool BindRuntimeExport(const RuntimeBuildRequest& request, const RuntimeBuildResult& result,
                           CookSettings& settings, std::string& error);
    bool BindRuntimeCook(const RuntimeBuildRequest& request, CookSettings& settings, std::string& error);
    // Run on a worker. Cancellation terminates the owned process tree on Windows.
    RuntimeBuildResult BuildRuntime(const RuntimeBuildRequest& request, const std::atomic_bool& cancel);
}
