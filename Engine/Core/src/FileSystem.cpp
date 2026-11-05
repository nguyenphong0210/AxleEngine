#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axle {

    static std::filesystem::path s_AssetDirectory;
    static std::unique_ptr<Package> s_MountedPackage = nullptr;

    bool FileSystem::MountPackage(const std::filesystem::path& packagePath) {
        auto pkg = std::make_unique<Package>();
        if (pkg->Open(packagePath)) {
            s_MountedPackage = std::move(pkg);
            return true;
        }
        return false;
    }

    void FileSystem::UnmountPackage() {
        if (s_MountedPackage) {
            s_MountedPackage->Close();
            s_MountedPackage.reset();
        }
    }

    bool FileSystem::HasMountedPackage() {
        return s_MountedPackage != nullptr && s_MountedPackage->IsOpen();
    }

    const Package* FileSystem::GetMountedPackage() {
        return s_MountedPackage.get();
    }

    void FileSystem::SetAssetDirectory(const std::filesystem::path& path) {
        s_AssetDirectory = path;
    }

    const std::filesystem::path& FileSystem::GetAssetDirectory() {
        // An explicit project root remains authoritative even before its directory exists.
        // Never discover another project's sample assets by walking the engine checkout.
        if (s_AssetDirectory.empty()) s_AssetDirectory = std::filesystem::absolute("assets");
        return s_AssetDirectory;
    }

    std::filesystem::path FileSystem::ResolveAssetPath(const std::filesystem::path& relativePath) {
        // 0. If package is mounted and contains this virtual path, return it directly
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = Package::NormalizePath(relativePath.generic_string());
            if (s_MountedPackage->HasEntry(norm)) {
                return relativePath;
            }
        }

        // If already an absolute path and exists, return directly
        if (relativePath.is_absolute() && std::filesystem::exists(relativePath)) {
            return relativePath;
        }

        // 1. Try against active asset directory
        auto resolved = GetAssetDirectory() / relativePath;
        if (std::filesystem::exists(resolved)) {
            return resolved;
        }

        std::string relStr = relativePath.generic_string();
        if (relStr.rfind("assets/", 0) == 0) {
            auto subCandidate = GetAssetDirectory() / relStr.substr(7);
            if (std::filesystem::exists(subCandidate)) {
                return subCandidate;
            }
        }

        // 2. Try against Engine Resources (Engine/Resources or Resources in binary dir)
        static const std::vector<std::filesystem::path> s_ResourceRoots = {
            "Engine/Resources",
            "../../Engine/Resources",
            "../../../Engine/Resources",
            "../Engine/Resources",
            "Resources",
            "../../Resources"
        };

        for (const auto& root : s_ResourceRoots) {
            auto candidate = root / relativePath;
            if (std::filesystem::exists(candidate)) {
                return std::filesystem::canonical(candidate);
            }

            // Also check Shaders/ subfolder if relativePath begins with shaders/
            std::string relStr = relativePath.generic_string();
            if (relStr.rfind("shaders/", 0) == 0) {
                auto candidateShaders = root / "Shaders" / relStr.substr(8);
                if (std::filesystem::exists(candidateShaders)) {
                    return std::filesystem::canonical(candidateShaders);
                }
            }
        }

        // Fallback checks
        if (std::filesystem::exists(relativePath)) {
            return relativePath;
        }

        return resolved;
    }

    std::string FileSystem::ReadTextFile(const std::filesystem::path& filepath) {
        // 1. Check mounted VFS package first
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = Package::NormalizePath(filepath.generic_string());
            if (s_MountedPackage->HasEntry(norm)) {
                return s_MountedPackage->ReadEntryAsText(norm);
            }
        }

        // 2. Physical disk read
        std::ifstream file(filepath);
        if (!file.is_open()) {
            auto resolved = ResolveAssetPath(filepath);
            if (resolved != filepath && std::filesystem::exists(resolved)) {
                std::ifstream resFile(resolved);
                if (resFile.is_open()) {
                    std::stringstream buffer;
                    buffer << resFile.rdbuf();
                    return buffer.str();
                }
            }
            AXLE_CORE_WARN("FileSystem: Unable to open file for reading: {0}", filepath.string());
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::vector<uint8_t> FileSystem::ReadBinaryFile(const std::filesystem::path& filepath) {
        // 1. Check mounted VFS package first
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = Package::NormalizePath(filepath.generic_string());
            if (s_MountedPackage->HasEntry(norm)) {
                return s_MountedPackage->ReadEntry(norm);
            }
        }

        // 2. Physical disk read
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            auto resolved = ResolveAssetPath(filepath);
            if (resolved != filepath && std::filesystem::exists(resolved)) {
                std::ifstream resFile(resolved, std::ios::binary | std::ios::ate);
                if (resFile.is_open()) {
                    std::streamsize size = resFile.tellg();
                    resFile.seekg(0, std::ios::beg);
                    std::vector<uint8_t> buffer(static_cast<size_t>(size));
                    if (resFile.read(reinterpret_cast<char*>(buffer.data()), size)) {
                        return buffer;
                    }
                }
            }
            AXLE_CORE_WARN("FileSystem: Unable to open binary file for reading: {0}", filepath.string());
            return {};
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            AXLE_CORE_WARN("FileSystem: Failed reading binary content from: {0}", filepath.string());
            return {};
        }

        return buffer;
    }

    bool FileSystem::WriteTextFile(const std::filesystem::path& filepath, const std::string& content) {
        return WriteTextFileAtomically(filepath, content);
    }

    bool FileSystem::WriteTextFileAtomically(const std::filesystem::path& filepath, const std::string& text, std::string* outError) {
        return WriteBinaryFileAtomically(filepath, text.data(), text.size(), outError);
    }

    bool FileSystem::WriteBinaryFileAtomically(const std::filesystem::path& filepath, const void* data, size_t size, std::string* outError) {
        if (filepath.empty()) {
            if (outError) *outError = "Filepath cannot be empty";
            return false;
        }

        std::filesystem::path target = filepath;
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (outError) *outError = "Cannot create directory: " + ec.message();
                return false;
            }
        }

        static std::atomic<uint64_t> s_Counter{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path tempPath = target.string() + ".tmp." + std::to_string(now) + "." + std::to_string(s_Counter++);

        {
            std::ofstream out(tempPath, std::ios::binary);
            if (!out.is_open()) {
                if (outError) *outError = "Failed to open temporary file for write: " + tempPath.string();
                return false;
            }
            if (size > 0 && data != nullptr) {
                out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            }
            out.flush();
            if (!out.good()) {
                out.close();
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
                if (outError) *outError = "Failed to write complete data to temporary file";
                return false;
            }
            out.close();
        }

#ifdef _WIN32
        if (!MoveFileExW(tempPath.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            if (outError) *outError = "Failed to atomically replace target file: Windows error " + std::to_string(GetLastError());
            return false;
        }
#else
        std::error_code ec;
        std::filesystem::rename(tempPath, target, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            if (outError) *outError = "Failed to atomically replace target file: " + ec.message();
            return false;
        }
#endif

        return true;
    }

    bool FileSystem::Exists(const std::filesystem::path& filepath) {
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = Package::NormalizePath(filepath.generic_string());
            if (s_MountedPackage->HasEntry(norm)) {
                return true;
            }
        }
        return std::filesystem::exists(filepath);
    }

}
