#include <Axle/Path.hpp>
#include <Axle/AssetCooker.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/Log.hpp>
#include <Axle/UUID.hpp>

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

    static CookResult CookInto(const CookSettings& settings, AssetCooker::ProgressCallback progressCb) {
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
            if (settings.RequireScriptsDll) scriptsDll = resolveExecutable("AxleScripts.dll");
            if (runtimeExe.empty() || (settings.RequireScriptsDll && scriptsDll.empty())) {
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
            if (!writer.AddFile("Project.axleproj", projFile)) throw std::runtime_error("Failed to append required package input");
            if (PathToUtf8(projFile.filename()) != "Project.axleproj") {
                if (!writer.AddFile(PathToUtf8(projFile.filename()), projFile)) throw std::runtime_error("Failed to append required package input");
            }
            result.TotalAssetsCooked++;
            AXLE_INFO("AssetCooker: Bundled project configuration '{0}'", PathToUtf8(projFile.filename()));
        }

        // 2. Asset Directory Traversal
        const auto& assetDir = effectiveAssets;

        std::vector<std::filesystem::path> assetFiles;
        if (std::filesystem::exists(assetDir)) {
            AXLE_INFO("AssetCooker: Scanning asset directory '{0}'", PathToUtf8(assetDir));
            for (std::filesystem::recursive_directory_iterator it(assetDir), end; it != end; ++it) {
                if (it->is_symlink()) throw std::runtime_error("Cook input contains a filesystem link");
                if (it->is_directory() && it->path().filename() == ".cache") { it.disable_recursion_pending(); continue; }
                const auto& entry = *it;
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

                std::uint64_t embeddedId = 0;
                FileSystem::ReadFileIdentity(path, embeddedId);
                UUID uuid = embeddedId;
                uint16_t assetType = static_cast<uint16_t>(GetAssetTypeFromPath(path));

                if ((ext == ".axle" || ext == ".axlemesh") && assetType != static_cast<std::uint16_t>(AssetType::Assembly) && settings.BakeMeshesToBinary) {
                    // Pre-bake or ensure binary cache exists
                    const auto cacheRel = relPath.parent_path() / ".cache" / (relPath.stem().wstring() + L".axlebin");
                    const auto cachePath = outputDir / ".cook-cache" / relPath;
                    std::filesystem::create_directories(cachePath.parent_path());
                    AxleMeshSource mesh;
                    if (!AssetSerializer::LoadFromAxleText(path, mesh) || !AssetSerializer::SaveToAxleBinary(cachePath, mesh)) throw std::runtime_error("Required mesh cannot be baked: " + PathToUtf8(path));
                    if (!writer.AddFile(PathToUtf8(cacheRel), cachePath, uuid, assetType)) throw std::runtime_error("Failed to append baked mesh");
                    result.TotalAssetsCooked++;
                }

                if (!writer.AddFile(virtPath, path, uuid, assetType)) throw std::runtime_error("Failed to append required package input");
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

            std::filesystem::path shaderDir = settings.ShaderDirectory;
            if (!shaderDir.empty() && !std::filesystem::is_directory(shaderDir)) throw std::runtime_error("Product shader directory is missing");
            for (const auto& root : shaderSearchRoots) {
                if (!shaderDir.empty()) break;
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
                        if (!writer.AddFile(virtPath, entry.path())) throw std::runtime_error("Failed to append required package input");
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
        std::filesystem::remove_all(outputDir / ".cook-cache");

        result.PackageSizeBytes = std::filesystem::file_size(pkgPath, ec);
        result.Success = true;

        AXLE_INFO("AssetCooker: Package archive '{0}' created ({1} assets, {2} KB)",
            settings.PackageName, result.TotalAssetsCooked, result.PackageSizeBytes / 1024);

        // 5. Copy Standalone Executables & DLLs
        if (settings.CopyExecutables) {
            for (const auto* companion : {"dxcompiler.dll", "dxil.dll", "Resources", "AxleRuntime.build.receipt"}) {
                const auto source = settings.ExecutableDirectory / companion;
                if (settings.ExecutableDirectory.empty() || !std::filesystem::exists(source)) continue;
                std::filesystem::copy(source, outputDir / companion,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
            }
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
            }

            // Copy audio assets directory loose if present so miniaudio disk streaming operates without hitch
            auto audioSrcDir = assetDir / "audio";
            if (std::filesystem::exists(audioSrcDir, ec)) {
                auto audioDstDir = outputDir / "assets" / "audio";
                std::filesystem::create_directories(audioDstDir, ec);
                std::filesystem::copy(audioSrcDir, audioDstDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) throw std::runtime_error("Cannot copy required loose audio: " + ec.message());
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
                    if (ec) throw std::runtime_error("Cannot copy required audio file: " + ec.message());
                }
            }
        }

        return result;
    }

    CookResult AssetCooker::CookProject(const CookSettings& settings, ProgressCallback progressCb) {
        CookResult result;
        std::filesystem::path stage, backup, output;
        try {
            auto requested = settings.OutputDirectory.empty() ? std::filesystem::path("Export") : settings.OutputDirectory;
            if (requested.is_relative()) requested = (settings.ProjectPath.empty() ? std::filesystem::current_path() : settings.ProjectPath.parent_path()) / requested;
            output = std::filesystem::weakly_canonical(requested);
            const auto overlaps = [](const auto& a, const auto& b) {
                if (a.empty() || b.empty()) return false;
                const auto left = std::filesystem::weakly_canonical(a), right = std::filesystem::weakly_canonical(b);
                const auto within = [](const auto& child, const auto& parent) { const auto rel = child.lexically_relative(parent); return !rel.empty() && *rel.begin() != ".."; };
                return within(left, right) || within(right, left);
            };
            const auto assets = settings.AssetDirectory.empty() ? FileSystem::GetAssetDirectory() : settings.AssetDirectory;
            if (output == output.root_path() || output.filename().empty() || overlaps(output, assets) ||
                (!settings.ExecutableDirectory.empty() && overlaps(output, settings.ExecutableDirectory)) ||
                (!settings.ProjectPath.empty() && overlaps(output, settings.ProjectPath))) {
                result.ErrorMessage = "Export output overlaps a protected product input"; return result;
            }
            if (settings.PackageName.empty() || std::filesystem::path(settings.PackageName).filename() != settings.PackageName) {
                result.ErrorMessage = "Package name must be a filename"; return result;
            }
            if (std::filesystem::exists(output) && (!std::filesystem::is_directory(output) ||
                (!std::filesystem::is_empty(output) && !std::filesystem::is_regular_file(output / "AxleExport.receipt")))) {
                result.ErrorMessage = "Export output contains files not owned by an earlier export"; return result;
            }
            std::filesystem::create_directories(output.parent_path());
#ifdef _WIN32
            struct OutputLock {
                HANDLE File = INVALID_HANDLE_VALUE;
                ~OutputLock() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); }
            } lock;
            const auto lockPath = output.parent_path() / (output.filename().wstring() + L".export.lock");
            lock.File = CreateFileW(lockPath.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (lock.File == INVALID_HANDLE_VALUE) { result.ErrorMessage = "Another export owns this output directory"; return result; }
#endif
            const auto suffix = std::to_string(static_cast<std::uint64_t>(UUID()));
            stage = output.parent_path() / (output.filename().wstring() + L".staging-" + std::filesystem::path(suffix).wstring());
            backup = output.parent_path() / (output.filename().wstring() + L".previous-" + std::filesystem::path(suffix).wstring());
            auto staged = settings; staged.OutputDirectory = stage;
            result = CookInto(staged, std::move(progressCb));
            if (!result.Success) { std::filesystem::remove_all(stage); return result; }
            if (settings.Preflight && !settings.Preflight(result.ErrorMessage)) { result.Success = false; std::filesystem::remove_all(stage); return result; }
            std::ofstream marker(stage / "AxleExport.receipt"); marker << "schema=1\npackage=" << settings.PackageName << '\n'; marker.close();
            if (!marker) throw std::runtime_error("Cannot finish export receipt");
            if (std::filesystem::exists(output)) std::filesystem::rename(output, backup);
            try { std::filesystem::rename(stage, output); }
            catch (...) { if (std::filesystem::exists(backup)) std::filesystem::rename(backup, output); throw; }
            std::error_code cleanup; std::filesystem::remove_all(backup, cleanup);
            result.PackagePath = output / settings.PackageName;
        } catch (const std::exception& exception) {
            result.Success = false; result.ErrorMessage = exception.what();
            std::error_code cleanup; if (!stage.empty()) std::filesystem::remove_all(stage, cleanup);
        }
        return result;
    }

}
