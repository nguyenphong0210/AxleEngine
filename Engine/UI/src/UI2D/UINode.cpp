#include <Axle/UI/UINode.hpp>
#include <algorithm>
#include <sstream>

namespace Axle {

    glm::vec4 UINode::ParseHexColor(std::string_view hexStr) {
        if (hexStr.empty()) return glm::vec4(1.0f);
        if (hexStr[0] == '#') hexStr.remove_prefix(1);

        uint32_t val = 0;
        std::stringstream ss;
        ss << std::hex << hexStr;
        ss >> val;

        if (hexStr.length() == 6) { // #RRGGBB
            float r = ((val >> 16) & 0xFF) / 255.0f;
            float g = ((val >> 8) & 0xFF) / 255.0f;
            float b = (val & 0xFF) / 255.0f;
            return glm::vec4(r, g, b, 1.0f);
        } else if (hexStr.length() == 8) { // #RRGGBBAA
            float r = ((val >> 24) & 0xFF) / 255.0f;
            float g = ((val >> 16) & 0xFF) / 255.0f;
            float b = ((val >> 8) & 0xFF) / 255.0f;
            float a = (val & 0xFF) / 255.0f;
            return glm::vec4(r, g, b, a);
        } else if (hexStr.length() == 3) { // #RGB
            float r = ((val >> 8) & 0xF) / 15.0f;
            float g = ((val >> 4) & 0xF) / 15.0f;
            float b = (val & 0xF) / 15.0f;
            return glm::vec4(r, g, b, 1.0f);
        }

        return glm::vec4(1.0f);
    }

    UIAnchor UINode::ParseAnchor(std::string_view anchorStr) {
        return StringToUIAnchor(anchorStr);
    }

    glm::vec2 UINode::OnMeasure(const std::shared_ptr<UIFont>& defaultFont) {
        DesiredSize = Size;
        for (UINode* child : Children) {
            if (child && child->Visible) {
                child->OnMeasure(defaultFont);
            }
        }
        return DesiredSize;
    }

    void UINode::OnArrange(const UIRect& parentRect) {
        if (UseRectTransform) {
            ComputedRect = Transform.ComputeRect(parentRect);
            for (UINode* child : Children) {
                if (child && child->Visible) {
                    child->OnArrange(ComputedRect);
                }
            }
            return;
        }

        float Px = parentRect.X;
        float Py = parentRect.Y;
        float Pw = parentRect.Width;
        float Ph = parentRect.Height;

        float W = (DesiredSize.x > 0.0f) ? DesiredSize.x : Size.x;
        float H = (DesiredSize.y > 0.0f) ? DesiredSize.y : Size.y;

        float ML = Margin.x;
        float MT = Margin.y;
        float MR = Margin.z;
        float MB = Margin.w;

        float X = Px;
        float Y = Py;

        switch (Anchor) {
            case UIAnchor::TopLeft:
                X = Px + ML + Position.x;
                Y = Py + MT + Position.y;
                break;
            case UIAnchor::TopCenter:
                X = Px + 0.5f * (Pw - W) + Position.x;
                Y = Py + MT + Position.y;
                break;
            case UIAnchor::TopRight:
                X = Px + Pw - W - MR + Position.x;
                Y = Py + MT + Position.y;
                break;
            case UIAnchor::CenterLeft:
                X = Px + ML + Position.x;
                Y = Py + 0.5f * (Ph - H) + Position.y;
                break;
            case UIAnchor::Center:
                X = Px + 0.5f * (Pw - W) + Position.x;
                Y = Py + 0.5f * (Ph - H) + Position.y;
                break;
            case UIAnchor::CenterRight:
                X = Px + Pw - W - MR + Position.x;
                Y = Py + 0.5f * (Ph - H) + Position.y;
                break;
            case UIAnchor::BottomLeft:
                X = Px + ML + Position.x;
                Y = Py + Ph - H - MB + Position.y;
                break;
            case UIAnchor::BottomCenter:
                X = Px + 0.5f * (Pw - W) + Position.x;
                Y = Py + Ph - H - MB + Position.y;
                break;
            case UIAnchor::BottomRight:
                X = Px + Pw - W - MR + Position.x;
                Y = Py + Ph - H - MB + Position.y;
                break;
            case UIAnchor::TopStretch:
                X = Px + ML;
                Y = Py + MT + Position.y;
                W = Pw - (ML + MR);
                break;
            case UIAnchor::BottomStretch:
                X = Px + ML;
                Y = Py + Ph - H - MB + Position.y;
                W = Pw - (ML + MR);
                break;
            case UIAnchor::StretchAll:
                X = Px + ML;
                Y = Py + MT;
                W = Pw - (ML + MR);
                H = Ph - (MT + MB);
                break;
        }

        ComputedRect = UIRect{ X, Y, W, H };

        for (UINode* child : Children) {
            if (child && child->Visible) {
                child->OnArrange(ComputedRect);
            }
        }
    }

