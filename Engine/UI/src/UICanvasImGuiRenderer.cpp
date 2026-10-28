#include <Axle/UICanvasImGuiRenderer.hpp>
#include <Axle/UI/UINode.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    static inline ImU32 ToImU32(const glm::vec4& c) {
        int r = static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        int g = static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        int b = static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        int a = static_cast<int>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
        return IM_COL32(r, g, b, a);
    }

    UICanvasMapping UICanvasImGuiRenderer::ComputeMapping(
        const UIRect& canvasRect,
        const glm::vec2& viewportScreenPos,
        const glm::vec2& viewportScreenSize
    ) {
        UICanvasMapping m;
        float refW = (canvasRect.Width > 0.0f) ? canvasRect.Width : 1920.0f;
        float refH = (canvasRect.Height > 0.0f) ? canvasRect.Height : 1080.0f;

        float sx = (refW > 0.0f) ? (viewportScreenSize.x / refW) : 1.0f;
        float sy = (refH > 0.0f) ? (viewportScreenSize.y / refH) : 1.0f;
        m.Scale = std::min(sx, sy);
        if (m.Scale <= 0.0f) m.Scale = 1.0f;

        m.DisplaySize = glm::vec2(refW, refH) * m.Scale;
        m.Origin = viewportScreenPos + (viewportScreenSize - m.DisplaySize) * 0.5f;
        return m;
    }

    void UICanvasImGuiRenderer::Render(
        UIDocument* document,
        ImDrawList* drawList,
        const glm::vec2& viewportScreenPos,
        const glm::vec2& viewportScreenSize
    ) {
        if (!document || !drawList) return;
        UINode* root = document->GetRootNode();
        if (!root || !root->Visible) return;

        float refW = 1920.0f;
        float refH = 1080.0f;
        if (auto* canvasNode = dynamic_cast<UICanvasNode*>(root)) {
            if (canvasNode->ReferenceResolution.x > 0.0f) refW = canvasNode->ReferenceResolution.x;
            if (canvasNode->ReferenceResolution.y > 0.0f) refH = canvasNode->ReferenceResolution.y;
        }

        UIRect rootRect{ 0.0f, 0.0f, refW, refH };
        UICanvasMapping mapping = ComputeMapping(rootRect, viewportScreenPos, viewportScreenSize);

        // Render root children
        for (UINode* child : root->Children) {
            RenderNodeRecursive(child, drawList, mapping);
        }
    }

    void UICanvasImGuiRenderer::RenderNodeRecursive(
        UINode* node,
        ImDrawList* drawList,
        const UICanvasMapping& mapping
    ) {
        if (!node || !node->Visible) return;

        const UIRect& cr = node->ComputedRect;
        ImVec2 sMin(mapping.Origin.x + cr.X * mapping.Scale, mapping.Origin.y + cr.Y * mapping.Scale);
        ImVec2 sMax(sMin.x + cr.Width * mapping.Scale, sMin.y + cr.Height * mapping.Scale);

        // 1. UIRectangleNode
        if (auto* rn = dynamic_cast<UIRectangleNode*>(node)) {
            ImU32 bgCol = ToImU32(rn->Style.BackgroundColor);
            float r = rn->Style.CornerRadius * mapping.Scale;
            if ((bgCol >> 24) > 0) {
                drawList->AddRectFilled(sMin, sMax, bgCol, r);
            }
            if (rn->Style.BorderWidth > 0.0f) {
                ImU32 borderCol = ToImU32(rn->Style.BorderColor);
                if ((borderCol >> 24) > 0) {
                    drawList->AddRect(sMin, sMax, borderCol, r, 0, rn->Style.BorderWidth * mapping.Scale);
                }
            }
        }
        // 2. UIProgressBarNode
        else if (auto* pb = dynamic_cast<UIProgressBarNode*>(node)) {
            ImU32 bgCol = ToImU32(pb->BackgroundColor);
            float r = pb->CornerRadius * mapping.Scale;
            if ((bgCol >> 24) > 0) {
                drawList->AddRectFilled(sMin, sMax, bgCol, r);
            }
            if (pb->Value > 0.001f) {
                float fillW = (sMax.x - sMin.x) * std::clamp(pb->Value, 0.0f, 1.0f);
                ImVec2 fillMax(sMin.x + fillW, sMax.y);
                ImU32 fillCol = ToImU32(pb->FillColor);
                if ((fillCol >> 24) > 0) {
                    drawList->AddRectFilled(sMin, fillMax, fillCol, r);
                }
            }
        }
        // 3. UIRadialGaugeNode
        else if (auto* rg = dynamic_cast<UIRadialGaugeNode*>(node)) {
            ImVec2 center((sMin.x + sMax.x) * 0.5f, (sMin.y + sMax.y) * 0.5f);
            float thickness = std::max(1.0f, rg->Thickness * mapping.Scale);
            float radius = std::max(2.0f, std::min(sMax.x - sMin.x, sMax.y - sMin.y) * 0.5f - (thickness * 0.5f));

            float startRad = glm::radians(rg->StartAngle);
            float endRad = glm::radians(rg->EndAngle);
            const float TWO_PI = glm::two_pi<float>();
            float totalSpan = std::fmod(endRad - startRad + TWO_PI * 2.0f, TWO_PI);
            if (totalSpan <= 0.0001f) totalSpan = TWO_PI;

            // Background track arc
            ImU32 trackCol = ToImU32(rg->TrackColor);
            if ((trackCol >> 24) > 0) {
                drawList->PathClear();
                drawList->PathArcTo(center, radius, startRad, endRad, 48);
                drawList->PathStroke(trackCol, 0, thickness);
            }

            // Active value fill arc
            float clampedVal = std::clamp(rg->Value, 0.0f, 1.0f);
            if (clampedVal > 0.001f) {
                float activeRad = startRad + totalSpan * clampedVal;
                ImU32 fillCol = ToImU32(rg->Color);
                if ((fillCol >> 24) > 0) {
                    drawList->PathClear();
                    drawList->PathArcTo(center, radius, startRad, activeRad, 48);
                    drawList->PathStroke(fillCol, 0, thickness);
                }
            }
        }
        // 4. UITextNode
        else if (auto* tn = dynamic_cast<UITextNode*>(node)) {
            if (!tn->Text.empty()) {
                ImU32 textCol = ToImU32(tn->Color);
                float scaledFontSize = tn->FontSize * mapping.Scale;
                if (scaledFontSize >= 6.0f) {
                    drawList->AddText(ImGui::GetFont(), scaledFontSize, sMin, textCol, tn->Text.c_str());
                }
            }
        }
        // 5. UIButtonNode
        else if (auto* btn = dynamic_cast<UIButtonNode*>(node)) {
            ImU32 bgCol = ToImU32(btn->IsPressed ? btn->PressedColor : (btn->IsHovered || btn->IsFocused ? btn->HoverColor : btn->NormalColor));
            float r = btn->Style.CornerRadius * mapping.Scale;
            if ((bgCol >> 24) > 0) {
                drawList->AddRectFilled(sMin, sMax, bgCol, r);
            }
            if (btn->Style.BorderWidth > 0.0f) {
                ImU32 borderCol = ToImU32(btn->Style.BorderColor);
                if ((borderCol >> 24) > 0) {
                    drawList->AddRect(sMin, sMax, borderCol, r, 0, btn->Style.BorderWidth * mapping.Scale);
                }
            }
        }

        // Traverse children recursively
        for (UINode* child : node->Children) {
            RenderNodeRecursive(child, drawList, mapping);
        }
    }

} // namespace Axle
