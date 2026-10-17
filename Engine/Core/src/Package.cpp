#include <Axle/Package.hpp>
#include <Axle/Log.hpp>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>

namespace Axle {

    // Standard IEEE 802.3 CRC32 lookup table
    static uint32_t s_CRC32Table[256];
    static bool s_CRC32TableInitialized = false;

    static void InitializeCRC32Table() {
        if (s_CRC32TableInitialized) return;
        const uint32_t polynomial = 0xEDB88320;
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (size_t j = 0; j < 8; j++) {
                if (c & 1) {
                    c = polynomial ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }
            s_CRC32Table[i] = c;
        }
        s_CRC32TableInitialized = true;
    }

    uint32_t PackageWriter::CalculateCRC32(const void* data, size_t size) {
        InitializeCRC32Table();
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < size; i++) {
            crc = s_CRC32Table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    }

    std::string Package::NormalizePath(const std::string& path) {
        std::string p = path;
        std::replace(p.begin(), p.end(), '\\', '/');

        // Strip leading redundant slashes or current dir tokens
        while (p.rfind("./", 0) == 0) {
            p = p.substr(2);
        }
        while (!p.empty() && p[0] == '/') {
            p = p.substr(1);
        }

        // Canonical prefix normalizations
        static const std::vector<std::string> prefixes = {
            "Editor/assets/",
            "Sandbox/assets/",
            "assets/",
            "Engine/Resources/",
            "Resources/"
        };

        for (const auto& prefix : prefixes) {
            if (p.rfind(prefix, 0) == 0) {
                p = p.substr(prefix.length());
                break;
            }
        }

        // Normalize Shaders/ to shaders/
        if (p.rfind("Shaders/", 0) == 0) {
            p = "shaders/" + p.substr(8);
        }

        return p;
    }

    Package::~Package() {
        Close();
    }

    bool Package::Open(const std::filesystem::path& filepath) {
        Close();

        FILE* f = fopen(filepath.string().c_str(), "rb");
        if (!f) {
            AXLE_CORE_ERROR("Package: Failed to open package file at '{0}'", filepath.string());
            return false;
        }

        PackageHeader header;
        if (fread(&header, sizeof(PackageHeader), 1, f) != 1) {
            AXLE_CORE_ERROR("Package: Failed to read package header from '{0}'", filepath.string());
            fclose(f);
            return false;
        }

        if (memcmp(header.Magic, "AXPK", 4) != 0) {
            AXLE_CORE_ERROR("Package: Invalid magic header in '{0}'. Expected 'AXPK'", filepath.string());
            fclose(f);
            return false;
        }

        if (header.Version != 1) {
            AXLE_CORE_ERROR("Package: Unsupported version {0} in '{1}'", header.Version, filepath.string());
            fclose(f);
            return false;
        }

        // Seek to Table of Contents
        if (fseek(f, static_cast<long>(header.TableOfContentsOffset), SEEK_SET) != 0) {
            AXLE_CORE_ERROR("Package: Corrupt table of contents offset in '{0}'", filepath.string());
            fclose(f);
            return false;
        }

        m_EntriesByPath.clear();
        m_EntriesByUUID.clear();
        m_EntriesByPath.reserve(header.EntryCount);
        m_EntriesByUUID.reserve(header.EntryCount);

        for (uint32_t i = 0; i < header.EntryCount; i++) {
            PackageEntry entry;
            uint64_t rawUUID = 0;
            if (fread(&rawUUID, sizeof(rawUUID), 1, f) != 1) break;
            entry.AssetUUID = UUID(rawUUID);

            if (fread(&entry.Type, sizeof(entry.Type), 1, f) != 1) break;

            uint16_t pathLen = 0;
            if (fread(&pathLen, sizeof(pathLen), 1, f) != 1) break;

            std::string path(pathLen, '\0');
            if (pathLen > 0) {
                if (fread(&path[0], 1, pathLen, f) != pathLen) break;
            }
            entry.VirtualPath = path;

            if (fread(&entry.Offset, sizeof(entry.Offset), 1, f) != 1) break;
            if (fread(&entry.Size, sizeof(entry.Size), 1, f) != 1) break;
            if (fread(&entry.CompressedSize, sizeof(entry.CompressedSize), 1, f) != 1) break;
            if (fread(&entry.Compression, sizeof(entry.Compression), 1, f) != 1) break;
            if (fread(&entry.Checksum, sizeof(entry.Checksum), 1, f) != 1) break;

            std::string normPath = NormalizePath(entry.VirtualPath);
            m_EntriesByPath[normPath] = entry;
            if (entry.AssetUUID != 0) {
                m_EntriesByUUID[entry.AssetUUID] = entry;
            }
        }

        m_Header = header;
        m_FilePath = filepath;
        m_IsOpen = true;
        m_FileHandle = f;

        AXLE_CORE_INFO("Package: Successfully mounted archive '{0}' ({1} assets, {2} KB data)",
            filepath.filename().string(), m_EntriesByPath.size(), m_Header.TotalDataSize / 1024);

        return true;
    }

