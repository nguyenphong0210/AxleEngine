#pragma once
#include <Axle/Types.hpp>
#include <Axle/Package.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

namespace Axle {

    struct CookSettings {
        std::filesystem::path ProjectPath;                 // Path to active .axleproj
        std::filesystem::path AssetDirectory;              // Path to assets root (if empty, auto-detected via FileSystem)
        std::filesystem::path OutputDirectory = "Export";  // Target export directory
        std::string PackageName = "Data.axlepak";          // Name of monolithic package archive
        bool BakeMeshesToBinary = true;                    // Pre-compile text .axle to fast binary .axlebin
        bool IncludeShaders = true;                        // Bundle Engine shaders (Checkerboard, Skybox, etc.)
        bool CopyExecutables = true;                       // Bundle AxleRuntime.exe & AxleScripts.dll
    };

    struct CookResult {
        bool Success = false;
        std::filesystem::path PackagePath;
        size_t TotalAssetsCooked = 0;
        uint64_t PackageSizeBytes = 0;
        std::string ErrorMessage;
        bool RuntimeCopied = false;
        bool ScriptsCopied = false;
    };

    class AssetCooker {
    public:
        using ProgressCallback = std::function<void(const std::string& currentAsset, float progress)>;

        static CookResult CookProject(const CookSettings& settings, ProgressCallback progressCb = nullptr);
    };

}
