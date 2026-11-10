#pragma once
#include <Axle/MaterialGraph.hpp>
#include <algorithm>
#include <cmath>

namespace Axle::MaterialGraphInternal {
    inline size_t Width(MaterialValueType type) {
        switch (type) {
            case MaterialValueType::Float: return 1;
            case MaterialValueType::Float2: return 2;
            case MaterialValueType::Float3: return 3;
            case MaterialValueType::Float4: return 4;
        }
        return 0;
    }

    inline bool ValidValue(const MaterialValue& value) {
        return Width(value.Type) != 0 && value.Components.size() == Width(value.Type) &&
            std::all_of(value.Components.begin(), value.Components.end(), [](float v){return std::isfinite(v);});
    }

    inline bool CanonicalAssetPath(const std::string& path) {
        if (path.empty() || path.find_first_of("\\:") != std::string::npos || path.front() == '/') return false;
        if (std::any_of(path.begin(), path.end(), [](unsigned char c){return c < 32;})) return false;
        std::string_view rest(path);
        while (true) {
            const auto slash = rest.find('/'); const auto part = rest.substr(0, slash);
            if (part.empty() || part == "." || part == "..") return false;
            if (slash == std::string_view::npos) return true;
            rest.remove_prefix(slash+1);
        }
    }
}
