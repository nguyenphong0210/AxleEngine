#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <string_view>
#include <filesystem>
#include <algorithm>

namespace Axle {

    enum class AssetType : uint16_t {
        None = 0,
        Scene,
        Mesh,
        Assembly,
        Material,
        PhysicsMaterial,
        Script,
        Texture,
        Audio,
        Font
    };

    inline const char* AssetTypeToString(AssetType type) {
        switch (type) {
            case AssetType::Scene:           return "Scene";
            case AssetType::Mesh:            return "Mesh";
            case AssetType::Assembly:        return "Assembly";
            case AssetType::Material:        return "Material";
            case AssetType::PhysicsMaterial: return "PhysicsMaterial";
            case AssetType::Script:          return "Script";
            case AssetType::Texture:         return "Texture";
            case AssetType::Audio:           return "Audio";
            case AssetType::Font:            return "Font";
            case AssetType::None:
            default:                         return "None";
        }
    }

    inline AssetType AssetTypeFromString(std::string_view typeStr) {
        if (typeStr == "Scene")           return AssetType::Scene;
        if (typeStr == "Mesh")            return AssetType::Mesh;
        if (typeStr == "Assembly")        return AssetType::Assembly;
        if (typeStr == "Material")        return AssetType::Material;
        if (typeStr == "PhysicsMaterial") return AssetType::PhysicsMaterial;
        if (typeStr == "Script")          return AssetType::Script;
        if (typeStr == "Texture")         return AssetType::Texture;
        if (typeStr == "Audio")           return AssetType::Audio;
        if (typeStr == "Font")            return AssetType::Font;
        return AssetType::None;
    }

    inline AssetType GetAssetTypeFromPath(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

        if (ext == ".axlescene" || ext == ".scene") return AssetType::Scene;
        if (ext == ".axlemat" || ext == ".amat" || ext == ".mat") return AssetType::Material;
        if (ext == ".physmat") return AssetType::PhysicsMaterial;
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") return AssetType::Mesh;
        if (ext == ".hpp" || ext == ".cpp" || ext == ".cs" || ext == ".lua") return AssetType::Script;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") return AssetType::Texture;
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return AssetType::Audio;
        if (ext == ".ttf" || ext == ".otf") return AssetType::Font;

        if (ext == ".axle") {
            // Check if file content indicates Assembly or Mesh
            return AssetType::Assembly;
        }

        return AssetType::None;
    }

}
