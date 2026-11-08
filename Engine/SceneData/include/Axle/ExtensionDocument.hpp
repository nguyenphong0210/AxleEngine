#pragma once
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
namespace Axle {
struct ExtensionDocument {
    std::string TypeId;
    std::uint32_t Schema = 0;
    bool Required = false;
    bool EntityReferences = false;
    std::string Payload;
};
bool IsExtensionDocument(const std::string& section);
bool ParseExtensionDocument(const std::string& section, const std::vector<std::string>& lines, ExtensionDocument& document);
void WriteExtensionDocument(std::ostream& output, const ExtensionDocument& document);
}
