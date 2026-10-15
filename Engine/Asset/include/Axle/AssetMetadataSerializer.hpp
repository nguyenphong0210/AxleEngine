#pragma once
#include <Axle/AssetMetadata.hpp>
#include <filesystem>

namespace Axle {

    class AssetMetadataSerializer {
    public:
        static std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath);
        static bool IsMetaFile(const std::filesystem::path& path);
        static std::filesystem::path GetAssetPathFromMeta(const std::filesystem::path& metaPath);

        static bool Serialize(const std::filesystem::path& metaPath, const AssetMetadata& metadata);
        static bool Deserialize(const std::filesystem::path& metaPath, AssetMetadata& outMetadata);
    };

}
