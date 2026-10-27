#pragma once
#include <Axle/UI/UITypes.hpp>
#include <Axle/UI/RectTransform.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

namespace Axle {

    enum class Gizmo2DHandle : uint32_t {
        None = 0,
        Body,            // Translate entire element
        Corner_TL,       // Resize Top-Left
        Corner_TR,       // Resize Top-Right
        Corner_BR,       // Resize Bottom-Right
        Corner_BL,       // Resize Bottom-Left
        Edge_T,          // Resize Top
        Edge_B,          // Resize Bottom
        Edge_L,          // Resize Left
        Edge_R,          // Resize Right
        Pivot,           // Reposition Pivot
        Anchor_TL,       // Anchor Min/Max Top-Left petal
        Anchor_TR,       // Anchor Min/Max Top-Right petal
        Anchor_BR,       // Anchor Min/Max Bottom-Right petal
        Anchor_BL        // Anchor Min/Max Bottom-Left petal
    };

    struct RectTransformGizmoSettings {
        bool  ShowCanvasBounds   = true;
        bool  ShowAnchorPetals   = true;
        bool  ShowPivotCrosshair = true;
        bool  ShowResizeHandles  = true;
        bool  SnapToGrid         = false;
        bool  SnapIsPercentage   = false;     // false = Pixel increment, true = Percentage increment
        float SnapIncrement      = 10.0f;     // Grid increment in canvas pixels (e.g. 1px, 5px, 10px, 25px)
        float SnapPercent        = 5.0f;      // Grid increment in percentage (e.g. 1%, 5%, 10%, 25%)
        ImU32 CanvasColor        = IM_COL32(100, 116, 139, 140);   // Slate outline
        ImU32 BoundsColor        = IM_COL32(56, 189, 248, 255);    // Sky cyan
        ImU32 BoundsFillColor    = IM_COL32(56, 189, 248, 25);     // Sky cyan fill
        ImU32 HandleColor        = IM_COL32(255, 255, 255, 255);    // White
        ImU32 HandleHoverColor   = IM_COL32(251, 191, 36, 255);     // Amber yellow
        ImU32 PivotColor         = IM_COL32(14, 165, 233, 255);     // Cyan-blue
        ImU32 AnchorColor        = IM_COL32(236, 72, 153, 255);    // Magenta/Pink (Unity style)
    };

    class RectTransformGizmo {
    public:
        static RectTransformGizmo& Get();

        /// @brief Draw interactive 2D canvas gizmo overlay on top of active Viewport
        /// @param parentRect Canvas reference rect (e.g. 1920x1080)
        /// @param targetTransform RectTransform to manipulate
        /// @param viewportScreenPos Top-left screen coordinate of Viewport window
        /// @param viewportScreenSize Dimensions of Viewport window
        /// @param isViewportHovered Whether the Viewport window is currently hovered
        /// @param outModified Optional flag set to true if mouse drag altered targetTransform
        /// @return true if mouse is currently interacting with or hovering over gizmo handles
        bool DrawGizmo(
            const UIRect& parentRect,
            RectTransform& targetTransform,
            const glm::vec2& viewportScreenPos,
            const glm::vec2& viewportScreenSize,
            bool isViewportHovered,
            bool* outModified = nullptr
        );

        /// @brief Headless hit-test evaluator for automated tests
        Gizmo2DHandle HitTest(
            const UIRect& parentRect,
            const RectTransform& transform,
            const glm::vec2& viewportScreenPos,
            const glm::vec2& viewportScreenSize,
            const glm::vec2& mouseScreenPos
        ) const;

        bool IsUsing() const { return m_ActiveHandle != Gizmo2DHandle::None; }
        bool IsOver() const { return m_HoveredHandle != Gizmo2DHandle::None; }
        Gizmo2DHandle GetActiveHandle() const { return m_ActiveHandle; }
        Gizmo2DHandle GetHoveredHandle() const { return m_HoveredHandle; }

        RectTransformGizmoSettings& GetSettings() { return m_Settings; }
        const RectTransformGizmoSettings& GetSettings() const { return m_Settings; }

        /// @brief Calculate effective canvas snap step based on pixel or percentage settings
        glm::vec2 ComputeSnapIncrement(const UIRect& parentRect) const;

    private:
        RectTransformGizmoSettings m_Settings{};
        Gizmo2DHandle m_ActiveHandle = Gizmo2DHandle::None;
        Gizmo2DHandle m_HoveredHandle = Gizmo2DHandle::None;

        glm::vec2 m_DragStartMousePos{ 0.0f };
        RectTransform m_InitialTransform{};
        UIRect m_InitialRect{};
    };

} // namespace Axle
