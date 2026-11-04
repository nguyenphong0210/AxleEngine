#pragma once

#include <Axle/Components.hpp>
#include <ostream>

namespace Axle::Detail {

// Component headers in the scene/assembly format start at column four.
// Inspect boundaries before generic trimming can consume nested sections or comments.
inline bool BeginLegacyComponent(const std::string& line, LegacyComponentPayload& payload) {
    if (line.find_first_not_of(" \t") != 4) return false;
    const auto colon = line.find(':', 4);
    if (colon == std::string::npos) return false;
    const auto name = line.substr(4, colon - 4);
    if (name != "VehicleComponent" && name != "WheelComponent" && name != "VehicleWheel3D"
        && name != "VehicleAudioComponent" && name != "VehicleVFXComponent") return false;
    payload = {name, {}, line};
    return true;
}

inline bool AppendLegacyLine(const std::string& line, LegacyComponentPayload& payload) {
    const auto indent = line.find_first_not_of(" \t");
    if (indent == std::string::npos) {
        payload.Lines.push_back("");
        return true;
    }
    if (indent > 4 || (indent == 4 && line[indent] == '#')) {
        if (line.size() >= 6 && line.compare(0, 6, "      ") == 0) {
            payload.Lines.push_back(line.substr(6));
        } else {
            payload.Lines.push_back(line.substr(indent));
        }
        return true;
    }
    return false;
}

inline void WriteLegacyComponent(std::ostream& out, const LegacyComponentPayload& payload) {
    if (payload.RawHeader.empty()) out << "    " << payload.TypeName << ":\n";
    else out << payload.RawHeader << '\n';
    for (const auto& line : payload.Lines) {
        if (line.empty()) out << '\n';
        else out << "      " << line << '\n';
    }
}

} // namespace Axle::Detail
