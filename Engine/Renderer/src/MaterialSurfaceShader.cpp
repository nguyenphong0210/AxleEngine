#include <Axle/MaterialSurfaceShader.hpp>
#include "MaterialSurfaceSource.hpp"
#include <stdexcept>

namespace Axle {
    std::string AssembleMaterialSurfaceShader(std::string_view declarations, std::string_view surfaceFunction) {
        std::string result(MaterialSurfaceSource::Common);
        result += "\n#line 1 \"mg_surface_template\"\n";
        std::string body(MaterialSurfaceSource::Template);
        auto replace = [&](std::string_view marker, std::string_view value) {
            const auto position = body.find(marker);
            if (position == std::string::npos) throw std::logic_error("Missing material surface template marker");
            body.replace(position, marker.size(), value);
        };
        replace("// @AXLE_GRAPH_FUNCTION@", surfaceFunction);
        replace("// @AXLE_GRAPH_DECLARATIONS@", declarations);
        return result + body;
    }
}
