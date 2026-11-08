#include <Axle/Path.hpp>
#include <Axle/AssetCooker.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/Log.hpp>

#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axle {

    static std::filesystem::path FindExecutable(const std::string& name) {
        static const std::vector<std::filesystem::path> searchPaths = {
            "build/Runtime/Release",
            "build/Runtime/Debug",
            "build/Runtime",
            "build/Editor/Release",
            "build/Editor/Debug",
            "build/Editor",
            "Runtime/Release",
            "Runtime/Debug",
            "Editor/Release",
            "Editor/Debug",
            "bin/Release",
            "bin/Debug",
            "bin",
            "."
        };

        // 1. Check relative to current working directory
        for (const auto& dir : searchPaths) {
            auto candidate = dir / name;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !std::filesystem::is_directory(candidate, ec)) {
                return std::filesystem::canonical(candidate, ec);
            }
        }

        // 2. Check relative to running executable directory (e.g. build/Editor/Release)
#ifdef _WIN32
        wchar_t exePathBuf[MAX_PATH];
        if (GetModuleFileNameW(NULL, exePathBuf, MAX_PATH)) {
            std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
            const std::vector<std::filesystem::path> relToExe = {
                exeDir / name,
                exeDir / ".." / name,
                exeDir / ".." / "Runtime" / "Release" / name,
                exeDir / ".." / "Runtime" / "Debug" / name,
                exeDir / ".." / "Runtime" / name,
                exeDir / ".." / ".." / "Runtime" / "Release" / name,
                exeDir / ".." / ".." / "Runtime" / "Debug" / name,
                exeDir / ".." / ".." / "Runtime" / name,
                exeDir / ".." / ".." / "build" / "Runtime" / "Release" / name,
                exeDir / ".." / ".." / "build" / "Runtime" / "Debug" / name,
                exeDir / ".." / ".." / "build" / "Runtime" / name,
                exeDir / ".." / ".." / "bin" / "Release" / name,
                exeDir / ".." / ".." / "bin" / "Debug" / name,
                exeDir / ".." / ".." / "bin" / name
            };
            for (const auto& p : relToExe) {
                std::error_code ec;
                if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                    return std::filesystem::canonical(p, ec);
                }
            }
        }
