#pragma once
#include <string>
namespace Axle {
    class SceneData;
    // CPU structural codec; concrete system sections remain passive authored text.
    class SceneDataDocument {
    public:
        static bool Deserialize(const std::string& text, SceneData& scene);
        static std::string Serialize(const SceneData& scene);
    };
}
