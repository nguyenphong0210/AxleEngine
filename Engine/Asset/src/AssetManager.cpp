#include <Axle/AssetManager.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <cctype>

namespace Axle {

    std::filesystem::path AssetManager::s_AssetDirectory;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Assets;
    std::unordered_map<std::string, UUID> AssetManager::s_PathToUUID;
    std::recursive_mutex AssetManager::s_AssetMutex;
    bool AssetManager::s_Initialized = false;

    std::string AssetManager::NormalizePathKey(const std::filesystem::path& path) {
        std::filesystem::path p = path;
        try {
            if (!s_AssetDirectory.empty() && p.is_absolute()) {
                p = std::filesystem::relative(p, s_AssetDirectory);
            }
        } catch (...) {}

        std::string s = p.generic_string();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    void AssetManager::Init(const std::filesystem::path& assetDirectory) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_AssetDirectory = std::filesystem::absolute(assetDirectory);
        s_Assets.clear();
        s_PathToUUID.clear();
        s_Initialized = true;
        AXLE_INFO("AssetManager initialized with AssetDirectory: '{0}'", s_AssetDirectory.string());
        Sync();
    }

    void AssetManager::Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_Assets.clear();
        s_PathToUUID.clear();
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

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(targetDir, ec)) {
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            if (path.extension() == ".meta") continue;

            std::string filename = path.filename().string();
            if (!filename.empty() && filename[0] == '.') continue;

            GetOrCreateMetadata(path);
        }
    }

    AssetMetadata AssetManager::GetOrCreateMetadata(const std::filesystem::path& assetPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        if (assetPath.empty()) return AssetMetadata{};

        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            return s_Assets[it->second];
        }

        std::filesystem::path metaPath = AssetMetadataSerializer::GetMetaPath(assetPath);
        std::filesystem::path relPath = assetPath;
        try {
            if (!s_AssetDirectory.empty()) {
                relPath = std::filesystem::relative(assetPath, s_AssetDirectory);
            }
        } catch (...) {}

        if (std::filesystem::exists(metaPath)) {
            AssetMetadata meta;
            if (AssetMetadataSerializer::Deserialize(metaPath, meta)) {
                meta.FilePath = relPath;
                s_Assets[meta.Handle] = meta;
                s_PathToUUID[key] = meta.Handle;
                return meta;
            }
        }

        // Companion .meta does not exist or failed to parse -> auto-generate fresh persistent UUID
        UUID newHandle;
        AssetType type = GetAssetTypeFromPath(assetPath);
        AssetMetadata meta{ newHandle, type, relPath };

        AssetMetadataSerializer::Serialize(metaPath, meta);
        s_Assets[newHandle] = meta;
        s_PathToUUID[key] = newHandle;

        AXLE_INFO("AssetManager: Generated companion .meta for '{0}' [UUID: {1}, Type: {2}]",
                  relPath.string(), (uint64_t)newHandle, AssetTypeToString(type));
        return meta;
    }

    UUID AssetManager::GetAssetUUID(const std::filesystem::path& assetPath) {
        return GetOrCreateMetadata(assetPath).Handle;
    }

    std::filesystem::path AssetManager::GetAssetPath(UUID uuid) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it == s_Assets.end()) return {};

        const auto& rel = it->second.FilePath;
        if (rel.is_absolute()) return rel;
        return s_AssetDirectory / rel;
    }

    std::filesystem::path AssetManager::GetRelativeAssetPath(UUID uuid) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it == s_Assets.end()) return {};
        return it->second.FilePath;
    }

    AssetType AssetManager::GetAssetType(UUID uuid) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it == s_Assets.end()) return AssetType::None;
        return it->second.Type;
    }

    AssetType AssetManager::GetAssetType(const std::filesystem::path& assetPath) {
        return GetOrCreateMetadata(assetPath).Type;
    }

    const AssetMetadata* AssetManager::GetMetadata(UUID uuid) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        auto it = s_Assets.find(uuid);
        if (it == s_Assets.end()) return nullptr;
        return &it->second;
    }

    const AssetMetadata* AssetManager::GetMetadata(const std::filesystem::path& assetPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            return &s_Assets[it->second];
        }

        AssetMetadata meta = GetOrCreateMetadata(assetPath);
        if (meta.IsValid()) {
            return &s_Assets[meta.Handle];
        }
        return nullptr;
    }

    bool AssetManager::HasAsset(UUID uuid) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        return s_Assets.find(uuid) != s_Assets.end();
    }

    bool AssetManager::HasAsset(const std::filesystem::path& assetPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::string key = NormalizePathKey(assetPath);
        return s_PathToUUID.find(key) != s_PathToUUID.end();
    }

    void AssetManager::OnAssetRenamed(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path oldMeta = AssetMetadataSerializer::GetMetaPath(oldPath);
        std::filesystem::path newMeta = AssetMetadataSerializer::GetMetaPath(newPath);

        std::error_code ec;
        if (std::filesystem::exists(oldMeta, ec)) {
            std::filesystem::rename(oldMeta, newMeta, ec);
        }

        std::string oldKey = NormalizePathKey(oldPath);
        auto it = s_PathToUUID.find(oldKey);
        if (it != s_PathToUUID.end()) {
            UUID id = it->second;
            s_PathToUUID.erase(it);

            std::string newKey = NormalizePathKey(newPath);
            s_PathToUUID[newKey] = id;

            std::filesystem::path relPath = newPath;
            try {
                if (!s_AssetDirectory.empty()) {
                    relPath = std::filesystem::relative(newPath, s_AssetDirectory);
                }
            } catch (...) {}
            s_Assets[id].FilePath = relPath;
        }
    }

    void AssetManager::OnAssetDeleted(const std::filesystem::path& assetPath) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path metaPath = AssetMetadataSerializer::GetMetaPath(assetPath);

        std::error_code ec;
        if (std::filesystem::exists(metaPath, ec)) {
            std::filesystem::remove(metaPath, ec);
        }

        std::string key = NormalizePathKey(assetPath);
        auto it = s_PathToUUID.find(key);
        if (it != s_PathToUUID.end()) {
            s_Assets.erase(it->second);
            s_PathToUUID.erase(it);
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
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_AssetDirectory = std::filesystem::absolute(dir);
    }

}
