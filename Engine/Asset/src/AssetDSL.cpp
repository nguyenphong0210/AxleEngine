#include <Axle/AssetDSL.hpp>
#include <Axle/Path.hpp>
#include <Axle/FileSystem.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>

namespace Axle {

    static std::string_view Trim(std::string_view s) {
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == ';')) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == ';')) {
            s.remove_suffix(1);
        }
        return s;
    }

    static void SkipWhitespaceAndSeparators(std::string_view& src) {
        while (!src.empty() && (std::isspace(static_cast<unsigned char>(src.front())) || src.front() == ';' || src.front() == ',')) {
            src.remove_prefix(1);
        }
    }

    static std::string_view ReadWord(std::string_view& src) {
        SkipWhitespaceAndSeparators(src);
        size_t len = 0;
        while (len < src.size() && (std::isalnum(static_cast<unsigned char>(src[len])) || src[len] == '_' || src[len] == '.')) {
            len++;
        }
        std::string_view word = src.substr(0, len);
        src.remove_prefix(len);
        return word;
    }

    AssetKind AssetDSL::StringToAssetKind(std::string_view str) {
        if (str == "Material") return AssetKind::Material;
        if (str == "Canvas") return AssetKind::Canvas;
        if (str == "ParticleEmitter") return AssetKind::ParticleEmitter;
        if (str == "Mesh") return AssetKind::Mesh;
        if (str == "Texture") return AssetKind::Texture;
        if (str == "Terrain") return AssetKind::Terrain;
        if (str == "PhysicsMaterial") return AssetKind::PhysicsMaterial;
        if (str == "Skeleton") return AssetKind::Skeleton;
        if (str == "SkeletalMesh") return AssetKind::SkeletalMesh;
        if (str == "AnimationClip") return AssetKind::AnimationClip;
        if (str == "AnimatorController") return AssetKind::AnimatorController;
        return AssetKind::Unknown;
    }

    std::string_view AssetDSL::AssetKindToString(AssetKind kind) {
        switch (kind) {
            case AssetKind::Material: return "Material";
            case AssetKind::Canvas: return "Canvas";
            case AssetKind::ParticleEmitter: return "ParticleEmitter";
            case AssetKind::Mesh: return "Mesh";
            case AssetKind::Texture: return "Texture";
            case AssetKind::Terrain: return "Terrain";
            case AssetKind::PhysicsMaterial: return "PhysicsMaterial";
            case AssetKind::Skeleton: return "Skeleton";
            case AssetKind::SkeletalMesh: return "SkeletalMesh";
            case AssetKind::AnimationClip: return "AnimationClip";
            case AssetKind::AnimatorController: return "AnimatorController";
            case AssetKind::Unknown:
            default:
                return "Unknown";
        }
    }

    std::string AssetDSL::StripComments(std::string_view source) { return BlockText::StripComments(source); }
    std::string AssetDSL::QuoteString(std::string_view value) { return BlockText::QuoteString(value); }

    AssetKind AssetDSL::PeekAssetKind(std::string_view source) {
        std::string clean = StripComments(source);
        std::string_view src = clean;
        std::string_view rootWord = ReadWord(src);
        return StringToAssetKind(rootWord);
    }

    AssetKind AssetDSL::PeekAssetKindFromFile(const std::string& filepath) {
        const auto content = FileSystem::ReadTextFile(PathFromUtf8(filepath));
        return PeekAssetKind(content);
    }

    glm::vec4 AssetDSL::ParseHexColor(std::string_view hex) {
        hex = Trim(hex);
        if (hex.starts_with('"') && hex.ends_with('"') && hex.size() >= 2) {
            hex = hex.substr(1, hex.size() - 2);
        }
        if (hex.starts_with('#')) {
            hex.remove_prefix(1);
        }

        auto hexCharToInt = [](char c) -> uint32_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return 0;
        };

        if (hex.size() == 6) {
            uint32_t r = (hexCharToInt(hex[0]) << 4) | hexCharToInt(hex[1]);
            uint32_t g = (hexCharToInt(hex[2]) << 4) | hexCharToInt(hex[3]);
            uint32_t b = (hexCharToInt(hex[4]) << 4) | hexCharToInt(hex[5]);
            return glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        } else if (hex.size() == 8) {
            uint32_t r = (hexCharToInt(hex[0]) << 4) | hexCharToInt(hex[1]);
            uint32_t g = (hexCharToInt(hex[2]) << 4) | hexCharToInt(hex[3]);
            uint32_t b = (hexCharToInt(hex[4]) << 4) | hexCharToInt(hex[5]);
            uint32_t a = (hexCharToInt(hex[6]) << 4) | hexCharToInt(hex[7]);
            return glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        } else if (hex.size() == 3) {
            uint32_t r = hexCharToInt(hex[0]) * 17;
            uint32_t g = hexCharToInt(hex[1]) * 17;
            uint32_t b = hexCharToInt(hex[2]) * 17;
            return glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        }

        return glm::vec4(1.0f);
    }

    std::string AssetDSL::FormatHexColor(const glm::vec4& color, bool includeAlpha) {
        uint32_t r = static_cast<uint32_t>(std::clamp(color.r * 255.0f + 0.5f, 0.0f, 255.0f));
        uint32_t g = static_cast<uint32_t>(std::clamp(color.g * 255.0f + 0.5f, 0.0f, 255.0f));
        uint32_t b = static_cast<uint32_t>(std::clamp(color.b * 255.0f + 0.5f, 0.0f, 255.0f));
        uint32_t a = static_cast<uint32_t>(std::clamp(color.a * 255.0f + 0.5f, 0.0f, 255.0f));

        std::ostringstream ss;
        ss << "#" << std::uppercase << std::hex << std::setfill('0');
        ss << std::setw(2) << r << std::setw(2) << g << std::setw(2) << b;
        if (includeAlpha) {
            ss << std::setw(2) << a;
        }
        return ss.str();
    }

    // -------------------------------------------------------------------------
    // AssetProperty Implementation
    // -------------------------------------------------------------------------

    std::string AssetProperty::AsString() const {
        std::string_view val = Trim(RawValue);
        if (val.size() < 2 || val.front() != '"' || val.back() != '"') return std::string(val);
        std::string result;
        for (size_t i = 1; i + 1 < val.size(); ++i) {
            if (val[i] != '\\' || i + 2 >= val.size()) { result += val[i]; continue; }
            switch (val[++i]) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                default: result += val[i]; break;
            }
        }
        return result;
    }

    float AssetProperty::AsFloat(float defaultVal) const {
        try {
            return std::stof(std::string(Trim(RawValue)));
        } catch (...) {
            return defaultVal;
        }
    }

    int32_t AssetProperty::AsInt(int32_t defaultVal) const {
        try {
            return std::stoi(std::string(Trim(RawValue)));
        } catch (...) {
            return defaultVal;
        }
    }

    uint32_t AssetProperty::AsUInt(uint32_t defaultVal) const {
        try {
            return static_cast<uint32_t>(std::stoul(std::string(Trim(RawValue))));
        } catch (...) {
            return defaultVal;
        }
    }

    bool AssetProperty::AsBool(bool defaultVal) const {
        std::string s = std::string(Trim(RawValue));
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
        if (s == "false" || s == "0" || s == "no" || s == "off") return false;
        return defaultVal;
    }

    glm::vec2 AssetProperty::AsVec2(const glm::vec2& defaultVal) const {
        std::string_view val = Trim(RawValue);
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }
        float x = defaultVal.x, y = defaultVal.y;
        char comma;
        std::string strVal(val);
        std::stringstream ss(strVal);
        if (ss >> x) {
            if (ss >> comma && comma == ',') {
                ss >> y;
            } else {
                ss >> y;
            }
            return glm::vec2(x, y);
        }
        return defaultVal;
    }

    glm::vec3 AssetProperty::AsVec3(const glm::vec3& defaultVal) const {
        std::string_view val = Trim(RawValue);
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }
        std::vector<float> values;
        std::string strVal(val);
        std::stringstream ss(strVal);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try {
                values.push_back(std::stof(std::string(Trim(item))));
            } catch (...) {}
        }
        if (values.size() >= 3) {
            return glm::vec3(values[0], values[1], values[2]);
        }
        return defaultVal;
    }

    glm::vec4 AssetProperty::AsVec4(const glm::vec4& defaultVal) const {
        std::string_view val = Trim(RawValue);
        if (val.starts_with('#') || (val.size() >= 2 && val.front() == '"' && val[1] == '#')) {
            return AssetDSL::ParseHexColor(val);
        }
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }
        std::vector<float> values;
        std::string strVal(val);
        std::stringstream ss(strVal);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try {
                values.push_back(std::stof(std::string(Trim(item))));
            } catch (...) {}
        }
        if (values.size() == 4) {
            return glm::vec4(values[0], values[1], values[2], values[3]);
        } else if (values.size() == 3) {
            return glm::vec4(values[0], values[1], values[2], 1.0f);
        }
        return defaultVal;
    }

    std::vector<float> AssetProperty::AsFloatArray() const {
        std::vector<float> result;
        std::string_view val = Trim(RawValue);
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }
        std::string strVal(val);
        std::stringstream ss(strVal);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try {
                result.push_back(std::stof(std::string(Trim(item))));
            } catch (...) {}
        }
        return result;
    }

    std::vector<std::string> AssetProperty::AsStringArray() const {
        std::vector<std::string> result;
        std::string_view val = Trim(RawValue);
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }
        size_t start = 0;
        bool quoted = false;
        for (size_t i = 0; i <= val.size(); ++i) {
            if (i < val.size() && quoted && val[i] == '\\' && i + 1 < val.size()) { ++i; continue; }
            if (i < val.size() && val[i] == '"') quoted = !quoted;
            if (i == val.size() || (!quoted && val[i] == ',')) {
                const auto item = Trim(val.substr(start, i-start));
                if (!item.empty()) result.push_back(AssetProperty{"", std::string(item)}.AsString());
                start = i+1;
            }
        }
        return result;
    }

    std::vector<glm::vec3> AssetProperty::AsVec3Array() const {
        std::vector<glm::vec3> result;
        std::string_view val = Trim(RawValue);
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }

        size_t i = 0;
        while (i < val.size()) {
            size_t openBracket = val.find('[', i);
            if (openBracket == std::string_view::npos) break;
            size_t closeBracket = val.find(']', openBracket);
            if (closeBracket == std::string_view::npos) break;

            std::string_view inner = val.substr(openBracket + 1, closeBracket - openBracket - 1);
            std::string strInner(inner);
            std::stringstream ss(strInner);
            std::string item;
            std::vector<float> coords;
            while (std::getline(ss, item, ',')) {
                try {
                    coords.push_back(std::stof(std::string(Trim(item))));
                } catch (...) {}
            }
            if (coords.size() >= 3) {
                result.emplace_back(coords[0], coords[1], coords[2]);
            }
            i = closeBracket + 1;
        }

        return result;
    }

    std::vector<glm::ivec3> AssetProperty::AsIVec3Array() const {
        std::vector<glm::ivec3> result;
        std::string_view val = Trim(RawValue);
        if (val.starts_with('[') && val.ends_with(']')) {
            val = val.substr(1, val.size() - 2);
        }

        size_t i = 0;
        while (i < val.size()) {
            size_t openBracket = val.find('[', i);
            if (openBracket == std::string_view::npos) break;
            size_t closeBracket = val.find(']', openBracket);
            if (closeBracket == std::string_view::npos) break;

            std::string_view inner = val.substr(openBracket + 1, closeBracket - openBracket - 1);
            std::string strInner(inner);
            std::stringstream ss(strInner);
            std::string item;
            std::vector<int> indices;
            while (std::getline(ss, item, ',')) {
                try {
                    indices.push_back(std::stoi(std::string(Trim(item))));
                } catch (...) {}
            }
            if (indices.size() >= 3) {
                result.emplace_back(indices[0], indices[1], indices[2]);
            }
            i = closeBracket + 1;
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // AssetBlock Implementation
    // -------------------------------------------------------------------------

    bool AssetBlock::HasProperty(const std::string& key) const {
        return GetProperty(key) != nullptr;
    }

    const AssetProperty* AssetBlock::GetProperty(const std::string& key) const {
        for (const auto& prop : Properties) {
            if (prop.Key == key) {
                return &prop;
            }
        }
        return nullptr;
    }

    std::string AssetBlock::GetString(const std::string& key, const std::string& defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsString() : defaultVal;
    }

    float AssetBlock::GetFloat(const std::string& key, float defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsFloat(defaultVal) : defaultVal;
    }

    int32_t AssetBlock::GetInt(const std::string& key, int32_t defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsInt(defaultVal) : defaultVal;
    }

    uint32_t AssetBlock::GetUInt(const std::string& key, uint32_t defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsUInt(defaultVal) : defaultVal;
    }

    bool AssetBlock::GetBool(const std::string& key, bool defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsBool(defaultVal) : defaultVal;
    }

    glm::vec2 AssetBlock::GetVec2(const std::string& key, const glm::vec2& defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsVec2(defaultVal) : defaultVal;
    }

    glm::vec3 AssetBlock::GetVec3(const std::string& key, const glm::vec3& defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsVec3(defaultVal) : defaultVal;
    }

    glm::vec4 AssetBlock::GetVec4(const std::string& key, const glm::vec4& defaultVal) const {
        const auto* prop = GetProperty(key);
        return prop ? prop->AsVec4(defaultVal) : defaultVal;
    }

    void AssetBlock::SetString(const std::string& key, const std::string& val) {
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = AssetDSL::QuoteString(val);
                return;
            }
        }
        Properties.push_back({ key, AssetDSL::QuoteString(val) });
    }

    void AssetBlock::SetFloat(const std::string& key, float val) {
        std::ostringstream ss;
        ss << val;
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = ss.str();
                return;
            }
        }
        Properties.push_back({ key, ss.str() });
    }

    void AssetBlock::SetInt(const std::string& key, int32_t val) {
        std::string s = std::to_string(val);
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = s;
                return;
            }
        }
        Properties.push_back({ key, s });
    }

    void AssetBlock::SetUInt(const std::string& key, uint32_t val) {
        std::string s = std::to_string(val);
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = s;
                return;
            }
        }
        Properties.push_back({ key, s });
    }

    void AssetBlock::SetBool(const std::string& key, bool val) {
        std::string s = val ? "true" : "false";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = s;
                return;
            }
        }
        Properties.push_back({ key, s });
    }

    void AssetBlock::SetVec2(const std::string& key, const glm::vec2& val) {
        std::ostringstream ss;
        ss << "[" << val.x << ", " << val.y << "]";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = ss.str();
                return;
            }
        }
        Properties.push_back({ key, ss.str() });
    }

    void AssetBlock::SetVec3(const std::string& key, const glm::vec3& val) {
        std::ostringstream ss;
        ss << "[" << val.x << ", " << val.y << ", " << val.z << "]";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = ss.str();
                return;
            }
        }
        Properties.push_back({ key, ss.str() });
    }

    void AssetBlock::SetVec4(const std::string& key, const glm::vec4& val) {
        std::ostringstream ss;
        ss << "[" << val.x << ", " << val.y << ", " << val.z << ", " << val.w << "]";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = ss.str();
                return;
            }
        }
        Properties.push_back({ key, ss.str() });
    }

    void AssetBlock::SetHexColor(const std::string& key, const glm::vec4& color, bool includeAlpha) {
        std::string hex = "\"" + AssetDSL::FormatHexColor(color, includeAlpha) + "\"";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = hex;
                return;
            }
        }
        Properties.push_back({ key, hex });
    }

    void AssetBlock::SetFloatArray(const std::string& key, const std::vector<float>& values) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < values.size(); ++i) {
            ss << values[i];
            if (i + 1 < values.size()) ss << ", ";
        }
        ss << "]";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = ss.str();
                return;
            }
        }
        Properties.push_back({ key, ss.str() });
    }

    void AssetBlock::SetStringArray(const std::string& key, const std::vector<std::string>& values) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < values.size(); ++i) {
            ss << AssetDSL::QuoteString(values[i]);
            if (i + 1 < values.size()) ss << ", ";
        }
        ss << "]";
        for (auto& prop : Properties) {
            if (prop.Key == key) {
                prop.RawValue = ss.str();
                return;
            }
        }
        Properties.push_back({ key, ss.str() });
    }

    // -------------------------------------------------------------------------
    // AssetDSL Parsing
    // -------------------------------------------------------------------------

    namespace {
        std::shared_ptr<AssetBlock> ImportBlock(BlockTextNode& text) {
            auto block = std::make_shared<AssetBlock>();
            block->Type = std::move(text.Type); block->Source = text.Source;
            block->Properties.reserve(text.Properties.size());
            for (auto& property : text.Properties)
                block->Properties.push_back({std::move(property.Key), std::move(property.RawValue), property.Source});
            block->Children.reserve(text.Children.size());
            for (auto& child : text.Children) block->Children.push_back(ImportBlock(*child));
            return block;
        }
    }

    bool AssetDSL::ParseSource(std::string_view source, AssetDocument& outDoc, std::string* outError,
                              AssetSourceLocation* outLocation) {
        std::shared_ptr<BlockTextNode> text;
        if (!BlockText::Parse(source, text, outError, outLocation)) return false;
        AssetDocument candidate;
        candidate.Root = ImportBlock(*text);
        candidate.Kind = StringToAssetKind(candidate.Root->Type);
        outDoc = std::move(candidate);
        return true;
    }

    bool AssetDSL::ParseFile(const std::string& filepath, AssetDocument& outDoc, std::string* outError) {
        const auto content = FileSystem::ReadTextFile(PathFromUtf8(filepath));
        if (content.empty()) {
            if (outError) *outError = "Could not read asset file: " + filepath;
            return false;
        }
        return ParseSource(content, outDoc, outError);
    }

    // -------------------------------------------------------------------------
    // AssetDSL Serialization
    // -------------------------------------------------------------------------

    std::string AssetDSL::SerializeBlock(const AssetBlock& block, int indentLevel) {
        std::string indent(indentLevel * 4, ' ');
        std::ostringstream ss;

        ss << indent << block.Type << " {\n";

        for (const auto& prop : block.Properties) {
            ss << indent << "    " << prop.Key << ": " << prop.RawValue << "\n";
        }

        if (!block.Properties.empty() && !block.Children.empty()) {
            ss << "\n";
        }

        for (size_t i = 0; i < block.Children.size(); ++i) {
            ss << SerializeBlock(*block.Children[i], indentLevel + 1);
            if (i + 1 < block.Children.size()) {
                ss << "\n";
            }
        }

        ss << indent << "}\n";
        return ss.str();
    }

    std::string AssetDSL::SerializeDocument(const AssetDocument& doc) {
        if (!doc.Root) return "";
        return SerializeBlock(*doc.Root, 0);
    }

    bool AssetDSL::SaveFile(const std::string& filepath, const AssetDocument& doc, std::string* outError) {
        const auto content = SerializeDocument(doc);
        if (content.empty()) {
            if (outError) *outError = "Cannot save an empty asset document";
            return false;
        }
        return FileSystem::WriteIdentifiedTextFile(filepath, content, outError);
    }

} // namespace Axle
