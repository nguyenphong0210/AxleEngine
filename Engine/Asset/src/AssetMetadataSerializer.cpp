#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <algorithm>
#include <array>
#include <fstream>

namespace Axle {
    bool AssetMetadataSerializer::IsNativeAsset(const std::filesystem::path& path) {
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        static constexpr std::array extensions = {".axleasset", ".axle", ".axlescene", ".scene", ".axle_vfx", ".vfx", ".axlemat", ".amat", ".mat",
            ".physmat", ".skeleton", ".skelmesh", ".skeletalmesh", ".animclip", ".clip",
            ".animcontroller", ".controller", ".axleui", ".axleterrain", ".axlemesh"};
        return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
    }

    bool AssetMetadataSerializer::Serialize(const std::filesystem::path& path, const AssetMetadata& metadata, std::string* error) {
        if (!IsNativeAsset(path) || metadata.Handle == 0 || metadata.Version != 1) {
            if (error) *error = "Embedded identity requires a native asset and a valid current UUID";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            if (error) *error = "Cannot read asset payload before assigning identity";
            return false;
        }
        std::string text((std::istreambuf_iterator<char>(file)), {});
        if (file.bad() || text.empty() || text.find(char(0)) != std::string::npos) {
            if (error) *error = "Cannot identify an empty or unreadable native asset";
            return false;
        }
        file.close(); // Windows replacement requires our own read handle to be closed first.
        return FileSystem::WriteIdentifiedTextFile(path, text, error, static_cast<uint64_t>(metadata.Handle));
    }

    bool AssetMetadataSerializer::Deserialize(const std::filesystem::path& path, AssetMetadata& metadata, std::string* error) {
        uint64_t id = 0;
        if (!IsNativeAsset(path) || !FileSystem::ReadFileIdentity(path, id, error)) return false;
        if (id >= 0xBA5E0001ULL && id <= 0xBA5E0005ULL) {
            if (error) *error = "Native file uses a reserved builtin identity";
            return false;
        }
        auto type = GetAssetTypeFromPath(path);
        if (type == AssetType::None) {
            if (error) *error = "Native asset has an invalid or unsupported resource type";
            return false;
        }
        metadata = AssetMetadata{UUID(id), type, path, 1};
        return true;
    }
}
