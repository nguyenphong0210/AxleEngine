#include <Axle/UI/UIFont.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Axle {

    UIFont::UIFont() {
        InitDefaultEmbeddedFont();
    }

    void UIFont::InitDefaultEmbeddedFont() {
        m_Glyphs.clear();
        m_KerningMap.clear();
        m_LineHeight = 1.15f;

        // Space ' '
        GlyphMetrics spaceGlyph{
            .Codepoint = 32,
            .AdvanceX = 0.35f,
            .PlaneBounds = glm::vec4(0.0f),
            .AtlasBounds = glm::vec4(0.0f)
        };
        m_Glyphs[32] = spaceGlyph;

        // Populate ASCII characters 33 to 126
        for (uint32_t cp = 33; cp <= 126; ++cp) {
            GlyphMetrics g{
                .Codepoint = cp,
                .AdvanceX = 0.55f,
                .PlaneBounds = glm::vec4(0.05f, 0.0f, 0.5f, 0.75f),
                .AtlasBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)
            };

            // Digits '0'..'9'
            if (cp >= '0' && cp <= '9') {
                g.AdvanceX = 0.6f;
                g.PlaneBounds = glm::vec4(0.05f, 0.0f, 0.55f, 0.85f);
            }
            // Uppercase 'A'..'Z'
            else if (cp >= 'A' && cp <= 'Z') {
                g.AdvanceX = 0.65f;
                g.PlaneBounds = glm::vec4(0.05f, 0.0f, 0.6f, 0.85f);
            }
            // Lowercase 'a'..'z'
            else if (cp >= 'a' && cp <= 'z') {
                g.AdvanceX = 0.55f;
                g.PlaneBounds = glm::vec4(0.05f, 0.0f, 0.5f, 0.65f);
            }

            m_Glyphs[cp] = g;
        }

        // Default kerning pairs
        SetKerning('A', 'V', -0.05f);
        SetKerning('V', 'A', -0.05f);
        SetKerning('T', 'o', -0.04f);
    }

    const GlyphMetrics* UIFont::GetGlyph(uint32_t codepoint) const {
        auto it = m_Glyphs.find(codepoint);
        if (it != m_Glyphs.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void UIFont::SetGlyph(uint32_t codepoint, const GlyphMetrics& metrics) {
        m_Glyphs[codepoint] = metrics;
    }

    float UIFont::GetKerning(uint32_t first, uint32_t second) const {
        uint64_t key = (static_cast<uint64_t>(first) << 32) | static_cast<uint64_t>(second);
        auto it = m_KerningMap.find(key);
        if (it != m_KerningMap.end()) {
            return it->second;
        }
        return 0.0f;
    }

    void UIFont::SetKerning(uint32_t first, uint32_t second, float amount) {
        uint64_t key = (static_cast<uint64_t>(first) << 32) | static_cast<uint64_t>(second);
        m_KerningMap[key] = amount;
    }

    glm::vec2 UIFont::MeasureText(std::string_view text, float fontSize) const {
        if (text.empty() || fontSize <= 0.0f) {
            return glm::vec2(0.0f);
        }

        float maxWidth = 0.0f;
        float currentLineWidth = 0.0f;
        int lineCount = 1;
        uint32_t prevCodepoint = 0;

        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') {
                maxWidth = std::max(maxWidth, currentLineWidth);
                currentLineWidth = 0.0f;
                lineCount++;
                prevCodepoint = 0;
                continue;
            }

            uint32_t cp = static_cast<uint8_t>(c);
            const GlyphMetrics* glyph = GetGlyph(cp);
            if (!glyph) {
                glyph = GetGlyph('?');
                if (!glyph) continue;
            }

            float kerning = prevCodepoint ? GetKerning(prevCodepoint, cp) : 0.0f;
            currentLineWidth += (glyph->AdvanceX + kerning) * fontSize;
            prevCodepoint = cp;
        }

        maxWidth = std::max(maxWidth, currentLineWidth);
        float totalHeight = static_cast<float>(lineCount) * fontSize * m_LineHeight;

        return glm::vec2(maxWidth, totalHeight);
    }

    bool UIFont::SaveToAxleFont(const std::string& filepath) const {
        std::ofstream out(filepath);
        if (!out.is_open()) return false;

        out << "# Axle MSDF Font Asset File\n";
        out << "LineHeight: " << m_LineHeight << "\n";
        out << "TextureIndex: " << m_BindlessTextureIndex << "\n";
        out << "GlyphCount: " << m_Glyphs.size() << "\n";
        out << "Glyphs:\n";

        for (const auto& [cp, g] : m_Glyphs) {
            out << "  - Codepoint: " << cp << "\n";
            out << "    AdvanceX: " << g.AdvanceX << "\n";
            out << "    PlaneBounds: [" << g.PlaneBounds.x << ", " << g.PlaneBounds.y << ", " << g.PlaneBounds.z << ", " << g.PlaneBounds.w << "]\n";
            out << "    AtlasBounds: [" << g.AtlasBounds.x << ", " << g.AtlasBounds.y << ", " << g.AtlasBounds.z << ", " << g.AtlasBounds.w << "]\n";
        }

        return true;
    }

    bool UIFont::LoadFromAxleFont(const std::string& filepath) {
        std::ifstream in(filepath);
        if (!in.is_open()) return false;

        std::string line;
        uint32_t currentCodepoint = 0;
        GlyphMetrics currentGlyph{};
        bool inGlyph = false;

        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;

            auto trim = [](std::string s) {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
                return s;
            };
            line = trim(line);

            if (line.rfind("LineHeight:", 0) == 0) {
                m_LineHeight = std::stof(trim(line.substr(11)));
            } else if (line.rfind("TextureIndex:", 0) == 0) {
                m_BindlessTextureIndex = static_cast<uint32_t>(std::stoul(trim(line.substr(13))));
            } else if (line.rfind("- Codepoint:", 0) == 0) {
                if (inGlyph && currentCodepoint > 0) {
                    m_Glyphs[currentCodepoint] = currentGlyph;
                }
                currentCodepoint = static_cast<uint32_t>(std::stoul(trim(line.substr(12))));
                currentGlyph = GlyphMetrics{ .Codepoint = currentCodepoint };
                inGlyph = true;
            } else if (line.rfind("AdvanceX:", 0) == 0) {
                currentGlyph.AdvanceX = std::stof(trim(line.substr(9)));
            }
        }

        if (inGlyph && currentCodepoint > 0) {
            m_Glyphs[currentCodepoint] = currentGlyph;
        }

        return true;
    }

} // namespace Axle
