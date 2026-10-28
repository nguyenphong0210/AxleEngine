#pragma once
#include <Axle/UI/UIDocument.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

namespace Axle {

    struct UICanvasMapping {
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

    class UICanvasImGuiRenderer {
    public:
        // Compute letterboxed/pillarboxed mapping from canvas coordinates to screen
        static UICanvasMapping ComputeMapping(
            const UIRect& canvasRect,
            const glm::vec2& viewportScreenPos,
            const glm::vec2& viewportScreenSize
        );

        // Renders an active UIDocument onto an ImDrawList
        static void Render(
            UIDocument* document,
            ImDrawList* drawList,
            const glm::vec2& viewportScreenPos,
            const glm::vec2& viewportScreenSize
        );

    private:
        static void RenderNodeRecursive(
            UINode* node,
            ImDrawList* drawList,
            const UICanvasMapping& mapping
        );
    };

} // namespace Axle
