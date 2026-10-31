#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetMetadata.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Material.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <memory>

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

        // ---------------------------------------------------------------------
        // Centralized Resource Cache (TextureCache & MaterialCache)
        // ---------------------------------------------------------------------
        // Texture Cache
        static std::shared_ptr<Texture2D> GetTexture(const std::filesystem::path& path);
        static std::shared_ptr<Texture2D> GetTexture(UUID uuid);
        static void ClearTextureCache();

        // Material Cache
        static std::shared_ptr<Material> GetMaterial(const std::filesystem::path& path);
        static std::shared_ptr<Material> GetMaterial(UUID uuid);
        static void ReloadMaterial(UUID uuid);
        static void ReloadMaterial(const std::filesystem::path& path);
        static void ClearMaterialCache();

        // Multi-Backend RHI Material Cache
        static std::shared_ptr<RHI::RHIMaterial> GetRHIMaterial(const std::filesystem::path& path);
        static std::shared_ptr<RHI::RHIMaterial> GetRHIMaterial(UUID uuid);
        static void ClearRHIMaterialCache();

    private:
        static std::string NormalizePathKey(const std::filesystem::path& path);

    private:
        static std::filesystem::path s_AssetDirectory;
        static std::unordered_map<UUID, AssetMetadata> s_Assets;
        static std::unordered_map<std::string, UUID> s_PathToUUID;

        static std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_TextureCacheByPath;
        static std::unordered_map<UUID, std::shared_ptr<Texture2D>> s_TextureCacheByUUID;
        static std::unordered_map<UUID, std::shared_ptr<Material>> s_MaterialCacheByUUID;
        static std::unordered_map<std::string, std::shared_ptr<Material>> s_MaterialCacheByPath;
        static std::unordered_map<UUID, std::shared_ptr<RHI::RHIMaterial>> s_RHIMaterialCacheByUUID;
        static std::unordered_map<std::string, std::shared_ptr<RHI::RHIMaterial>> s_RHIMaterialCacheByPath;

        static std::recursive_mutex s_AssetMutex;
        static bool s_Initialized;
    };

}
