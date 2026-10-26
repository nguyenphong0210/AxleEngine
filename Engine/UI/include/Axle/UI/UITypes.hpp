#pragma once
#include <glm/glm.hpp>
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
        SDF_RadialArc = 3
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

    struct alignas(16) UIVertex {
        glm::vec2 ScreenPos{ 0.0f };      // [0..7]   Screen-space coords (pixels)
        glm::vec2 UV{ 0.0f };             // [8..15]  Normalized quad UV [0..1] or atlas UV
        glm::vec4 Color{ 1.0f };          // [16..31] Primary color (fill / text / arc)
        glm::vec4 BorderColor{ 0.0f };    // [32..47] Border / track color
        glm::vec2 QuadSize{ 100.0f };     // [48..55] Quad size in pixels
        float     CornerRadius = 0.0f;    // [56..59] Corner radius (pixels) or arc thickness
        float     BorderWidth  = 0.0f;    // [60..63] Border width (pixels) or active angle (rad)
        glm::vec2 AngleRange{ 0.0f };     // [64..71] (StartAngleRad, EndAngleRad)
        uint32_t  TextureIndex  = 0;      // [72..75] Bindless Texture Descriptor index
        uint32_t  PrimitiveType = 0;      // [76..79] 0: SDF_Rect, 1: Sprite, 2: MSDF_Text, 3: SDF_RadialArc
    };
    static_assert(sizeof(UIVertex) == 80, "UIVertex size must be exactly 80 bytes");

    const char* UIAnchorToString(UIAnchor anchor);
    UIAnchor StringToUIAnchor(std::string_view str);

    const char* UIPrimitiveTypeToString(UIPrimitiveType type);
    UIPrimitiveType StringToUIPrimitiveType(std::string_view str);

} // namespace Axle
