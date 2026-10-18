#pragma once
#include <Axle/Entity.hpp>
#include <ostream>
#include <string>
#include <vector>

namespace Axle::Spline {

    class SplineSerializer {
    public:
        static void Serialize(Entity entity, std::ostream& out);
        static bool Deserialize(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines);
    };

}
