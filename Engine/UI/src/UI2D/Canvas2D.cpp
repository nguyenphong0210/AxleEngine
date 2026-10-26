#include <Axle/UI/Canvas2D.hpp>
#include <Axle/UI/UIFont.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Axle {

    Canvas2D::Canvas2D() {
        m_Vertices.reserve(MaxVertices);
        m_Indices.reserve(MaxIndices);
    }

    Canvas2D::~Canvas2D() {
        Shutdown();
    }

    void Canvas2D::Init() {
        m_Initialized = true;
    }

    void Canvas2D::Shutdown() {
        m_Vertices.clear();
        m_Indices.clear();
        m_ClipStack.clear();
        m_Stats = Canvas2DStats{};
        m_Initialized = false;
    }

    void Canvas2D::BeginCanvas(const glm::vec2& viewportSize, float dpiScale) {
        m_ViewportSize = viewportSize;
        m_DPIScale = (dpiScale > 0.0f) ? dpiScale : 1.0f;
        m_Vertices.clear();
        m_Indices.clear();
        m_ClipStack.clear();
        m_Stats = Canvas2DStats{};
    }

    void Canvas2D::EndCanvas() {
        if (m_Vertices.empty()) {
            m_Stats.DrawCalls = 0;
            return;
        }

        // 1 unified batch draw call for the entire canvas
        m_Stats.DrawCalls = 1;
    }

    void Canvas2D::PushClipRect(const UIRect& clipRect) {
        UIRect scaledClip{
            clipRect.X * m_DPIScale,
            clipRect.Y * m_DPIScale,
            clipRect.Width * m_DPIScale,
            clipRect.Height * m_DPIScale
        };

        if (m_ClipStack.empty()) {
            m_ClipStack.push_back(scaledClip);
        } else {
            m_ClipStack.push_back(m_ClipStack.back().Intersect(scaledClip));
        }
    }

    void Canvas2D::PopClipRect() {
        if (!m_ClipStack.empty()) {
            m_ClipStack.pop_back();
        }
    }

    UIRect Canvas2D::GetCurrentClipRect() const {
        if (!m_ClipStack.empty()) {
            return m_ClipStack.back();
        }
        return UIRect{ 0.0f, 0.0f, m_ViewportSize.x, m_ViewportSize.y };
    }

    void Canvas2D::DrawRect(const UIRect& rect, const UIStyle& style) {
        EmitQuad(rect, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                 style.BackgroundColor, style.BorderColor,
                 style.CornerRadius, style.BorderWidth,
                 glm::vec2(0.0f), 0, UIPrimitiveType::SDF_Rect);
    }

    void Canvas2D::DrawSprite(const UIRect& rect, uint32_t bindlessTexIndex, const glm::vec4& tint,
                              const glm::vec4& uvBounds) {
        EmitQuad(rect, uvBounds, tint, glm::vec4(0.0f),
                 0.0f, 0.0f, glm::vec2(0.0f),
                 bindlessTexIndex, UIPrimitiveType::Sprite);
    }

    void Canvas2D::DrawRadialArc(const UIRect& rect, float startAngleDeg, float endAngleDeg, float valueNormalized,
                                 float thicknessPx, const glm::vec4& fillColor, const glm::vec4& trackColor) {
        float startRad = glm::radians(startAngleDeg);
        float endRad = glm::radians(endAngleDeg);
        float valueClamped = std::clamp(valueNormalized, 0.0f, 1.0f);

        const float TWO_PI = glm::two_pi<float>();
        float totalSpan = std::fmod(endRad - startRad + TWO_PI * 2.0f, TWO_PI);
        if (totalSpan <= 0.0001f) totalSpan = TWO_PI;
        float activeRad = startRad + totalSpan * valueClamped;

        EmitQuad(rect, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                 fillColor, trackColor,
                 thicknessPx, activeRad,
                 glm::vec2(startRad, endRad), 0, UIPrimitiveType::SDF_RadialArc);
    }

    void Canvas2D::DrawTextString(std::string_view text, const std::shared_ptr<UIFont>& font,
                                  const UIRect& rect, float fontSize, const glm::vec4& color) {
        if (text.empty() || !font || fontSize <= 0.0f) return;

        float cursorX = rect.X;
        float cursorY = rect.Y;
        uint32_t prevCodepoint = 0;

        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') {
                cursorX = rect.X;
                cursorY += fontSize * font->GetLineHeight();
                prevCodepoint = 0;
                continue;
            }

            uint32_t cp = static_cast<uint8_t>(c);
            const GlyphMetrics* glyph = font->GetGlyph(cp);
            if (!glyph) {
                glyph = font->GetGlyph('?');
                if (!glyph) continue;
            }

            float kerning = prevCodepoint ? font->GetKerning(prevCodepoint, cp) : 0.0f;
            cursorX += kerning * fontSize;

            // Emit quad only for visible glyphs (space ' ' is non-rendering)
            if (c != ' ') {
                float gx = cursorX + glyph->PlaneBounds.x * fontSize;
                float gy = cursorY + (font->GetLineHeight() - glyph->PlaneBounds.w) * fontSize;
                float gw = (glyph->PlaneBounds.z - glyph->PlaneBounds.x) * fontSize;
                float gh = (glyph->PlaneBounds.w - glyph->PlaneBounds.y) * fontSize;

                if (gw > 0.0f && gh > 0.0f) {
                    EmitQuad(UIRect{ gx, gy, gw, gh }, glyph->AtlasBounds,
                             color, glm::vec4(0.0f),
                             0.0f, 0.0f, glm::vec2(0.0f),
                             font->GetBindlessTextureIndex(), UIPrimitiveType::MSDF_Text);
                }
            }

            cursorX += glyph->AdvanceX * fontSize;
            prevCodepoint = cp;
        }
    }

    void Canvas2D::EmitQuad(const UIRect& unscaledRect, const glm::vec4& uvBounds,
                            const glm::vec4& color, const glm::vec4& borderColor,
                            float cornerRadius, float borderWidth, const glm::vec2& angleRange,
                            uint32_t texIndex, UIPrimitiveType type) {
        if (m_Vertices.size() + 4 > MaxVertices || m_Indices.size() + 6 > MaxIndices) {
            return;
        }

        float scaledX = unscaledRect.X * m_DPIScale;
        float scaledY = unscaledRect.Y * m_DPIScale;
        float scaledW = unscaledRect.Width * m_DPIScale;
        float scaledH = unscaledRect.Height * m_DPIScale;
        float scaledRadius = (type == UIPrimitiveType::SDF_RadialArc) ? (cornerRadius * m_DPIScale) : (cornerRadius * m_DPIScale);
        float scaledBorder = (type == UIPrimitiveType::SDF_RadialArc) ? borderWidth : (borderWidth * m_DPIScale);

        // Hardware scissor clipping culling test
        if (!m_ClipStack.empty()) {
            UIRect quadRect{ scaledX, scaledY, scaledW, scaledH };
            UIRect isect = quadRect.Intersect(GetCurrentClipRect());
            if (isect.Width <= 0.0f || isect.Height <= 0.0f) {
                return; // Culled by active clip rect!
            }
        }

        uint32_t baseIdx = static_cast<uint32_t>(m_Vertices.size());
        glm::vec2 quadSize{ scaledW, scaledH };

        // Top-Left (0)
        m_Vertices.push_back(UIVertex{
            .ScreenPos = glm::vec2(scaledX, scaledY),
            .UV = glm::vec2(uvBounds.x, uvBounds.y),
            .Color = color,
            .BorderColor = borderColor,
            .QuadSize = quadSize,
            .CornerRadius = scaledRadius,
            .BorderWidth = scaledBorder,
            .AngleRange = angleRange,
            .TextureIndex = texIndex,
            .PrimitiveType = static_cast<uint32_t>(type)
        });

        // Top-Right (1)
        m_Vertices.push_back(UIVertex{
            .ScreenPos = glm::vec2(scaledX + scaledW, scaledY),
            .UV = glm::vec2(uvBounds.z, uvBounds.y),
            .Color = color,
            .BorderColor = borderColor,
            .QuadSize = quadSize,
            .CornerRadius = scaledRadius,
            .BorderWidth = scaledBorder,
            .AngleRange = angleRange,
            .TextureIndex = texIndex,
            .PrimitiveType = static_cast<uint32_t>(type)
        });

        // Bottom-Right (2)
        m_Vertices.push_back(UIVertex{
            .ScreenPos = glm::vec2(scaledX + scaledW, scaledY + scaledH),
            .UV = glm::vec2(uvBounds.z, uvBounds.w),
            .Color = color,
            .BorderColor = borderColor,
            .QuadSize = quadSize,
            .CornerRadius = scaledRadius,
            .BorderWidth = scaledBorder,
            .AngleRange = angleRange,
            .TextureIndex = texIndex,
            .PrimitiveType = static_cast<uint32_t>(type)
        });

        // Bottom-Left (3)
        m_Vertices.push_back(UIVertex{
            .ScreenPos = glm::vec2(scaledX, scaledY + scaledH),
            .UV = glm::vec2(uvBounds.x, uvBounds.w),
            .Color = color,
            .BorderColor = borderColor,
            .QuadSize = quadSize,
            .CornerRadius = scaledRadius,
            .BorderWidth = scaledBorder,
            .AngleRange = angleRange,
            .TextureIndex = texIndex,
            .PrimitiveType = static_cast<uint32_t>(type)
        });

        // Standard counter-clockwise quad triangulation (TL -> TR -> BR, BR -> BL -> TL)
        m_Indices.push_back(baseIdx + 0);
        m_Indices.push_back(baseIdx + 1);
        m_Indices.push_back(baseIdx + 2);
        m_Indices.push_back(baseIdx + 2);
        m_Indices.push_back(baseIdx + 3);
        m_Indices.push_back(baseIdx + 0);

        m_Stats.QuadCount++;
        m_Stats.VertexCount += 4;
        m_Stats.IndexCount += 6;
    }

} // namespace Axle
