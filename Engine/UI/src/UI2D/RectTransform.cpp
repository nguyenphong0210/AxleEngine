#include <Axle/UI/RectTransform.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    UIRect RectTransform::ComputeRect(const UIRect& parentRect) const {
        glm::vec2 parentPos(parentRect.X, parentRect.Y);
        glm::vec2 parentSize(parentRect.Width, parentRect.Height);

        glm::vec2 anchorMinScreen = parentPos + AnchorMin * parentSize;
        glm::vec2 anchorMaxScreen = parentPos + AnchorMax * parentSize;
        glm::vec2 anchorSpan = anchorMaxScreen - anchorMinScreen;

        glm::vec2 computedSize = anchorSpan + SizeDelta;
        glm::vec2 anchorRef = anchorMinScreen + Pivot * anchorSpan;
        glm::vec2 pivotScreenPos = anchorRef + AnchoredPosition;

        glm::vec2 rectPos = pivotScreenPos - Pivot * computedSize;

        return UIRect{ rectPos.x, rectPos.y, computedSize.x, computedSize.y };
    }

    glm::vec2 RectTransform::GetPivotScreenPos(const UIRect& parentRect) const {
        glm::vec2 parentPos(parentRect.X, parentRect.Y);
        glm::vec2 parentSize(parentRect.Width, parentRect.Height);

        glm::vec2 anchorMinScreen = parentPos + AnchorMin * parentSize;
        glm::vec2 anchorMaxScreen = parentPos + AnchorMax * parentSize;
        glm::vec2 anchorSpan = anchorMaxScreen - anchorMinScreen;

        glm::vec2 anchorRef = anchorMinScreen + Pivot * anchorSpan;
        return anchorRef + AnchoredPosition;
    }

    glm::vec2 RectTransform::GetOffsetMin(const UIRect& parentRect) const {
        UIRect r = ComputeRect(parentRect);
        glm::vec2 anchorMinScreen = glm::vec2(parentRect.X, parentRect.Y) + AnchorMin * glm::vec2(parentRect.Width, parentRect.Height);
        return glm::vec2(r.X, r.Y) - anchorMinScreen;
    }

    glm::vec2 RectTransform::GetOffsetMax(const UIRect& parentRect) const {
        UIRect r = ComputeRect(parentRect);
        glm::vec2 anchorMaxScreen = glm::vec2(parentRect.X, parentRect.Y) + AnchorMax * glm::vec2(parentRect.Width, parentRect.Height);
        return glm::vec2(r.X + r.Width, r.Y + r.Height) - anchorMaxScreen;
    }

    void RectTransform::SetOffsets(const glm::vec2& offsetMin, const glm::vec2& offsetMax, const UIRect& parentRect) {
        SizeDelta = offsetMax - offsetMin;
        AnchoredPosition = offsetMin + Pivot * SizeDelta;
    }

    void RectTransform::SetOffsetMin(const glm::vec2& offsetMin, const UIRect& parentRect) {
        glm::vec2 currentMax = GetOffsetMax(parentRect);
        SetOffsets(offsetMin, currentMax, parentRect);
    }

    void RectTransform::SetOffsetMax(const glm::vec2& offsetMax, const UIRect& parentRect) {
        glm::vec2 currentMin = GetOffsetMin(parentRect);
        SetOffsets(currentMin, offsetMax, parentRect);
    }

    void RectTransform::SetRect(const UIRect& targetRect, const UIRect& parentRect) {
        glm::vec2 parentPos(parentRect.X, parentRect.Y);
        glm::vec2 parentSize(parentRect.Width, parentRect.Height);

        glm::vec2 anchorMinScreen = parentPos + AnchorMin * parentSize;
        glm::vec2 anchorMaxScreen = parentPos + AnchorMax * parentSize;
        glm::vec2 anchorSpan = anchorMaxScreen - anchorMinScreen;

        glm::vec2 targetSize(targetRect.Width, targetRect.Height);
        SizeDelta = targetSize - anchorSpan;

        glm::vec2 targetPos(targetRect.X, targetRect.Y);
        glm::vec2 pivotScreen = targetPos + Pivot * targetSize;
        glm::vec2 anchorRef = anchorMinScreen + Pivot * anchorSpan;

        AnchoredPosition = pivotScreen - anchorRef;
    }

    void RectTransform::SetAnchorPreset(UIAnchor preset, bool keepScreenRect, const UIRect& parentRect) {
        UIRect currentRect{};
        if (keepScreenRect && parentRect.Width > 0.0f && parentRect.Height > 0.0f) {
            currentRect = ComputeRect(parentRect);
        }

        switch (preset) {
            case UIAnchor::TopLeft:
                AnchorMin = { 0.0f, 0.0f }; AnchorMax = { 0.0f, 0.0f }; Pivot = { 0.0f, 0.0f }; break;
            case UIAnchor::TopCenter:
                AnchorMin = { 0.5f, 0.0f }; AnchorMax = { 0.5f, 0.0f }; Pivot = { 0.5f, 0.0f }; break;
            case UIAnchor::TopRight:
                AnchorMin = { 1.0f, 0.0f }; AnchorMax = { 1.0f, 0.0f }; Pivot = { 1.0f, 0.0f }; break;
            case UIAnchor::CenterLeft:
                AnchorMin = { 0.0f, 0.5f }; AnchorMax = { 0.0f, 0.5f }; Pivot = { 0.0f, 0.5f }; break;
            case UIAnchor::Center:
                AnchorMin = { 0.5f, 0.5f }; AnchorMax = { 0.5f, 0.5f }; Pivot = { 0.5f, 0.5f }; break;
            case UIAnchor::CenterRight:
                AnchorMin = { 1.0f, 0.5f }; AnchorMax = { 1.0f, 0.5f }; Pivot = { 1.0f, 0.5f }; break;
            case UIAnchor::BottomLeft:
                AnchorMin = { 0.0f, 1.0f }; AnchorMax = { 0.0f, 1.0f }; Pivot = { 0.0f, 1.0f }; break;
            case UIAnchor::BottomCenter:
                AnchorMin = { 0.5f, 1.0f }; AnchorMax = { 0.5f, 1.0f }; Pivot = { 0.5f, 1.0f }; break;
            case UIAnchor::BottomRight:
                AnchorMin = { 1.0f, 1.0f }; AnchorMax = { 1.0f, 1.0f }; Pivot = { 1.0f, 1.0f }; break;
            case UIAnchor::TopStretch:
                AnchorMin = { 0.0f, 0.0f }; AnchorMax = { 1.0f, 0.0f }; Pivot = { 0.5f, 0.0f }; break;
            case UIAnchor::BottomStretch:
                AnchorMin = { 0.0f, 1.0f }; AnchorMax = { 1.0f, 1.0f }; Pivot = { 0.5f, 1.0f }; break;
            case UIAnchor::StretchAll:
                AnchorMin = { 0.0f, 0.0f }; AnchorMax = { 1.0f, 1.0f }; Pivot = { 0.5f, 0.5f }; break;
        }

        if (keepScreenRect && parentRect.Width > 0.0f && parentRect.Height > 0.0f) {
            SetRect(currentRect, parentRect);
        }
    }

    void RectTransform::GetWorldCorners(const UIRect& parentRect, glm::vec2 outCorners[4]) const {
        UIRect r = ComputeRect(parentRect);
        glm::vec2 p0(r.X, r.Y);                         // Top-Left
        glm::vec2 p1(r.X + r.Width, r.Y);               // Top-Right
        glm::vec2 p2(r.X + r.Width, r.Y + r.Height);     // Bottom-Right
        glm::vec2 p3(r.X, r.Y + r.Height);              // Bottom-Left

        if (std::abs(Rotation) < 1e-4f && std::abs(Scale.x - 1.0f) < 1e-4f && std::abs(Scale.y - 1.0f) < 1e-4f) {
            outCorners[0] = p0;
            outCorners[1] = p1;
            outCorners[2] = p2;
            outCorners[3] = p3;
            return;
        }

        glm::vec2 pivotPos = GetPivotScreenPos(parentRect);
        float rad = glm::radians(Rotation);
        float cosA = std::cos(rad);
        float sinA = std::sin(rad);

        auto transformPoint = [&](const glm::vec2& p) -> glm::vec2 {
            glm::vec2 d = (p - pivotPos) * Scale;
            return pivotPos + glm::vec2(d.x * cosA - d.y * sinA, d.x * sinA + d.y * cosA);
        };

        outCorners[0] = transformPoint(p0);
        outCorners[1] = transformPoint(p1);
        outCorners[2] = transformPoint(p2);
        outCorners[3] = transformPoint(p3);
    }

} // namespace Axle
