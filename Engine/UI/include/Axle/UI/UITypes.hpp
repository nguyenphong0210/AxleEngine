#pragma once
#include <glm/glm.hpp>
#include <Axle/CanvasRenderer.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace Axle {

    enum class UIAnchor : uint8_t {
        TopLeft = 0,
        TopCenter,
        TopRight,
        CenterLeft,
        Center,
        CenterRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        TopStretch,
        BottomStretch,
        StretchAll
    };

    enum class UIPrimitiveType : uint32_t {
        SDF_Rect      = 0,
        Sprite        = 1,
        MSDF_Text     = 2,
        SDF_RadialArc = 3,
        BitmapText = 4
    };

    struct UIRect {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 0.0f;
        float Height = 0.0f;

        constexpr bool Contains(const glm::vec2& pt) const {
            return pt.x >= X && pt.x <= (X + Width) &&
                   pt.y >= Y && pt.y <= (Y + Height);
        }

        constexpr UIRect Intersect(const UIRect& other) const {
            float x0 = (X > other.X) ? X : other.X;
            float y0 = (Y > other.Y) ? Y : other.Y;
            float x1 = ((X + Width) < (other.X + other.Width)) ? (X + Width) : (other.X + other.Width);
            float y1 = ((Y + Height) < (other.Y + other.Height)) ? (Y + Height) : (other.Y + other.Height);
            if (x1 <= x0 || y1 <= y0) return { 0.0f, 0.0f, 0.0f, 0.0f };
            return { x0, y0, x1 - x0, y1 - y0 };
        }
    };

    struct UIStyle {
        glm::vec4 BackgroundColor{ 0.07f, 0.09f, 0.13f, 0.85f };
        glm::vec4 BorderColor{ 0.22f, 0.74f, 0.97f, 1.0f };
        float CornerRadius = 0.0f;
        float BorderWidth  = 0.0f;
    };

    using UIVertex = CanvasVertex;

    const char* UIAnchorToString(UIAnchor anchor);
    UIAnchor StringToUIAnchor(std::string_view str);

    const char* UIPrimitiveTypeToString(UIPrimitiveType type);
    UIPrimitiveType StringToUIPrimitiveType(std::string_view str);

} // namespace Axle
