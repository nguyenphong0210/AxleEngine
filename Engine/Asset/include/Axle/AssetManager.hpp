#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetMetadata.hpp>
#include <Axle/AssetRegistrySerializer.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/Material.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <string>

namespace Axle {

    namespace BuiltinAssets {
        inline constexpr uint64_t CubeUUID     = 0xBA5E0001ULL;
        inline constexpr uint64_t SphereUUID   = 0xBA5E0002ULL;
        inline constexpr uint64_t CylinderUUID = 0xBA5E0003ULL;
        inline constexpr uint64_t ConeUUID     = 0xBA5E0004ULL;
        inline constexpr uint64_t PlaneUUID    = 0xBA5E0005ULL;
    }

    class AssetManager {
    public:
        static void Init(const std::filesystem::path& assetDirectory);
        static void Shutdown();

        // Recursively traverses the asset directory and ensures entries exist in root registry
        static void Sync(const std::filesystem::path& directory = {});

        // Retrieves existing metadata or allocates fresh UUID and persists into root registry for new imports
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

        // Builtin asset primitives
        static bool IsBuiltin(const std::filesystem::path& assetPath);
        static bool IsBuiltin(UUID uuid);

        // Reference tracking & diagnostics
        static void TrackReferencedAsset(UUID uuid, const std::filesystem::path& expectedPath = {});
        static bool IsAssetReferenced(UUID uuid);
        static const std::vector<AssetDiagnostic>& GetDiagnostics();
        static void ClearDiagnostics();
        static bool HasDiagnostics();
        static bool HasDiagnosticErrors();
        static void ReportDiagnostic(AssetDiagnostic::Level severity, const std::string& message, const std::filesystem::path& path = {}, UUID uuid = 0);

        // Explicit Relink for external moves
        static bool RelinkAsset(UUID uuid, const std::filesystem::path& newPath, std::string* outError = nullptr);

        // Explicit Relocation versus Copy
        // Relocation (move/rename) preserves the existing UUID and updates root registry path
        static bool RelocateAsset(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::string* outError = nullptr);

        // Copy duplicates the asset and allocates a brand-new distinct authored UUID in root registry
        static bool CopyAsset(const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath, std::string* outError = nullptr);

        // File lifecycle events
        static void OnAssetRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        static void OnAssetDeleted(const std::filesystem::path& assetPath);

        // Registry queries
        static size_t GetAssetCount();
        static const std::filesystem::path& GetAssetDirectory();
        static void SetAssetDirectory(const std::filesystem::path& dir);
        static std::filesystem::path GetRegistryPath();

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
        static AssetMetadata GetBuiltinMetadata(const std::filesystem::path& assetPath);
        static AssetMetadata GetBuiltinMetadata(UUID uuid);
        static bool SaveRegistryLocked(std::string* outError = nullptr);

    private:
        static std::filesystem::path s_AssetDirectory;
        static std::unordered_map<UUID, AssetMetadata> s_Assets;
        static std::unordered_map<std::string, UUID> s_PathToUUID;
        static std::unordered_map<UUID, std::filesystem::path> s_ReferencedAssets;
        static std::vector<AssetDiagnostic> s_Diagnostics;

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
