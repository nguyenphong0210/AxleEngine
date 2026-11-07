#pragma once
#include <ostream>
#include <string_view>
namespace Axle {
    struct MaterialComponent;
    // One authored material schema for Scene and both Assembly save/load paths.
    // GPU caches and transient diagnostics/generation IDs are never serialized.
    void WriteMaterialComponent(std::ostream& stream,const MaterialComponent& material);
    bool ReadMaterialComponentLine(MaterialComponent& material,std::string_view line);
}
