#include <Axle/BlockText.hpp>
#include <cctype>
#include <unordered_set>

namespace Axle {
    std::string BlockText::StripComments(std::string_view source) {
        // Preserve offsets and token boundaries for diagnostics and scalar readers.
        std::string result(source);
        bool quoted = false;
        for (size_t i = 0; i < source.size(); ++i) {
            if (quoted) {
                if (source[i] == '\\' && i + 1 < source.size()) ++i;
                else if (source[i] == '"') quoted = false;
            } else if (source[i] == '"') quoted = true;
            else if ((source[i] == '#' && (i == 0 || source[i-1] == '\n' || source[i-1] == '\r')) ||
                     (source[i] == '/' && i + 1 < source.size() && source[i+1] == '/')) {
                while (i < source.size() && source[i] != '\n' && source[i] != '\r') result[i++] = ' ';
                if (i == source.size()) break;
            } else if (source[i] == '/' && i + 1 < source.size() && source[i+1] == '*') {
                result[i++] = ' '; result[i] = ' ';
                while (++i < source.size()) {
                    if (source[i] == '*' && i + 1 < source.size() && source[i+1] == '/') {
                        result[i++] = ' '; result[i] = ' '; break;
                    }
                    if (source[i] != '\n' && source[i] != '\r') result[i] = ' ';
                }
            }
        }
        return result;
    }

    std::string BlockText::QuoteString(std::string_view value) {
        std::string result = "\"";
        for (char c : value) {
            switch (c) {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                default: result += c; break;
            }
        }
        return result + '"';
    }

    namespace {
        class DSLReader {
        public:
            explicit DSLReader(std::string_view text) : Text(text) {}
            std::string Error;
            TextSourceLocation ErrorLocation;

            bool Document(std::shared_ptr<BlockTextNode>& result) {
                if (!Skip(false)) return false;
                auto root = std::make_shared<BlockTextNode>();
                root->Source = Location();
                if (!Word(root->Type)) return Fail("Expected root asset type");
                if (!Skip(false) || !Take('{')) return Fail("Expected '{' after root asset type");
                if (!Block(*root, 0) || !Skip(false)) return false;
                if (Position != Text.size()) return Fail("Unexpected text after root block");
                result = std::move(root);
                return true;
            }

