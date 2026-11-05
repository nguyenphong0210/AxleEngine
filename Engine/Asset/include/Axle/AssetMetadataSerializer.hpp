#pragma once
#include <Axle/AssetMetadata.hpp>
#include <filesystem>
#include <string>

namespace Axle {
    // Reads/writes identity inside an Engine-owned asset file. There is no sidecar path.
    class AssetMetadataSerializer {
    public:
        static bool IsNativeAsset(const std::filesystem::path& path);
        static bool Serialize(const std::filesystem::path& assetPath, const AssetMetadata& metadata, std::string* error = nullptr);
        static bool Deserialize(const std::filesystem::path& assetPath, AssetMetadata& metadata, std::string* error = nullptr);
    };
}
