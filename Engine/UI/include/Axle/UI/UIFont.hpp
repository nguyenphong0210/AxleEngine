#pragma once
#include <Axle/UI/UITypes.hpp>
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace Axle {

    struct GlyphMetrics {
        uint32_t  Codepoint = 0;
        float     AdvanceX  = 0.6f;                       // Normalized to 1.0 em
        glm::vec4 PlaneBounds{ 0.0f, 0.0f, 0.55f, 0.8f }; // Left, Bottom, Right, Top (em)
        glm::vec4 AtlasBounds{ 0.0f, 0.0f, 1.0f, 1.0f };  // u0, v0, u1, v1 (normalized UV)
    };

    class UIFont {
    public:
        UIFont();
        ~UIFont() = default;

        void InitDefaultEmbeddedFont();
        bool LoadFromAxleFont(const std::string& filepath);
        bool SaveToAxleFont(const std::string& filepath) const;

        const GlyphMetrics* GetGlyph(uint32_t codepoint) const;
        void SetGlyph(uint32_t codepoint, const GlyphMetrics& metrics);

        float GetKerning(uint32_t first, uint32_t second) const;
        void SetKerning(uint32_t first, uint32_t second, float amount);

        glm::vec2 MeasureText(std::string_view text, float fontSize) const;

        uint32_t GetBindlessTextureIndex() const { return m_BindlessTextureIndex; }
        void SetBindlessTextureIndex(uint32_t idx) { m_BindlessTextureIndex = idx; }
        float GetLineHeight() const { return m_LineHeight; }
        void SetLineHeight(float lineHeight) { m_LineHeight = lineHeight; }

    private:
        float m_LineHeight = 1.15f;
        uint32_t m_BindlessTextureIndex = 0;
        std::unordered_map<uint32_t, GlyphMetrics> m_Glyphs;
        std::unordered_map<uint64_t, float> m_KerningMap;
    };

} // namespace Axle
