#pragma once
#include <string>
#include <string_view>

namespace Axle {
    // Embeds the same PBR BRDF source used by ForwardPBR. No disk includes are
    // required at runtime; graph declarations/evaluation contain only surface logic.
    std::string AssembleMaterialSurfaceShader(std::string_view declarations, std::string_view surfaceFunction);
}