        private:
            std::string_view Text;
            size_t Position = 0, Line = 1, Column = 1;
            TextSourceLocation Location() const { return {Position, Line, Column}; }
            char Current() const { return Position < Text.size() ? Text[Position] : '\0'; }
            void Advance() {
                if (Current() == '\n') { ++Line; Column = 1; } else ++Column;
                ++Position;
            }
            bool Take(char c) { if (Current() != c) return false; Advance(); return true; }
            bool Fail(std::string message) {
                if (Error.empty()) {
                    ErrorLocation = Location();
                    Error = "Line " + std::to_string(Line) + ", column " + std::to_string(Column) + ": " + message;
                }
                return false;
            }
            bool Skip(bool separators = true) {
                while (Position < Text.size()) {
                    const char c = Current();
                    const char next = Position + 1 < Text.size() ? Text[Position+1] : '\0';
                    if (std::isspace(static_cast<unsigned char>(c)) || (separators && (c == ',' || c == ';'))) {
                        Advance();
                    } else if ((c == '#' && (Position == 0 || Text[Position-1] == '\n' || Text[Position-1] == '\r')) ||
                               (c == '/' && next == '/')) {
                        while (Position < Text.size() && Current() != '\n') Advance();
                    } else if (c == '/' && next == '*') {
                        Advance(); Advance();
                        while (Position < Text.size() && !(Current() == '*' && Position+1 < Text.size() && Text[Position+1] == '/')) Advance();
                        if (Position == Text.size()) return Fail("Unterminated block comment");
                        Advance(); Advance();
                    } else break;
                }
                return true;
            }
            bool Word(std::string& value) {
                const size_t start = Position;
                if (!(std::isalpha(static_cast<unsigned char>(Current())) || Current() == '_')) return false;
                do { Advance(); } while (Position < Text.size() &&
                    (std::isalnum(static_cast<unsigned char>(Current())) || Current() == '_' || Current() == '.'));
                value = Text.substr(start, Position-start);
                return true;
            }
            bool String() {
                Advance();
                while (Position < Text.size()) {
                    if (Take('"')) return true;
                    if (static_cast<unsigned char>(Current()) < 32) return Fail("Unescaped control character in string");
                    if (Take('\\')) {
                        const char escaped = Current();
                        if (escaped == '\0' || std::string_view("\"\\/nrtbf").find(escaped) == std::string_view::npos)
                            return Fail("Unsupported or incomplete string escape");
                    }
                    Advance();
                }
                return Fail("Unterminated string");
            }
            bool Value(size_t depth) {
                if (depth > 128) return Fail("Asset nesting exceeds 128 levels");
                if (Current() == '"') return String();
                if (Take('[')) {
                    if (!Skip(false)) return false;
                    if (Take(']')) return true;
                    while (true) {
                        if (!Value(depth+1) || !Skip(false)) return false;
                        if (Take(']')) return true;
                        if (!Take(',')) return Fail("Expected ',' or ']' in array");
                        if (!Skip(false)) return false;
                        if (Current() == ']') return Fail("Expected array element after ','");
                    }
                }
                const size_t start = Position;
                while (Position < Text.size()) {
                    const char c = Current();
                    if (std::isspace(static_cast<unsigned char>(c)) || std::string_view(",;{}[]:\"").find(c) != std::string_view::npos ||
                        (c == '/' && Position+1 < Text.size() && (Text[Position+1] == '/' || Text[Position+1] == '*'))) break;
                    Advance();
                }
                return Position != start || Fail("Expected property value");
            }
            bool Block(BlockTextNode& block, size_t depth) {
                if (depth > 128) return Fail("Asset nesting exceeds 128 levels");
                std::unordered_set<std::string> keys;
                while (true) {
                    if (!Skip()) return false;
                    if (Take('}')) return true;
                    if (Position == Text.size()) return Fail("Expected '}' before end of source");
                    const auto location = Location();
                    std::string key;
                    if (!Word(key)) return Fail("Expected property or child block name");
                    if (!Skip(false)) return false;
                    if (Take('{')) {
                        auto child = std::make_shared<BlockTextNode>();
                        child->Type = std::move(key); child->Source = location;
                        if (!Block(*child, depth+1)) return false;
                        block.Children.push_back(std::move(child));
                    } else {
                        if (!Take(':')) return Fail("Expected ':' or '{' after '" + key + "'");
                        if (!keys.insert(key).second) return Fail("Duplicate property '" + key + "'");
                        if (!Skip(false)) return false;
                        const size_t start = Position;
                        if (!Value(depth+1)) return false;
                        block.Properties.push_back({std::move(key), BlockText::StripComments(Text.substr(start, Position-start)), location});
                    }
                }
            }
        };
    }

    bool BlockText::Parse(std::string_view source, std::shared_ptr<BlockTextNode>& outRoot, std::string* outError,
                              TextSourceLocation* outLocation) {
        if (outError) outError->clear();
        if (outLocation) *outLocation = {};
        DSLReader reader(source);
        std::shared_ptr<BlockTextNode> candidate;
        if (!reader.Document(candidate)) {
            if (outError) *outError = std::move(reader.Error);
            if (outLocation) *outLocation = reader.ErrorLocation;
            return false;
        }
        outRoot = std::move(candidate);
        return true;
    }

    bool BlockText::IsUnversionedFlatDocument(std::string_view source, std::string_view rootType) {
        std::shared_ptr<BlockTextNode> root;
        if (!Parse(source, root) || root->Type != rootType || !root->Children.empty()) return false;
        for (const auto& property : root->Properties)
            if (property.Key == "schemaVersion" || property.Key == "type") return false;
        return true;
    }
}
