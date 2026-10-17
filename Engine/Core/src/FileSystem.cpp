#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>

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
        if (!s_AssetDirectory.empty() && std::filesystem::exists(s_AssetDirectory)) {
            return s_AssetDirectory;
        }

        // Auto-detection strategy:
        // 1. Check "Editor/assets" or "Sandbox/assets" relative to current working directory
        if (std::filesystem::exists("Editor/assets")) {
            s_AssetDirectory = std::filesystem::absolute("Editor/assets");
            return s_AssetDirectory;
        }
        if (std::filesystem::exists("Sandbox/assets")) {
            s_AssetDirectory = std::filesystem::absolute("Sandbox/assets");
            return s_AssetDirectory;
        }

        // 2. Check relative paths from build output folder (e.g., build/Editor/Release)
        if (std::filesystem::exists("../../Editor/assets")) {
            s_AssetDirectory = std::filesystem::canonical("../../Editor/assets");
            return s_AssetDirectory;
        }
        if (std::filesystem::exists("../../../Editor/assets")) {
            s_AssetDirectory = std::filesystem::canonical("../../../Editor/assets");
            return s_AssetDirectory;
        }
        if (std::filesystem::exists("../../Sandbox/assets")) {
            s_AssetDirectory = std::filesystem::canonical("../../Sandbox/assets");
            return s_AssetDirectory;
        }

        // 3. Check "assets" in current working directory (typical when running standalone distribution)
        if (std::filesystem::exists("assets")) {
            s_AssetDirectory = std::filesystem::absolute("assets");
            return s_AssetDirectory;
        }

        // Fallback default
        s_AssetDirectory = "Editor/assets";
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
        // Ensure parent directories exist
        if (filepath.has_parent_path()) {
            std::filesystem::create_directories(filepath.parent_path());
        }

        std::ofstream file(filepath);
        if (!file.is_open()) {
            AXLE_CORE_ERROR("FileSystem: Unable to open file for writing: {0}", filepath.string());
            return false;
        }

        file << content;
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
