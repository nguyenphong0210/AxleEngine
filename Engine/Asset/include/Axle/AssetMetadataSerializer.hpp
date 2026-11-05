#pragma once
#include <Axle/AssetMetadata.hpp>
#include <filesystem>
#include <string>

namespace Axle {

    class AssetMetadataSerializer {
    public:
        static inline constexpr uint32_t CurrentVersion = 1;

        static std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath);
        static bool IsMetaFile(const std::filesystem::path& path);
        static std::filesystem::path GetAssetPathFromMeta(const std::filesystem::path& metaPath);

        static bool Serialize(const std::filesystem::path& metaPath, const AssetMetadata& metadata, std::string* outError = nullptr);
        static bool Deserialize(const std::filesystem::path& metaPath, AssetMetadata& outMetadata, std::string* outError = nullptr);
    };

}