#endif

        // 3. Check relative to project or asset directory root
        auto assetDir = FileSystem::GetAssetDirectory();
        if (!assetDir.empty()) {
            std::vector<std::filesystem::path> roots = {
                assetDir.parent_path(),
                assetDir.parent_path().parent_path()
            };
            for (const auto& root : roots) {
                for (const auto& dir : searchPaths) {
                    auto candidate = root / dir / name;
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec) && !std::filesystem::is_directory(candidate, ec)) {
                        return std::filesystem::canonical(candidate, ec);
                    }
                }
            }
        }

        return {};
    }

    CookResult AssetCooker::CookProject(const CookSettings& settings, ProgressCallback progressCb) {
        CookResult result;
        result.Success = false;
        const auto effectiveAssets = settings.AssetDirectory.empty() ? FileSystem::GetAssetDirectory() : settings.AssetDirectory;

        try {
            if (settings.Preflight && !settings.Preflight(result.ErrorMessage)) return result;
            if (!std::filesystem::is_directory(effectiveAssets)) { result.ErrorMessage = "Asset directory is unavailable: " + PathToUtf8(effectiveAssets); return result; }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(effectiveAssets)) {
                if (!entry.is_regular_file()) continue;
                auto extension = PathToUtf8(entry.path().extension());
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension != ".axlescene" && extension != ".axle" && extension != ".axleasset") continue;
                const auto text = FileSystem::ReadTextFile(entry.path());
                if (text.find("AxlePlugin_") == std::string::npos) continue;
                if (!settings.ValidatePluginDocument) {
                    result.ErrorMessage = "Plugin data requires a plugin-aware document preflight: " + PathToUtf8(entry.path()); return result;
                }
                if (!settings.ValidatePluginDocument(entry.path(), result.ErrorMessage)) {
                    result.ErrorMessage = PathToUtf8(entry.path()) + ": " + result.ErrorMessage; return result;
                }
            }
        } catch (const std::exception& error) { result.ErrorMessage = error.what(); return result; }

        const auto resolveExecutable = [&](const char* name) {
            if (!settings.ExecutableDirectory.empty()) {
                auto path = settings.ExecutableDirectory / name;
                std::error_code error;
                return std::filesystem::is_regular_file(path, error) ? path : std::filesystem::path{};
            }
            return FindExecutable(name);
        };
        std::filesystem::path runtimeExe, scriptsDll;
        if (settings.CopyExecutables) {
            runtimeExe = resolveExecutable("AxleRuntime.exe");
            scriptsDll = resolveExecutable("AxleScripts.dll");
            if (runtimeExe.empty() || scriptsDll.empty()) {
                result.ErrorMessage = "Standalone player or scripts module is missing. Build Runtime before exporting executables.";
                return result;
            }
        }

        std::filesystem::path outputDir = settings.OutputDirectory;
        if (outputDir.empty()) {
            outputDir = "Export";
        }

        // If outputDir is relative, anchor it to project directory or project root instead of build binary folder
        if (outputDir.is_relative()) {
            if (!settings.ProjectPath.empty()) {
                outputDir = settings.ProjectPath.parent_path() / outputDir;
            } else {
                auto assetDir = FileSystem::GetAssetDirectory();
                if (!assetDir.empty()) {
                    auto candidateRoot = assetDir.parent_path().parent_path();
                    if (std::filesystem::exists(candidateRoot / "CMakeLists.txt")) {
                        outputDir = candidateRoot / outputDir;
                    } else if (std::filesystem::exists(assetDir.parent_path() / "CMakeLists.txt")) {
                        outputDir = assetDir.parent_path() / outputDir;
                    }
                }
            }
        }

        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec) {
            result.ErrorMessage = "Failed to create output directory: " + ec.message();
            AXLE_ERROR("AssetCooker: {0}", result.ErrorMessage);
            return result;
        }

        std::filesystem::path pkgPath = outputDir / settings.PackageName;
        result.PackagePath = pkgPath;

        PackageWriter writer;
        if (!writer.Begin(pkgPath)) {
            result.ErrorMessage = "Failed to begin package at " + PathToUtf8(pkgPath);
            AXLE_ERROR("AssetCooker: {0}", result.ErrorMessage);
            return result;
        }

        AXLE_INFO("AssetCooker: Starting project cooking pipeline -> '{0}'", PathToUtf8(pkgPath));

        // 1. Project Configuration File
        std::filesystem::path projFile = settings.ProjectPath;
        if (projFile.empty() || !std::filesystem::exists(projFile)) {
            static const std::vector<std::filesystem::path> candidates = {
                "Sandbox.axleproj",
                "Project.axleproj",
                FileSystem::ResolveAssetPath("Sandbox.axleproj")
            };
            for (const auto& c : candidates) {
                if (std::filesystem::exists(c)) {
                    projFile = c;
                    break;
                }
            }
        }

        if (!projFile.empty() && std::filesystem::exists(projFile)) {
            writer.AddFile("Project.axleproj", projFile);
            if (PathToUtf8(projFile.filename()) != "Project.axleproj") {
                writer.AddFile(PathToUtf8(projFile.filename()), projFile);
            }
            result.TotalAssetsCooked++;
            AXLE_INFO("AssetCooker: Bundled project configuration '{0}'", PathToUtf8(projFile.filename()));
        }

        // 2. Asset Directory Traversal
        const auto& assetDir = effectiveAssets;

        std::vector<std::filesystem::path> assetFiles;
        if (std::filesystem::exists(assetDir)) {
            AXLE_INFO("AssetCooker: Scanning asset directory '{0}'", PathToUtf8(assetDir));
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
                if (entry.is_regular_file()) {
                    assetFiles.push_back(entry.path());
                }
            }

            size_t total = assetFiles.size();
            for (size_t i = 0; i < total; i++) {
                const auto& path = assetFiles[i];
                std::string ext = PathToUtf8(path.extension());

                // Skip non-runtime companion or temporary files
                if (ext == ".meta" || ext == ".tmp" || ext == ".bak" || ext == ".log") {
                    continue;
                }

                auto relPath = std::filesystem::relative(path, assetDir);
                std::string virtPath = PathToUtf8(relPath);

                UUID uuid = AssetManager::GetAssetUUID(path);
                uint16_t assetType = static_cast<uint16_t>(GetAssetTypeFromPath(path));

                if ((ext == ".axle" || ext == ".axlemesh") && assetType != static_cast<std::uint16_t>(AssetType::Assembly) && settings.BakeMeshesToBinary) {
                    // Pre-bake or ensure binary cache exists
                    auto cachePath = AssetSerializer::GetCachePathFor(path);
                    if (!std::filesystem::exists(cachePath)) {
                        AssetSerializer::LoadMesh(path);
                    }

                    if (std::filesystem::exists(cachePath)) {
                        auto cacheRel = std::filesystem::relative(cachePath, assetDir);
                        writer.AddFile(PathToUtf8(cacheRel), cachePath, uuid, assetType);
                        result.TotalAssetsCooked++;
                    }
                }

                writer.AddFile(virtPath, path, uuid, assetType);
                result.TotalAssetsCooked++;

                if (progressCb) {
                    float p = static_cast<float>(i + 1) / static_cast<float>(total);
                    progressCb(virtPath, p);
                }
            }
        }

        // 3. Engine Resources & Shaders
        if (settings.IncludeShaders) {
            static const std::vector<std::filesystem::path> shaderSearchRoots = {
                "Engine/Resources/Shaders",
                "Engine/Resources/shaders",
                "../../Engine/Resources/Shaders",
                "../../../Engine/Resources/Shaders",
                "Resources/Shaders",
                "Resources/shaders"
            };

            std::filesystem::path shaderDir;
            for (const auto& root : shaderSearchRoots) {
                if (std::filesystem::exists(root)) {
                    shaderDir = root;
                    break;
                }
            }

            if (!shaderDir.empty()) {
                AXLE_INFO("AssetCooker: Packaging engine shaders from '{0}'", PathToUtf8(shaderDir));
                for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
                    if (entry.is_regular_file()) {
                        std::string virtPath = "shaders/" + PathToUtf8(entry.path().filename());
                        writer.AddFile(virtPath, entry.path());
                        result.TotalAssetsCooked++;
                    }
                }
            }
        }

        // 4. Finalize Monolithic Package Archive
        if (!writer.Finish()) {
            result.ErrorMessage = "Failed writing final package archive: " + PathToUtf8(pkgPath);
            AXLE_ERROR("AssetCooker: {0}", result.ErrorMessage);
            return result;
        }

        result.PackageSizeBytes = std::filesystem::file_size(pkgPath, ec);
        result.Success = true;

        AXLE_INFO("AssetCooker: Package archive '{0}' created ({1} assets, {2} KB)",
            settings.PackageName, result.TotalAssetsCooked, result.PackageSizeBytes / 1024);

        // 5. Copy Standalone Executables & DLLs
        if (settings.CopyExecutables) {
            if (!runtimeExe.empty()) {
                auto targetExe = outputDir / "AxleRuntime.exe";
                std::filesystem::copy_file(runtimeExe, targetExe, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    result.RuntimeCopied = true;
                    AXLE_INFO("AssetCooker: Bundled standalone player '{0}' (from '{1}')", PathToUtf8(targetExe), PathToUtf8(runtimeExe));
                } else {
                    result.Success = false;
                    result.ErrorMessage = "Failed to copy standalone player: " + ec.message();
                    return result;
                }
            } else {
                AXLE_WARN("AssetCooker: AxleRuntime.exe not found in search paths. Standalone player was not copied.");
            }

            if (!scriptsDll.empty()) {
                auto targetDll = outputDir / "AxleScripts.dll";
                std::filesystem::copy_file(scriptsDll, targetDll, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    result.ScriptsCopied = true;
                    AXLE_INFO("AssetCooker: Bundled scripts module '{0}' (from '{1}')", PathToUtf8(targetDll), PathToUtf8(scriptsDll));
                } else {
                    result.Success = false;
                    result.ErrorMessage = "Failed to copy scripts module: " + ec.message();
                    return result;
                }
            } else {
                AXLE_WARN("AssetCooker: AxleScripts.dll not found in search paths.");
            }

            // Copy audio assets directory loose if present so miniaudio disk streaming operates without hitch
            auto audioSrcDir = assetDir / "audio";
            if (std::filesystem::exists(audioSrcDir, ec)) {
                auto audioDstDir = outputDir / "assets" / "audio";
                std::filesystem::create_directories(audioDstDir, ec);
                std::filesystem::copy(audioSrcDir, audioDstDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    AXLE_INFO("AssetCooker: Bundled companion audio assets to '{0}'", PathToUtf8(audioDstDir));
                }
            }

            // Also mirror any loose audio files from other subfolders
            for (const auto& path : assetFiles) {
                std::string ext = PathToUtf8(path.extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
                    auto rel = std::filesystem::relative(path, assetDir);
                    auto targetAudioPath = outputDir / "assets" / rel;
                    std::filesystem::create_directories(targetAudioPath.parent_path(), ec);
                    std::filesystem::copy_file(path, targetAudioPath, std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        }

        return result;
    }

}
