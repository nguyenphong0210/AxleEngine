#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>

namespace Axle {

    static std::filesystem::path s_AssetDirectory;

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

        // 2. Check "assets" in current working directory (typical when running from output directory)
        if (std::filesystem::exists("assets")) {
            s_AssetDirectory = std::filesystem::absolute("assets");
            return s_AssetDirectory;
        }

        // 3. Check relative paths from build output folder (e.g., build/Editor/Release)
        if (std::filesystem::exists("../../Editor/assets")) {
            s_AssetDirectory = std::filesystem::canonical("../../Editor/assets");
            return s_AssetDirectory;
        }
        if (std::filesystem::exists("../../Sandbox/assets")) {
            s_AssetDirectory = std::filesystem::canonical("../../Sandbox/assets");
            return s_AssetDirectory;
        }

        // Fallback default
        s_AssetDirectory = "Editor/assets";
        return s_AssetDirectory;
    }

    std::filesystem::path FileSystem::ResolveAssetPath(const std::filesystem::path& relativePath) {
        // If already an absolute path and exists, return directly
        if (relativePath.is_absolute() && std::filesystem::exists(relativePath)) {
            return relativePath;
        }

        // Try against active asset directory
        auto resolved = GetAssetDirectory() / relativePath;
        if (std::filesystem::exists(resolved)) {
            return resolved;
        }

        // Fallback checks
        if (std::filesystem::exists(relativePath)) {
            return relativePath;
        }

        return resolved;
    }

    std::string FileSystem::ReadTextFile(const std::filesystem::path& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            AXLE_CORE_WARN("FileSystem: Unable to open file for reading: {0}", filepath.string());
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::vector<uint8_t> FileSystem::ReadBinaryFile(const std::filesystem::path& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
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
        return std::filesystem::exists(filepath);
    }

}
