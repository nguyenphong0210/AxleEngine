#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetMetadata.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <filesystem>
#include <unordered_map>
#include <mutex>

namespace Axle {

    class AssetManager {
    public:
        static void Init(const std::filesystem::path& assetDirectory);
        static void Shutdown();

        // Recursively traverses the asset directory and ensures companion .meta files exist
        static void Sync(const std::filesystem::path& directory = {});

        // Retrieves existing metadata or automatically creates & serializes a companion .meta file
        static AssetMetadata GetOrCreateMetadata(const std::filesystem::path& assetPath);

        // Fast Lookups
        static UUID GetAssetUUID(const std::filesystem::path& assetPath);
        static std::filesystem::path GetAssetPath(UUID uuid); // Returns absolute/resolved path
        static std::filesystem::path GetRelativeAssetPath(UUID uuid);
        static AssetType GetAssetType(UUID uuid);
        static AssetType GetAssetType(const std::filesystem::path& assetPath);

        static const AssetMetadata* GetMetadata(UUID uuid);
        static const AssetMetadata* GetMetadata(const std::filesystem::path& assetPath);

        static bool HasAsset(UUID uuid);
        static bool HasAsset(const std::filesystem::path& assetPath);

        // File lifecycle events
        static void OnAssetRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        static void OnAssetDeleted(const std::filesystem::path& assetPath);

        // Registry queries
        static size_t GetAssetCount();
        static const std::filesystem::path& GetAssetDirectory();
        static void SetAssetDirectory(const std::filesystem::path& dir);

    private:
        static std::string NormalizePathKey(const std::filesystem::path& path);

    private:
        static std::filesystem::path s_AssetDirectory;
        static std::unordered_map<UUID, AssetMetadata> s_Assets;
        static std::unordered_map<std::string, UUID> s_PathToUUID;
        static std::recursive_mutex s_AssetMutex;
        static bool s_Initialized;
    };

}
