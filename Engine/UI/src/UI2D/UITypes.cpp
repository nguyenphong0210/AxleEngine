#include <Axle/UI/UITypes.hpp>
#include <algorithm>

namespace Axle {

    const char* UIAnchorToString(UIAnchor anchor) {
        switch (anchor) {
            case UIAnchor::TopLeft:       return "TopLeft";
            case UIAnchor::TopCenter:     return "TopCenter";
            case UIAnchor::TopRight:      return "TopRight";
            case UIAnchor::CenterLeft:    return "CenterLeft";
            case UIAnchor::Center:        return "Center";
            case UIAnchor::CenterRight:   return "CenterRight";
            case UIAnchor::BottomLeft:    return "BottomLeft";
            case UIAnchor::BottomCenter:  return "BottomCenter";
            case UIAnchor::BottomRight:   return "BottomRight";
            case UIAnchor::TopStretch:    return "TopStretch";
            case UIAnchor::BottomStretch: return "BottomStretch";
            case UIAnchor::StretchAll:    return "StretchAll";
            default:                      return "Unknown";
        }
    }

    UIAnchor StringToUIAnchor(std::string_view str) {
        if (str == "TopLeft" || str == "top_left") return UIAnchor::TopLeft;
        if (str == "TopCenter" || str == "top_center" || str == "top") return UIAnchor::TopCenter;
        if (str == "TopRight" || str == "top_right") return UIAnchor::TopRight;
        if (str == "CenterLeft" || str == "center_left" || str == "left") return UIAnchor::CenterLeft;
        if (str == "Center" || str == "center") return UIAnchor::Center;
        if (str == "CenterRight" || str == "center_right" || str == "right") return UIAnchor::CenterRight;
        if (str == "BottomLeft" || str == "bottom_left") return UIAnchor::BottomLeft;
        if (str == "BottomCenter" || str == "bottom_center" || str == "bottom") return UIAnchor::BottomCenter;
        if (str == "BottomRight" || str == "bottom_right") return UIAnchor::BottomRight;
        if (str == "TopStretch" || str == "top_stretch") return UIAnchor::TopStretch;
        if (str == "BottomStretch" || str == "bottom_stretch") return UIAnchor::BottomStretch;
        if (str == "StretchAll" || str == "stretch_all" || str == "fill" || str == "stretch") return UIAnchor::StretchAll;
        return UIAnchor::TopLeft;
    }

    const char* UIPrimitiveTypeToString(UIPrimitiveType type) {
        switch (type) {
            case UIPrimitiveType::SDF_Rect:      return "SDF_Rect";
            case UIPrimitiveType::Sprite:        return "Sprite";
            case UIPrimitiveType::MSDF_Text:     return "MSDF_Text";
            case UIPrimitiveType::SDF_RadialArc: return "SDF_RadialArc";
            default:                             return "Unknown";
        }
    }

    UIPrimitiveType StringToUIPrimitiveType(std::string_view str) {
        if (str == "SDF_Rect" || str == "Rect" || str == "Rectangle" || str == "Panel") return UIPrimitiveType::SDF_Rect;
        if (str == "Sprite" || str == "Image") return UIPrimitiveType::Sprite;
        if (str == "MSDF_Text" || str == "Text" || str == "Label") return UIPrimitiveType::MSDF_Text;
        if (str == "SDF_RadialArc" || str == "RadialArc" || str == "Gauge") return UIPrimitiveType::SDF_RadialArc;
        return UIPrimitiveType::SDF_Rect;
    }

} // namespace Axle
