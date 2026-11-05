#include <Axle/AssetManager.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace Axle {
    std::filesystem::path AssetManager::s_AssetDirectory;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Assets;
    std::unordered_map<std::string, UUID> AssetManager::s_PathToUUID;
    std::unordered_map<std::string, AssetMetadata> AssetManager::s_MetadataByPath;
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

    namespace {
        bool Within(const std::filesystem::path& path, const std::filesystem::path& root) {
            auto rel = path.lexically_relative(root);
            return !rel.empty() && !rel.is_absolute() && *rel.begin() != "..";
        }
        bool ResolveOwned(const std::filesystem::path& root, const std::filesystem::path& path,
                          std::filesystem::path& absolute, std::filesystem::path& relative, std::string* error) {
            auto fail = [&] { if (error) *error = "Asset path is empty or outside its configured root: " + path.generic_string(); return false; };
            if (root.empty() || path.empty()) return fail();
            std::error_code ec;
            auto candidate = path;
            if (!path.is_absolute()) {
                auto cwdPath = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
                candidate = !ec && Within(cwdPath, root) ? cwdPath : root/path;
            }
            absolute = std::filesystem::weakly_canonical(candidate, ec);
            if (ec || !Within(absolute, root) || absolute == root) return fail();
            relative = absolute.lexically_relative(root);
            return true;
        }
        std::string BuiltinName(const std::filesystem::path& path) {
            auto name = path.generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
            if (name.rfind("builtin:", 0) != 0) return {};
            name.erase(0, 8);
            while (!name.empty() && name.front() == '/') name.erase(name.begin());
            if (name == "cube" || name == "sphere" || name == "cylinder" || name == "cone" || name == "plane") return name;
            return {};
        }
        std::string PathKey(const std::filesystem::path& path) {
            auto key = path.lexically_normal().generic_string();
#ifdef _WIN32
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c); });
#endif
            return key;
        }
        struct PathEdit { std::filesystem::path path; std::string before, after; };
        // Only recognized serialized path fields are rewritten, not arbitrary names/comments.
        std::string RewritePaths(const std::string& text, const std::string& oldPath, const std::string& newPath) {
            static const std::regex property(R"(^([ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*:[ \t]*)(.*?)(\r?)$)");
            std::istringstream input(text);
            std::string line, output;
            while (std::getline(input, line)) {
                std::smatch match;
                if (std::regex_match(line, match, property)) {
                    auto key = match[2].str(), value = match[3].str();
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c); });
                    const bool pathField = key == "source" || key == "texture" || key == "font" ||
                        key.ends_with("path") || key.ends_with("map");
                    const bool quoted = value.size() >= 2 && value.front() == '"' && value.back() == '"';
                    auto plain = quoted ? value.substr(1, value.size()-2) : value;
                    if (pathField && PathKey(plain) == PathKey(oldPath)) {
                        line = match[1].str() + (quoted ? "\"" + newPath + "\"" : newPath) + match[4].str();
                    }
                }
                output += line;
                if (!input.eof()) output += "\n";
            }
            return output;
        }
        bool CollectPathEdits(const std::filesystem::path& root, const std::string& oldPath,
                              const std::string& newPath, std::vector<PathEdit>& edits, std::string* error) {
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
                if (!it->is_regular_file() || !AssetMetadataSerializer::IsNativeAsset(it->path())) continue;
                auto before = FileSystem::ReadTextFile(it->path());
                auto after = RewritePaths(before, oldPath, newPath);
                if (after != before) edits.push_back({it->path(), before, after});
            }
            if (ec) { if (error) *error = ec.message(); return false; }
            return true;
        }
        bool RollbackEdits(const std::vector<PathEdit>& edits, size_t count, std::string* error) {
            bool success = true;
            while (count) {
                const auto& edit = edits[--count];
                std::string failure;
                if (!FileSystem::WriteTextFileAtomically(edit.path, edit.before, &failure)) {
                    success = false;
                    if (error) *error += "; rollback failed for " + edit.path.string() + ": " + failure;
                }
            }
            return success;
        }
    }

    bool AssetManager::IsBuiltin(const std::filesystem::path& path) { return !BuiltinName(path).empty(); }
    bool AssetManager::IsBuiltin(UUID id) {
        return uint64_t(id) >= BuiltinAssets::CubeUUID && uint64_t(id) <= BuiltinAssets::PlaneUUID;
    }
    AssetMetadata AssetManager::GetBuiltinMetadata(const std::filesystem::path& path) {
        auto name = BuiltinName(path);
        if (name.empty()) return {};
        uint64_t id = name == "cube" ? BuiltinAssets::CubeUUID : name == "sphere" ? BuiltinAssets::SphereUUID :
                      name == "cylinder" ? BuiltinAssets::CylinderUUID : name == "cone" ? BuiltinAssets::ConeUUID : BuiltinAssets::PlaneUUID;
        return {UUID(id), AssetType::Mesh, "builtin://" + name, 1};
    }
    AssetMetadata AssetManager::GetBuiltinMetadata(UUID id) {
        if (!IsBuiltin(id)) return {};
        static constexpr const char* names[] = {"cube","sphere","cylinder","cone","plane"};
        return {id, AssetType::Mesh, std::string("builtin://")+names[uint64_t(id)-BuiltinAssets::CubeUUID], 1};
    }
    std::string AssetManager::NormalizePathKey(const std::filesystem::path& path) {
        if (IsBuiltin(path)) return GetBuiltinMetadata(path).FilePath.generic_string();
        std::filesystem::path full, relative;
        return ResolveOwned(s_AssetDirectory, path, full, relative, nullptr) ? PathKey(relative) : std::string{};
    }
    std::filesystem::path AssetManager::GetReferencePath(const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex);
        if (IsBuiltin(path)) return GetBuiltinMetadata(path).FilePath;
        std::filesystem::path full, relative;
        return ResolveOwned(s_AssetDirectory, path, full, relative, nullptr) ? relative : std::filesystem::path{};
    }
    void AssetManager::ReportDiagnostic(AssetDiagnostic::Level severity, const std::string& message,
                                        const std::filesystem::path& path, UUID id) {
        std::lock_guard lock(s_AssetMutex);
        s_Diagnostics.push_back({severity, message, path, id});
    }
    std::vector<AssetDiagnostic> AssetManager::GetDiagnostics() { std::lock_guard lock(s_AssetMutex); return s_Diagnostics; }
    void AssetManager::ClearDiagnostics() { std::lock_guard lock(s_AssetMutex); s_Diagnostics.clear(); }
    bool AssetManager::HasDiagnostics() { std::lock_guard lock(s_AssetMutex); return !s_Diagnostics.empty(); }
    bool AssetManager::HasDiagnosticErrors() {
        std::lock_guard lock(s_AssetMutex);
        return std::any_of(s_Diagnostics.begin(),s_Diagnostics.end(),[](const auto& d){ return d.Severity == AssetDiagnostic::Level::Error; });
    }
    void AssetManager::TrackReferencedAsset(UUID id, const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex);
        if (id != 0) s_ReferencedAssets[id] = path;
    }
    bool AssetManager::IsAssetReferenced(UUID id) { std::lock_guard lock(s_AssetMutex); return s_ReferencedAssets.contains(id); }

    void AssetManager::Init(const std::filesystem::path& root) {
        std::lock_guard lock(s_AssetMutex);
        Shutdown();
        if (root.empty()) return;
        s_AssetDirectory = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
        FileSystem::SetAssetDirectory(s_AssetDirectory);
        s_Initialized = true;
        Sync();
    }
    void AssetManager::Shutdown() {
        std::lock_guard lock(s_AssetMutex);
        ClearTextureCache(); ClearMaterialCache(); ClearRHIMaterialCache();
        s_Assets.clear(); s_PathToUUID.clear(); s_MetadataByPath.clear(); s_ReferencedAssets.clear(); s_Diagnostics.clear();
        s_AssetDirectory.clear(); s_Initialized = false;
    }
    void AssetManager::Sync(const std::filesystem::path& directory) {
        std::lock_guard lock(s_AssetMutex);
        if (!s_Initialized) return;
        // Discovery is read-only. Rebuild the complete index, including native files moved outside Editor.
        std::unordered_map<UUID,AssetMetadata> assets;
        std::unordered_map<std::string,UUID> paths;
        std::unordered_map<std::string,AssetMetadata> metadata;
        std::set<uint64_t> duplicates;
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        if (const auto* package = FileSystem::GetMountedPackage()) {
            for (const auto& [path, entry] : package->GetEntries()) files.push_back(s_AssetDirectory/path);
        } else if (std::filesystem::is_directory(s_AssetDirectory)) {
            for (std::filesystem::recursive_directory_iterator it(s_AssetDirectory,ec), end; it != end && !ec; it.increment(ec))
                if (it->is_regular_file()) files.push_back(it->path());
        }
        for (const auto& file : files) {
            std::filesystem::path full, relative;
            if (!ResolveOwned(s_AssetDirectory,file,full,relative,nullptr)) continue;
            auto type = GetAssetTypeFromPath(full);
            if (type == AssetType::None) continue;
            AssetMetadata item{UUID(0),type,relative,1};
            const auto key = PathKey(relative);
            if (AssetMetadataSerializer::IsNativeAsset(full)) {
                std::string error;
                if (!AssetMetadataSerializer::Deserialize(full,item,&error)) {
                    ReportDiagnostic(AssetDiagnostic::Level::Error,error,relative); continue;
                }
                item.FilePath = relative;
                if (duplicates.contains(uint64_t(item.Handle))) continue;
                auto existing = assets.find(item.Handle);
                if (existing != assets.end()) {
                    paths.erase(PathKey(existing->second.FilePath));
                    metadata.erase(PathKey(existing->second.FilePath));
                    assets.erase(existing);
                    duplicates.insert(uint64_t(item.Handle));
                    ReportDiagnostic(AssetDiagnostic::Level::Error,"Duplicate embedded asset UUID; all copies are unresolved",relative,item.Handle);
                    continue;
                }
                assets[item.Handle] = item;
                paths[key] = item.Handle;
            }
            metadata[key] = item;
        }
        if (ec) { ReportDiagnostic(AssetDiagnostic::Level::Error,ec.message(),s_AssetDirectory); return; }
        s_Assets.swap(assets); s_PathToUUID.swap(paths); s_MetadataByPath.swap(metadata);
        for (const auto& [id,path] : s_ReferencedAssets)
            if (!IsBuiltin(id) && !s_Assets.contains(id))
                ReportDiagnostic(AssetDiagnostic::Level::Error,"Referenced native asset UUID is unresolved",path,id);
    }
    AssetMetadata AssetManager::GetOrCreateMetadata(const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex);
        if (IsBuiltin(path)) return GetBuiltinMetadata(path);
        std::filesystem::path full, relative;
        std::string error;
        if (!ResolveOwned(s_AssetDirectory,path,full,relative,&error) || !std::filesystem::is_regular_file(full)) {
            ReportDiagnostic(AssetDiagnostic::Level::Error,error.empty() ? "Asset file does not exist" : error,path); return {};
        }
        auto type = GetAssetTypeFromPath(full);
        if (type == AssetType::None) return {};
        if (!AssetMetadataSerializer::IsNativeAsset(full)) {
            AssetMetadata raw{UUID(0),type,relative,1};
            s_MetadataByPath[PathKey(relative)] = raw;
            return raw;
        }
        AssetMetadata item;
        uint64_t existingID = 0;
        if (!FileSystem::ReadFileIdentity(full, existingID, nullptr)) {
            // Explicit creation/import may identify a new document; discovery and UUID lookup never do.
            for (const auto& [id,expected] : s_ReferencedAssets)
                if (!expected.empty() && NormalizePathKey(expected) == PathKey(relative)) {
                    ReportDiagnostic(AssetDiagnostic::Level::Error,"Referenced native file has missing or invalid identity",relative,id); return {};
                }
            std::ifstream input(full);
            std::string first;
            std::getline(input,first);
            input.close();
            if (first.rfind("# AxleFileIdentity:",0) == 0) {
                ReportDiagnostic(AssetDiagnostic::Level::Error,"Malformed existing identity cannot be regenerated",relative); return {};
            }
            UUID id;
            while (id == 0 || IsBuiltin(id)) id = UUID();
            AssetMetadata fresh{id,type,relative,1};
            if (!AssetMetadataSerializer::Serialize(full,fresh,&error)) {
                ReportDiagnostic(AssetDiagnostic::Level::Error,error,relative); return {};
            }
        }
        Sync();
        auto found = s_MetadataByPath.find(PathKey(relative));
        return found != s_MetadataByPath.end() ? found->second : AssetMetadata{};
    }
    UUID AssetManager::GetAssetUUID(const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex);
        if (IsBuiltin(path)) return GetBuiltinMetadata(path).Handle;
        const auto key = NormalizePathKey(path);
        auto found = s_PathToUUID.find(key);
        if (AssetMetadataSerializer::IsNativeAsset(path)) {
            uint64_t actual = 0;
            if (found == s_PathToUUID.end() ||
                !FileSystem::ReadFileIdentity(s_AssetDirectory/key, actual) || actual != uint64_t(found->second)) {
                Sync(); found = s_PathToUUID.find(key);
            }
        }
        return found != s_PathToUUID.end() ? found->second : UUID(0);
    }
    std::filesystem::path AssetManager::GetAssetPath(UUID id) {
        std::lock_guard lock(s_AssetMutex);
        if (IsBuiltin(id)) return GetBuiltinMetadata(id).FilePath;
        auto found = s_Assets.find(id);
        AssetMetadata actual;
        if (found == s_Assets.end() ||
            !AssetMetadataSerializer::Deserialize(s_AssetDirectory/found->second.FilePath,actual) || actual.Handle != id) {
            Sync(); found = s_Assets.find(id);
        }
        return found != s_Assets.end() ? s_AssetDirectory/found->second.FilePath : std::filesystem::path{};
    }
    std::filesystem::path AssetManager::GetRelativeAssetPath(UUID id) {
        auto path = GetAssetPath(id);
        return path.empty() || IsBuiltin(id) ? path : GetReferencePath(path);
    }
    AssetType AssetManager::GetAssetType(UUID id) {
        std::lock_guard lock(s_AssetMutex);
        if (IsBuiltin(id)) return AssetType::Mesh;
        auto found = s_Assets.find(id);
        return found != s_Assets.end() ? found->second.Type : AssetType::None;
    }
    AssetType AssetManager::GetAssetType(const std::filesystem::path& path) {
        if (IsBuiltin(path)) return AssetType::Mesh;
        std::lock_guard lock(s_AssetMutex);
        // Type inspection is also used for external import sources. It does not
        // publish identity or authorize an out-of-root resource reference.
        if (std::filesystem::is_regular_file(path)) return GetAssetTypeFromPath(path);
        std::filesystem::path full, relative;
        return ResolveOwned(s_AssetDirectory,path,full,relative,nullptr) ? GetAssetTypeFromPath(full) : AssetType::None;
    }
    const AssetMetadata* AssetManager::GetMetadata(UUID id) {
        std::lock_guard lock(s_AssetMutex);
        auto found = s_Assets.find(id); return found != s_Assets.end() ? &found->second : nullptr;
    }
    const AssetMetadata* AssetManager::GetMetadata(const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex);
        auto found = s_MetadataByPath.find(NormalizePathKey(path));
        return found != s_MetadataByPath.end() ? &found->second : nullptr;
    }
    bool AssetManager::HasAsset(UUID id) {
        std::lock_guard lock(s_AssetMutex); return IsBuiltin(id) || s_Assets.contains(id);
    }
    bool AssetManager::HasAsset(const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex); return IsBuiltin(path) || s_MetadataByPath.contains(NormalizePathKey(path));
    }
    bool AssetManager::RelinkAsset(UUID id, const std::filesystem::path& path, std::string* error) {
        std::lock_guard lock(s_AssetMutex);
        std::filesystem::path full, relative;
        AssetMetadata item;
        if (!ResolveOwned(s_AssetDirectory,path,full,relative,error) ||
            !AssetMetadataSerializer::Deserialize(full,item,error) || item.Handle != id) {
            if (error && error->empty()) *error = "Relink target has a different embedded UUID";
            return false;
        }
        Sync();
        return HasAsset(id);
    }
    bool AssetManager::RelinkPathReferences(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::string* error) {
        std::lock_guard lock(s_AssetMutex);
        std::filesystem::path oldFull,oldRel,newFull,newRel;
        if (!ResolveOwned(s_AssetDirectory,oldPath,oldFull,oldRel,error) ||
            !ResolveOwned(s_AssetDirectory,newPath,newFull,newRel,error) || !std::filesystem::is_regular_file(newFull)) return false;
        std::vector<PathEdit> edits;
        if (!CollectPathEdits(s_AssetDirectory,oldRel.generic_string(),newRel.generic_string(),edits,error)) return false;
        size_t count = 0;
        for (const auto& edit : edits) {
            if (!FileSystem::WriteTextFileAtomically(edit.path,edit.after,error)) {
                RollbackEdits(edits,count,error); return false;
            }
            ++count;
        }
        Sync();
        return true;
    }
    bool AssetManager::RelocateAsset(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::string* error) {
        std::lock_guard lock(s_AssetMutex);
        std::filesystem::path oldFull,oldRel,newFull,newRel;
        auto fail = [&](const std::string& message){ if(error)*error=message; ReportDiagnostic(AssetDiagnostic::Level::Error,message,newPath); return false; };
        if (!ResolveOwned(s_AssetDirectory,oldPath,oldFull,oldRel,error) ||
            !ResolveOwned(s_AssetDirectory,newPath,newFull,newRel,error)) return false;
        if (!std::filesystem::is_regular_file(oldFull)) return fail("Relocation source does not exist");
        if (oldFull == newFull) return true;
        if (std::filesystem::exists(newFull)) return fail("Relocation destination already exists");
        if (AssetMetadataSerializer::IsNativeAsset(oldFull) != AssetMetadataSerializer::IsNativeAsset(newFull))
            return fail("Relocation cannot change native/raw identity kind");
        std::error_code ec;
        std::filesystem::create_directories(newFull.parent_path(),ec);
        if (ec) return fail(ec.message());
        std::vector<PathEdit> edits;
        if (!AssetMetadataSerializer::IsNativeAsset(oldFull) &&
            !CollectPathEdits(s_AssetDirectory,oldRel.generic_string(),newRel.generic_string(),edits,error)) return false;
        size_t count = 0;
        for (const auto& edit : edits) {
            if (!FileSystem::WriteTextFileAtomically(edit.path,edit.after,error)) {
                RollbackEdits(edits,count,error); return false;
            }
            ++count;
        }
        std::filesystem::rename(oldFull,newFull,ec);
        if (ec) {
            if (error) *error=ec.message();
            RollbackEdits(edits,count,error); return false;
        }
        Sync();
        return true;
    }
    bool AssetManager::CopyAsset(const std::filesystem::path& sourcePath, const std::filesystem::path& destination, std::string* error) {
        std::lock_guard lock(s_AssetMutex);
        std::filesystem::path source,sourceRel,target,targetRel;
        if (!ResolveOwned(s_AssetDirectory,sourcePath,source,sourceRel,error) ||
            !ResolveOwned(s_AssetDirectory,destination,target,targetRel,error)) return false;
        if (!std::filesystem::is_regular_file(source) || std::filesystem::exists(target)) {
            if(error)*error="Copy requires an existing source and an unused destination"; return false;
        }
        if (AssetMetadataSerializer::IsNativeAsset(source) != AssetMetadataSerializer::IsNativeAsset(target)) {
            if(error)*error="Copy cannot change native/raw identity kind"; return false;
        }
        if (AssetMetadataSerializer::IsNativeAsset(source)) {
            AssetMetadata item;
            if (!AssetMetadataSerializer::Deserialize(source,item,error)) return false;
            UUID id;
            while(id == 0 || IsBuiltin(id) || s_Assets.contains(id)) id=UUID();
            if (!FileSystem::WriteIdentifiedTextFile(target,FileSystem::ReadTextFile(source),error,uint64_t(id))) return false;
        } else {
            std::ifstream input(source,std::ios::binary);
            if (!input) { if(error)*error="Cannot read copy source"; return false; }
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
            if (input.bad()) { if(error)*error="Failed while reading copy source"; return false; }
            if (!FileSystem::WriteBinaryFileAtomically(target,bytes.data(),bytes.size(),error)) return false;
        }
        Sync();
        return true;
    }
    void AssetManager::OnAssetRenamed(const std::filesystem::path& oldPath,const std::filesystem::path& newPath) {
        if (std::filesystem::exists(oldPath)) RelocateAsset(oldPath,newPath);
        else if (!AssetMetadataSerializer::IsNativeAsset(newPath)) RelinkPathReferences(oldPath,newPath);
        else Sync();
    }
    void AssetManager::OnAssetDeleted(const std::filesystem::path&) { Sync(); }
    size_t AssetManager::GetAssetCount() { std::lock_guard lock(s_AssetMutex); return s_MetadataByPath.size(); }
    const std::filesystem::path& AssetManager::GetAssetDirectory() { return s_AssetDirectory; }
    void AssetManager::SetAssetDirectory(const std::filesystem::path& root) { Init(root); }


    std::shared_ptr<Texture2D> AssetManager::GetTexture(const std::filesystem::path& path) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path ownedPath, reference;
        if (!ResolveOwned(s_AssetDirectory, path, ownedPath, reference, nullptr) || !FileSystem::Exists(ownedPath)) {
            ReportDiagnostic(AssetDiagnostic::Level::Error, "Resource path is missing or outside its root", path);
            return nullptr;
        }
        if (AssetMetadataSerializer::IsNativeAsset(ownedPath)) {
            TextureDescriptor descriptor;
            if (!TextureDescriptor::Load(ownedPath.string(), descriptor)) return nullptr;
            return GetTexture(std::filesystem::path(descriptor.Source));
        }
        std::string key = NormalizePathKey(path);
        auto it = s_TextureCacheByPath.find(key);
        if (it != s_TextureCacheByPath.end()) return it->second;

        const auto& fullPath = ownedPath;
        if (!FileSystem::Exists(fullPath)) {
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
        if (GetAssetPath(uuid).empty()) return nullptr;
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

    static void AttachMaterialTextures(const std::shared_ptr<Material>& material) {
        // Loading a texture must not silently re-enable an explicitly disabled map.
        if (!material->GetClearcoatPath().empty()) {
            bool enabled=material->IsClearcoatMapEnabled();
            auto texture=AssetManager::GetTexture(material->GetClearcoatPath());
            material->SetClearcoatTexture(texture,material->GetClearcoatPath());
            material->SetUseClearcoatMap(enabled);
        }
        if (!material->GetAnisotropyFlowPath().empty()) {
            bool enabled=material->IsAnisotropyFlowMapEnabled();
            auto texture=AssetManager::GetTexture(material->GetAnisotropyFlowPath());
            material->SetAnisotropyFlowTexture(texture,material->GetAnisotropyFlowPath());
            material->SetUseAnisotropyFlowMap(enabled);
        }
        if (!material->GetDetailNormalPath().empty()) {
            bool enabled=material->IsDetailNormalMapEnabled();
            auto texture=AssetManager::GetTexture(material->GetDetailNormalPath());
            material->SetDetailNormalTexture(texture,material->GetDetailNormalPath());
            material->SetUseDetailNormalMap(enabled);
        }
        if (!material->GetDetailMaskPath().empty()) {
            bool enabled=material->IsDetailMaskMapEnabled();
            auto texture=AssetManager::GetTexture(material->GetDetailMaskPath());
            material->SetDetailMaskTexture(texture,material->GetDetailMaskPath());
            material->SetUseDetailMaskMap(enabled);
        }
        if (!material->GetAlbedoPath().empty()) {
            bool enabled = material->IsAlbedoMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetAlbedoPath());
            material->SetAlbedoTexture(texture, material->GetAlbedoPath());
            material->SetUseAlbedoMap(enabled);
        }
        if (!material->GetNormalPath().empty()) {
            bool enabled = material->IsNormalMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetNormalPath());
            material->SetNormalTexture(texture, material->GetNormalPath());
            material->SetUseNormalMap(enabled);
        }
        if (!material->GetRoughnessPath().empty()) {
            bool enabled = material->IsRoughnessMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetRoughnessPath());
            material->SetRoughnessTexture(texture, material->GetRoughnessPath());
            material->SetUseRoughnessMap(enabled);
        }
        if (!material->GetMetallicPath().empty()) {
            bool enabled = material->IsMetallicMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetMetallicPath());
            material->SetMetallicTexture(texture, material->GetMetallicPath());
            material->SetUseMetallicMap(enabled);
        }
        if (!material->GetAOPath().empty()) {
            bool enabled = material->IsAOMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetAOPath());
            material->SetAOTexture(texture, material->GetAOPath());
            material->SetUseAOMap(enabled);
        }
        if (!material->GetEmissivePath().empty()) {
            bool enabled = material->IsEmissiveMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetEmissivePath());
            material->SetEmissiveTexture(texture, material->GetEmissivePath());
            material->SetUseEmissiveMap(enabled);
        }
        if (!material->GetORMPath().empty()) {
            bool enabled = material->IsORMMapEnabled();
            auto texture = AssetManager::GetTexture(material->GetORMPath());
            material->SetORMTexture(texture, material->GetORMPath());
            material->SetUseORMMap(enabled);
        }
    }

    std::shared_ptr<Material> AssetManager::GetMaterial(const std::filesystem::path& path) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path ownedPath, reference;
        if (!ResolveOwned(s_AssetDirectory, path, ownedPath, reference, nullptr) || !FileSystem::Exists(ownedPath)) {
            ReportDiagnostic(AssetDiagnostic::Level::Error, "Resource path is missing or outside its root", path);
            return nullptr;
        }
        const UUID identity = GetAssetUUID(path);
        if (identity == 0) return nullptr;
        std::string key = NormalizePathKey(path);
        auto it = s_MaterialCacheByPath.find(key);
        if (it != s_MaterialCacheByPath.end()) {
            if (it->second && it->second->GetHandle() == identity) return it->second;
            s_MaterialCacheByPath.erase(it);
        }
        if (auto cached = s_MaterialCacheByUUID.find(identity); cached != s_MaterialCacheByUUID.end()) {
            cached->second->SetFilePath(ownedPath);
            s_MaterialCacheByPath[key] = cached->second;
            return cached->second;
        }

        const auto& fullPath = ownedPath;
        if (!FileSystem::Exists(fullPath)) {
            AXLE_WARN("AssetManager::GetMaterial: file not found: {0}", fullPath.string());
            return nullptr;
        }

        auto mat = std::make_shared<Material>();
        if (mat->Load(fullPath)) {
            mat->SetHandle(GetAssetUUID(path));
            AttachMaterialTextures(mat);
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
        if (GetAssetPath(uuid).empty()) return nullptr;
        auto it = s_MaterialCacheByUUID.find(uuid);
        if (it != s_MaterialCacheByUUID.end()) return it->second;

        std::filesystem::path path = GetAssetPath(uuid);
        if (path.empty()) return nullptr;
        return GetMaterial(path);
    }

    void AssetManager::ReloadMaterial(UUID uuid) {
        if (uuid == 0) return;
        std::lock_guard lock(s_AssetMutex);
        const auto path = GetAssetPath(uuid);
        if (!path.empty()) ReloadMaterial(path);
    }

    void AssetManager::ReloadMaterial(const std::filesystem::path& path) {
        std::lock_guard lock(s_AssetMutex);
        std::filesystem::path full, relative;
        if (!ResolveOwned(s_AssetDirectory,path,full,relative,nullptr) || !FileSystem::Exists(full)) return;
        const auto uuid = GetAssetUUID(path);
        const auto key = PathKey(relative);
        std::shared_ptr<Material> material;
        if (auto found = s_MaterialCacheByPath.find(key); found != s_MaterialCacheByPath.end()) material = found->second;
        if (!material && uuid != 0)
            if (auto found = s_MaterialCacheByUUID.find(uuid); found != s_MaterialCacheByUUID.end()) material = found->second;
        if (material) {
            auto replacement = std::make_shared<Material>(*material);
            if (replacement->Load(full)) {
                replacement->SetHandle(uuid);
                AttachMaterialTextures(replacement);
                *material = std::move(*replacement);
            }
        }
        std::shared_ptr<RHI::RHIMaterial> rhiMaterial;
        if (auto found = s_RHIMaterialCacheByPath.find(key); found != s_RHIMaterialCacheByPath.end()) rhiMaterial = found->second;
        if (!rhiMaterial && uuid != 0)
            if (auto found = s_RHIMaterialCacheByUUID.find(uuid); found != s_RHIMaterialCacheByUUID.end()) rhiMaterial = found->second;
        if (rhiMaterial) rhiMaterial->Load(full);
    }

    void AssetManager::ClearMaterialCache() {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        s_MaterialCacheByPath.clear();
        s_MaterialCacheByUUID.clear();
    }

    std::shared_ptr<RHI::RHIMaterial> AssetManager::GetRHIMaterial(const std::filesystem::path& path) {
        std::lock_guard<std::recursive_mutex> lock(s_AssetMutex);
        std::filesystem::path ownedPath, reference;
        if (!ResolveOwned(s_AssetDirectory, path, ownedPath, reference, nullptr) || !FileSystem::Exists(ownedPath)) {
            ReportDiagnostic(AssetDiagnostic::Level::Error, "Resource path is missing or outside its root", path);
            return nullptr;
        }
        const UUID identity = GetAssetUUID(path);
        if (identity == 0) return nullptr;
        std::string key = NormalizePathKey(path);
        auto it = s_RHIMaterialCacheByPath.find(key);
        auto cached = s_RHIMaterialCacheByUUID.find(identity);
        if (cached != s_RHIMaterialCacheByUUID.end()) {
            s_RHIMaterialCacheByPath[key] = cached->second;
            return cached->second;
        }
        if (it != s_RHIMaterialCacheByPath.end()) s_RHIMaterialCacheByPath.erase(it);

        const auto& fullPath = ownedPath;
        if (!FileSystem::Exists(fullPath)) {
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
        if (GetAssetPath(uuid).empty()) return nullptr;
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
