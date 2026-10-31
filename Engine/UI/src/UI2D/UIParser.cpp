#include <Axle/UI/UIParser.hpp>
#include <Axle/FileSystem.hpp>
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
        std::filesystem::path p(filepath);
        std::string source = FileSystem::ReadTextFile(p);
        if (source.empty() && p.extension() == ".axleui") {
            p.replace_extension(".axleasset");
            source = FileSystem::ReadTextFile(p);
        }
        if (source.empty()) {
            auto resolved = FileSystem::ResolveAssetPath(filepath);
            source = FileSystem::ReadTextFile(resolved);
            if (source.empty() && resolved.extension() == ".axleui") {
                resolved.replace_extension(".axleasset");
                source = FileSystem::ReadTextFile(resolved);
            }
            if (!source.empty()) {
                p = resolved;
            }
        }

        if (source.empty()) {
            if (outError) *outError = "Failed to open UI asset file: " + filepath;
            return false;
        }

        bool success = ParseSource(source, outDocument, outError);
        if (success) {
            outDocument.SetFilePath(p.string());
        }
        return success;
    }

    static std::string FormatFloat(float val) {
        if (std::abs(val - std::round(val)) < 1e-4f) {
            return std::to_string(static_cast<long long>(std::round(val)));
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", val);
        std::string s(buf);
        while (s.find('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) {
            bool wasDot = (s.back() == '.');
            s.pop_back();
            if (wasDot) break;
        }
        return s;
    }

    static void SerializeNode(const UINode* node, const UIDocument& doc, int level, std::stringstream& ss) {
        if (!node) return;

        std::string ind(level * 4, ' ');

        // 1. Determine Type Name
        std::string typeName = "Rectangle";
        if (dynamic_cast<const UICanvasNode*>(node)) {
            typeName = "Canvas";
        } else if (dynamic_cast<const UIButtonNode*>(node)) {
            typeName = "Button";
        } else if (dynamic_cast<const UITextNode*>(node)) {
            typeName = "Text";
        } else if (dynamic_cast<const UIImageNode*>(node)) {
            typeName = "Image";
        } else if (dynamic_cast<const UIProgressBarNode*>(node)) {
            typeName = "ProgressBar";
        } else if (dynamic_cast<const UIRadialGaugeNode*>(node)) {
            typeName = "RadialGauge";
        } else if (auto* box = dynamic_cast<const UIBoxContainerNode*>(node)) {
            typeName = box->IsVertical ? "Column" : "Row";
        } else if (dynamic_cast<const UIRectangleNode*>(node)) {
            typeName = "Rectangle";
        }

        ss << ind << typeName << " {\n";
        std::string propInd = ind + "    ";

        // 2. ID
        if (!node->ID.empty()) {
            ss << propInd << "id: \"" << node->ID << "\"\n";
        }

        // Helper to check for property bindings
        auto getBindingExpr = [&](const std::string& propName) -> std::string {
            for (const auto& b : doc.GetBindings()) {
                if (b.TargetNode == node && b.PropertyName == propName) {
                    if (b.Type == UIBindingType::Direct) {
                        return "{" + b.SourceKey + "}";
                    } else if (b.Type == UIBindingType::Interpolated) {
                        return b.TemplateText;
                    } else if (b.Type == UIBindingType::Ternary) {
                        return "{" + b.SourceKey + " ? " + b.TrueValue + " : " + b.FalseValue + "}";
                    }
                }
            }
            return "";
        };

        // 3. Visibility
        std::string visBinding = getBindingExpr("visible");
        if (!visBinding.empty()) {
            ss << propInd << "visible: \"" << visBinding << "\"\n";
        } else if (!node->Visible) {
            ss << propInd << "visible: false\n";
        }

        // 4. Layout & RectTransform
        if (node->UseRectTransform) {
            ss << propInd << "useRectTransform: true\n";
            ss << propInd << "anchorMin: [" << FormatFloat(node->Transform.AnchorMin.x) << ", " << FormatFloat(node->Transform.AnchorMin.y) << "]\n";
            ss << propInd << "anchorMax: [" << FormatFloat(node->Transform.AnchorMax.x) << ", " << FormatFloat(node->Transform.AnchorMax.y) << "]\n";
            ss << propInd << "pivot: [" << FormatFloat(node->Transform.Pivot.x) << ", " << FormatFloat(node->Transform.Pivot.y) << "]\n";
            ss << propInd << "anchoredPosition: [" << FormatFloat(node->Transform.AnchoredPosition.x) << ", " << FormatFloat(node->Transform.AnchoredPosition.y) << "]\n";
            ss << propInd << "sizeDelta: [" << FormatFloat(node->Transform.SizeDelta.x) << ", " << FormatFloat(node->Transform.SizeDelta.y) << "]\n";
            if (std::abs(node->Transform.Rotation) > 1e-4f) {
                ss << propInd << "rotation: " << FormatFloat(node->Transform.Rotation) << "\n";
            }
            if (std::abs(node->Transform.Scale.x - 1.0f) > 1e-4f || std::abs(node->Transform.Scale.y - 1.0f) > 1e-4f) {
                ss << propInd << "scale: [" << FormatFloat(node->Transform.Scale.x) << ", " << FormatFloat(node->Transform.Scale.y) << "]\n";
            }
        } else {
            ss << propInd << "position: [" << FormatFloat(node->Position.x) << ", " << FormatFloat(node->Position.y) << "]\n";
            ss << propInd << "size: [" << FormatFloat(node->Size.x) << ", " << FormatFloat(node->Size.y) << "]\n";
            if (node->Anchor != UIAnchor::TopLeft) {
                ss << propInd << "anchor: \"" << UIAnchorToString(node->Anchor) << "\"\n";
            }
        }

        // 5. Node Specific Properties
        if (auto* canvas = dynamic_cast<const UICanvasNode*>(node)) {
            ss << propInd << "referenceResolution: [" << FormatFloat(canvas->ReferenceResolution.x) << ", " << FormatFloat(canvas->ReferenceResolution.y) << "]\n";
        } else if (auto* btn = dynamic_cast<const UIButtonNode*>(node)) {
            ss << propInd << "normalColor: \"" << UINode::ToHexColor(btn->NormalColor) << "\"\n";
            ss << propInd << "hoverColor: \"" << UINode::ToHexColor(btn->HoverColor) << "\"\n";
            ss << propInd << "pressedColor: \"" << UINode::ToHexColor(btn->PressedColor) << "\"\n";
            if (btn->Style.CornerRadius > 0.0f) {
                ss << propInd << "cornerRadius: " << FormatFloat(btn->Style.CornerRadius) << "\n";
            }
            if (btn->Style.BorderWidth > 0.0f) {
                ss << propInd << "borderWidth: " << FormatFloat(btn->Style.BorderWidth) << "\n";
                ss << propInd << "borderColor: \"" << UINode::ToHexColor(btn->Style.BorderColor) << "\"\n";
            }
            if (!btn->OnClickAction.empty()) {
                ss << propInd << "onClick: \"" << btn->OnClickAction << "\"\n";
            }
            if (!btn->NavUp.empty())    ss << propInd << "navUp: \"" << btn->NavUp << "\"\n";
            if (!btn->NavDown.empty())  ss << propInd << "navDown: \"" << btn->NavDown << "\"\n";
            if (!btn->NavLeft.empty())  ss << propInd << "navLeft: \"" << btn->NavLeft << "\"\n";
            if (!btn->NavRight.empty()) ss << propInd << "navRight: \"" << btn->NavRight << "\"\n";
        } else if (auto* rect = dynamic_cast<const UIRectangleNode*>(node)) {
            std::string colBinding = getBindingExpr("color");
            if (!colBinding.empty()) {
                ss << propInd << "color: \"" << colBinding << "\"\n";
            } else {
                ss << propInd << "color: \"" << UINode::ToHexColor(rect->Style.BackgroundColor) << "\"\n";
            }
            if (rect->Style.CornerRadius > 0.0f) {
                ss << propInd << "cornerRadius: " << FormatFloat(rect->Style.CornerRadius) << "\n";
            }
            if (rect->Style.BorderWidth > 0.0f) {
                ss << propInd << "borderWidth: " << FormatFloat(rect->Style.BorderWidth) << "\n";
                ss << propInd << "borderColor: \"" << UINode::ToHexColor(rect->Style.BorderColor) << "\"\n";
            }
        } else if (auto* text = dynamic_cast<const UITextNode*>(node)) {
            std::string textBinding = getBindingExpr("text");
            if (!textBinding.empty()) {
                ss << propInd << "text: \"" << textBinding << "\"\n";
            } else {
                ss << propInd << "text: \"" << text->Text << "\"\n";
            }
            ss << propInd << "fontSize: " << FormatFloat(text->FontSize) << "\n";
            std::string colBinding = getBindingExpr("color");
            if (!colBinding.empty()) {
                ss << propInd << "color: \"" << colBinding << "\"\n";
            } else {
                ss << propInd << "color: \"" << UINode::ToHexColor(text->Color) << "\"\n";
            }
            if (text->AutoSize) {
                ss << propInd << "autoSize: true\n";
            }
        } else if (auto* img = dynamic_cast<const UIImageNode*>(node)) {
            if (!img->TexturePath.empty()) {
                ss << propInd << "texture: \"" << img->TexturePath << "\"\n";
            }
            ss << propInd << "tintColor: \"" << UINode::ToHexColor(img->TintColor) << "\"\n";
        } else if (auto* bar = dynamic_cast<const UIProgressBarNode*>(node)) {
            std::string valBinding = getBindingExpr("value");
            if (!valBinding.empty()) {
                ss << propInd << "value: \"" << valBinding << "\"\n";
            } else {
                ss << propInd << "value: " << FormatFloat(bar->Value) << "\n";
            }
            ss << propInd << "fillColor: \"" << UINode::ToHexColor(bar->FillColor) << "\"\n";
            ss << propInd << "backgroundColor: \"" << UINode::ToHexColor(bar->BackgroundColor) << "\"\n";
            if (bar->CornerRadius > 0.0f) {
                ss << propInd << "cornerRadius: " << FormatFloat(bar->CornerRadius) << "\n";
            }
        } else if (auto* gauge = dynamic_cast<const UIRadialGaugeNode*>(node)) {
            std::string valBinding = getBindingExpr("value");
            if (!valBinding.empty()) {
                ss << propInd << "value: \"" << valBinding << "\"\n";
            } else {
                ss << propInd << "value: " << FormatFloat(gauge->Value) << "\n";
            }
            ss << propInd << "startAngle: " << FormatFloat(gauge->StartAngle) << "\n";
            ss << propInd << "endAngle: " << FormatFloat(gauge->EndAngle) << "\n";
            ss << propInd << "thickness: " << FormatFloat(gauge->Thickness) << "\n";
            ss << propInd << "color: \"" << UINode::ToHexColor(gauge->Color) << "\"\n";
            ss << propInd << "trackColor: \"" << UINode::ToHexColor(gauge->TrackColor) << "\"\n";
        } else if (auto* box = dynamic_cast<const UIBoxContainerNode*>(node)) {
            if (box->Spacing > 0.0f) {
                ss << propInd << "spacing: " << FormatFloat(box->Spacing) << "\n";
            }
        }

        // 6. Child Elements
        for (const UINode* child : node->Children) {
            ss << "\n";
            SerializeNode(child, doc, level + 1, ss);
        }

        ss << ind << "}\n";
    }

    std::string UIParser::SerializeDocument(const UIDocument& document) {
        std::stringstream ss;
        if (const UINode* root = document.GetRootNode()) {
            SerializeNode(root, document, 0, ss);
        }
        return ss.str();
    }

    bool UIParser::SaveFile(const std::string& filepath, const UIDocument& document, std::string* outError) {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            if (outError) *outError = "Failed to open file for writing: " + filepath;
            return false;
        }

        std::string serialized = SerializeDocument(document);
        file << serialized;
        return true;
    }

} // namespace Axle

