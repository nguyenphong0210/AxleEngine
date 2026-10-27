#pragma once
#include <Axle/UI/UITypes.hpp>
#include <glm/glm.hpp>
#include <cmath>

namespace Axle {

    /// @brief Unity-style geometric layout transform for 2D UI elements.
    /// Supports point anchors, stretch anchors, normalized pivots, size deltas,
    /// anchored positions, and multi-resolution responsive positioning.
    struct RectTransform {
        glm::vec2 AnchorMin{ 0.0f, 0.0f };        // Normalized [0..1] range relative to parent (Top-Left: 0,0)
        glm::vec2 AnchorMax{ 0.0f, 0.0f };        // Normalized [0..1] range relative to parent (Bottom-Right: 1,1)
        glm::vec2 Pivot{ 0.5f, 0.5f };            // Normalized [0..1] origin within element bounds (Center: 0.5, 0.5)
        glm::vec2 AnchoredPosition{ 0.0f, 0.0f }; // Pivot offset from anchor reference point
        glm::vec2 SizeDelta{ 100.0f, 100.0f };    // Size when point anchored, or margin delta when stretched
        float     Rotation = 0.0f;                // Orientation in degrees around Pivot
        glm::vec2 Scale{ 1.0f, 1.0f };            // Local scale factor

        // Geometric evaluation
        UIRect ComputeRect(const UIRect& parentRect) const;
        glm::vec2 GetPivotScreenPos(const UIRect& parentRect) const;

        // Inset margins (OffsetMin / OffsetMax)
        // OffsetMin: offset from AnchorMin point to element Top-Left corner
        // OffsetMax: offset from AnchorMax point to element Bottom-Right corner
        glm::vec2 GetOffsetMin(const UIRect& parentRect) const;
        glm::vec2 GetOffsetMax(const UIRect& parentRect) const;
        void SetOffsetMin(const glm::vec2& offsetMin, const UIRect& parentRect);
        void SetOffsetMax(const glm::vec2& offsetMax, const UIRect& parentRect);
        void SetOffsets(const glm::vec2& offsetMin, const glm::vec2& offsetMax, const UIRect& parentRect);

        // Adjust AnchoredPosition and SizeDelta to fit targetRect without changing anchors or pivot
        void SetRect(const UIRect& targetRect, const UIRect& parentRect);

        // Anchor Presets
        void SetAnchorPreset(UIAnchor preset, bool keepScreenRect = false, const UIRect& parentRect = {});

        // Stretch checks
        bool IsStretchedHorizontal() const { return std::abs(AnchorMax.x - AnchorMin.x) > 1e-5f; }
        bool IsStretchedVertical() const { return std::abs(AnchorMax.y - AnchorMin.y) > 1e-5f; }
        bool IsStretched() const { return IsStretchedHorizontal() || IsStretchedVertical(); }

        // Corners in screen space (accounting for Rotation & Scale around Pivot)
        // Order: 0: Top-Left, 1: Top-Right, 2: Bottom-Right, 3: Bottom-Left
        void GetWorldCorners(const UIRect& parentRect, glm::vec2 outCorners[4]) const;
    };

} // namespace Axle
