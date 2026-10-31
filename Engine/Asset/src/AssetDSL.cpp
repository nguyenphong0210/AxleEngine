#include <Axle/AssetDSL.hpp>
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
            case AssetKind::Unknown:
            default:
                return "Unknown";
        }
    }

    std::string AssetDSL::StripComments(std::string_view source) {
        std::string result;
        result.reserve(source.size());

        bool inString = false;
        bool inLineComment = false;
        bool inBlockComment = false;

        for (size_t i = 0; i < source.size(); ++i) {
            char c = source[i];
            char next = (i + 1 < source.size()) ? source[i + 1] : '\0';

            if (inLineComment) {
                if (c == '\n' || c == '\r') {
                    inLineComment = false;
                    result.push_back(c);
                }
                continue;
            }

            if (inBlockComment) {
                if (c == '*' && next == '/') {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }

            if (inString) {
                result.push_back(c);
                if (c == '\\' && i + 1 < source.size()) {
                    result.push_back(source[++i]);
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }

            if (c == '"') {
                inString = true;
                result.push_back(c);
            } else if (c == '/' && next == '/') {
                inLineComment = true;
                ++i;
            } else if (c == '/' && next == '*') {
                inBlockComment = true;
                ++i;
            } else {
                result.push_back(c);
            }
        }

        return result;
    }

    AssetKind AssetDSL::PeekAssetKind(std::string_view source) {
        std::string clean = StripComments(source);
        std::string_view src = clean;
        std::string_view rootWord = ReadWord(src);
        return StringToAssetKind(rootWord);
    }

    AssetKind AssetDSL::PeekAssetKindFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return AssetKind::Unknown;

        char buffer[512];
        file.read(buffer, sizeof(buffer) - 1);
        std::streamsize bytesRead = file.gcount();
        buffer[bytesRead] = '\0';

        return PeekAssetKind(std::string_view(buffer, static_cast<size_t>(bytesRead)));
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
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            return std::string(val.substr(1, val.size() - 2));
        }
        return std::string(val);
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
        std::string strVal(val);
        std::stringstream ss(strVal);
        std::string item;
        while (std::getline(ss, item, ',')) {
            std::string_view trimmed = Trim(item);
            if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                result.push_back(std::string(trimmed.substr(1, trimmed.size() - 2)));
            } else if (!trimmed.empty()) {
                result.push_back(std::string(trimmed));
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
                prop.RawValue = "\"" + val + "\"";
                return;
            }
        }
        Properties.push_back({ key, "\"" + val + "\"" });
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
            ss << "\"" << values[i] << "\"";
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

    bool AssetDSL::ParseBlockInternal(std::string_view& src, std::shared_ptr<AssetBlock> block, std::string* outError) {
        SkipWhitespaceAndSeparators(src);

        while (!src.empty()) {
            SkipWhitespaceAndSeparators(src);
            if (src.empty()) break;

            if (src.front() == '}') {
                src.remove_prefix(1);
                return true;
            }

            std::string_view identifier = ReadWord(src);
            if (identifier.empty()) {
                if (!src.empty() && src.front() == '}') {
                    src.remove_prefix(1);
                    return true;
                }
                src.remove_prefix(1);
                continue;
            }

            SkipWhitespaceAndSeparators(src);
            if (src.empty()) break;

            // Check if it's a child block: Identifier { ... }
            if (src.front() == '{') {
                src.remove_prefix(1);
                auto child = std::make_shared<AssetBlock>();
                child->Type = std::string(identifier);
                if (!ParseBlockInternal(src, child, outError)) {
                    return false;
                }
                block->Children.push_back(child);
                continue;
            }

            // Otherwise, it must be a property: Identifier : Value
            if (src.front() == ':') {
                src.remove_prefix(1);
                SkipWhitespaceAndSeparators(src);

                std::string rawVal;
                if (!src.empty() && src.front() == '"') {
                    // Quoted string literal
                    rawVal.push_back('"');
                    src.remove_prefix(1);
                    bool escaped = false;
                    while (!src.empty()) {
                        char c = src.front();
                        src.remove_prefix(1);
                        rawVal.push_back(c);
                        if (escaped) {
                            escaped = false;
                        } else if (c == '\\') {
                            escaped = true;
                        } else if (c == '"') {
                            break;
                        }
                    }
                } else if (!src.empty() && src.front() == '[') {
                    // Array or vector, potentially nested: [...]
                    int bracketDepth = 0;
                    while (!src.empty()) {
                        char c = src.front();
                        src.remove_prefix(1);
                        rawVal.push_back(c);
                        if (c == '[') {
                            bracketDepth++;
                        } else if (c == ']') {
                            bracketDepth--;
                            if (bracketDepth <= 0) break;
                        }
                    }
                } else {
                    // Scalar, boolean, or identifier
                    while (!src.empty()) {
                        char c = src.front();
                        if (c == '\n' || c == '\r' || c == ';' || c == '}') {
                            break;
                        }
                        rawVal.push_back(c);
                        src.remove_prefix(1);
                    }
                }

                block->Properties.push_back({ std::string(identifier), std::string(Trim(rawVal)) });
            }
        }

        return true;
    }

    bool AssetDSL::ParseSource(std::string_view source, AssetDocument& outDoc, std::string* outError) {
        std::string clean = StripComments(source);
        std::string_view src = clean;

        SkipWhitespaceAndSeparators(src);
        if (src.empty()) {
            if (outError) *outError = "Asset source is empty.";
            return false;
        }

        std::string_view rootType = ReadWord(src);
        if (rootType.empty()) {
            if (outError) *outError = "Expected root asset type declaration.";
            return false;
        }

        SkipWhitespaceAndSeparators(src);
        if (src.empty() || src.front() != '{') {
            if (outError) *outError = "Expected '{' after root asset type '" + std::string(rootType) + "'.";
            return false;
        }
        src.remove_prefix(1);

        auto root = std::make_shared<AssetBlock>();
        root->Type = std::string(rootType);

        if (!ParseBlockInternal(src, root, outError)) {
            return false;
        }

        outDoc.Root = root;
        outDoc.Kind = StringToAssetKind(rootType);
        return true;
    }

    bool AssetDSL::ParseFile(const std::string& filepath, AssetDocument& outDoc, std::string* outError) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            if (outError) *outError = "Could not open asset file: " + filepath;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return ParseSource(buffer.str(), outDoc, outError);
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
        std::ofstream file(filepath);
        if (!file.is_open()) {
            if (outError) *outError = "Failed to open file for writing: " + filepath;
            return false;
        }

        std::string content = SerializeDocument(doc);
        file << content;
        return true;
    }

} // namespace Axle
