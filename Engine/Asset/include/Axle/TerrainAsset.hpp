#pragma once

#include <Axle/Types.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <glm/glm.hpp>

namespace Axle {

    struct TerrainAsset {
        std::string Name = "NewTerrain";
        glm::vec2 Size{ 1024.0f, 1024.0f };
        float HeightScale = 60.0f;
        std::string Heightmap;
        std::vector<std::string> Layers;

        static bool Load(const std::filesystem::path& filepath, TerrainAsset& outAsset);
        static bool Save(const std::filesystem::path& filepath, const TerrainAsset& asset);
        static std::string Serialize(const TerrainAsset& asset);
        static bool Deserialize(std::string_view source, TerrainAsset& outAsset);
    };

} // namespace Axle