    void Package::Close() {
        if (m_FileHandle) {
            fclose(static_cast<FILE*>(m_FileHandle));
            m_FileHandle = nullptr;
        }
        m_IsOpen = false;
        m_EntriesByPath.clear();
        m_EntriesByUUID.clear();
        m_FilePath.clear();
    }

    bool Package::HasEntry(const std::string& virtualPath) const {
        if (!m_IsOpen) return false;
        return m_EntriesByPath.contains(NormalizePath(virtualPath));
    }

    bool Package::HasEntry(UUID uuid) const {
        if (!m_IsOpen || uuid == 0) return false;
        return m_EntriesByUUID.contains(uuid);
    }

    const PackageEntry* Package::GetEntry(const std::string& virtualPath) const {
        if (!m_IsOpen) return nullptr;
        auto it = m_EntriesByPath.find(NormalizePath(virtualPath));
        if (it != m_EntriesByPath.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const PackageEntry* Package::GetEntry(UUID uuid) const {
        if (!m_IsOpen || uuid == 0) return nullptr;
        auto it = m_EntriesByUUID.find(uuid);
        if (it != m_EntriesByUUID.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<uint8_t> Package::ReadEntry(const PackageEntry& entry) const {
        if (!m_IsOpen || !m_FileHandle) return {};

        FILE* f = static_cast<FILE*>(m_FileHandle);
        if (fseek(f, static_cast<long>(entry.Offset), SEEK_SET) != 0) {
            AXLE_CORE_ERROR("Package: Failed seeking to offset {0} for '{1}'", entry.Offset, entry.VirtualPath);
            return {};
        }

        std::vector<uint8_t> buffer(entry.Size);
        if (entry.Size > 0) {
            size_t bytesRead = fread(buffer.data(), 1, entry.Size, f);
            if (bytesRead != entry.Size) {
                AXLE_CORE_ERROR("Package: Short read ({0}/{1} bytes) for '{2}'", bytesRead, entry.Size, entry.VirtualPath);
                return {};
            }
        }

        // Verify checksum if present
        if (entry.Checksum != 0) {
            uint32_t calcCrc = PackageWriter::CalculateCRC32(buffer.data(), buffer.size());
            if (calcCrc != entry.Checksum) {
                AXLE_CORE_WARN("Package: Checksum mismatch for '{0}' (expected 0x{1:08X}, got 0x{2:08X})",
                    entry.VirtualPath, entry.Checksum, calcCrc);
            }
        }

        return buffer;
    }

    std::vector<uint8_t> Package::ReadEntry(const std::string& virtualPath) const {
        const auto* entry = GetEntry(virtualPath);
        if (!entry) return {};
        return ReadEntry(*entry);
    }

    std::vector<uint8_t> Package::ReadEntry(UUID uuid) const {
        const auto* entry = GetEntry(uuid);
        if (!entry) return {};
        return ReadEntry(*entry);
    }

    std::string Package::ReadEntryAsText(const std::string& virtualPath) const {
        auto buffer = ReadEntry(virtualPath);
        if (buffer.empty()) return "";
        return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    std::string Package::ReadEntryAsText(UUID uuid) const {
        auto buffer = ReadEntry(uuid);
        if (buffer.empty()) return "";
        return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    // -------------------------------------------------------------
    // PackageWriter Implementation
    // -------------------------------------------------------------

    PackageWriter::~PackageWriter() {
        m_PendingEntries.clear();
    }

    bool PackageWriter::Begin(const std::filesystem::path& outputPath) {
        m_OutputPath = outputPath;
        m_PendingEntries.clear();
        m_IsWriting = true;

        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }

        return true;
    }

    bool PackageWriter::AddFile(const std::string& virtualPath, const std::filesystem::path& sourceDiskPath, UUID uuid, uint16_t type) {
        if (!m_IsWriting) return false;
        if (!std::filesystem::exists(sourceDiskPath)) {
            AXLE_CORE_WARN("PackageWriter: Source disk path does not exist: '{0}'", sourceDiskPath.string());
            return false;
        }

        PendingEntry pending;
        pending.Entry.VirtualPath = Package::NormalizePath(virtualPath);
        pending.Entry.AssetUUID = uuid;
        pending.Entry.Type = type;
        pending.SourceFilePath = sourceDiskPath;
        pending.IsFromDisk = true;

        m_PendingEntries.push_back(std::move(pending));
        return true;
    }

    bool PackageWriter::AddData(const std::string& virtualPath, const void* data, size_t size, UUID uuid, uint16_t type) {
        if (!m_IsWriting) return false;

        PendingEntry pending;
        pending.Entry.VirtualPath = Package::NormalizePath(virtualPath);
        pending.Entry.AssetUUID = uuid;
        pending.Entry.Type = type;
        pending.Entry.Size = size;
        pending.Entry.CompressedSize = size;
        pending.IsFromDisk = false;

        if (data && size > 0) {
            pending.Data.resize(size);
            memcpy(pending.Data.data(), data, size);
        }

        m_PendingEntries.push_back(std::move(pending));
        return true;
    }

    bool PackageWriter::Finish() {
        if (!m_IsWriting) return false;

        FILE* f = fopen(m_OutputPath.string().c_str(), "wb");
        if (!f) {
            AXLE_CORE_ERROR("PackageWriter: Could not create output file '{0}'", m_OutputPath.string());
            m_IsWriting = false;
            return false;
        }

        PackageHeader header{};
        memcpy(header.Magic, "AXPK", 4);
        header.Version = 1;
        header.Flags = 0;
        header.EntryCount = static_cast<uint32_t>(m_PendingEntries.size());
        header.DataOffset = sizeof(PackageHeader);

        // Reserve header space at offset 0
        fwrite(&header, sizeof(PackageHeader), 1, f);

        uint64_t currentDataOffset = sizeof(PackageHeader);

        // 1. Write Asset Data Chunks
        for (auto& pending : m_PendingEntries) {
            pending.Entry.Offset = currentDataOffset;

            if (pending.IsFromDisk) {
                std::ifstream srcFile(pending.SourceFilePath, std::ios::binary | std::ios::ate);
                if (srcFile.is_open()) {
                    size_t fileSize = static_cast<size_t>(srcFile.tellg());
                    srcFile.seekg(0, std::ios::beg);

                    std::vector<uint8_t> buffer(fileSize);
                    if (fileSize > 0) {
                        srcFile.read(reinterpret_cast<char*>(buffer.data()), fileSize);
                    }

                    pending.Entry.Size = fileSize;
                    pending.Entry.CompressedSize = fileSize;
                    pending.Entry.Checksum = CalculateCRC32(buffer.data(), buffer.size());

                    if (fileSize > 0) {
                        fwrite(buffer.data(), 1, fileSize, f);
                        currentDataOffset += fileSize;
                    }
                } else {
                    AXLE_CORE_ERROR("PackageWriter: Failed reading file '{0}'", pending.SourceFilePath.string());
                    pending.Entry.Size = 0;
                    pending.Entry.CompressedSize = 0;
                }
            } else {
                pending.Entry.Size = pending.Data.size();
                pending.Entry.CompressedSize = pending.Data.size();
                pending.Entry.Checksum = CalculateCRC32(pending.Data.data(), pending.Data.size());

                if (!pending.Data.empty()) {
                    fwrite(pending.Data.data(), 1, pending.Data.size(), f);
                    currentDataOffset += pending.Data.size();
                }
            }
        }

        header.TotalDataSize = currentDataOffset - header.DataOffset;
        header.TableOfContentsOffset = currentDataOffset;

        // 2. Write Table of Contents
        uint64_t tocStart = currentDataOffset;
        for (const auto& pending : m_PendingEntries) {
            const auto& e = pending.Entry;
            uint64_t rawUUID = static_cast<uint64_t>(e.AssetUUID);
            fwrite(&rawUUID, sizeof(rawUUID), 1, f);
            fwrite(&e.Type, sizeof(e.Type), 1, f);

            uint16_t pathLen = static_cast<uint16_t>(e.VirtualPath.length());
            fwrite(&pathLen, sizeof(pathLen), 1, f);
            if (pathLen > 0) {
                fwrite(e.VirtualPath.data(), 1, pathLen, f);
            }

            fwrite(&e.Offset, sizeof(e.Offset), 1, f);
            fwrite(&e.Size, sizeof(e.Size), 1, f);
            fwrite(&e.CompressedSize, sizeof(e.CompressedSize), 1, f);
            fwrite(&e.Compression, sizeof(e.Compression), 1, f);
            fwrite(&e.Checksum, sizeof(e.Checksum), 1, f);
        }

        uint64_t tocEnd = ftell(f);
        header.TableOfContentsSize = tocEnd - tocStart;

        // 3. Update Header at beginning of file
        fseek(f, 0, SEEK_SET);
        fwrite(&header, sizeof(PackageHeader), 1, f);

        fclose(f);
        m_IsWriting = false;

        AXLE_CORE_INFO("PackageWriter: Finished building '{0}' ({1} entries, {2} KB total)",
            m_OutputPath.filename().string(), header.EntryCount, (header.TotalDataSize + header.TableOfContentsSize) / 1024);

        return true;
    }

}
