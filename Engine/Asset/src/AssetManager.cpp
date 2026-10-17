#include <Axle/AssetManager.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>

namespace Axle {

    std::filesystem::path AssetManager::s_AssetDirectory;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Assets;
    std::unordered_map<std::string, UUID> AssetManager::s_PathToUUID;
    std::unordered_map<std::string, std::shared_ptr<Texture2D>> AssetManager::s_TextureCacheByPath;
    std::unordered_map<UUID, std::shared_ptr<Texture2D>> AssetManager::s_TextureCacheByUUID;
    std::unordered_map<UUID, std::shared_ptr<Material>> AssetManager::s_MaterialCacheByUUID;
    std::unordered_map<std::string, std::shared_ptr<Material>> AssetManager::s_MaterialCacheByPath;
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
        ClearTextureCache();
        ClearMaterialCache();
        s_Initialized = true;
        AXLE_INFO("AssetManager initialized with AssetDirectory: '{0}'", s_AssetDirectory.string());
        Sync();
    }

    void AssetManager::Shutdown() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        ClearTextureCache();
        ClearMaterialCache();
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
            s_TextureCacheByUUID.erase(it->second);
            s_MaterialCacheByUUID.erase(it->second);
            s_Assets.erase(it->second);
            s_PathToUUID.erase(it);
        }
        s_TextureCacheByPath.erase(key);
        s_MaterialCacheByPath.erase(key);
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

    // -------------------------------------------------------------------------
    // Centralized TextureCache
    // -------------------------------------------------------------------------
    std::shared_ptr<Texture2D> AssetManager::GetTexture(const std::filesystem::path& path) {
        if (path.empty()) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);

        std::string key = NormalizePathKey(path);
        auto it = s_TextureCacheByPath.find(key);
        if (it != s_TextureCacheByPath.end() && it->second && it->second->IsLoaded()) {
            return it->second;
        }

        // Check if there is an associated UUID
        auto uuidIt = s_PathToUUID.find(key);
        if (uuidIt != s_PathToUUID.end()) {
            auto texIt = s_TextureCacheByUUID.find(uuidIt->second);
            if (texIt != s_TextureCacheByUUID.end() && texIt->second && texIt->second->IsLoaded()) {
                s_TextureCacheByPath[key] = texIt->second;
                return texIt->second;
            }
        }

        // Only create texture if an active graphics context exists
        if (glfwGetCurrentContext() == nullptr) {
            return nullptr;
        }

        // Resolve path through FileSystem (supporting VFS & disk)
        std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
        auto texture = Texture2D::Create(resolved.string());
        if (texture && texture->IsLoaded()) {
            s_TextureCacheByPath[key] = texture;
            if (uuidIt != s_PathToUUID.end()) {
                s_TextureCacheByUUID[uuidIt->second] = texture;
            }
            return texture;
        }

        return nullptr;
    }

    std::shared_ptr<Texture2D> AssetManager::GetTexture(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);

        auto it = s_TextureCacheByUUID.find(uuid);
        if (it != s_TextureCacheByUUID.end() && it->second && it->second->IsLoaded()) {
            return it->second;
        }

        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return nullptr;

        auto tex = GetTexture(path);
        if (tex) {
            s_TextureCacheByUUID[uuid] = tex;
        }
        return tex;
    }

    void AssetManager::ClearTextureCache() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_TextureCacheByPath.clear();
        s_TextureCacheByUUID.clear();
    }

    // -------------------------------------------------------------------------
    // Centralized MaterialCache
    // -------------------------------------------------------------------------
    std::shared_ptr<Material> AssetManager::GetMaterial(const std::filesystem::path& path) {
        if (path.empty()) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);

        std::string key = NormalizePathKey(path);
        auto it = s_MaterialCacheByPath.find(key);
        if (it != s_MaterialCacheByPath.end() && it->second) {
            return it->second;
        }

        UUID uuid = GetAssetUUID(path);
        if (uuid != 0) {
            auto matIt = s_MaterialCacheByUUID.find(uuid);
            if (matIt != s_MaterialCacheByUUID.end() && matIt->second) {
                s_MaterialCacheByPath[key] = matIt->second;
                return matIt->second;
            }
        }

        // Create and load material
        std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
        auto material = Material::Create(path.stem().string(), uuid);
        material->SetFilePath(path);
        if (!material->Load(resolved)) {
            return nullptr;
        }

        // Attach textures through centralized TextureCache
        if (!material->GetAlbedoPath().empty()) {
            auto tex = GetTexture(material->GetAlbedoPath());
            if (tex) material->SetAlbedoTexture(tex, material->GetAlbedoPath());
        }
        if (!material->GetNormalPath().empty()) {
            auto tex = GetTexture(material->GetNormalPath());
            if (tex) material->SetNormalTexture(tex, material->GetNormalPath());
        }
        if (!material->GetRoughnessPath().empty()) {
            auto tex = GetTexture(material->GetRoughnessPath());
            if (tex) material->SetRoughnessTexture(tex, material->GetRoughnessPath());
        }
        if (!material->GetMetallicPath().empty()) {
            auto tex = GetTexture(material->GetMetallicPath());
            if (tex) material->SetMetallicTexture(tex, material->GetMetallicPath());
        }

        s_MaterialCacheByPath[key] = material;
        if (uuid != 0) {
            s_MaterialCacheByUUID[uuid] = material;
        }

        return material;
    }

    std::shared_ptr<Material> AssetManager::GetMaterial(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);

        auto it = s_MaterialCacheByUUID.find(uuid);
        if (it != s_MaterialCacheByUUID.end() && it->second) {
            return it->second;
        }

        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return nullptr;

        auto mat = GetMaterial(path);
        if (mat) {
            s_MaterialCacheByUUID[uuid] = mat;
        }
        return mat;
    }

    void AssetManager::ReloadMaterial(UUID uuid) {
        if (uuid == 0) return;
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);

        auto it = s_MaterialCacheByUUID.find(uuid);
        if (it != s_MaterialCacheByUUID.end() && it->second) {
            std::filesystem::path path = it->second->GetFilePath();
            if (path.empty()) path = GetAssetPath(uuid);
            if (!path.empty()) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                it->second->Load(resolved);

                if (!it->second->GetAlbedoPath().empty()) {
                    auto tex = GetTexture(it->second->GetAlbedoPath());
                    if (tex) it->second->SetAlbedoTexture(tex, it->second->GetAlbedoPath());
                }
                if (!it->second->GetNormalPath().empty()) {
                    auto tex = GetTexture(it->second->GetNormalPath());
                    if (tex) it->second->SetNormalTexture(tex, it->second->GetNormalPath());
                }
                if (!it->second->GetRoughnessPath().empty()) {
                    auto tex = GetTexture(it->second->GetRoughnessPath());
                    if (tex) it->second->SetRoughnessTexture(tex, it->second->GetRoughnessPath());
                }
                if (!it->second->GetMetallicPath().empty()) {
                    auto tex = GetTexture(it->second->GetMetallicPath());
                    if (tex) it->second->SetMetallicTexture(tex, it->second->GetMetallicPath());
                }
            }
        }
    }

    void AssetManager::ReloadMaterial(const std::filesystem::path& path) {
        UUID uuid = GetAssetUUID(path);
        if (uuid != 0) {
            ReloadMaterial(uuid);
        } else {
            std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
            std::string key = NormalizePathKey(path);
            auto it = s_MaterialCacheByPath.find(key);
            if (it != s_MaterialCacheByPath.end() && it->second) {
                std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
                it->second->Load(resolved);
            }
        }
    }

    void AssetManager::ClearMaterialCache() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_MaterialCacheByPath.clear();
        s_MaterialCacheByUUID.clear();
    }

}
