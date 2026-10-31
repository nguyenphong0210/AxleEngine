#pragma once

#include <Axle/VFXCommon.hpp>
#include <string>

namespace Axle {

    class CurveEditorWidget {
    public:
        // Draws an interactive cubic Bezier curve editor with grid, polyline, and draggable control handles
        // Returns true if modified
        static bool DrawCurveEditor(const char* label, FloatCurve& curve, float height = 80.0f);

        // Draws an interactive multi-key gradient ramp editor with color & alpha key markers
        // Returns true if modified
        static bool DrawGradientEditor(const char* label, ColorGradient& gradient, float height = 28.0f);
    };

}
