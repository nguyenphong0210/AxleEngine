#include <Axle/TerrainAsset.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <sstream>
#include <iomanip>

namespace Axle {

    std::string TerrainAsset::Serialize(const TerrainAsset& asset) {
        std::ostringstream ss;
        ss << "Terrain {\n";
        ss << "    name: \"" << (asset.Name.empty() ? "Terrain" : asset.Name) << "\"\n";
        ss << "    size: [" << std::fixed << std::setprecision(1) << asset.Size.x << ", " << asset.Size.y << "]\n";
        ss << "    heightScale: " << std::fixed << std::setprecision(1) << asset.HeightScale << "\n";
        if (!asset.Heightmap.empty()) {
            ss << "    heightmap: \"" << asset.Heightmap << "\"\n";
        }
        if (!asset.Layers.empty()) {
            ss << "\n    layers: [\n";
            for (size_t i = 0; i < asset.Layers.size(); ++i) {
                ss << "        \"" << asset.Layers[i] << "\"" << (i + 1 < asset.Layers.size() ? "," : "") << "\n";
            }
            ss << "    ]\n";
        }
        ss << "}\n";
        return ss.str();
    }

    bool TerrainAsset::Deserialize(std::string_view source, TerrainAsset& outAsset) {
        AssetDocument doc;
        std::string err;
        if (!AssetDSL::ParseSource(source, doc, &err) || !doc.IsValid() || !doc.Root) {
            AXLE_ERROR("TerrainAsset: Failed to parse .axleasset: {0}", err);
            return false;
        }

        if (doc.Kind != AssetKind::Terrain && doc.Root->Type != "Terrain") {
            AXLE_ERROR("TerrainAsset: Root block is not 'Terrain' (found '{0}')", doc.Root->Type);
            return false;
        }

        outAsset = TerrainAsset{};
        outAsset.Name = doc.Root->GetString("name", "NewTerrain");
        if (doc.Root->HasProperty("size")) {
            outAsset.Size = doc.Root->GetVec2("size", glm::vec2(1024.0f, 1024.0f));
        }
        outAsset.HeightScale = doc.Root->GetFloat("heightScale", 60.0f);
        outAsset.Heightmap = doc.Root->GetString("heightmap", "");
        if (doc.Root->HasProperty("layers")) {
            outAsset.Layers = doc.Root->GetProperty("layers")->AsStringArray();
        }
        return true;
    }

    bool TerrainAsset::Save(const std::filesystem::path& filepath, const TerrainAsset& asset) {
        std::string content = Serialize(asset);
        return FileSystem::WriteTextFile(filepath, content);
    }

    bool TerrainAsset::Load(const std::filesystem::path& filepath, TerrainAsset& outAsset) {
        std::filesystem::path actualPath = filepath;
        if (!FileSystem::Exists(actualPath)) {
            if (actualPath.extension() == ".terrain") {
                actualPath.replace_extension(".axleasset");
            }
        }
        std::string content = FileSystem::ReadTextFile(actualPath);
        if (content.empty()) {
            AXLE_ERROR("TerrainAsset: Failed to read file: '{0}'", filepath.string());
            return false;
        }
        if (outAsset.Name.empty() || outAsset.Name == "NewTerrain") {
            outAsset.Name = actualPath.stem().string();
        }
        return Deserialize(content, outAsset);
    }

} // namespace Axle
