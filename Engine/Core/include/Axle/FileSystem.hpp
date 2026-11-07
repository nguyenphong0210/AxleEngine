#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace Axle {

    class Package;

    class FileSystem {
    public:
        // VFS Package Management
        static bool MountPackage(const std::filesystem::path& packagePath);
        static void UnmountPackage();
        static bool HasMountedPackage();
        static const Package* GetMountedPackage();

        // Configure or query asset directory root
        static void SetAssetDirectory(const std::filesystem::path& path);
        static const std::filesystem::path& GetAssetDirectory();
        static uint64_t GetSourceGeneration();

        // Resolve a relative asset path (e.g. "shaders/Checkerboard.vert") to an existing filesystem path
        static std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativePath);

        // Read entire text file to string
        static std::string ReadTextFile(const std::filesystem::path& filepath);

        // Read entire binary file into byte buffer
        static std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& filepath);

        // Write string content to text file (atomically replaces target, preserving prior file on failure)
        static bool WriteTextFile(const std::filesystem::path& filepath, const std::string& content);

        // Atomic write operations (guarantees target preservation on failure)
        static bool WriteTextFileAtomically(const std::filesystem::path& filepath, const std::string& text, std::string* outError = nullptr);
        static bool WriteBinaryFileAtomically(const std::filesystem::path& filepath, const void* data, size_t size, std::string* outError = nullptr);

        static bool MakeAssetReference(const std::filesystem::path& path, std::string& reference);

        // Optional versioned identity envelope for native text documents; no sidecars or registry.
        static bool ReadFileIdentity(const std::filesystem::path& path, uint64_t& id, std::string* error = nullptr);
        static bool WriteIdentifiedTextFile(const std::filesystem::path& path, const std::string& text,
                                           std::string* error = nullptr, uint64_t requestedID = 0);

        // Checks whether a path exists
        static bool Exists(const std::filesystem::path& filepath);
    };

}