    void UINode::SyncTransformFromLegacy() {
        Transform.SetAnchorPreset(Anchor);
        if (Anchor == UIAnchor::StretchAll) {
            Transform.AnchorMin = { 0.0f, 0.0f };
            Transform.AnchorMax = { 1.0f, 1.0f };
            Transform.Pivot = { 0.5f, 0.5f };
            Transform.SetOffsets({ Margin.x, Margin.y }, { -Margin.z, -Margin.w }, { 0.0f, 0.0f, 100.0f, 100.0f });
        } else if (Anchor == UIAnchor::TopStretch) {
            Transform.AnchorMin = { 0.0f, 0.0f };
            Transform.AnchorMax = { 1.0f, 0.0f };
            Transform.Pivot = { 0.5f, 0.0f };
            Transform.SizeDelta = { -(Margin.x + Margin.z), Size.y };
            Transform.AnchoredPosition = { (Margin.x - Margin.z) * 0.5f, Margin.y + Position.y };
        } else if (Anchor == UIAnchor::BottomStretch) {
            Transform.AnchorMin = { 0.0f, 1.0f };
            Transform.AnchorMax = { 1.0f, 1.0f };
            Transform.Pivot = { 0.5f, 1.0f };
            Transform.SizeDelta = { -(Margin.x + Margin.z), Size.y };
            Transform.AnchoredPosition = { (Margin.x - Margin.z) * 0.5f, -(Margin.w - Position.y) };
        } else {
            Transform.SizeDelta = (DesiredSize.x > 0.0f || DesiredSize.y > 0.0f) ? DesiredSize : Size;
            Transform.AnchoredPosition = Position;
        }
    }

    void UINode::SyncLegacyFromTransform() {
        Size = Transform.SizeDelta;
        Position = Transform.AnchoredPosition;
    }

    void UINode::SetUseRectTransform(bool enable) {
        if (enable && !UseRectTransform) {
            SyncTransformFromLegacy();
        } else if (!enable && UseRectTransform) {
            SyncLegacyFromTransform();
        }
        UseRectTransform = enable;
    }

