#pragma once

#include <Axle/Entity.hpp>
#include <ostream>
#include <string>
#include <vector>

namespace Axle::Terrain {

    struct TerrainComponent;

    class TerrainSerializer {
    public:
        static void Serialize(Entity entity, std::ostream& out);
        static bool Deserialize(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines);

        static std::string SerializeToString(const TerrainComponent& comp);
        static bool DeserializeFromString(TerrainComponent& comp, const std::string& text);
    };

} // namespace Axle::Terrain
