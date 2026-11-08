#include <Axle/ExtensionDocument.hpp>
#include <charconv>
#include <map>
namespace Axle {
namespace {
std::string Hex(const std::string& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (unsigned char byte : bytes) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}
bool Unhex(const std::string& text, std::string& bytes) {
    if (text.size() % 2) return false;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    bytes.clear();
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto a = digit(text[i]), b = digit(text[i + 1]);
        if (a < 0 || b < 0) return false;
        bytes += static_cast<char>((a << 4) | b);
    }
    return true;
}
std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
}
bool IsExtensionDocument(const std::string& section) { return section.starts_with("AxlePlugin_"); }
bool ParseExtensionDocument(const std::string& section, const std::vector<std::string>& lines, ExtensionDocument& document) {
    if (!IsExtensionDocument(section)) return false;
    ExtensionDocument parsed;
    if (!Unhex(section.substr(11), parsed.TypeId) || parsed.TypeId.empty() || parsed.TypeId.find('/') == std::string::npos) return false;
    std::map<std::string, std::string> fields;
    for (const auto& line : lines) {
        const auto text = Trim(line);
        if (text.empty() || text.starts_with('#')) continue;
        const auto colon = text.find(':');
        if (colon == std::string::npos || !fields.emplace(text.substr(0, colon), Trim(text.substr(colon + 1))).second) return false;
    }
    if (fields.size() != 4 || !fields.contains("Schema") || !fields.contains("Required") ||
        !fields.contains("EntityReferences") || !fields.contains("Payload")) return false;
    const auto& schema = fields.at("Schema");
    const auto number = std::from_chars(schema.data(), schema.data() + schema.size(), parsed.Schema);
    if (number.ec != std::errc{} || number.ptr != schema.data() + schema.size() || !parsed.Schema) return false;
    for (const auto* flag : {"Required", "EntityReferences"}) if (fields.at(flag) != "true" && fields.at(flag) != "false") return false;
    parsed.Required = fields.at("Required") == "true"; parsed.EntityReferences = fields.at("EntityReferences") == "true";
    if (!Unhex(fields.at("Payload"), parsed.Payload)) return false;
    document = std::move(parsed); return true;
}
void WriteExtensionDocument(std::ostream& output, const ExtensionDocument& document) {
    output << "    AxlePlugin_" << Hex(document.TypeId) << ":\n      Schema: " << document.Schema
        << "\n      Required: " << (document.Required ? "true" : "false")
        << "\n      EntityReferences: " << (document.EntityReferences ? "true" : "false")
        << "\n      Payload: " << Hex(document.Payload) << '\n';
}
}