    bool UINode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "id") {
            ID = std::string(strVal);
            return true;
        }
        if (propName == "anchor" || propName == "anchors.fill" || propName == "anchors.center") {
            Anchor = ParseAnchor(strVal);
            Transform.SetAnchorPreset(Anchor);
            return true;
        }
        return false;
    }

    bool UINode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "x") { Position.x = val; Transform.AnchoredPosition.x = val; return true; }
        if (propName == "y") { Position.y = val; Transform.AnchoredPosition.y = val; return true; }
        if (propName == "width") { Size.x = val; Transform.SizeDelta.x = val; return true; }
        if (propName == "height") { Size.y = val; Transform.SizeDelta.y = val; return true; }
        if (propName == "rotation") { Transform.Rotation = val; return true; }
        if (propName == "zIndex") { ZIndex = static_cast<int32_t>(val); return true; }
        if (propName == "margin") { Margin = glm::vec4(val); return true; }
        if (propName == "padding") { Padding = glm::vec4(val); return true; }
        return false;
    }

    bool UINode::SetPropertyBool(std::string_view propName, bool val) {
        if (propName == "visible") { Visible = val; return true; }
        if (propName == "useRectTransform") { SetUseRectTransform(val); return true; }
        return false;
    }

    bool UINode::SetPropertyVec2(std::string_view propName, const glm::vec2& val) {
        if (propName == "position") { Position = val; Transform.AnchoredPosition = val; return true; }
        if (propName == "size") { Size = val; Transform.SizeDelta = val; return true; }
        if (propName == "anchorMin") { Transform.AnchorMin = val; UseRectTransform = true; return true; }
        if (propName == "anchorMax") { Transform.AnchorMax = val; UseRectTransform = true; return true; }
        if (propName == "pivot") { Transform.Pivot = val; UseRectTransform = true; return true; }
        if (propName == "anchoredPosition" || propName == "anchoredPos") { Transform.AnchoredPosition = val; UseRectTransform = true; return true; }
        if (propName == "sizeDelta") { Transform.SizeDelta = val; UseRectTransform = true; return true; }
        if (propName == "scale") { Transform.Scale = val; return true; }
        return false;
    }

    bool UINode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "margin") { Margin = val; return true; }
        if (propName == "padding") { Padding = val; return true; }
        return false;
    }

    // --- UICanvasNode ---
    bool UICanvasNode::SetPropertyVec2(std::string_view propName, const glm::vec2& val) {
        if (propName == "referenceResolution") { ReferenceResolution = val; return true; }
        return UINode::SetPropertyVec2(propName, val);
    }

    bool UICanvasNode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "referenceWidth") { ReferenceResolution.x = val; return true; }
        if (propName == "referenceHeight") { ReferenceResolution.y = val; return true; }
        return UINode::SetPropertyFloat(propName, val);
    }

    // --- UIRectangleNode ---
    bool UIRectangleNode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "backgroundColor" || propName == "color") {
            Style.BackgroundColor = ParseHexColor(strVal);
            return true;
        }
        if (propName == "borderColor") {
            Style.BorderColor = ParseHexColor(strVal);
            return true;
        }
        return UINode::SetProperty(propName, strVal);
    }

    bool UIRectangleNode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "cornerRadius" || propName == "radius") {
            Style.CornerRadius = val;
            return true;
        }
        if (propName == "borderWidth" || propName == "border") {
            Style.BorderWidth = val;
            return true;
        }
        return UINode::SetPropertyFloat(propName, val);
    }

    bool UIRectangleNode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "backgroundColor" || propName == "color") {
            Style.BackgroundColor = val;
            return true;
        }
        if (propName == "borderColor") {
            Style.BorderColor = val;
            return true;
        }
        return UINode::SetPropertyVec4(propName, val);
    }

    void UIRectangleNode::OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) {
        (void)defaultFont;
        if (!Visible) return;
        canvas.DrawRect(ComputedRect, Style);
    }

    // --- UITextNode ---
    glm::vec2 UITextNode::OnMeasure(const std::shared_ptr<UIFont>& defaultFont) {
        if (AutoSize && defaultFont) {
            DesiredSize = defaultFont->MeasureText(Text, FontSize);
        } else {
            DesiredSize = Size;
        }
        return DesiredSize;
    }

    bool UITextNode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "text") { Text = std::string(strVal); return true; }
        if (propName == "color") { Color = ParseHexColor(strVal); return true; }
        return UINode::SetProperty(propName, strVal);
    }

    bool UITextNode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "fontSize") { FontSize = val; return true; }
        return UINode::SetPropertyFloat(propName, val);
    }

    bool UITextNode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "color") { Color = val; return true; }
        return UINode::SetPropertyVec4(propName, val);
    }

    void UITextNode::OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) {
        if (!Visible || Text.empty() || !defaultFont) return;
        canvas.DrawTextString(Text, defaultFont, ComputedRect, FontSize, Color);
    }

    // --- UIImageNode ---
    bool UIImageNode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "source" || propName == "texturePath") { TexturePath = std::string(strVal); return true; }
        if (propName == "tint" || propName == "tintColor") { TintColor = ParseHexColor(strVal); return true; }
        return UINode::SetProperty(propName, strVal);
    }

    bool UIImageNode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "tint" || propName == "tintColor") { TintColor = val; return true; }
        return UINode::SetPropertyVec4(propName, val);
    }

    void UIImageNode::OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) {
        (void)defaultFont;
        if (!Visible) return;
        canvas.DrawSprite(ComputedRect, BindlessTextureIndex, TintColor);
    }

    // --- UIProgressBarNode ---
    bool UIProgressBarNode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "fillColor") { FillColor = ParseHexColor(strVal); return true; }
        if (propName == "backgroundColor") { BackgroundColor = ParseHexColor(strVal); return true; }
        return UINode::SetProperty(propName, strVal);
    }

    bool UIProgressBarNode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "value") { Value = std::clamp(val, 0.0f, 1.0f); return true; }
        if (propName == "cornerRadius") { CornerRadius = val; return true; }
        return UINode::SetPropertyFloat(propName, val);
    }

    bool UIProgressBarNode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "fillColor") { FillColor = val; return true; }
        if (propName == "backgroundColor") { BackgroundColor = val; return true; }
        return UINode::SetPropertyVec4(propName, val);
    }

    void UIProgressBarNode::OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) {
        (void)defaultFont;
        if (!Visible) return;

        // Background track
        canvas.DrawRect(ComputedRect, UIStyle{
            .BackgroundColor = BackgroundColor,
            .BorderColor = glm::vec4(0.0f),
            .CornerRadius = CornerRadius,
            .BorderWidth = 0.0f
        });

        // Foreground fill bar
        float fillW = ComputedRect.Width * std::clamp(Value, 0.0f, 1.0f);
        if (fillW > 0.0f) {
            UIRect fillRect{ ComputedRect.X, ComputedRect.Y, fillW, ComputedRect.Height };
            canvas.DrawRect(fillRect, UIStyle{
                .BackgroundColor = FillColor,
                .BorderColor = glm::vec4(0.0f),
                .CornerRadius = CornerRadius,
                .BorderWidth = 0.0f
            });
        }
    }

    // --- UIRadialGaugeNode ---
    bool UIRadialGaugeNode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "color" || propName == "fillColor") { Color = ParseHexColor(strVal); return true; }
        if (propName == "trackColor") { TrackColor = ParseHexColor(strVal); return true; }
        return UINode::SetProperty(propName, strVal);
    }

    bool UIRadialGaugeNode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "value") { Value = std::clamp(val, 0.0f, 1.0f); return true; }
        if (propName == "startAngle") { StartAngle = val; return true; }
        if (propName == "endAngle") { EndAngle = val; return true; }
        if (propName == "thickness") { Thickness = val; return true; }
        return UINode::SetPropertyFloat(propName, val);
    }

    bool UIRadialGaugeNode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "color" || propName == "fillColor") { Color = val; return true; }
        if (propName == "trackColor") { TrackColor = val; return true; }
        return UINode::SetPropertyVec4(propName, val);
    }

    void UIRadialGaugeNode::OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) {
        (void)defaultFont;
        if (!Visible) return;
        canvas.DrawRadialArc(ComputedRect, StartAngle, EndAngle, Value, Thickness, Color, TrackColor);
    }

    // --- UIBoxContainerNode (Row / Column) ---
    glm::vec2 UIBoxContainerNode::OnMeasure(const std::shared_ptr<UIFont>& defaultFont) {
        float maxChildW = 0.0f;
        float maxChildH = 0.0f;
        float sumChildW = 0.0f;
        float sumChildH = 0.0f;
        int activeCount = 0;

        for (UINode* child : Children) {
            if (child && child->Visible) {
                glm::vec2 cSize = child->OnMeasure(defaultFont);
                maxChildW = std::max(maxChildW, cSize.x);
                maxChildH = std::max(maxChildH, cSize.y);
                sumChildW += cSize.x;
                sumChildH += cSize.y;
                activeCount++;
            }
        }

        float spacingTotal = (activeCount > 1) ? static_cast<float>(activeCount - 1) * Spacing : 0.0f;

        if (IsVertical) {
            DesiredSize.x = Padding.x + Padding.z + maxChildW;
            DesiredSize.y = Padding.y + Padding.w + sumChildH + spacingTotal;
        } else {
            DesiredSize.x = Padding.x + Padding.z + sumChildW + spacingTotal;
            DesiredSize.y = Padding.y + Padding.w + maxChildH;
        }

        if (Size.x > 0.0f) DesiredSize.x = std::max(DesiredSize.x, Size.x);
        if (Size.y > 0.0f) DesiredSize.y = std::max(DesiredSize.y, Size.y);

        return DesiredSize;
    }

    void UIBoxContainerNode::OnArrange(const UIRect& parentRect) {
        // Arrange self first
        UINode::OnArrange(parentRect);

        // Stack children sequentially
        float currentX = ComputedRect.X + Padding.x;
        float currentY = ComputedRect.Y + Padding.y;

        for (UINode* child : Children) {
            if (!child || !child->Visible) continue;

            float cw = (child->DesiredSize.x > 0.0f) ? child->DesiredSize.x : child->Size.x;
            float ch = (child->DesiredSize.y > 0.0f) ? child->DesiredSize.y : child->Size.y;

            UIRect slotRect{ currentX, currentY, cw, ch };
            child->OnArrange(slotRect);

            if (IsVertical) {
                currentY += ch + Spacing;
            } else {
                currentX += cw + Spacing;
            }
        }
    }

    bool UIBoxContainerNode::SetPropertyFloat(std::string_view propName, float val) {
        if (propName == "spacing") { Spacing = val; return true; }
        return UINode::SetPropertyFloat(propName, val);
    }

    // --- UIButtonNode ---
    bool UIButtonNode::SetProperty(std::string_view propName, std::string_view strVal) {
        if (propName == "onClick" || propName == "action") { OnClickAction = std::string(strVal); return true; }
        if (propName == "navUp") { NavUp = std::string(strVal); return true; }
        if (propName == "navDown") { NavDown = std::string(strVal); return true; }
        if (propName == "navLeft") { NavLeft = std::string(strVal); return true; }
        if (propName == "navRight") { NavRight = std::string(strVal); return true; }
        if (propName == "hoverColor") { HoverColor = ParseHexColor(strVal); return true; }
        if (propName == "pressedColor") { PressedColor = ParseHexColor(strVal); return true; }
        return UIRectangleNode::SetProperty(propName, strVal);
    }

    bool UIButtonNode::SetPropertyVec4(std::string_view propName, const glm::vec4& val) {
        if (propName == "hoverColor") { HoverColor = val; return true; }
        if (propName == "pressedColor") { PressedColor = val; return true; }
        return UIRectangleNode::SetPropertyVec4(propName, val);
    }

    void UIButtonNode::OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) {
        (void)defaultFont;
        if (!Visible) return;

        UIStyle currentStyle = Style;
        if (IsPressed) {
            currentStyle.BackgroundColor = PressedColor;
        } else if (IsHovered || IsFocused) {
            currentStyle.BackgroundColor = HoverColor;
        } else {
            currentStyle.BackgroundColor = NormalColor;
        }

        canvas.DrawRect(ComputedRect, currentStyle);
    }

} // namespace Axle
