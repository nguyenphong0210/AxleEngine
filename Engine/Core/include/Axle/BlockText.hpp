#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Axle {
    struct TextSourceLocation {
        size_t Offset = 0;
        size_t Line = 1;
        size_t Column = 1;
    };

    struct BlockTextProperty {
        std::string Key;
        std::string RawValue;
        TextSourceLocation Source;
    };

    struct BlockTextNode {
        std::string Type;
        std::vector<BlockTextProperty> Properties;
        std::vector<std::shared_ptr<BlockTextNode>> Children;
        TextSourceLocation Source;
    };

    // Domain-neutral block syntax. No asset kinds, rendering or editor semantics.
    class BlockText {
    public:
        static bool Parse(std::string_view source, std::shared_ptr<BlockTextNode>& outRoot,
                          std::string* error = nullptr, TextSourceLocation* location = nullptr);
        static std::string StripComments(std::string_view source);
        static std::string QuoteString(std::string_view value);
        // Conservative guard for writers that only preserve flat, unversioned fields.
        // Rejects malformed syntax, children, and reserved type/schemaVersion tags.
        static bool IsUnversionedFlatDocument(std::string_view source, std::string_view rootType);
    };
}
