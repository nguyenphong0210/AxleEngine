#include <Axle/AssetManager.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Axle {

    std::filesystem::path AssetManager::s_AssetDirectory;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Assets;
    std::unordered_map<std::string, UUID> AssetManager::s_PathToUUID;
    std::unordered_map<UUID, std::filesystem::path> AssetManager::s_ReferencedAssets;
    std::vector<AssetDiagnostic> AssetManager::s_Diagnostics;

    std::unordered_map<std::string, std::shared_ptr<Texture2D>> AssetManager::s_TextureCacheByPath;
    std::unordered_map<UUID, std::shared_ptr<Texture2D>> AssetManager::s_TextureCacheByUUID;
    std::unordered_map<UUID, std::shared_ptr<Material>> AssetManager::s_MaterialCacheByUUID;
    std::unordered_map<std::string, std::shared_ptr<Material>> AssetManager::s_MaterialCacheByPath;
    std::unordered_map<UUID, std::shared_ptr<RHI::RHIMaterial>> AssetManager::s_RHIMaterialCacheByUUID;
    std::unordered_map<std::string, std::shared_ptr<RHI::RHIMaterial>> AssetManager::s_RHIMaterialCacheByPath;

    std::recursive_mutex AssetManager::s_AssetMutex;
    bool AssetManager::s_Initialized = false;

    // -------------------------------------------------------------------------
    // Builtin Asset Identities
    // -------------------------------------------------------------------------
    bool AssetManager::IsBuiltin(const std::filesystem::path& assetPath) {
        std::string s = assetPath.generic_string();
        return s.rfind("builtin://", 0) == 0 || s.rfind("builtin:", 0) == 0;
    }

    bool AssetManager::IsBuiltin(UUID uuid) {
        uint64_t val = static_cast<uint64_t>(uuid);
        return val >= BuiltinAssets::CubeUUID && val <= BuiltinAssets::PlaneUUID;
    }

    AssetMetadata AssetManager::GetBuiltinMetadata(const std::filesystem::path& assetPath) {
        std::string s = assetPath.generic_string();
        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

        uint64_t id = BuiltinAssets::CubeUUID;
        if (lower == "builtin://cube" || lower == "builtin:cube") id = BuiltinAssets::CubeUUID;
        else if (lower == "builtin://sphere" || lower == "builtin:sphere") id = BuiltinAssets::SphereUUID;
        else if (lower == "builtin://cylinder" || lower == "builtin:cylinder") id = BuiltinAssets::CylinderUUID;
        else if (lower == "builtin://cone" || lower == "builtin:cone") id = BuiltinAssets::ConeUUID;
        else if (lower == "builtin://plane" || lower == "builtin:plane") id = BuiltinAssets::PlaneUUID;

        return AssetMetadata{ UUID(id), AssetType::Mesh, s, 1 };
    }

    AssetMetadata AssetManager::GetBuiltinMetadata(UUID uuid) {
        uint64_t val = static_cast<uint64_t>(uuid);
        std::string path;
        switch (val) {
            case BuiltinAssets::CubeUUID: path = "builtin://cube"; break;
            case BuiltinAssets::SphereUUID: path = "builtin://sphere"; break;
            case BuiltinAssets::CylinderUUID: path = "builtin://cylinder"; break;
            case BuiltinAssets::ConeUUID: path = "builtin://cone"; break;
            case BuiltinAssets::PlaneUUID: path = "builtin://plane"; break;
            default: path = "builtin://unknown"; break;
        }
        return AssetMetadata{ uuid, AssetType::Mesh, path, 1 };
    }

    // -------------------------------------------------------------------------
    // Path Key Normalization
    // -------------------------------------------------------------------------
    std::string AssetManager::NormalizePathKey(const std::filesystem::path& path) {
        if (IsBuiltin(path)) {
            std::string s = path.generic_string();
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        std::filesystem::path p = path;
        try {
            if (!s_AssetDirectory.empty() && p.is_absolute()) {
                p = std::filesystem::relative(p, s_AssetDirectory);
            }
        } catch (...) {}

        std::string s = p.lexically_normal().generic_string();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // -------------------------------------------------------------------------
    // Registry Path and Safe Persistence
    // -------------------------------------------------------------------------
    std::filesystem::path AssetManager::GetRegistryPath() {
        if (s_AssetDirectory.empty()) return {};
        return s_AssetDirectory / AssetRegistrySerializer::RegistryFileName;
    }

    bool AssetManager::SaveRegistryLocked(std::string* outError) {
        auto regPath = GetRegistryPath();
        if (regPath.empty()) {
            if (outError) *outError = "Asset directory not set";
            return false;
        }
        return AssetRegistrySerializer::Serialize(regPath, s_Assets, outError);
    }

    // -------------------------------------------------------------------------
    // Diagnostics and Reference Tracking
    // -------------------------------------------------------------------------
    void AssetManager::ReportDiagnostic(AssetDiagnostic::Level severity, const std::string& message, const std::filesystem::path& path, UUID uuid) {
        s_Diagnostics.push_back({ severity, message, path, uuid });
    }

    const std::vector<AssetDiagnostic>& AssetManager::GetDiagnostics() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        return s_Diagnostics;
    }

    void AssetManager::ClearDiagnostics() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_Diagnostics.clear();
    }

    bool AssetManager::HasDiagnostics() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        return !s_Diagnostics.empty();
    }

    bool AssetManager::HasDiagnosticErrors() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        for (const auto& diag : s_Diagnostics) {
            if (diag.Severity == AssetDiagnostic::Level::Error) return true;
        }
        return false;
    }

    void AssetManager::TrackReferencedAsset(UUID uuid, const std::filesystem::path& expectedPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        if (uuid != 0) {
            s_ReferencedAssets[uuid] = expectedPath;
        }
    }

    bool AssetManager::IsAssetReferenced(UUID uuid) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        return s_ReferencedAssets.find(uuid) != s_ReferencedAssets.end();
    }

    // -------------------------------------------------------------------------
    // Lifecycle Management
    // -------------------------------------------------------------------------
    void AssetManager::Init(const std::filesystem::path& assetDirectory) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_AssetDirectory = std::filesystem::absolute(assetDirectory);
        s_Assets.clear();
        s_PathToUUID.clear();
        s_ReferencedAssets.clear();
        s_Diagnostics.clear();
        ClearTextureCache();
        ClearMaterialCache();
        ClearRHIMaterialCache();
        s_Initialized = true;
        AXLE_INFO("AssetManager initialized with AssetDirectory: '{0}'", s_AssetDirectory.string());

        // Load root registry if it exists
        auto regPath = GetRegistryPath();
        if (std::filesystem::exists(regPath)) {
            std::string err;
            if (!AssetRegistrySerializer::Deserialize(regPath, s_Assets, s_PathToUUID, &s_Diagnostics, &err)) {
                AXLE_ERROR("AssetManager: Failed to deserialize asset registry '{0}': {1}", regPath.generic_string(), err);
                ReportDiagnostic(AssetDiagnostic::Level::Error, err, regPath, 0);
            }
        }
    }

    void AssetManager::Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        ClearTextureCache();
        ClearMaterialCache();
        ClearRHIMaterialCache();
        s_Assets.clear();
        s_PathToUUID.clear();
        s_ReferencedAssets.clear();
        s_Diagnostics.clear();
        s_AssetDirectory.clear();
        s_Initialized = false;
        AXLE_INFO("AssetManager shutdown.");
    }

    void AssetManager::Sync(const std::filesystem::path& directory) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path targetDir = directory.empty() ? s_AssetDirectory : directory;
        if (targetDir.empty() || !std::filesystem::exists(targetDir)) {
            return;
        }

        bool registryChanged = false;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(targetDir, ec)) {
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            if (path.filename() == AssetRegistrySerializer::RegistryFileName) continue;
            if (path.extension() == ".meta" || path.extension() == ".uid" || path.extension() == ".import") continue;

            std::string filename = path.filename().string();
            if (!filename.empty() && filename[0] == '.') continue;

            std::filesystem::path relPath = path;
            try {
                if (!s_AssetDirectory.empty() && path.is_absolute()) {
                    relPath = std::filesystem::relative(path, s_AssetDirectory);
                }
            } catch (...) {}

            std::string key = NormalizePathKey(relPath);
            if (s_PathToUUID.find(key) != s_PathToUUID.end()) {
                continue; // Already registered
            }

            // Check if this was an already-referenced asset whose registry entry is missing
            bool wasReferenced = false;
            for (const auto& [refUUID, refPath] : s_ReferencedAssets) {
                if (!refPath.empty() && NormalizePathKey(refPath) == key) {
                    std::string msg = "Missing authored registry entry for referenced asset '" + path.generic_string() +
                                      "' (expected UUID " + std::to_string(static_cast<uint64_t>(refUUID)) + ")";
                    AXLE_ERROR("AssetManager: {0}", msg);
                    ReportDiagnostic(AssetDiagnostic::Level::Error, msg, path, refUUID);
                    wasReferenced = true;
                    break;
                }
            }
            if (wasReferenced) {
                continue; // Do not silently regenerate UUID for referenced asset
            }

            // Genuinely new import: allocate fresh UUID
            UUID newHandle;
            AssetType type = GetAssetTypeFromPath(path);
            AssetMetadata meta{ newHandle, type, relPath, 1 };

            s_Assets[newHandle] = meta;
            s_PathToUUID[key] = newHandle;
            registryChanged = true;
        }

        if (registryChanged) {
            std::string saveErr;
            if (!SaveRegistryLocked(&saveErr)) {
                AXLE_ERROR("AssetManager: Failed to save registry during Sync: {0}", saveErr);
                ReportDiagnostic(AssetDiagnostic::Level::Error, "Failed to save registry during Sync: " + saveErr, GetRegistryPath(), 0);
            }
        }
    }

    AssetMetadata AssetManager::GetOrCreateMetadata(const std::filesystem::path& assetPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        if (assetPath.empty()) return AssetMetadata{};

        // Builtin primitives use canonical builtin identities; never write registry entries
        if (IsBuiltin(assetPath)) {
            return GetBuiltinMetadata(assetPath);
        }

        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            return s_Assets[it->second];
        }

        std::filesystem::path relPath = assetPath;
        try {
            if (!s_AssetDirectory.empty() && assetPath.is_absolute()) {
                relPath = std::filesystem::relative(assetPath, s_AssetDirectory);
            }
        } catch (...) {}

        // Check if this was an already-referenced asset whose registry entry is missing
        for (const auto& [refUUID, refPath] : s_ReferencedAssets) {
            if (!refPath.empty() && NormalizePathKey(refPath) == key) {
                std::string msg = "Missing authored registry entry for referenced asset '" + assetPath.generic_string() +
                                  "' (expected UUID " + std::to_string(static_cast<uint64_t>(refUUID)) + ")";
                AXLE_ERROR("AssetManager: {0}", msg);
                ReportDiagnostic(AssetDiagnostic::Level::Error, msg, assetPath, refUUID);
                return AssetMetadata{}; // Reject without regenerating
            }
        }

        // Genuinely new import: allocate a fresh UUID and atomically persist registry before publishing
        UUID newHandle;
        AssetType type = GetAssetTypeFromPath(assetPath);
        AssetMetadata meta{ newHandle, type, relPath, 1 };

        s_Assets[newHandle] = meta;
        s_PathToUUID[key] = newHandle;

        std::string writeError;
        if (!SaveRegistryLocked(&writeError)) {
            s_Assets.erase(newHandle);
            s_PathToUUID.erase(key);

            std::string msg = "Failed to atomically persist registry for new import '" + assetPath.generic_string() +
                              "': " + writeError;
            AXLE_ERROR("AssetManager: {0}", msg);
            ReportDiagnostic(AssetDiagnostic::Level::Error, msg, assetPath, newHandle);
            return AssetMetadata{};
        }

        AXLE_INFO("AssetManager: Persisted registry entry for new import '{0}' [UUID: {1}, Type: {2}]",
                  relPath.string(), static_cast<uint64_t>(newHandle), AssetTypeToString(type));
        return meta;
    }

    UUID AssetManager::GetAssetUUID(const std::filesystem::path& assetPath) {
        if (IsBuiltin(assetPath)) {
            return GetBuiltinMetadata(assetPath).Handle;
        }
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            return it->second;
        }
        return GetOrCreateMetadata(assetPath).Handle;
    }

    std::filesystem::path AssetManager::GetAssetPath(UUID uuid) {
        if (uuid == 0) return {};
        if (IsBuiltin(uuid)) {
            return GetBuiltinMetadata(uuid).FilePath;
        }

        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it != s_Assets.end()) {
            const auto& rel = it->second.FilePath;
            if (rel.is_absolute()) return rel;
            if (!s_AssetDirectory.empty()) {
                return (s_AssetDirectory / rel).lexically_normal();
            }
            return rel;
        }
        return {};
    }

    std::filesystem::path AssetManager::GetRelativeAssetPath(UUID uuid) {
        if (uuid == 0) return {};
        if (IsBuiltin(uuid)) {
            return GetBuiltinMetadata(uuid).FilePath;
        }

        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it != s_Assets.end()) {
            return it->second.FilePath;
        }
        return {};
    }

    AssetType AssetManager::GetAssetType(UUID uuid) {
        if (uuid == 0) return AssetType::None;
        if (IsBuiltin(uuid)) {
            return AssetType::Mesh;
        }

        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it != s_Assets.end()) {
            return it->second.Type;
        }
        return AssetType::None;
    }

    AssetType AssetManager::GetAssetType(const std::filesystem::path& assetPath) {
        if (assetPath.empty()) return AssetType::None;
        if (IsBuiltin(assetPath)) return AssetType::Mesh;

        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            return s_Assets[it->second].Type;
        }
        return GetAssetTypeFromPath(assetPath);
    }

    const AssetMetadata* AssetManager::GetMetadata(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it != s_Assets.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const AssetMetadata* AssetManager::GetMetadata(const std::filesystem::path& assetPath) {
        if (assetPath.empty()) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            return &s_Assets[it->second];
        }
        return nullptr;
    }

    bool AssetManager::HasAsset(UUID uuid) {
        if (uuid == 0) return false;
        if (IsBuiltin(uuid)) return true;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        return s_Assets.find(uuid) != s_Assets.end();
    }

    bool AssetManager::HasAsset(const std::filesystem::path& assetPath) {
        if (assetPath.empty()) return false;
        if (IsBuiltin(assetPath)) return true;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        return s_PathToUUID.find(key) != s_PathToUUID.end();
    }

    // -------------------------------------------------------------------------
    // External Relink
    // -------------------------------------------------------------------------
    bool AssetManager::RelinkAsset(UUID uuid, const std::filesystem::path& newPath, std::string* outError) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        if (uuid == 0 || newPath.empty()) {
            if (outError) *outError = "Invalid UUID or empty path for RelinkAsset";
            return false;
        }

        auto it = s_Assets.find(uuid);
        if (it == s_Assets.end()) {
            if (outError) *outError = "UUID not found in registry: " + std::to_string(static_cast<uint64_t>(uuid));
            return false;
        }

        std::filesystem::path relPath = newPath;
        try {
            if (!s_AssetDirectory.empty() && newPath.is_absolute()) {
                relPath = std::filesystem::relative(newPath, s_AssetDirectory);
            }
        } catch (...) {}

        std::string oldKey = NormalizePathKey(it->second.FilePath);
        std::string newKey = NormalizePathKey(relPath);

        auto destIt = s_PathToUUID.find(newKey);
        if (destIt != s_PathToUUID.end() && destIt->second != uuid) {
            if (outError) *outError = "Destination path already assigned in registry to another asset: " + newPath.generic_string();
            ReportDiagnostic(AssetDiagnostic::Level::Error, *outError, newPath, uuid);
            return false;
        }

        auto oldMeta = it->second;
        it->second.FilePath = relPath;
        s_PathToUUID.erase(oldKey);
        s_PathToUUID[newKey] = uuid;

        std::string saveErr;
        if (!SaveRegistryLocked(&saveErr)) {
            // Roll back
            it->second = oldMeta;
            s_PathToUUID.erase(newKey);
            s_PathToUUID[oldKey] = uuid;
            if (outError) *outError = "Failed to atomically persist registry during RelinkAsset: " + saveErr;
            ReportDiagnostic(AssetDiagnostic::Level::Error, *outError, newPath, uuid);
            return false;
        }

        AXLE_INFO("AssetManager: Relinked asset [UUID: {0}] to '{1}'", static_cast<uint64_t>(uuid), relPath.generic_string());
        return true;
    }

    // -------------------------------------------------------------------------
    // Relocation versus Copy
    // -------------------------------------------------------------------------
    bool AssetManager::RelocateAsset(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::string* outError) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        if (oldPath.empty() || newPath.empty()) {
            if (outError) *outError = "Paths cannot be empty for relocation";
            return false;
        }

        std::string oldKey = NormalizePathKey(oldPath);
        std::string newKey = NormalizePathKey(newPath);
        if (oldKey == newKey) return true;

        auto it = s_PathToUUID.find(oldKey);
        if (it == s_PathToUUID.end()) {
            std::filesystem::path absOld = oldPath.is_absolute() ? oldPath : (s_AssetDirectory / oldPath);
            std::string absOldKey = NormalizePathKey(absOld);
            it = s_PathToUUID.find(absOldKey);
            if (it == s_PathToUUID.end()) {
                if (outError) *outError = "Asset not found in registry for relocation: " + oldPath.string();
                return false;
            }
            oldKey = absOldKey;
        }

        UUID id = it->second;

        auto destIt = s_PathToUUID.find(newKey);
        if (destIt != s_PathToUUID.end() && destIt->second != id) {
            if (outError) *outError = "Destination path already occupied in registry: " + newPath.string();
            ReportDiagnostic(AssetDiagnostic::Level::Error, *outError, newPath, id);
            return false;
        }

        std::error_code ec;
        if (newPath.has_parent_path()) {
            std::filesystem::create_directories(newPath.parent_path(), ec);
        }

        // 1. Move physical asset if oldPath physically exists and newPath does not
        bool physicallyMoved = false;
        if (std::filesystem::exists(oldPath, ec) && !std::filesystem::exists(newPath, ec)) {
            std::filesystem::rename(oldPath, newPath, ec);
            if (ec) {
                if (outError) *outError = "Failed to rename asset file: " + ec.message();
                ReportDiagnostic(AssetDiagnostic::Level::Error, *outError, oldPath, id);
                return false;
            }
            physicallyMoved = true;
        }

        // 2. Update FilePath in registry
        std::filesystem::path relPath = newPath;
        try {
            if (!s_AssetDirectory.empty() && newPath.is_absolute()) {
                relPath = std::filesystem::relative(newPath, s_AssetDirectory);
            }
        } catch (...) {}

        auto previousMeta = s_Assets[id];
        s_Assets[id].FilePath = relPath;
        s_PathToUUID.erase(oldKey);
        s_PathToUUID[newKey] = id;

        std::string saveErr;
        if (!SaveRegistryLocked(&saveErr)) {
            // Roll back
            s_Assets[id] = previousMeta;
            s_PathToUUID.erase(newKey);
            s_PathToUUID[oldKey] = id;
            if (physicallyMoved) {
                std::filesystem::rename(newPath, oldPath, ec);
            }
            if (outError) *outError = "Failed to update registry during relocation: " + saveErr;
            ReportDiagnostic(AssetDiagnostic::Level::Error, *outError, newPath, id);
            return false;
        }

        // Update path-based caches
        auto texIt = s_TextureCacheByPath.find(oldKey);
        if (texIt != s_TextureCacheByPath.end()) {
            auto tex = texIt->second;
            s_TextureCacheByPath.erase(texIt);
            s_TextureCacheByPath[newKey] = tex;
        }
        auto matIt = s_MaterialCacheByPath.find(oldKey);
        if (matIt != s_MaterialCacheByPath.end()) {
            auto mat = matIt->second;
            s_MaterialCacheByPath.erase(matIt);
            s_MaterialCacheByPath[newKey] = mat;
        }
        auto rhiMatIt = s_RHIMaterialCacheByPath.find(oldKey);
        if (rhiMatIt != s_RHIMaterialCacheByPath.end()) {
            auto rhiMat = rhiMatIt->second;
            s_RHIMaterialCacheByPath.erase(rhiMatIt);
            s_RHIMaterialCacheByPath[newKey] = rhiMat;
        }

        AXLE_INFO("AssetManager: Relocated asset [UUID: {0}] from '{1}' to '{2}'",
                  static_cast<uint64_t>(id), oldPath.string(), newPath.string());
        return true;
    }

    bool AssetManager::CopyAsset(const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath, std::string* outError) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        if (sourcePath.empty() || destinationPath.empty()) {
            if (outError) *outError = "Paths cannot be empty for copy";
            return false;
        }

        std::string destKey = NormalizePathKey(destinationPath);
        if (s_PathToUUID.find(destKey) != s_PathToUUID.end()) {
            if (outError) *outError = "Destination path already exists in registry: " + destinationPath.string();
            return false;
        }

        std::error_code ec;
        if (destinationPath.has_parent_path()) {
            std::filesystem::create_directories(destinationPath.parent_path(), ec);
        }

        // 1. Copy physical asset file
        if (std::filesystem::exists(sourcePath, ec)) {
            std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (outError) *outError = "Failed to copy asset file: " + ec.message();
                return false;
            }
        }

        // 2. Allocate a brand-new distinct UUID for the copy
        UUID newHandle;
        AssetType type = GetAssetTypeFromPath(destinationPath);

        std::filesystem::path destRel = destinationPath;
        try {
            if (!s_AssetDirectory.empty() && destinationPath.is_absolute()) {
                destRel = std::filesystem::relative(destinationPath, s_AssetDirectory);
            }
        } catch (...) {}

        AssetMetadata copyMeta{ newHandle, type, destRel, 1 };
        s_Assets[newHandle] = copyMeta;
        s_PathToUUID[destKey] = newHandle;

        std::string writeError;
        if (!SaveRegistryLocked(&writeError)) {
            s_Assets.erase(newHandle);
            s_PathToUUID.erase(destKey);
            std::filesystem::remove(destinationPath, ec);
            if (outError) *outError = "Failed to persist registry for copied asset: " + writeError;
            return false;
        }

        AXLE_INFO("AssetManager: Copied asset '{0}' to '{1}' [New UUID: {2}]",
                  sourcePath.string(), destinationPath.string(), static_cast<uint64_t>(newHandle));
        return true;
    }

    void AssetManager::OnAssetRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
        RelocateAsset(oldPath, newPath, nullptr);
    }

    void AssetManager::OnAssetDeleted(const std::filesystem::path& assetPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            UUID id = it->second;
            s_Assets.erase(id);
            s_PathToUUID.erase(it);

            s_TextureCacheByPath.erase(key);
            s_TextureCacheByUUID.erase(id);
            s_MaterialCacheByPath.erase(key);
            s_MaterialCacheByUUID.erase(id);
            s_RHIMaterialCacheByPath.erase(key);
            s_RHIMaterialCacheByUUID.erase(id);

            SaveRegistryLocked();
        }
    }

    size_t AssetManager::GetAssetCount() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        return s_Assets.size();
    }

    const std::filesystem::path& AssetManager::GetAssetDirectory() {
        return s_AssetDirectory;
    }

    void AssetManager::SetAssetDirectory(const std::filesystem::path& dir) {
        Init(dir);
    }

    // -------------------------------------------------------------------------
    // Centralized Resource Caching
    // -------------------------------------------------------------------------
    std::shared_ptr<Texture2D> AssetManager::GetTexture(const std::filesystem::path& path) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(path);
        auto it = s_TextureCacheByPath.find(key);
        if (it != s_TextureCacheByPath.end()) return it->second;

        std::filesystem::path fullPath = path;
        if (!path.is_absolute() && !s_AssetDirectory.empty()) {
            fullPath = s_AssetDirectory / path;
        }

        if (!std::filesystem::exists(fullPath)) {
            AXLE_WARN("AssetManager::GetTexture: file not found: {0}", fullPath.string());
            return nullptr;
        }

        auto texture = Texture2D::Create(fullPath.string());
        if (texture) {
            s_TextureCacheByPath[key] = texture;
            UUID uuid = GetAssetUUID(path);
            if (uuid != 0) {
                s_TextureCacheByUUID[uuid] = texture;
            }
        }
        return texture;
    }

    std::shared_ptr<Texture2D> AssetManager::GetTexture(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_TextureCacheByUUID.find(uuid);
        if (it != s_TextureCacheByUUID.end()) return it->second;

        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return nullptr;
        return GetTexture(path);
    }

    void AssetManager::ClearTextureCache() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_TextureCacheByPath.clear();
        s_TextureCacheByUUID.clear();
    }

    std::shared_ptr<Material> AssetManager::GetMaterial(const std::filesystem::path& path) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(path);
        auto it = s_MaterialCacheByPath.find(key);
        if (it != s_MaterialCacheByPath.end()) return it->second;

        std::filesystem::path fullPath = path;
        if (!path.is_absolute() && !s_AssetDirectory.empty()) {
            fullPath = s_AssetDirectory / path;
        }

        if (!std::filesystem::exists(fullPath)) {
            AXLE_WARN("AssetManager::GetMaterial: file not found: {0}", fullPath.string());
            return nullptr;
        }

        auto mat = std::make_shared<Material>();
        if (mat->Load(fullPath)) {
            s_MaterialCacheByPath[key] = mat;
            UUID uuid = GetAssetUUID(path);
            if (uuid != 0) {
                s_MaterialCacheByUUID[uuid] = mat;
            }
            return mat;
        }
        return nullptr;
    }

    std::shared_ptr<Material> AssetManager::GetMaterial(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_MaterialCacheByUUID.find(uuid);
        if (it != s_MaterialCacheByUUID.end()) return it->second;

        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return nullptr;
        return GetMaterial(path);
    }

    void AssetManager::ReloadMaterial(UUID uuid) {
        if (uuid == 0) return;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return;

        s_MaterialCacheByUUID.erase(uuid);
        std::string key = NormalizePathKey(path);
        s_MaterialCacheByPath.erase(key);
        GetMaterial(uuid);
    }

    void AssetManager::ReloadMaterial(const std::filesystem::path& path) {
        if (path.empty()) return;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(path);
        s_MaterialCacheByPath.erase(key);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            s_MaterialCacheByUUID.erase(it->second);
        }
        GetMaterial(path);
    }

    void AssetManager::ClearMaterialCache() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_MaterialCacheByPath.clear();
        s_MaterialCacheByUUID.clear();
    }

    std::shared_ptr<RHI::RHIMaterial> AssetManager::GetRHIMaterial(const std::filesystem::path& path) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(path);
        auto it = s_RHIMaterialCacheByPath.find(key);
        if (it != s_RHIMaterialCacheByPath.end()) return it->second;

        std::filesystem::path fullPath = path;
        if (!path.is_absolute() && !s_AssetDirectory.empty()) {
            fullPath = s_AssetDirectory / path;
        }

        if (!std::filesystem::exists(fullPath)) {
            AXLE_WARN("AssetManager::GetRHIMaterial: file not found: {0}", fullPath.string());
            return nullptr;
        }

        auto rhiMat = std::make_shared<RHI::RHIMaterial>();
        if (rhiMat->Load(fullPath)) {
            s_RHIMaterialCacheByPath[key] = rhiMat;
            UUID uuid = GetAssetUUID(path);
            if (uuid != 0) {
                s_RHIMaterialCacheByUUID[uuid] = rhiMat;
            }
            return rhiMat;
        }
        return nullptr;
    }

    std::shared_ptr<RHI::RHIMaterial> AssetManager::GetRHIMaterial(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_RHIMaterialCacheByUUID.find(uuid);
        if (it != s_RHIMaterialCacheByUUID.end()) return it->second;

        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return nullptr;
        return GetRHIMaterial(path);
    }

    void AssetManager::ClearRHIMaterialCache() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_RHIMaterialCacheByPath.clear();
        s_RHIMaterialCacheByUUID.clear();
    }

}
