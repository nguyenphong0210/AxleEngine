#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace Axle {

    class FileSystem {
    public:
        // Configure or query asset directory root
        static void SetAssetDirectory(const std::filesystem::path& path);
        static const std::filesystem::path& GetAssetDirectory();

        // Resolve a relative asset path (e.g. "shaders/Checkerboard.vert") to an existing filesystem path
        static std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath);

        // Read entire text file to string
        static std::string ReadTextFile(const std::filesystem::path& filepath);

        // Read entire binary file into byte buffer
        static std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& filepath);

        // Write string content to text file
        static bool WriteTextFile(const std::filesystem::path& filepath, const std::string& content);

        // Checks whether a path exists
        static bool Exists(const std::filesystem::path& filepath);
    };

}
