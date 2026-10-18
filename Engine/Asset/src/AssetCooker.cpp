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
            "build/Runtime",
            "build/Editor/Release",
            "build/Editor",
            "Runtime/Release",
            "Editor/Release",
            "bin/Release",
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
                exeDir / ".." / "Runtime" / name,
                exeDir / ".." / ".." / "Runtime" / "Release" / name,
                exeDir / ".." / ".." / "Runtime" / name,
                exeDir / ".." / ".." / "build" / "Runtime" / "Release" / name,
                exeDir / ".." / ".." / "build" / "Runtime" / name,
                exeDir / ".." / ".." / "bin" / "Release" / name,
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
            result.ErrorMessage = "Failed to begin package at " + pkgPath.string();
            AXLE_ERROR("AssetCooker: {0}", result.ErrorMessage);
            return result;
        }

        AXLE_INFO("AssetCooker: Starting project cooking pipeline -> '{0}'", pkgPath.string());

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
            if (projFile.filename().string() != "Project.axleproj") {
                writer.AddFile(projFile.filename().string(), projFile);
            }
            result.TotalAssetsCooked++;
            AXLE_INFO("AssetCooker: Bundled project configuration '{0}'", projFile.filename().string());
        }

        // 2. Asset Directory Traversal
        std::filesystem::path assetDir = settings.AssetDirectory;
        if (assetDir.empty() || !std::filesystem::exists(assetDir)) {
            assetDir = FileSystem::GetAssetDirectory();
        }

        if (std::filesystem::exists(assetDir)) {
            AXLE_INFO("AssetCooker: Scanning asset directory '{0}'", assetDir.string());

            std::vector<std::filesystem::path> assetFiles;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
                if (entry.is_regular_file()) {
                    assetFiles.push_back(entry.path());
                }
            }

            size_t total = assetFiles.size();
            for (size_t i = 0; i < total; i++) {
                const auto& path = assetFiles[i];
                std::string ext = path.extension().string();

                // Skip non-runtime companion or temporary files
                if (ext == ".meta" || ext == ".tmp" || ext == ".bak" || ext == ".log") {
                    continue;
                }

                auto relPath = std::filesystem::relative(path, assetDir);
                std::string virtPath = relPath.generic_string();

                UUID uuid = AssetManager::GetAssetUUID(path);
                uint16_t assetType = static_cast<uint16_t>(GetAssetTypeFromPath(path));

                if ((ext == ".axle" || ext == ".axlemesh") && settings.BakeMeshesToBinary) {
                    // Pre-bake or ensure binary cache exists
                    auto cachePath = AssetSerializer::GetCachePathFor(path);
                    if (!std::filesystem::exists(cachePath)) {
                        AssetSerializer::LoadMesh(path);
                    }

                    if (std::filesystem::exists(cachePath)) {
                        auto cacheRel = std::filesystem::relative(cachePath, assetDir);
                        writer.AddFile(cacheRel.generic_string(), cachePath, uuid, assetType);
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
                AXLE_INFO("AssetCooker: Packaging engine shaders from '{0}'", shaderDir.string());
                for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
                    if (entry.is_regular_file()) {
                        std::string virtPath = "shaders/" + entry.path().filename().string();
                        writer.AddFile(virtPath, entry.path());
                        result.TotalAssetsCooked++;
                    }
                }
            }
        }

        // 4. Finalize Monolithic Package Archive
        if (!writer.Finish()) {
            result.ErrorMessage = "Failed writing final package archive: " + pkgPath.string();
            AXLE_ERROR("AssetCooker: {0}", result.ErrorMessage);
            return result;
        }

        result.PackageSizeBytes = std::filesystem::file_size(pkgPath, ec);
        result.Success = true;

        AXLE_INFO("AssetCooker: Package archive '{0}' created ({1} assets, {2} KB)",
            settings.PackageName, result.TotalAssetsCooked, result.PackageSizeBytes / 1024);

        // 5. Copy Standalone Executables & DLLs
        if (settings.CopyExecutables) {
            auto runtimeExe = FindExecutable("AxleRuntime.exe");
            if (!runtimeExe.empty()) {
                auto targetExe = outputDir / "AxleRuntime.exe";
                std::filesystem::copy_file(runtimeExe, targetExe, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    result.RuntimeCopied = true;
                    AXLE_INFO("AssetCooker: Bundled standalone player '{0}' (from '{1}')", targetExe.string(), runtimeExe.string());
                } else {
                    AXLE_ERROR("AssetCooker: Failed to copy AxleRuntime.exe to '{0}': {1}", targetExe.string(), ec.message());
                }
            } else {
                AXLE_WARN("AssetCooker: AxleRuntime.exe not found in search paths. Standalone player was not copied.");
            }

            auto scriptsDll = FindExecutable("AxleScripts.dll");
            if (!scriptsDll.empty()) {
                auto targetDll = outputDir / "AxleScripts.dll";
                std::filesystem::copy_file(scriptsDll, targetDll, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    result.ScriptsCopied = true;
                    AXLE_INFO("AssetCooker: Bundled scripts module '{0}' (from '{1}')", targetDll.string(), scriptsDll.string());
                } else {
                    AXLE_ERROR("AssetCooker: Failed to copy AxleScripts.dll to '{0}': {1}", targetDll.string(), ec.message());
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
                    AXLE_INFO("AssetCooker: Bundled companion audio assets to '{0}'", audioDstDir.string());
                }
            }
        }

        return result;
    }

}
