#pragma once
#include <Axle/Entity.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <ostream>
#include <string>
#include <vector>

namespace Axle::Spline {

    class SplineSerializer {
    public:
        static void Serialize(Entity entity, std::ostream& out);
        static bool Deserialize(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines);

        static void SerializeComponent(const SplineComponent& sc, std::ostream& out);
        static bool DeserializeComponent(SplineComponent& sc, const std::string& sectionName, const std::vector<std::string>& lines);

        static std::string SerializeToString(const SplineComponent& sc);
        static bool DeserializeFromString(const std::string& text, SplineComponent& sc);
    };

}
