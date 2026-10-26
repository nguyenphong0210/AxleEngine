#include <Axle/UI/UIParser.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

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

    std::string UIParser::StripComments(std::string_view source) {
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

    static void SkipWhitespace(std::string_view& src) {
        while (!src.empty() && (std::isspace(static_cast<unsigned char>(src.front())) || src.front() == ';')) {
            src.remove_prefix(1);
        }
    }

    static std::string_view ReadWord(std::string_view& src) {
        SkipWhitespace(src);
        size_t len = 0;
        while (len < src.size() && (std::isalnum(static_cast<unsigned char>(src[len])) || src[len] == '_' || src[len] == '.')) {
            len++;
        }
        std::string_view word = src.substr(0, len);
        src.remove_prefix(len);
        return word;
    }

    bool UIParser::ParsePropertyValue(UINode* node, std::string_view propName, std::string_view rawVal, UIDocument& doc) {
        rawVal = Trim(rawVal);
        if (rawVal.empty() || !node) return false;

        // String literal in quotes
        if (rawVal.front() == '"' && rawVal.back() == '"' && rawVal.size() >= 2) {
            std::string_view content = rawVal.substr(1, rawVal.size() - 2);

            // Check for Binding Expressions:
            size_t openBrace = content.find('{');
            size_t closeBrace = content.rfind('}');

            if (openBrace != std::string_view::npos && closeBrace != std::string_view::npos && closeBrace > openBrace) {
                std::string_view inner = content.substr(openBrace + 1, closeBrace - openBrace - 1);
                inner = Trim(inner);

                size_t qmark = inner.find('?');
                size_t colon = inner.find(':');

                if (qmark != std::string_view::npos && colon != std::string_view::npos && colon > qmark) {
                    // Ternary Binding: "{Vehicle.IsRedline ? #FF1E42FF : #00F5D4FF}"
                    std::string_view condKey = Trim(inner.substr(0, qmark));
                    std::string_view trueVal = Trim(inner.substr(qmark + 1, colon - qmark - 1));
                    std::string_view falseVal = Trim(inner.substr(colon + 1));

                    UIPropertyBinding b;
                    b.TargetNode = node;
                    b.PropertyName = std::string(propName);
                    b.Type = UIBindingType::Ternary;
                    b.SourceKey = std::string(condKey);
                    b.TrueValue = std::string(trueVal);
                    b.FalseValue = std::string(falseVal);
                    doc.AddBinding(b);

                    // Set initial value
                    node->SetProperty(propName, falseVal);
                    return true;
                } else if (openBrace == 0 && closeBrace == content.size() - 1) {
                    // Direct Binding: "{Vehicle.SpeedKmh}"
                    UIPropertyBinding b;
                    b.TargetNode = node;
                    b.PropertyName = std::string(propName);
                    b.Type = UIBindingType::Direct;
                    b.SourceKey = std::string(inner);
                    doc.AddBinding(b);
                    return true;
                } else {
                    // Interpolated Binding: "FPS: {Engine.FPS}"
                    UIPropertyBinding b;
                    b.TargetNode = node;
                    b.PropertyName = std::string(propName);
                    b.Type = UIBindingType::Interpolated;
                    b.SourceKey = std::string(inner);
                    b.TemplateText = std::string(content);
                    doc.AddBinding(b);

                    node->SetProperty(propName, content);
                    return true;
                }
            }

            // Normal String
            return node->SetProperty(propName, content);
        }

        // Array literal: [v1, v2] or [v1, v2, v3, v4]
        if (rawVal.front() == '[' && rawVal.back() == ']' && rawVal.size() >= 2) {
            std::string_view inner = rawVal.substr(1, rawVal.size() - 2);
            std::vector<float> values;
            std::string innerStr(inner);
            std::stringstream ss(innerStr);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item = std::string(Trim(item));
                if (!item.empty()) {
                    try {
                        values.push_back(std::stof(item));
                    } catch (...) {}
                }
            }

            if (values.size() == 2) {
                glm::vec2 v(values[0], values[1]);
                if (propName == "pos" || propName == "position") return node->SetPropertyVec2("position", v);
                if (propName == "size") return node->SetPropertyVec2("size", v);
                if (propName == "referenceResolution") return node->SetPropertyVec2("referenceResolution", v);
                return node->SetPropertyVec2(propName, v);
            } else if (values.size() == 4) {
                glm::vec4 v(values[0], values[1], values[2], values[3]);
                if (propName == "margin") return node->SetPropertyVec4("margin", v);
                if (propName == "padding") return node->SetPropertyVec4("padding", v);
                return node->SetPropertyVec4(propName, v);
            }
            return false;
        }

        // Boolean literal
        if (rawVal == "true" || rawVal == "false") {
            return node->SetPropertyBool(propName, rawVal == "true");
        }

        // Numeric float/int literal
        try {
            size_t idx = 0;
            std::string s(rawVal);
            float f = std::stof(s, &idx);
            if (idx == s.size()) {
                return node->SetPropertyFloat(propName, f);
            }
        } catch (...) {}

        // Fallback to string property (e.g. Hex color "#RRGGBB", Anchor name "Center", etc.)
        return node->SetProperty(propName, rawVal);
    }

    bool UIParser::ParseNode(std::string_view& src, UINode* parent, UIDocument& doc, std::string* outError) {
        std::string_view typeName = ReadWord(src);
        if (typeName.empty()) return false;

        SkipWhitespace(src);
        if (src.empty() || src.front() != '{') {
            if (outError) *outError = "Expected '{' after type name " + std::string(typeName);
            return false;
        }
        src.remove_prefix(1); // consume '{'

        UINode* currentNode = nullptr;

        if (typeName == "Canvas") {
            currentNode = doc.CreateNode<UICanvasNode>(parent);
        } else if (typeName == "Rectangle" || typeName == "Panel") {
            currentNode = doc.CreateNode<UIRectangleNode>(parent);
        } else if (typeName == "Text") {
            currentNode = doc.CreateNode<UITextNode>(parent);
        } else if (typeName == "Image") {
            currentNode = doc.CreateNode<UIImageNode>(parent);
        } else if (typeName == "ProgressBar") {
            currentNode = doc.CreateNode<UIProgressBarNode>(parent);
        } else if (typeName == "RadialGauge") {
            currentNode = doc.CreateNode<UIRadialGaugeNode>(parent);
        } else if (typeName == "Column" || typeName == "VBox") {
            currentNode = doc.CreateNode<UIBoxContainerNode>(parent, true);
        } else if (typeName == "Row" || typeName == "HBox") {
            currentNode = doc.CreateNode<UIBoxContainerNode>(parent, false);
        } else if (typeName == "Button") {
            currentNode = doc.CreateNode<UIButtonNode>(parent);
        } else {
            // Default fallback node
            currentNode = doc.CreateNode<UIRectangleNode>(parent);
        }

        while (!src.empty()) {
            SkipWhitespace(src);
            if (src.empty()) break;

            if (src.front() == '}') {
                src.remove_prefix(1); // consume '}'
                return true;
            }

            std::string_view identifier = ReadWord(src);
            if (identifier.empty()) {
                if (src.front() == '}') {
                    src.remove_prefix(1);
                    return true;
                }
                src.remove_prefix(1);
                continue;
            }

            SkipWhitespace(src);
            if (src.empty()) break;

            if (src.front() == '{') {
                // Child element: `identifier` was the child's TypeName!
                // Rewind identifier so ParseNode can read it
                src = std::string_view(identifier.data(), src.data() - identifier.data() + src.size());
                if (!ParseNode(src, currentNode, doc, outError)) {
                    return false;
                }
            } else if (src.front() == ':') {
                src.remove_prefix(1); // consume ':'
                SkipWhitespace(src);

                // Read value up to newline or semicolon (unless in quotes or brackets)
                std::string_view rawVal;
                if (!src.empty() && src.front() == '"') {
                    size_t closeQuote = src.find('"', 1);
                    while (closeQuote != std::string_view::npos && src[closeQuote - 1] == '\\') {
                        closeQuote = src.find('"', closeQuote + 1);
                    }
                    if (closeQuote != std::string_view::npos) {
                        rawVal = src.substr(0, closeQuote + 1);
                        src.remove_prefix(closeQuote + 1);
                    }
                } else if (!src.empty() && src.front() == '[') {
                    size_t closeBracket = src.find(']');
                    if (closeBracket != std::string_view::npos) {
                        rawVal = src.substr(0, closeBracket + 1);
                        src.remove_prefix(closeBracket + 1);
                    }
                } else {
                    size_t endPos = 0;
                    while (endPos < src.size() && src[endPos] != '\n' && src[endPos] != '\r' && src[endPos] != ';' && src[endPos] != '}') {
                        endPos++;
                    }
                    rawVal = src.substr(0, endPos);
                    src.remove_prefix(endPos);
                }

                ParsePropertyValue(currentNode, identifier, rawVal, doc);
            }
        }

        return true;
    }

    bool UIParser::ParseSource(std::string_view source, UIDocument& outDocument, std::string* outError) {
        outDocument.Clear();
        std::string cleanSource = StripComments(source);
        std::string_view src(cleanSource);

        SkipWhitespace(src);
        while (!src.empty()) {
            if (!ParseNode(src, nullptr, outDocument, outError)) {
                return false;
            }
            SkipWhitespace(src);
        }

        outDocument.RebuildIDMap();
        return true;
    }

    bool UIParser::ParseFile(const std::string& filepath, UIDocument& outDocument, std::string* outError) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            if (outError) *outError = "Failed to open .axleui file: " + filepath;
            return false;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        bool success = ParseSource(source, outDocument, outError);
        if (success) {
            outDocument.SetFilePath(filepath);
        }
        return success;
    }

} // namespace Axle
