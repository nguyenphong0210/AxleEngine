#include <Axle/UI/RectTransformGizmo.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace Axle {

    static inline ImVec2 ToIm(const glm::vec2& v) { return ImVec2(v.x, v.y); }
    static inline glm::vec2 ToGlm(const ImVec2& v) { return glm::vec2(v.x, v.y); }

    RectTransformGizmo& RectTransformGizmo::Get() {
        static RectTransformGizmo s_Instance;
        return s_Instance;
    }

    struct CanvasViewportMapping {
        float Scale = 1.0f;
        glm::vec2 Origin{ 0.0f };
        glm::vec2 DisplaySize{ 0.0f };

        glm::vec2 CanvasToScreen(const glm::vec2& pt) const {
            return Origin + pt * Scale;
        }

        glm::vec2 ScreenToCanvas(const glm::vec2& pt) const {
            return (Scale > 1e-6f) ? ((pt - Origin) / Scale) : pt;
        }
    };

    static CanvasViewportMapping ComputeMapping(
        const UIRect& parentRect,
        const glm::vec2& viewportScreenPos,
        const glm::vec2& viewportScreenSize
    ) {
        CanvasViewportMapping m;
        float refW = (parentRect.Width > 0.0f) ? parentRect.Width : 1920.0f;
        float refH = (parentRect.Height > 0.0f) ? parentRect.Height : 1080.0f;

        float sx = viewportScreenSize.x / refW;
        float sy = viewportScreenSize.y / refH;
        m.Scale = std::min(sx, sy);
        if (m.Scale <= 0.0f) m.Scale = 1.0f;

        m.DisplaySize = glm::vec2(refW, refH) * m.Scale;
        m.Origin = viewportScreenPos + (viewportScreenSize - m.DisplaySize) * 0.5f;
        return m;
    }

    Gizmo2DHandle RectTransformGizmo::HitTest(
        const UIRect& parentRect,
        const RectTransform& transform,
        const glm::vec2& viewportScreenPos,
        const glm::vec2& viewportScreenSize,
        const glm::vec2& mouseScreenPos
    ) const {
        CanvasViewportMapping mapping = ComputeMapping(parentRect, viewportScreenPos, viewportScreenSize);

        // 1. Check Pivot
        if (m_Settings.ShowPivotCrosshair) {
            glm::vec2 pivotCanvas = transform.GetPivotScreenPos(parentRect);
            glm::vec2 pivotScreen = mapping.CanvasToScreen(pivotCanvas);
            if (glm::distance(mouseScreenPos, pivotScreen) <= 9.0f) {
                return Gizmo2DHandle::Pivot;
            }
        }

        // 2. Check 4 Anchor Petals
        if (m_Settings.ShowAnchorPetals) {
            glm::vec2 parentPos(parentRect.X, parentRect.Y);
            glm::vec2 parentSize(parentRect.Width, parentRect.Height);
            glm::vec2 aMin = mapping.CanvasToScreen(parentPos + transform.AnchorMin * parentSize);
            glm::vec2 aMax = mapping.CanvasToScreen(parentPos + transform.AnchorMax * parentSize);

            glm::vec2 anchorTL = aMin;
            glm::vec2 anchorTR = glm::vec2(aMax.x, aMin.y);
            glm::vec2 anchorBR = aMax;
            glm::vec2 anchorBL = glm::vec2(aMin.x, aMax.y);

            const float kAnchorHitRadius = 9.0f;
            if (glm::distance(mouseScreenPos, anchorTL) <= kAnchorHitRadius) return Gizmo2DHandle::Anchor_TL;
            if (glm::distance(mouseScreenPos, anchorTR) <= kAnchorHitRadius) return Gizmo2DHandle::Anchor_TR;
            if (glm::distance(mouseScreenPos, anchorBR) <= kAnchorHitRadius) return Gizmo2DHandle::Anchor_BR;
            if (glm::distance(mouseScreenPos, anchorBL) <= kAnchorHitRadius) return Gizmo2DHandle::Anchor_BL;
        }

        // 3. Check 8 Resize Handles
        UIRect r = transform.ComputeRect(parentRect);
        glm::vec2 p0 = mapping.CanvasToScreen(glm::vec2(r.X, r.Y));
        glm::vec2 p1 = mapping.CanvasToScreen(glm::vec2(r.X + r.Width, r.Y));
        glm::vec2 p2 = mapping.CanvasToScreen(glm::vec2(r.X + r.Width, r.Y + r.Height));
        glm::vec2 p3 = mapping.CanvasToScreen(glm::vec2(r.X, r.Y + r.Height));

        if (m_Settings.ShowResizeHandles) {
            const float kHandleHitRadius = 7.0f;
            if (glm::distance(mouseScreenPos, p0) <= kHandleHitRadius) return Gizmo2DHandle::Corner_TL;
            if (glm::distance(mouseScreenPos, p1) <= kHandleHitRadius) return Gizmo2DHandle::Corner_TR;
            if (glm::distance(mouseScreenPos, p2) <= kHandleHitRadius) return Gizmo2DHandle::Corner_BR;
            if (glm::distance(mouseScreenPos, p3) <= kHandleHitRadius) return Gizmo2DHandle::Corner_BL;

            glm::vec2 midT = (p0 + p1) * 0.5f;
            glm::vec2 midR = (p1 + p2) * 0.5f;
            glm::vec2 midB = (p2 + p3) * 0.5f;
            glm::vec2 midL = (p3 + p0) * 0.5f;

            if (glm::distance(mouseScreenPos, midT) <= kHandleHitRadius) return Gizmo2DHandle::Edge_T;
            if (glm::distance(mouseScreenPos, midR) <= kHandleHitRadius) return Gizmo2DHandle::Edge_R;
            if (glm::distance(mouseScreenPos, midB) <= kHandleHitRadius) return Gizmo2DHandle::Edge_B;
            if (glm::distance(mouseScreenPos, midL) <= kHandleHitRadius) return Gizmo2DHandle::Edge_L;
        }

        // 4. Check Body (inside bounding rectangle)
        float minX = std::min({ p0.x, p1.x, p2.x, p3.x });
        float maxX = std::max({ p0.x, p1.x, p2.x, p3.x });
        float minY = std::min({ p0.y, p1.y, p2.y, p3.y });
        float maxY = std::max({ p0.y, p1.y, p2.y, p3.y });

        if (mouseScreenPos.x >= minX && mouseScreenPos.x <= maxX &&
            mouseScreenPos.y >= minY && mouseScreenPos.y <= maxY) {
            return Gizmo2DHandle::Body;
        }

        return Gizmo2DHandle::None;
    }

    bool RectTransformGizmo::DrawGizmo(
        const UIRect& parentRect,
        RectTransform& targetTransform,
        const glm::vec2& viewportScreenPos,
        const glm::vec2& viewportScreenSize,
        bool isViewportHovered,
        bool* outModified
    ) {
        if (outModified) *outModified = false;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!drawList) return false;

        CanvasViewportMapping mapping = ComputeMapping(parentRect, viewportScreenPos, viewportScreenSize);
        ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 mousePosGlm = ToGlm(mousePos);

        // 1. Draw Canvas Reference Resolution Outline
        if (m_Settings.ShowCanvasBounds) {
            ImVec2 cMin = ToIm(mapping.Origin);
            ImVec2 cMax = ToIm(mapping.Origin + mapping.DisplaySize);
            drawList->AddRect(cMin, cMax, m_Settings.CanvasColor, 0.0f, 0, 1.5f);

            std::string label = "Canvas (" + std::to_string(static_cast<int>(parentRect.Width)) +
                                " x " + std::to_string(static_cast<int>(parentRect.Height)) + ")";
            drawList->AddText(ImVec2(cMin.x + 8.0f, cMin.y + 6.0f), m_Settings.CanvasColor, label.c_str());
        }

        // 2. Compute Element Bounds & Corner Positions
        UIRect r = targetTransform.ComputeRect(parentRect);
        glm::vec2 p0 = mapping.CanvasToScreen(glm::vec2(r.X, r.Y));
        glm::vec2 p1 = mapping.CanvasToScreen(glm::vec2(r.X + r.Width, r.Y));
        glm::vec2 p2 = mapping.CanvasToScreen(glm::vec2(r.X + r.Width, r.Y + r.Height));
        glm::vec2 p3 = mapping.CanvasToScreen(glm::vec2(r.X, r.Y + r.Height));

        glm::vec2 midT = (p0 + p1) * 0.5f;
        glm::vec2 midR = (p1 + p2) * 0.5f;
        glm::vec2 midB = (p2 + p3) * 0.5f;
        glm::vec2 midL = (p3 + p0) * 0.5f;

        // 3. Compute Anchor Petal Screen Points
        glm::vec2 parentPos(parentRect.X, parentRect.Y);
        glm::vec2 parentSize(parentRect.Width, parentRect.Height);
        glm::vec2 aMin = mapping.CanvasToScreen(parentPos + targetTransform.AnchorMin * parentSize);
        glm::vec2 aMax = mapping.CanvasToScreen(parentPos + targetTransform.AnchorMax * parentSize);

        glm::vec2 anchorTL = aMin;
        glm::vec2 anchorTR = glm::vec2(aMax.x, aMin.y);
        glm::vec2 anchorBR = aMax;
        glm::vec2 anchorBL = glm::vec2(aMin.x, aMax.y);

        // 4. Compute Pivot Screen Point
        glm::vec2 pivotCanvas = targetTransform.GetPivotScreenPos(parentRect);
        glm::vec2 pivotScreen = mapping.CanvasToScreen(pivotCanvas);

        // 5. Hit Testing & Hover State
        if (m_ActiveHandle == Gizmo2DHandle::None) {
            if (isViewportHovered) {
                m_HoveredHandle = HitTest(parentRect, targetTransform, viewportScreenPos, viewportScreenSize, mousePosGlm);
            } else {
                m_HoveredHandle = Gizmo2DHandle::None;
            }
        }

        // 6. Mouse Interaction State Machine
        bool modified = false;

        if (isViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (m_HoveredHandle != Gizmo2DHandle::None) {
                m_ActiveHandle = m_HoveredHandle;
                m_DragStartMousePos = mousePosGlm;
                m_InitialTransform = targetTransform;
                m_InitialRect = r;
            }
        }

        if (m_ActiveHandle != Gizmo2DHandle::None) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                glm::vec2 mouseDeltaScreen = mousePosGlm - m_DragStartMousePos;
                glm::vec2 mouseDeltaCanvas = mouseDeltaScreen / mapping.Scale;

                glm::vec2 snapStep = ComputeSnapIncrement(parentRect);
                if (m_Settings.SnapToGrid) {
                    if (snapStep.x > 0.0f) {
                        mouseDeltaCanvas.x = glm::round(mouseDeltaCanvas.x / snapStep.x) * snapStep.x;
                    }
                    if (snapStep.y > 0.0f) {
                        mouseDeltaCanvas.y = glm::round(mouseDeltaCanvas.y / snapStep.y) * snapStep.y;
                    }
                }

                switch (m_ActiveHandle) {
                    case Gizmo2DHandle::Body: {
                        targetTransform.AnchoredPosition = m_InitialTransform.AnchoredPosition + mouseDeltaCanvas;
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Corner_BR: {
                        UIRect tr = m_InitialRect;
                        tr.Width = std::max(10.0f, m_InitialRect.Width + mouseDeltaCanvas.x);
                        tr.Height = std::max(10.0f, m_InitialRect.Height + mouseDeltaCanvas.y);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Corner_TL: {
                        UIRect tr = m_InitialRect;
                        tr.X = m_InitialRect.X + mouseDeltaCanvas.x;
                        tr.Y = m_InitialRect.Y + mouseDeltaCanvas.y;
                        tr.Width = std::max(10.0f, m_InitialRect.Width - mouseDeltaCanvas.x);
                        tr.Height = std::max(10.0f, m_InitialRect.Height - mouseDeltaCanvas.y);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Corner_TR: {
                        UIRect tr = m_InitialRect;
                        tr.Y = m_InitialRect.Y + mouseDeltaCanvas.y;
                        tr.Width = std::max(10.0f, m_InitialRect.Width + mouseDeltaCanvas.x);
                        tr.Height = std::max(10.0f, m_InitialRect.Height - mouseDeltaCanvas.y);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Corner_BL: {
                        UIRect tr = m_InitialRect;
                        tr.X = m_InitialRect.X + mouseDeltaCanvas.x;
                        tr.Width = std::max(10.0f, m_InitialRect.Width - mouseDeltaCanvas.x);
                        tr.Height = std::max(10.0f, m_InitialRect.Height + mouseDeltaCanvas.y);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Edge_R: {
                        UIRect tr = m_InitialRect;
                        tr.Width = std::max(10.0f, m_InitialRect.Width + mouseDeltaCanvas.x);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Edge_L: {
                        UIRect tr = m_InitialRect;
                        tr.X = m_InitialRect.X + mouseDeltaCanvas.x;
                        tr.Width = std::max(10.0f, m_InitialRect.Width - mouseDeltaCanvas.x);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Edge_B: {
                        UIRect tr = m_InitialRect;
                        tr.Height = std::max(10.0f, m_InitialRect.Height + mouseDeltaCanvas.y);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Edge_T: {
                        UIRect tr = m_InitialRect;
                        tr.Y = m_InitialRect.Y + mouseDeltaCanvas.y;
                        tr.Height = std::max(10.0f, m_InitialRect.Height - mouseDeltaCanvas.y);
                        targetTransform.SetRect(tr, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Pivot: {
                        glm::vec2 newPivotPos = m_InitialTransform.GetPivotScreenPos(parentRect) + mouseDeltaCanvas;
                        targetTransform.Pivot.x = glm::clamp((newPivotPos.x - m_InitialRect.X) / m_InitialRect.Width, 0.0f, 1.0f);
                        targetTransform.Pivot.y = glm::clamp((newPivotPos.y - m_InitialRect.Y) / m_InitialRect.Height, 0.0f, 1.0f);
                        targetTransform.SetRect(m_InitialRect, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Anchor_TL: {
                        glm::vec2 normDelta = mouseDeltaCanvas / parentSize;
                        targetTransform.AnchorMin.x = glm::clamp(m_InitialTransform.AnchorMin.x + normDelta.x, 0.0f, targetTransform.AnchorMax.x);
                        targetTransform.AnchorMin.y = glm::clamp(m_InitialTransform.AnchorMin.y + normDelta.y, 0.0f, targetTransform.AnchorMax.y);
                        targetTransform.SetRect(m_InitialRect, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Anchor_TR: {
                        glm::vec2 normDelta = mouseDeltaCanvas / parentSize;
                        targetTransform.AnchorMax.x = glm::clamp(m_InitialTransform.AnchorMax.x + normDelta.x, targetTransform.AnchorMin.x, 1.0f);
                        targetTransform.AnchorMin.y = glm::clamp(m_InitialTransform.AnchorMin.y + normDelta.y, 0.0f, targetTransform.AnchorMax.y);
                        targetTransform.SetRect(m_InitialRect, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Anchor_BR: {
                        glm::vec2 normDelta = mouseDeltaCanvas / parentSize;
                        targetTransform.AnchorMax.x = glm::clamp(m_InitialTransform.AnchorMax.x + normDelta.x, targetTransform.AnchorMin.x, 1.0f);
                        targetTransform.AnchorMax.y = glm::clamp(m_InitialTransform.AnchorMax.y + normDelta.y, targetTransform.AnchorMin.y, 1.0f);
                        targetTransform.SetRect(m_InitialRect, parentRect);
                        modified = true;
                        break;
                    }
                    case Gizmo2DHandle::Anchor_BL: {
                        glm::vec2 normDelta = mouseDeltaCanvas / parentSize;
                        targetTransform.AnchorMin.x = glm::clamp(m_InitialTransform.AnchorMin.x + normDelta.x, 0.0f, targetTransform.AnchorMax.x);
                        targetTransform.AnchorMax.y = glm::clamp(m_InitialTransform.AnchorMax.y + normDelta.y, targetTransform.AnchorMin.y, 1.0f);
                        targetTransform.SetRect(m_InitialRect, parentRect);
                        modified = true;
                        break;
                    }
                    default:
                        break;
                }
            } else {
                m_ActiveHandle = Gizmo2DHandle::None;
            }
        }

        // 7. Mouse Cursor Feedback
        Gizmo2DHandle currentHandle = (m_ActiveHandle != Gizmo2DHandle::None) ? m_ActiveHandle : m_HoveredHandle;
        switch (currentHandle) {
            case Gizmo2DHandle::Corner_TL:
            case Gizmo2DHandle::Corner_BR:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                break;
            case Gizmo2DHandle::Corner_TR:
            case Gizmo2DHandle::Corner_BL:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                break;
            case Gizmo2DHandle::Edge_T:
            case Gizmo2DHandle::Edge_B:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                break;
            case Gizmo2DHandle::Edge_L:
            case Gizmo2DHandle::Edge_R:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case Gizmo2DHandle::Body:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                break;
            case Gizmo2DHandle::Pivot:
            case Gizmo2DHandle::Anchor_TL:
            case Gizmo2DHandle::Anchor_TR:
            case Gizmo2DHandle::Anchor_BR:
            case Gizmo2DHandle::Anchor_BL:
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                break;
            default:
                break;
        }

        // 8. Visual Overlay Rendering: Bounding Rect
        drawList->AddQuadFilled(ToIm(p0), ToIm(p1), ToIm(p2), ToIm(p3), m_Settings.BoundsFillColor);
        drawList->AddQuad(ToIm(p0), ToIm(p1), ToIm(p2), ToIm(p3), m_Settings.BoundsColor, 2.0f);

        // 9. Visual Overlay: 8 Peripheral Resize Handles
        if (m_Settings.ShowResizeHandles) {
            auto drawHandle = [&](const glm::vec2& pt, Gizmo2DHandle h) {
                bool isHover = (m_HoveredHandle == h || m_ActiveHandle == h);
                ImU32 col = isHover ? m_Settings.HandleHoverColor : m_Settings.HandleColor;
                const float s = 4.0f;
                drawList->AddRectFilled(ImVec2(pt.x - s, pt.y - s), ImVec2(pt.x + s, pt.y + s), col);
                drawList->AddRect(ImVec2(pt.x - s, pt.y - s), ImVec2(pt.x + s, pt.y + s), IM_COL32(15, 23, 42, 255), 0.0f, 0, 1.2f);
            };

            drawHandle(p0, Gizmo2DHandle::Corner_TL);
            drawHandle(p1, Gizmo2DHandle::Corner_TR);
            drawHandle(p2, Gizmo2DHandle::Corner_BR);
            drawHandle(p3, Gizmo2DHandle::Corner_BL);

            drawHandle(midT, Gizmo2DHandle::Edge_T);
            drawHandle(midR, Gizmo2DHandle::Edge_R);
            drawHandle(midB, Gizmo2DHandle::Edge_B);
            drawHandle(midL, Gizmo2DHandle::Edge_L);
        }

        // 10. Visual Overlay: 4 Anchor Petals
        if (m_Settings.ShowAnchorPetals) {
            // Draw anchor bounds lines if stretched
            if (targetTransform.IsStretched()) {
                drawList->AddLine(ToIm(anchorTL), ToIm(anchorTR), m_Settings.AnchorColor, 1.0f);
                drawList->AddLine(ToIm(anchorTR), ToIm(anchorBR), m_Settings.AnchorColor, 1.0f);
                drawList->AddLine(ToIm(anchorBR), ToIm(anchorBL), m_Settings.AnchorColor, 1.0f);
                drawList->AddLine(ToIm(anchorBL), ToIm(anchorTL), m_Settings.AnchorColor, 1.0f);
            }

            auto drawPetal = [&](const glm::vec2& pt, glm::vec2 dir, Gizmo2DHandle h) {
                bool isHover = (m_HoveredHandle == h || m_ActiveHandle == h);
                ImU32 col = isHover ? m_Settings.HandleHoverColor : m_Settings.AnchorColor;

                // Triangle pointing in 'dir' direction
                glm::vec2 perp(-dir.y, dir.x);
                glm::vec2 tip = pt + dir * 8.0f;
                glm::vec2 base1 = pt + perp * 5.0f;
                glm::vec2 base2 = pt - perp * 5.0f;

                drawList->AddTriangleFilled(ToIm(tip), ToIm(base1), ToIm(base2), col);
                drawList->AddTriangle(ToIm(tip), ToIm(base1), ToIm(base2), IM_COL32(15, 23, 42, 255), 1.0f);
            };

            // Petals point toward center of anchor span
            glm::vec2 centerAnchor = (anchorTL + anchorBR) * 0.5f;
            auto getDir = [&](const glm::vec2& p) {
                glm::vec2 d = centerAnchor - p;
                if (glm::length(d) < 1.0f) return glm::vec2(1.0f, 0.0f);
                return glm::normalize(d);
            };

            drawPetal(anchorTL, getDir(anchorTL), Gizmo2DHandle::Anchor_TL);
            drawPetal(anchorTR, getDir(anchorTR), Gizmo2DHandle::Anchor_TR);
            drawPetal(anchorBR, getDir(anchorBR), Gizmo2DHandle::Anchor_BR);
            drawPetal(anchorBL, getDir(anchorBL), Gizmo2DHandle::Anchor_BL);
        }

        // 11. Visual Overlay: Pivot Crosshair
        if (m_Settings.ShowPivotCrosshair) {
            bool isPivotHover = (m_HoveredHandle == Gizmo2DHandle::Pivot || m_ActiveHandle == Gizmo2DHandle::Pivot);
            ImU32 pivotCol = isPivotHover ? m_Settings.HandleHoverColor : m_Settings.PivotColor;

            drawList->AddCircleFilled(ToIm(pivotScreen), 4.5f, pivotCol);
            drawList->AddCircle(ToIm(pivotScreen), 7.0f, IM_COL32(255, 255, 255, 230), 16, 1.2f);
            drawList->AddCircle(ToIm(pivotScreen), 8.0f, IM_COL32(15, 23, 42, 255), 16, 1.0f);

            // Crosshair lines
            drawList->AddLine(ImVec2(pivotScreen.x - 10.0f, pivotScreen.y), ImVec2(pivotScreen.x - 3.0f, pivotScreen.y), pivotCol, 1.5f);
            drawList->AddLine(ImVec2(pivotScreen.x + 3.0f, pivotScreen.y), ImVec2(pivotScreen.x + 10.0f, pivotScreen.y), pivotCol, 1.5f);
            drawList->AddLine(ImVec2(pivotScreen.x, pivotScreen.y - 10.0f), ImVec2(pivotScreen.x, pivotScreen.y - 3.0f), pivotCol, 1.5f);
            drawList->AddLine(ImVec2(pivotScreen.x, pivotScreen.y + 3.0f), ImVec2(pivotScreen.x, pivotScreen.y + 10.0f), pivotCol, 1.5f);
        }

        if (outModified) *outModified = modified;
        return (m_ActiveHandle != Gizmo2DHandle::None || m_HoveredHandle != Gizmo2DHandle::None);
    }

    glm::vec2 RectTransformGizmo::ComputeSnapIncrement(const UIRect& parentRect) const {
        if (!m_Settings.SnapToGrid) {
            return glm::vec2(0.0f);
        }
        if (m_Settings.SnapIsPercentage && m_Settings.SnapPercent > 0.0f) {
            return glm::vec2(
                parentRect.Width * (m_Settings.SnapPercent / 100.0f),
                parentRect.Height * (m_Settings.SnapPercent / 100.0f)
            );
        }
        float inc = (m_Settings.SnapIncrement > 0.0f) ? m_Settings.SnapIncrement : 1.0f;
        return glm::vec2(inc, inc);
    }

} // namespace Axle
