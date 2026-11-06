#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/AssetMetadata.hpp>
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

        // Rebuilds a read-only in-memory index from embedded native IDs and raw paths.
        static void Sync(const std::filesystem::path& directory = {});

        // Explicitly identifies a newly authored native document; raw resources retain path identity.
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
        static std::vector<AssetDiagnostic> GetDiagnostics();
        static void ClearDiagnostics();
        static bool HasDiagnostics();
        static bool HasDiagnosticErrors();
        static void ReportDiagnostic(AssetDiagnostic::Level severity, const std::string& message, const std::filesystem::path& path = {}, UUID uuid = 0);

        // Explicit validation/reindex of an externally moved native file
        static bool RelinkAsset(UUID uuid, const std::filesystem::path& newPath, std::string* outError = nullptr);

        // Explicit Relocation versus Copy
        // Relocation (move/rename) preserves the existing UUID and updates the in-memory discovery index
        static bool RelocateAsset(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::string* outError = nullptr);

        // Copy duplicates the asset and allocates a brand-new distinct UUID embedded in the copied native file (raw copies remain path-addressed)
        static bool CopyAsset(const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath, std::string* outError = nullptr);

        // File lifecycle events
        static void OnAssetRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
        static void OnAssetDeleted(const std::filesystem::path& assetPath);

        // In-memory discovery queries
        static size_t GetAssetCount();
        static const std::filesystem::path& GetAssetDirectory();
        static void SetAssetDirectory(const std::filesystem::path& dir);
        static std::filesystem::path GetReferencePath(const std::filesystem::path& assetPath);
        static bool RelinkPathReferences(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::string* error = nullptr);

    private:
        static std::string NormalizePathKey(const std::filesystem::path& path);
        static AssetMetadata GetBuiltinMetadata(const std::filesystem::path& assetPath);
        static AssetMetadata GetBuiltinMetadata(UUID uuid);

    private:
        static std::filesystem::path s_AssetDirectory;
        static std::unordered_map<UUID, AssetMetadata> s_Assets;
        static std::unordered_map<std::string, UUID> s_PathToUUID;
        static std::unordered_map<std::string, AssetMetadata> s_MetadataByPath;
        static std::unordered_map<UUID, std::filesystem::path> s_ReferencedAssets;
        static std::vector<AssetDiagnostic> s_Diagnostics;

        static std::recursive_mutex s_AssetMutex;
        static bool s_Initialized;
    };

}
