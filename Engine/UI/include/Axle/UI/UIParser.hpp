#pragma once
#include <Axle/UI/UIDocument.hpp>
#include <string>
#include <string_view>

namespace Axle {

    class UIParser {
    public:
        static bool ParseSource(std::string_view source, UIDocument& outDocument, std::string* outError = nullptr);
        static bool ParseFile(const std::string& filepath, UIDocument& outDocument, std::string* outError = nullptr);

    private:
        static std::string StripComments(std::string_view source);
        static bool ParseNode(std::string_view& src, UINode* parent, UIDocument& doc, std::string* outError);
        static bool ParsePropertyValue(UINode* node, std::string_view propName, std::string_view rawVal, UIDocument& doc);
    };

    using AxleUIParser = UIParser;

} // namespace Axle
