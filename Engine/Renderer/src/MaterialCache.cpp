#include <Axle/MaterialCache.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <mutex>
#include <unordered_map>
#include <algorithm>

namespace Axle {

    static std::recursive_mutex s_CacheMutex;
    static MaterialCache::UUIDPathResolver s_UUIDPathResolver;
    static MaterialCache::PathUUIDResolver s_PathUUIDResolver;

    void MaterialCache::SetUUIDPathResolver(UUIDPathResolver resolver) {
        std::lock_guard lock(s_CacheMutex);
        s_UUIDPathResolver = std::move(resolver);
    }

    void MaterialCache::SetPathUUIDResolver(PathUUIDResolver resolver) {
        std::lock_guard lock(s_CacheMutex);
        s_PathUUIDResolver = std::move(resolver);
    }

    static std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_TextureCacheByPath;
    static std::unordered_map<UUID, std::shared_ptr<Texture2D>> s_TextureCacheByUUID;

    static std::unordered_map<std::string, std::shared_ptr<Material>> s_MaterialCacheByPath;
    static std::unordered_map<UUID, std::shared_ptr<Material>> s_MaterialCacheByUUID;

    static std::unordered_map<std::string, std::shared_ptr<RHI::RHIMaterial>> s_RHIMaterialCacheByPath;
    static std::unordered_map<UUID, std::shared_ptr<RHI::RHIMaterial>> s_RHIMaterialCacheByUUID;

    static std::string NormalizePathKey(const std::filesystem::path& path) {
        return path.lexically_normal().generic_string();
    }

    static std::filesystem::path ResolvePath(const std::filesystem::path& path) {
        if (FileSystem::Exists(path)) return path;
        auto resolved = FileSystem::ResolveAssetPath(path);
        if (FileSystem::Exists(resolved)) return resolved;
        return path;
    }

