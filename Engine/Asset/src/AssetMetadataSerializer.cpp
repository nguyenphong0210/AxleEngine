#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    std::filesystem::path AssetMetadataSerializer::GetMetaPath(const std::filesystem::path& assetPath) {
        return assetPath.string() + ".meta";
    }

    bool AssetMetadataSerializer::IsMetaFile(const std::filesystem::path& path) {
        return path.extension() == ".meta";
    }

    std::filesystem::path AssetMetadataSerializer::GetAssetPathFromMeta(const std::filesystem::path& metaPath) {
        std::string str = metaPath.generic_string();
        if (str.size() >= 5 && str.substr(str.size() - 5) == ".meta") {
            return str.substr(0, str.size() - 5);
        }
        return metaPath;
    }

    bool AssetMetadataSerializer::Serialize(const std::filesystem::path& metaPath, const AssetMetadata& metadata, std::string* outError) {
        if (!metadata.IsValid()) {
            if (outError) *outError = "Cannot serialize invalid metadata (missing Handle or AssetType)";
            return false;
        }

        std::stringstream fout;
        fout << "AssetMetadata:\n";
        fout << "  Version: " << metadata.Version << "\n";
        fout << "  UUID: " << (uint64_t)metadata.Handle << "\n";
        fout << "  Type: " << AssetTypeToString(metadata.Type) << "\n";

        if (!metadata.FilePath.empty()) {
            std::filesystem::path p = metadata.FilePath;
            // Prevent embedding machine-specific absolute roots
            if (p.is_absolute()) {
                p = p.filename();
            }
            fout << "  FilePath: " << p.generic_string() << "\n";
        }

        if (!FileSystem::WriteTextFileAtomically(metaPath, fout.str(), outError)) {
            AXLE_ERROR("AssetMetadataSerializer: Failed to atomically write .meta file: '{0}' ({1})",
                       metaPath.string(), outError ? *outError : "unknown error");
            return false;
        }

        return true;
    }

    bool AssetMetadataSerializer::Deserialize(const std::filesystem::path& metaPath, AssetMetadata& outMetadata, std::string* outError) {
        std::string content = FileSystem::ReadTextFile(metaPath);
        if (content.empty() && !FileSystem::Exists(metaPath)) {
            if (outError) *outError = "Metadata file does not exist: " + metaPath.string();
            return false;
        }

        std::istringstream fin(content);
        std::string line;
        uint32_t version = 1;
        uint64_t uuidVal = 0;
        AssetType type = AssetType::None;
        std::string filePathStr;
        bool hasUUID = false;
        bool hasType = false;

        while (std::getline(fin, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("Version:", 0) == 0) {
                std::string val = Trim(trimmed.substr(8));
                try {
                    version = static_cast<uint32_t>(std::stoul(val));
                } catch (...) {
                    if (outError) *outError = "Corrupt Version field in metadata: " + val;
                    return false;
                }
                if (version > CurrentVersion) {
                    if (outError) *outError = "Unsupported metadata version " + std::to_string(version) +
                                              " (current engine supports up to version " + std::to_string(CurrentVersion) + ")";
                    return false;
                }
            } else if (trimmed.rfind("UUID:", 0) == 0) {
                std::string val = Trim(trimmed.substr(5));
                try {
                    uuidVal = std::stoull(val);
                    hasUUID = (uuidVal != 0);
                } catch (...) {
                    if (outError) *outError = "Corrupt UUID value in metadata: " + val;
                    return false;
                }
            } else if (trimmed.rfind("Type:", 0) == 0) {
                std::string val = Trim(trimmed.substr(5));
                type = AssetTypeFromString(val);
                hasType = (type != AssetType::None);
            } else if (trimmed.rfind("FilePath:", 0) == 0) {
                filePathStr = Trim(trimmed.substr(9));
            }
        }

        if (!hasUUID || !hasType) {
            if (outError) *outError = "Corrupt metadata in '" + metaPath.string() + "': missing valid UUID or recognized Type";
            return false;
        }

        outMetadata.Handle = UUID(uuidVal);
        outMetadata.Type = type;
        outMetadata.Version = version;

        if (!filePathStr.empty()) {
            std::filesystem::path p(filePathStr);
            if (p.is_absolute()) {
                p = p.filename();
            }
            outMetadata.FilePath = p.generic_string();
        } else {
            outMetadata.FilePath = GetAssetPathFromMeta(metaPath);
        }

        return true;
    }

}
