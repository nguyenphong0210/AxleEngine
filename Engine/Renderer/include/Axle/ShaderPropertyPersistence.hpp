#pragma once
#include <Axle/ShaderPropertyReflection.hpp>
#include <ostream>
#include <string_view>

namespace Axle {
    void WriteShaderProperties(std::ostream& stream, const std::vector<ShaderPropertyInstance>& properties,
                               std::string_view indent = "    ");
    bool ReadShaderPropertyLine(std::string_view line, std::vector<ShaderPropertyInstance>& properties);
}