    static void AttachMaterialTextures(const std::shared_ptr<Material>& material) {
        if (!material) return;

        if (!material->GetClearcoatPath().empty()) {
            bool enabled = material->IsClearcoatMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetClearcoatPath());
            material->SetClearcoatTexture(texture, material->GetClearcoatPath());
            material->SetUseClearcoatMap(enabled);
        }
        if (!material->GetAnisotropyFlowPath().empty()) {
            bool enabled = material->IsAnisotropyFlowMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetAnisotropyFlowPath());
            material->SetAnisotropyFlowTexture(texture, material->GetAnisotropyFlowPath());
            material->SetUseAnisotropyFlowMap(enabled);
        }
        if (!material->GetDetailNormalPath().empty()) {
            bool enabled = material->IsDetailNormalMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetDetailNormalPath());
            material->SetDetailNormalTexture(texture, material->GetDetailNormalPath());
            material->SetUseDetailNormalMap(enabled);
        }
        if (!material->GetDetailMaskPath().empty()) {
            bool enabled = material->IsDetailMaskMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetDetailMaskPath());
            material->SetDetailMaskTexture(texture, material->GetDetailMaskPath());
            material->SetUseDetailMaskMap(enabled);
        }
        if (!material->GetAlbedoPath().empty()) {
            bool enabled = material->IsAlbedoMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetAlbedoPath());
            material->SetAlbedoTexture(texture, material->GetAlbedoPath());
            material->SetUseAlbedoMap(enabled);
        }
        if (!material->GetNormalPath().empty()) {
            bool enabled = material->IsNormalMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetNormalPath());
            material->SetNormalTexture(texture, material->GetNormalPath());
            material->SetUseNormalMap(enabled);
        }
        if (!material->GetRoughnessPath().empty()) {
            bool enabled = material->IsRoughnessMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetRoughnessPath());
            material->SetRoughnessTexture(texture, material->GetRoughnessPath());
            material->SetUseRoughnessMap(enabled);
        }
        if (!material->GetMetallicPath().empty()) {
            bool enabled = material->IsMetallicMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetMetallicPath());
            material->SetMetallicTexture(texture, material->GetMetallicPath());
            material->SetUseMetallicMap(enabled);
        }
        if (!material->GetAOPath().empty()) {
            bool enabled = material->IsAOMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetAOPath());
            material->SetAOTexture(texture, material->GetAOPath());
            material->SetUseAOMap(enabled);
        }
        if (!material->GetEmissivePath().empty()) {
            bool enabled = material->IsEmissiveMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetEmissivePath());
            material->SetEmissiveTexture(texture, material->GetEmissivePath());
            material->SetUseEmissiveMap(enabled);
        }
        if (!material->GetORMPath().empty()) {
            bool enabled = material->IsORMMapEnabled();
            auto texture = MaterialCache::GetTexture(material->GetORMPath());
            material->SetORMTexture(texture, material->GetORMPath());
            material->SetUseORMMap(enabled);
        }
    }

    std::shared_ptr<Texture2D> MaterialCache::GetTexture(const std::filesystem::path& path) {
        if (path.empty()) return nullptr;
        std::lock_guard lock(s_CacheMutex);
        const auto resolved = ResolvePath(path);
        const auto key = std::to_string(FileSystem::GetSourceGeneration()) + ":" + NormalizePathKey(resolved);
        const auto prior = s_TextureCacheByPath.find(key);
        UUID identity = 0;
        if (path.extension() == ".axleasset" && s_PathUUIDResolver) {
            identity = s_PathUUIDResolver(path);
            if (identity == 0) { s_TextureCacheByPath.erase(key); return nullptr; }
        }
        auto texture = Texture2D::Create(resolved.string());
        if (texture && texture->IsLoaded()) {
            s_TextureCacheByPath[key] = texture;
            uint64_t id = 0;
            if (FileSystem::ReadFileIdentity(resolved, id) && id) identity = UUID(id);
            if (identity != 0) s_TextureCacheByUUID[identity] = texture;
            return texture;
        }
        return prior != s_TextureCacheByPath.end() && prior->second && prior->second->IsLoaded() ? prior->second : nullptr;
    }

    std::shared_ptr<Texture2D> MaterialCache::GetTexture(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard lock(s_CacheMutex);

        if (s_UUIDPathResolver && s_UUIDPathResolver(uuid).empty()) {
            s_TextureCacheByUUID.erase(uuid);
            return nullptr;
        }

        if (s_UUIDPathResolver) {
            auto path = s_UUIDPathResolver(uuid);
            if (!path.empty()) return GetTexture(path);
        }
        auto cached = s_TextureCacheByUUID.find(uuid);
        return cached != s_TextureCacheByUUID.end() && cached->second && cached->second->IsLoaded() ? cached->second : nullptr;
    }

    void MaterialCache::ClearTextureCache() {
        std::lock_guard lock(s_CacheMutex);
        s_TextureCacheByPath.clear();
        s_TextureCacheByUUID.clear();
    }

    std::shared_ptr<Material> MaterialCache::GetMaterial(const std::filesystem::path& path) {
        if (path.empty()) return nullptr;
        std::lock_guard lock(s_CacheMutex);

        UUID identity = 0;
        if (s_PathUUIDResolver) {
            identity = s_PathUUIDResolver(path);
            if (identity == 0) {
                std::string key = NormalizePathKey(path);
                s_MaterialCacheByPath.erase(key);
                return nullptr;
            }
        }

        auto resolved = ResolvePath(path);
        if (!FileSystem::Exists(resolved)) {
            AXLE_WARN("MaterialCache::GetMaterial: file not found: {0}", resolved.string());
            return nullptr;
        }

        if (identity == 0) {
            uint64_t id = 0;
            if (FileSystem::ReadFileIdentity(resolved, id)) {
                identity = UUID(id);
            }
        }

        std::string key = NormalizePathKey(path);
        auto it = s_MaterialCacheByPath.find(key);
        if (it != s_MaterialCacheByPath.end()) {
            if (it->second && (identity == 0 || it->second->GetHandle() == identity)) {
                return it->second;
            }
            s_MaterialCacheByPath.erase(it);
        }

        if (identity != 0) {
            auto cached = s_MaterialCacheByUUID.find(identity);
            if (cached != s_MaterialCacheByUUID.end()) {
                cached->second->SetFilePath(resolved);
                s_MaterialCacheByPath[key] = cached->second;
                return cached->second;
            }
        }

        auto mat = std::make_shared<Material>();
        if (mat->Load(resolved)) {
            if (identity != 0) mat->SetHandle(identity);
            AttachMaterialTextures(mat);

            s_MaterialCacheByPath[key] = mat;
            std::string resolvedKey = NormalizePathKey(resolved);
            if (resolvedKey != key) s_MaterialCacheByPath[resolvedKey] = mat;

            if (mat->GetHandle() != 0) {
                s_MaterialCacheByUUID[mat->GetHandle()] = mat;
            }
            return mat;
        }
        return nullptr;
    }

    std::shared_ptr<Material> MaterialCache::GetMaterial(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard lock(s_CacheMutex);

        if (s_UUIDPathResolver && s_UUIDPathResolver(uuid).empty()) {
            s_MaterialCacheByUUID.erase(uuid);
            return nullptr;
        }

        auto it = s_MaterialCacheByUUID.find(uuid);
        if (it != s_MaterialCacheByUUID.end()) return it->second;

        if (s_UUIDPathResolver) {
            auto path = s_UUIDPathResolver(uuid);
            if (!path.empty()) return GetMaterial(path);
        }
        return nullptr;
    }

    void MaterialCache::ReloadMaterial(UUID uuid) {
        if (uuid == 0) return;
        std::lock_guard lock(s_CacheMutex);

        auto it = s_MaterialCacheByUUID.find(uuid);
        if (it != s_MaterialCacheByUUID.end() && it->second) {
            ReloadMaterial(it->second->GetFilePath());
        }
    }

    void MaterialCache::ReloadMaterial(const std::filesystem::path& path) {
        if (path.empty()) return;
        std::lock_guard lock(s_CacheMutex);

        auto resolved = ResolvePath(path);
        if (!FileSystem::Exists(resolved)) return;

        uint64_t id = 0;
        FileSystem::ReadFileIdentity(resolved, id);
        UUID uuid = UUID(id);

        std::string key = NormalizePathKey(path);
        std::string resolvedKey = NormalizePathKey(resolved);

        std::shared_ptr<Material> material;
        if (auto it = s_MaterialCacheByPath.find(key); it != s_MaterialCacheByPath.end()) material = it->second;
        if (!material) {
            if (auto it = s_MaterialCacheByPath.find(resolvedKey); it != s_MaterialCacheByPath.end()) material = it->second;
        }
        if (!material && uuid != 0) {
            if (auto it = s_MaterialCacheByUUID.find(uuid); it != s_MaterialCacheByUUID.end()) material = it->second;
        }

        if (material) {
            auto replacement = std::make_shared<Material>(*material);
            if (replacement->Load(resolved)) {
                if (uuid != 0) replacement->SetHandle(uuid);
                AttachMaterialTextures(replacement);
                *material = std::move(*replacement);
            }
        }

        std::shared_ptr<RHI::RHIMaterial> rhiMat;
        if (auto it = s_RHIMaterialCacheByPath.find(key); it != s_RHIMaterialCacheByPath.end()) rhiMat = it->second;
        if (!rhiMat) {
            if (auto it = s_RHIMaterialCacheByPath.find(resolvedKey); it != s_RHIMaterialCacheByPath.end()) rhiMat = it->second;
        }
        if (!rhiMat && uuid != 0) {
            if (auto it = s_RHIMaterialCacheByUUID.find(uuid); it != s_RHIMaterialCacheByUUID.end()) rhiMat = it->second;
        }

        if (rhiMat) {
            rhiMat->Load(resolved);
        }
    }

    void MaterialCache::ClearMaterialCache() {
        std::lock_guard lock(s_CacheMutex);
        s_MaterialCacheByPath.clear();
        s_MaterialCacheByUUID.clear();
    }

    std::shared_ptr<RHI::RHIMaterial> MaterialCache::GetRHIMaterial(const std::filesystem::path& path) {
        if (path.empty()) return nullptr;
        std::lock_guard lock(s_CacheMutex);

        UUID identity = 0;
        if (s_PathUUIDResolver) {
            identity = s_PathUUIDResolver(path);
            if (identity == 0) {
                std::string key = NormalizePathKey(path);
                s_RHIMaterialCacheByPath.erase(key);
                return nullptr;
            }
        }

        auto resolved = ResolvePath(path);
        if (!FileSystem::Exists(resolved)) {
            AXLE_WARN("MaterialCache::GetRHIMaterial: file not found: {0}", resolved.string());
            return nullptr;
        }

        if (identity == 0) {
            uint64_t id = 0;
            if (FileSystem::ReadFileIdentity(resolved, id)) {
                identity = UUID(id);
            }
        }

        std::string key = NormalizePathKey(path);
        if (identity != 0) {
            auto cached = s_RHIMaterialCacheByUUID.find(identity);
            if (cached != s_RHIMaterialCacheByUUID.end()) {
                s_RHIMaterialCacheByPath[key] = cached->second;
                return cached->second;
            }
        }

        auto it = s_RHIMaterialCacheByPath.find(key);
        if (it != s_RHIMaterialCacheByPath.end()) {
            s_RHIMaterialCacheByPath.erase(it);
        }

        auto rhiMat = std::make_shared<RHI::RHIMaterial>();
        if (rhiMat->Load(resolved)) {
            s_RHIMaterialCacheByPath[key] = rhiMat;
            std::string resolvedKey = NormalizePathKey(resolved);
            if (resolvedKey != key) s_RHIMaterialCacheByPath[resolvedKey] = rhiMat;

            if (identity != 0) {
                s_RHIMaterialCacheByUUID[identity] = rhiMat;
            }
            return rhiMat;
        }
        return nullptr;
    }

    std::shared_ptr<RHI::RHIMaterial> MaterialCache::GetRHIMaterial(UUID uuid) {
        if (uuid == 0) return nullptr;
        std::lock_guard lock(s_CacheMutex);

        if (s_UUIDPathResolver && s_UUIDPathResolver(uuid).empty()) {
            s_RHIMaterialCacheByUUID.erase(uuid);
            return nullptr;
        }

        auto it = s_RHIMaterialCacheByUUID.find(uuid);
        if (it != s_RHIMaterialCacheByUUID.end()) return it->second;

        if (s_UUIDPathResolver) {
            auto path = s_UUIDPathResolver(uuid);
            if (!path.empty()) return GetRHIMaterial(path);
        }
        return nullptr;
    }

    void MaterialCache::ClearRHIMaterialCache() {
        std::lock_guard lock(s_CacheMutex);
        s_RHIMaterialCacheByPath.clear();
        s_RHIMaterialCacheByUUID.clear();
    }

    void MaterialCache::ClearAll() {
        ClearTextureCache();
        ClearMaterialCache();
        ClearRHIMaterialCache();
    }

}

