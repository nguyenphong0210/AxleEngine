#include <Axle/AssetMetadataSerializer.hpp>
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
        std::string str = metaPath.string();
        if (str.size() >= 5 && str.substr(str.size() - 5) == ".meta") {
            return str.substr(0, str.size() - 5);
        }
        return metaPath;
    }

    bool AssetMetadataSerializer::Serialize(const std::filesystem::path& metaPath, const AssetMetadata& metadata) {
        std::ofstream fout(metaPath);
        if (!fout.is_open()) {
            AXLE_ERROR("AssetMetadataSerializer: Failed to write .meta file: '{0}'", metaPath.string());
            return false;
        }

        fout << "AssetMetadata:\n";
        fout << "  UUID: " << (uint64_t)metadata.Handle << "\n";
        fout << "  Type: " << AssetTypeToString(metadata.Type) << "\n";
        if (!metadata.FilePath.empty()) {
            std::filesystem::path p = metadata.FilePath;
            if (p.is_absolute()) {
                p = p.filename();
            }
            fout << "  FilePath: " << p.generic_string() << "\n";
        }
        fout.close();
        return true;
    }

    bool AssetMetadataSerializer::Deserialize(const std::filesystem::path& metaPath, AssetMetadata& outMetadata) {
        std::ifstream fin(metaPath);
        if (!fin.is_open()) return false;

        std::string line;
        uint64_t uuidVal = 0;
        AssetType type = AssetType::None;
        std::string filePathStr;

        while (std::getline(fin, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("UUID:", 0) == 0) {
                std::string val = Trim(trimmed.substr(5));
                try {
                    uuidVal = std::stoull(val);
                } catch (...) {
                    uuidVal = 0;
                }
            } else if (trimmed.rfind("Type:", 0) == 0) {
                std::string val = Trim(trimmed.substr(5));
                type = AssetTypeFromString(val);
            } else if (trimmed.rfind("FilePath:", 0) == 0) {
                filePathStr = Trim(trimmed.substr(9));
            }
        }

        if (uuidVal == 0 || type == AssetType::None) {
            return false;
        }

        outMetadata.Handle = UUID(uuidVal);
        outMetadata.Type = type;
        if (!filePathStr.empty()) {
            outMetadata.FilePath = filePathStr;
        } else {
            outMetadata.FilePath = GetAssetPathFromMeta(metaPath);
        }
        return true;
    }

}
