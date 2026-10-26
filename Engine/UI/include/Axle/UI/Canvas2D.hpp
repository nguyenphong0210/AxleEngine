#pragma once
#include <Axle/UI/UITypes.hpp>
#include <vector>
#include <memory>
#include <string_view>

namespace Axle {

    class UIFont;

    struct Canvas2DStats {
        uint32_t QuadCount   = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount  = 0;
        uint32_t DrawCalls   = 0;
    };

    class Canvas2D {
    public:
        static constexpr uint32_t MaxQuads    = 10000;
        static constexpr uint32_t MaxVertices = MaxQuads * 4;
        static constexpr uint32_t MaxIndices  = MaxQuads * 6;

        Canvas2D();
        ~Canvas2D();

        void Init();
        void Shutdown();

        void BeginCanvas(const glm::vec2& viewportSize, float dpiScale = 1.0f);
        void EndCanvas();

        void DrawRect(const UIRect& rect, const UIStyle& style);
        void DrawSprite(const UIRect& rect, uint32_t bindlessTexIndex, const glm::vec4& tint = glm::vec4(1.0f),
                        const glm::vec4& uvBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        void DrawRadialArc(const UIRect& rect, float startAngleDeg, float endAngleDeg, float valueNormalized,
                           float thicknessPx, const glm::vec4& fillColor, const glm::vec4& trackColor);
        void DrawTextString(std::string_view text, const std::shared_ptr<UIFont>& font,
                            const UIRect& rect, float fontSize, const glm::vec4& color);

        void PushClipRect(const UIRect& clipRect);
        void PopClipRect();
        UIRect GetCurrentClipRect() const;

        const std::vector<UIVertex>& GetVertexBuffer() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndexBuffer() const { return m_Indices; }
        const Canvas2DStats& GetStats() const { return m_Stats; }
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }
        float GetDPIScale() const { return m_DPIScale; }

    private:
        void EmitQuad(const UIRect& unscaledRect, const glm::vec4& uvBounds,
                      const glm::vec4& color, const glm::vec4& borderColor,
                      float cornerRadius, float borderWidth, const glm::vec2& angleRange,
                      uint32_t texIndex, UIPrimitiveType type);

    private:
        bool m_Initialized = false;
        glm::vec2 m_ViewportSize{ 1920.0f, 1080.0f };
        float m_DPIScale = 1.0f;

        std::vector<UIVertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        std::vector<UIRect>   m_ClipStack;
        Canvas2DStats         m_Stats{};
    };

} // namespace Axle
