#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <memory>
#include <cstdint>

namespace Axle {

    #pragma pack(push, 1)
    struct PackageHeader {
        char Magic[4] = { 'A', 'X', 'P', 'K' }; // "AXPK"
        uint32_t Version = 1;                   // Format version
        uint32_t Flags = 0;                     // Bit flags (e.g. compression, encryption)
        uint32_t EntryCount = 0;                // Number of asset entries in TOC
        uint64_t TableOfContentsOffset = 0;     // File offset where TOC starts
        uint64_t TableOfContentsSize = 0;       // Byte size of serialized TOC
        uint64_t DataOffset = 0;                // File offset where data blocks start
        uint64_t TotalDataSize = 0;             // Total size of all data blocks
    };
    #pragma pack(pop)

    struct PackageEntry {
        UUID AssetUUID = 0;                     // Persistent UUID (Axle::UUID)
        uint16_t Type = 0;                      // AssetType numeric id
        std::string VirtualPath;                // Relative virtual path (e.g. "scenes/CarTest.axlescene")
        uint64_t Offset = 0;                    // Absolute byte offset in package file
        uint64_t Size = 0;                      // Uncompressed byte size
        uint64_t CompressedSize = 0;            // Stored byte size in package
        uint32_t Compression = 0;               // 0 = None
        uint32_t Checksum = 0;                  // CRC-32 checksum
    };

    class Package {
    public:
        Package() = default;
        ~Package();

        bool Open(const std::filesystem::path& filepath);
        void Close();
        bool IsOpen() const { return m_IsOpen; }

        bool HasEntry(const std::string& virtualPath) const;
        bool HasEntry(UUID uuid) const;

        const PackageEntry* GetEntry(const std::string& virtualPath) const;
        const PackageEntry* GetEntry(UUID uuid) const;

        std::vector<uint8_t> ReadEntry(const PackageEntry& entry) const;
        std::vector<uint8_t> ReadEntry(const std::string& virtualPath) const;
        std::vector<uint8_t> ReadEntry(UUID uuid) const;

        std::string ReadEntryAsText(const std::string& virtualPath) const;
        std::string ReadEntryAsText(UUID uuid) const;

        const std::unordered_map<std::string, PackageEntry>& GetEntries() const { return m_EntriesByPath; }
        const std::filesystem::path& GetFilePath() const { return m_FilePath; }
        size_t GetEntryCount() const { return m_EntriesByPath.size(); }

        static std::string NormalizePath(const std::string& path);

    private:
        std::filesystem::path m_FilePath;
        PackageHeader m_Header{};
        std::unordered_map<std::string, PackageEntry> m_EntriesByPath;
        std::unordered_map<UUID, PackageEntry> m_EntriesByUUID;
        bool m_IsOpen = false;
        mutable void* m_FileHandle = nullptr; // Internal FILE* handle
    };

    class PackageWriter {
    public:
        PackageWriter() = default;
        ~PackageWriter();

        bool Begin(const std::filesystem::path& outputPath);
        bool AddFile(const std::string& virtualPath, const std::filesystem::path& sourceDiskPath, UUID uuid = 0, uint16_t type = 0);
        bool AddData(const std::string& virtualPath, const void* data, size_t size, UUID uuid = 0, uint16_t type = 0);
        bool Finish();

        static uint32_t CalculateCRC32(const void* data, size_t size);

    private:
        struct PendingEntry {
            PackageEntry Entry;
            std::vector<uint8_t> Data;
            std::filesystem::path SourceFilePath;
            bool IsFromDisk = false;
        };

        std::filesystem::path m_OutputPath;
        std::vector<PendingEntry> m_PendingEntries;
        bool m_IsWriting = false;
    };

}
