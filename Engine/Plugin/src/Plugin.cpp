#include <Axle/Plugin.hpp>
#include <Axle/PluginArtifact.hpp>
#include <Axle/FileSystem.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <tuple>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Axle::Plugin {
namespace {

struct JsonValue {
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Array = std::vector<JsonValue>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> Value = nullptr;

    const Object* ObjectValue() const { return std::get_if<Object>(&Value); }
    const Array* ArrayValue() const { return std::get_if<Array>(&Value); }
    const std::string* StringValue() const { return std::get_if<std::string>(&Value); }
    const double* NumberValue() const { return std::get_if<double>(&Value); }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : m_Text(text) {}

    std::optional<JsonValue> Parse(std::string& error) {
        SkipWhitespace();
        auto result = ParseValue(error);
        if (!result) return std::nullopt;
        SkipWhitespace();
        if (m_Position != m_Text.size()) {
            error = "unexpected trailing JSON data";
            return std::nullopt;
        }
        return result;
    }

private:
    void SkipWhitespace() {
        while (m_Position < m_Text.size() && (m_Text[m_Position] == ' ' || m_Text[m_Position] == '\n' ||
                                               m_Text[m_Position] == '\r' || m_Text[m_Position] == '\t')) {
            ++m_Position;
        }
    }

    bool Consume(char expected) {
        SkipWhitespace();
        if (m_Position >= m_Text.size() || m_Text[m_Position] != expected) return false;
        ++m_Position;
        return true;
    }

    std::optional<JsonValue> ParseValue(std::string& error) {
        SkipWhitespace();
        if (m_Position >= m_Text.size()) { error = "expected JSON value"; return std::nullopt; }
        switch (m_Text[m_Position]) {
        case '{': return ParseObject(error);
        case '[': return ParseArray(error);
        case '"': {
            auto value = ParseString(error);
            if (!value) return std::nullopt;
            return JsonValue{std::move(*value)};
        }
        case 't': if (ConsumeLiteral("true")) return JsonValue{true}; break;
        case 'f': if (ConsumeLiteral("false")) return JsonValue{false}; break;
        case 'n': if (ConsumeLiteral("null")) return JsonValue{nullptr}; break;
        default: {
            auto value = ParseNumber(error);
            if (value) return JsonValue{*value};
            return std::nullopt;
        }
        }
        error = "invalid JSON value";
        return std::nullopt;
    }

    bool ConsumeLiteral(std::string_view literal) {
        if (m_Text.substr(m_Position, literal.size()) != literal) return false;
        m_Position += literal.size();
        return true;
    }

    std::optional<JsonValue> ParseObject(std::string& error) {
        if (!Consume('{')) { error = "expected object"; return std::nullopt; }
        JsonValue::Object object;
        SkipWhitespace();
        if (Consume('}')) return JsonValue{std::move(object)};
        while (m_Position < m_Text.size()) {
            SkipWhitespace();
            auto key = ParseString(error);
            if (!key) return std::nullopt;
            if (!Consume(':')) { error = "expected ':' after object key"; return std::nullopt; }
            auto value = ParseValue(error);
            if (!value) return std::nullopt;
            if (!object.emplace(*key, std::move(*value)).second) {
                error = "duplicate object key '" + *key + "'";
                return std::nullopt;
            }
            SkipWhitespace();
            if (Consume('}')) return JsonValue{std::move(object)};
            if (!Consume(',')) { error = "expected ',' or '}' in object"; return std::nullopt; }
        }
        error = "unterminated object";
        return std::nullopt;
    }

    std::optional<JsonValue> ParseArray(std::string& error) {
        if (!Consume('[')) { error = "expected array"; return std::nullopt; }
        JsonValue::Array array;
        SkipWhitespace();
        if (Consume(']')) return JsonValue{std::move(array)};
        while (m_Position < m_Text.size()) {
            auto value = ParseValue(error);
            if (!value) return std::nullopt;
            array.emplace_back(std::move(*value));
            SkipWhitespace();
            if (Consume(']')) return JsonValue{std::move(array)};
            if (!Consume(',')) { error = "expected ',' or ']' in array"; return std::nullopt; }
        }
        error = "unterminated array";
        return std::nullopt;
    }

    std::optional<std::string> ParseString(std::string& error) {
        if (!Consume('"')) { error = "expected string"; return std::nullopt; }
        std::string result;
        while (m_Position < m_Text.size()) {
            const char c = m_Text[m_Position++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 0x20) { error = "control character in string"; return std::nullopt; }
            if (c != '\\') { result.push_back(c); continue; }
            if (m_Position >= m_Text.size()) break;
            const char escape = m_Text[m_Position++];
            switch (escape) {
            case '"': case '\\': case '/': result.push_back(escape); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                auto readHex = [this]() -> std::optional<uint32_t> {
                    if (m_Position + 4 > m_Text.size()) return std::nullopt;
                    uint32_t value = 0;
                    for (size_t i = 0; i < 4; ++i) {
                        const char digit = m_Text[m_Position++];
                        value <<= 4;
                        if (digit >= '0' && digit <= '9') value += static_cast<uint32_t>(digit - '0');
                        else if (digit >= 'a' && digit <= 'f') value += static_cast<uint32_t>(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F') value += static_cast<uint32_t>(digit - 'A' + 10);
                        else return std::nullopt;
                    }
                    return value;
                };
                auto codepoint = readHex();
                if (!codepoint) { error = "invalid unicode escape"; return std::nullopt; }
                if (*codepoint >= 0xD800 && *codepoint <= 0xDBFF) {
                    if (m_Position + 2 > m_Text.size() || m_Text[m_Position] != '\\' || m_Text[m_Position + 1] != 'u') {
                        error = "unpaired unicode surrogate"; return std::nullopt;
                    }
                    m_Position += 2;
                    auto low = readHex();
                    if (!low || *low < 0xDC00 || *low > 0xDFFF) { error = "invalid unicode surrogate pair"; return std::nullopt; }
                    *codepoint = 0x10000 + ((*codepoint - 0xD800) << 10) + (*low - 0xDC00);
                } else if (*codepoint >= 0xDC00 && *codepoint <= 0xDFFF) {
                    error = "unpaired unicode surrogate"; return std::nullopt;
                }
                if (*codepoint <= 0x7F) result.push_back(static_cast<char>(*codepoint));
                else if (*codepoint <= 0x7FF) {
                    result.push_back(static_cast<char>(0xC0 | (*codepoint >> 6)));
                    result.push_back(static_cast<char>(0x80 | (*codepoint & 0x3F)));
                } else if (*codepoint <= 0xFFFF) {
                    result.push_back(static_cast<char>(0xE0 | (*codepoint >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((*codepoint >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (*codepoint & 0x3F)));
                } else if (*codepoint <= 0x10FFFF) {
                    result.push_back(static_cast<char>(0xF0 | (*codepoint >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((*codepoint >> 12) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | ((*codepoint >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (*codepoint & 0x3F)));
                } else { error = "unicode codepoint is out of range"; return std::nullopt; }
                break;
            }
            default: error = "invalid string escape"; return std::nullopt;
            }
        }
        error = "unterminated string";
        return std::nullopt;
    }

    std::optional<double> ParseNumber(std::string& error) {
        const auto start = m_Position;
        if (m_Text[m_Position] == '-') ++m_Position;
        if (m_Position >= m_Text.size() || (m_Text[m_Position] == '0' && m_Position + 1 < m_Text.size() &&
            std::isdigit(static_cast<unsigned char>(m_Text[m_Position + 1])))) {
            error = "invalid number";
            return std::nullopt;
        }
        if (m_Position < m_Text.size() && m_Text[m_Position] == '0') ++m_Position;
        else if (m_Position < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Position]))) {
            while (m_Position < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Position]))) ++m_Position;
        } else { error = "invalid number"; return std::nullopt; }
        if (m_Position < m_Text.size() && m_Text[m_Position] == '.') {
            ++m_Position;
            const auto fractionStart = m_Position;
            while (m_Position < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Position]))) ++m_Position;
            if (fractionStart == m_Position) { error = "invalid number fraction"; return std::nullopt; }
        }
        if (m_Position < m_Text.size() && (m_Text[m_Position] == 'e' || m_Text[m_Position] == 'E')) {
            ++m_Position;
            if (m_Position < m_Text.size() && (m_Text[m_Position] == '+' || m_Text[m_Position] == '-')) ++m_Position;
            const auto exponentStart = m_Position;
            while (m_Position < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Position]))) ++m_Position;
            if (exponentStart == m_Position) { error = "invalid number exponent"; return std::nullopt; }
        }
        try { return std::stod(std::string(m_Text.substr(start, m_Position - start))); }
        catch (...) { error = "number is out of range"; return std::nullopt; }
    }

    std::string_view m_Text;
    size_t m_Position = 0;
};

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

bool IsValidId(std::string_view id) {
    if (id.empty() || id.front() == '.' || id.back() == '.') return false;
    for (const char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) return false;
    }
    return true;
}

std::filesystem::path PhysicalManifestPath(const std::filesystem::path& path) {
    std::error_code ec;
    auto result = std::filesystem::canonical(path, ec);
    if (ec) result = std::filesystem::absolute(path).lexically_normal();
#if defined(_WIN32)
    const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const DWORD size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (size) {
            std::wstring buffer(size + 1, wchar_t{});
            const DWORD count = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (count && count < buffer.size()) {
                buffer.resize(count);
                if (buffer.rfind(LR"(\\?\UNC\)", 0) == 0) buffer = LR"(\\)" + buffer.substr(8);
                else if (buffer.rfind(LR"(\\?\)", 0) == 0) buffer.erase(0, 4);
                result = std::filesystem::path(buffer);
            }
        }
        CloseHandle(file);
    }
#endif
    return result;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

std::optional<std::string> RequiredString(const JsonValue::Object& object, std::string_view key, std::string& error) {
    const auto* value = Find(object, key);
    if (!value || !value->StringValue() || value->StringValue()->empty()) {
        error = "missing non-empty string field '" + std::string(key) + "'";
        return std::nullopt;
    }
    return *value->StringValue();
}

bool ReadStringArray(const JsonValue* value, std::vector<std::string>& output, std::string& error, std::string_view field) {
    if (!value) return true;
    const auto* array = value->ArrayValue();
    if (!array) { error = "field '" + std::string(field) + "' must be an array"; return false; }
    std::set<std::string, std::less<>> seen;
    for (const auto& item : *array) {
        const auto* string = item.StringValue();
        if (!string || string->empty() || !seen.insert(*string).second) {
            error = "field '" + std::string(field) + "' contains an invalid or duplicate value";
            return false;
        }
        output.push_back(*string);
    }
    return true;
}

std::string HashInputs(const std::vector<BuildInput>& inputs, ProductKind product,
                       std::string_view configuration, std::string_view sdk, std::string_view toolchain) {
    uint64_t hash = 1469598103934665603ull;
    const auto add = [&hash](std::string_view value) {
        for (const unsigned char c : value) { hash ^= c; hash *= 1099511628211ull; }
        hash ^= 0xff; hash *= 1099511628211ull;
    };
    add(std::to_string(static_cast<int>(product))); add(configuration); add(sdk); add(toolchain);
    for (const auto& input : inputs) { add(PathUtf8(input.Path)); add(input.Content); }
    std::ostringstream out; out << std::hex << hash;
    return out.str();
}

} // namespace

std::optional<Manifest> ParseManifest(const std::filesystem::path& path, Diagnostic& diagnostic) {
    diagnostic.Path = path;
    std::ifstream file(path, std::ios::binary);
    if (!file) { diagnostic.Message = "unable to open manifest"; return std::nullopt; }
    std::ostringstream contents; contents << file.rdbuf();
    std::string parseError;
    const auto rootValue = JsonParser(contents.str()).Parse(parseError);
    if (!rootValue || !rootValue->ObjectValue()) { diagnostic.Message = "invalid JSON: " + parseError; return std::nullopt; }
    const auto& root = *rootValue->ObjectValue();
    const auto* schema = Find(root, "schema");
    if (!schema || !schema->NumberValue() || *schema->NumberValue() != kManifestSchemaVersion) {
        diagnostic.Message = "unsupported or missing manifest schema (expected 1)"; return std::nullopt;
    }
    std::string error;
    auto id = RequiredString(root, "id", error); if (!id || !IsValidId(*id)) { diagnostic.Message = id ? "invalid plugin id" : error; return std::nullopt; }
    auto version = RequiredString(root, "version", error); if (!version) { diagnostic.Message = error; return std::nullopt; }
    auto name = RequiredString(root, "name", error); if (!name) { diagnostic.Message = error; return std::nullopt; }

    Manifest manifest;
    manifest.AuthoredManifest = contents.str();
    manifest.Id = *id; manifest.Version = *version; manifest.Name = *name;
    if (const auto* description = Find(root, "description")) {
        if (!description->StringValue()) { diagnostic.Message = "description must be a string"; return std::nullopt; }
        manifest.Description = *description->StringValue();
    }
    if (const auto* api = Find(root, "engineApiVersion")) {
        if (!api->NumberValue() || *api->NumberValue() < 1 || *api->NumberValue() > 2147483647 ||
            static_cast<int>(*api->NumberValue()) != *api->NumberValue()) {
            diagnostic.Message = "engineApiVersion must be a positive integer"; return std::nullopt;
        }
        manifest.EngineApiVersion = static_cast<int>(*api->NumberValue());
    }
    if (const auto* platforms = Find(root, "platforms")) {
        manifest.Platforms.clear();
        if (!ReadStringArray(platforms, manifest.Platforms, error, "platforms") || manifest.Platforms.empty()) {
            diagnostic.Message = "platforms must contain supported target names"; return std::nullopt;
        }
    }
    manifest.ManifestPath = path.lexically_normal(); manifest.Root = path.parent_path().lexically_normal();
    const auto* dependencies = Find(root, "dependencies");
    if (dependencies) {
        const auto* array = dependencies->ArrayValue();
        if (!array) { diagnostic.Message = "field 'dependencies' must be an array"; return std::nullopt; }
        std::set<std::string, std::less<>> seen;
        for (const auto& item : *array) {
            const auto* object = item.ObjectValue();
            if (!object) { diagnostic.Message = "dependency entries must be objects"; return std::nullopt; }
            auto depId = RequiredString(*object, "id", error); if (!depId || !IsValidId(*depId)) { diagnostic.Message = depId ? "invalid dependency id" : error; return std::nullopt; }
            auto depVersion = RequiredString(*object, "version", error); if (!depVersion) { diagnostic.Message = error; return std::nullopt; }
            if (!seen.insert(*depId).second) { diagnostic.Message = "duplicate dependency '" + *depId + "'"; return std::nullopt; }
            manifest.Dependencies.push_back({*depId, *depVersion});
        }
    }

    const auto* modules = Find(root, "modules");
    if (!modules) { diagnostic.Message = "missing modules array"; return std::nullopt; }
    const auto* moduleArray = modules->ArrayValue();
    if (!moduleArray || moduleArray->empty()) { diagnostic.Message = "field 'modules' must be a non-empty array"; return std::nullopt; }
    std::set<std::string, std::less<>> moduleNames;
    for (const auto& item : *moduleArray) {
        const auto* object = item.ObjectValue();
        if (!object) { diagnostic.Message = "module entries must be objects"; return std::nullopt; }
        auto moduleName = RequiredString(*object, "name", error); if (!moduleName || !IsValidId(*moduleName)) { diagnostic.Message = moduleName ? "invalid module name" : error; return std::nullopt; }
        auto moduleKind = RequiredString(*object, "kind", error); if (!moduleKind) { diagnostic.Message = error; return std::nullopt; }
        ModuleKind kind;
        if (*moduleKind == "runtime") kind = ModuleKind::Runtime;
        else if (*moduleKind == "editor") kind = ModuleKind::Editor;
        else if (*moduleKind == "content") kind = ModuleKind::Content;
        else { diagnostic.Message = "unknown module kind '" + *moduleKind + "'"; return std::nullopt; }
        if (!moduleNames.insert(*moduleName).second) { diagnostic.Message = "duplicate module '" + *moduleName + "'"; return std::nullopt; }
        ModuleDescriptor module{*moduleName, kind};
        if (const auto* sourceRoot = Find(*object, "sourceRoot")) {
            const auto* value = sourceRoot->StringValue();
            if (!value || !IsSafeRelativePath(*value)) { diagnostic.Message = "module sourceRoot must be a safe relative path"; return std::nullopt; }
            module.SourceRoot = *value;
        }
        if (!ReadStringArray(Find(*object, "dependencies"), module.Dependencies, error, "module dependencies")) { diagnostic.Message = error; return std::nullopt; }
        manifest.Modules.push_back(std::move(module));
    }
    if (const auto* sourceRoot = Find(root, "sourceRoot")) {
        const auto* value = sourceRoot->StringValue();
        if (!value || !IsSafeRelativePath(*value)) { diagnostic.Message = "sourceRoot must be a safe relative path"; return std::nullopt; }
        manifest.Root = (path.parent_path() / *value).lexically_normal();
    }
    return manifest;
}

Catalog Discover(const DiscoveryOptions& options) {
    Catalog catalog;
    struct Candidate { std::filesystem::path Path; Origin Source; };
    std::vector<Candidate> manifests;
    for (const auto& root : options.Roots) {
        std::error_code error;
        if (!std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)) {
            catalog.Diagnostics.push_back({root, "plugin discovery root does not exist or is not a directory"});
            continue;
        }
        for (std::filesystem::recursive_directory_iterator it(root, error), end; it != end; it.increment(error)) {
            if (error) { catalog.Diagnostics.push_back({root, "plugin discovery failed: " + error.message()}); error.clear(); continue; }
            if (!it->is_regular_file(error) || it->path().extension() != ".axleplugin") continue;
            Origin origin = Origin::Engine;
            for (const auto& projectRoot : options.ProjectRoots) {
                auto ancestor = std::filesystem::absolute(it->path()).parent_path();
                while (!ancestor.empty()) {
                    std::error_code equivalentError;
                    if (std::filesystem::equivalent(ancestor, projectRoot, equivalentError)) { origin = Origin::Project; break; }
                    const auto parent = ancestor.parent_path();
                    if (parent == ancestor) break;
                    ancestor = parent;
                }
                if (origin == Origin::Project) break;
            }
            const auto same = std::find_if(manifests.begin(), manifests.end(), [&](const auto& existing) {
                std::error_code equivalentError;
                return std::filesystem::equivalent(existing.Path, it->path(), equivalentError);
            });
            if (same != manifests.end()) {
                if (origin == Origin::Project) same->Source = origin;
                const auto normalized = PhysicalManifestPath(it->path());
                if (normalized < same->Path) same->Path = normalized;
                continue;
            }
            manifests.push_back({PhysicalManifestPath(it->path()), origin});
        }
    }
    std::sort(manifests.begin(), manifests.end(), [](const auto& a, const auto& b) { return a.Path < b.Path; });
    std::set<std::string, std::less<>> ids;
    for (const auto& candidate : manifests) {
        const auto& path = candidate.Path;
        Diagnostic diagnostic;
        auto manifest = ParseManifest(path, diagnostic);
        if (!manifest) { catalog.Diagnostics.push_back(std::move(diagnostic)); continue; }
        if (!ids.insert(manifest->Id).second) {
            catalog.AmbiguousIds.push_back(manifest->Id);
            const auto first = std::find_if(catalog.Plugins.begin(), catalog.Plugins.end(), [&](const auto& p) { return p.Id == manifest->Id; });
            catalog.Diagnostics.push_back({path, "duplicate plugin id '" + manifest->Id + "'; first declaration: " + PathUtf8(first->ManifestPath)});
            continue;
        }
        manifest->Source = candidate.Source;
        catalog.Plugins.push_back(std::move(*manifest));
    }
    return catalog;
}

Resolution Resolve(const Catalog& catalog, const std::vector<std::string>& selectedIds, ProductKind product) {
    Resolution result;
    std::map<std::string, const Manifest*, std::less<>> byId;
    std::set<std::string, std::less<>> ambiguous(catalog.AmbiguousIds.begin(), catalog.AmbiguousIds.end());
    for (const auto& plugin : catalog.Plugins) {
        if (!byId.emplace(plugin.Id, &plugin).second) ambiguous.insert(plugin.Id);
    }
    std::set<std::string, std::less<>> selected;
    for (const auto& id : selectedIds) {
        if (!selected.insert(id).second) { result.Diagnostics.push_back({{}, "duplicate selected plugin '" + id + "'"}); continue; }
        if (!byId.contains(id)) { result.Diagnostics.push_back({{}, "selected plugin is not discovered: '" + id + "'"}); }
    }
    std::set<std::string, std::less<>> visiting, visited;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (visited.contains(id)) return;
        if (ambiguous.contains(id)) { result.Diagnostics.push_back({{}, "ambiguous selected plugin id '" + id + "'"}); return; }
        if (!visiting.insert(id).second) { result.Diagnostics.push_back({{}, "plugin dependency cycle at '" + id + "'"}); return; }
        const auto it = byId.find(id); if (it == byId.end()) { visiting.erase(id); return; }
        if (it->second->EngineApiVersion != 1) result.Diagnostics.push_back({it->second->ManifestPath, "unsupported Engine API version for '" + id + "'"});
        if (std::find(it->second->Platforms.begin(), it->second->Platforms.end(), "windows-x64") == it->second->Platforms.end())
            result.Diagnostics.push_back({it->second->ManifestPath, "plugin does not support windows-x64: '" + id + "'"});
        for (const auto& dependency : it->second->Dependencies) {
            const auto dep = byId.find(dependency.Id);
            if (dep == byId.end()) { result.Diagnostics.push_back({it->second->ManifestPath, "missing dependency '" + dependency.Id + "'"}); continue; }
            if (it->second->Source == Origin::Engine && dep->second->Source == Origin::Project) {
                result.Diagnostics.push_back({it->second->ManifestPath, "Engine plugin cannot depend on project plugin '" + dependency.Id + "'"}); continue;
            }
            if (!dependency.Version.empty() && dependency.Version != "*" && dependency.Version != dep->second->Version) {
                result.Diagnostics.push_back({it->second->ManifestPath, "dependency version mismatch for '" + dependency.Id + "'"}); continue;
            }
            visit(dependency.Id);
        }
        visiting.erase(id); visited.insert(id); result.Plugins.push_back(*it->second);
    };
    for (const auto& id : selected) visit(id);
    for (const auto& plugin : result.Plugins) {
        bool hasRuntime = false;
        for (const auto& module : plugin.Modules) {
            if (module.Kind == ModuleKind::Runtime) hasRuntime = true;
        }
        if (product == ProductKind::Runtime && !hasRuntime && !plugin.Modules.empty()) {
            bool contentOnly = std::all_of(plugin.Modules.begin(), plugin.Modules.end(), [](const auto& m) { return m.Kind == ModuleKind::Content; });
            if (!contentOnly) result.Diagnostics.push_back({plugin.ManifestPath, "Runtime plugin has no runtime or content module"});
        }
    }

    struct ModuleNode { const Manifest* Plugin; const ModuleDescriptor* Module; std::string Key; };
    std::map<std::string, std::vector<ModuleNode>, std::less<>> modulesByName;
    std::map<std::string, ModuleNode, std::less<>> modulesByKey;
    for (const auto& plugin : result.Plugins) {
        for (const auto& module : plugin.Modules) {
            const auto key = plugin.Id + ":" + module.Name;
            ModuleNode node{&plugin, &module, key};
            modulesByName[module.Name].push_back(node);
            modulesByKey.emplace(key, node);
        }
    }
    std::map<std::string, std::vector<std::string>, std::less<>> moduleEdges;
    for (const auto& [name, candidates] : modulesByName) {
        if (candidates.size() > 1) result.Diagnostics.push_back({{}, "selected module names must be unique: '" + name + "'"});
    }
    for (const auto& [key, node] : modulesByKey) {
        for (const auto& dependency : node.Module->Dependencies) {
            const auto candidates = modulesByName.find(dependency);
            if (candidates == modulesByName.end()) {
                result.Diagnostics.push_back({node.Plugin->ManifestPath, "missing module dependency '" + dependency + "' for '" + key + "'"});
                continue;
            }
            std::vector<ModuleNode> usable;
            for (const auto& candidate : candidates->second) {
                const bool samePlugin = candidate.Plugin->Id == node.Plugin->Id;
                const bool declaredPluginDependency = std::any_of(node.Plugin->Dependencies.begin(), node.Plugin->Dependencies.end(),
                    [&](const auto& pluginDependency) { return pluginDependency.Id == candidate.Plugin->Id; });
                if (samePlugin || declaredPluginDependency) usable.push_back(candidate);
            }
            if (usable.size() != 1) {
                result.Diagnostics.push_back({node.Plugin->ManifestPath, usable.empty()
                    ? "module dependency '" + dependency + "' is outside the plugin dependency closure"
                    : "module dependency '" + dependency + "' is ambiguous"});
                continue;
            }
            const auto& target = usable.front();
            if (node.Module->Kind == ModuleKind::Runtime && target.Module->Kind == ModuleKind::Editor) {
                result.Diagnostics.push_back({node.Plugin->ManifestPath, "runtime module '" + key + "' cannot depend on editor module '" + target.Key + "'"});
            }
            if (node.Module->Kind == ModuleKind::Content && target.Module->Kind != ModuleKind::Content) {
                result.Diagnostics.push_back({node.Plugin->ManifestPath, "content module '" + key + "' cannot depend on code module '" + target.Key + "'"});
            }
            moduleEdges[key].push_back(target.Key);
        }
    }
    std::map<std::string, int, std::less<>> moduleVisit;
    std::function<void(const std::string&)> visitModule = [&](const std::string& key) {
        if (moduleVisit[key] == 1) { result.Diagnostics.push_back({{}, "module dependency cycle at '" + key + "'"}); return; }
        if (moduleVisit[key] == 2) return;
        moduleVisit[key] = 1;
        for (const auto& dependency : moduleEdges[key]) visitModule(dependency);
        moduleVisit[key] = 2;
    };
    for (const auto& [key, unused] : modulesByKey) visitModule(key);
    return result;
}

BuildPlan CreateBuildPlan(const Resolution& resolution, ProductKind product, std::string configuration,
                          std::string sdkFingerprint, std::string toolchainFingerprint) {
    BuildPlan plan; plan.Product = product; plan.Configuration = std::move(configuration); plan.SdkFingerprint = std::move(sdkFingerprint); plan.ToolchainFingerprint = toolchainFingerprint;
    plan.FrozenResolution = resolution;
    if (!resolution.IsValid()) { plan.Diagnostics = resolution.Diagnostics; return plan; }
    if (plan.Configuration.empty() || plan.SdkFingerprint.empty() || plan.ToolchainFingerprint.empty()) {
        plan.Diagnostics.push_back({{}, "configuration, SDK and toolchain identities are required"}); return plan;
    }
    for (const auto& plugin : resolution.Plugins) {
        std::ifstream manifestFile(plugin.ManifestPath, std::ios::binary);
        std::ostringstream manifestBytes; manifestBytes << manifestFile.rdbuf();
        if (!manifestFile || manifestFile.bad() || plugin.AuthoredManifest.empty() || manifestBytes.str() != plugin.AuthoredManifest) {
            plan.Diagnostics.push_back({plugin.ManifestPath, "manifest changed since discovery; refresh and resolve again"}); continue;
        }
        plan.Inputs.push_back({plugin.ManifestPath.lexically_normal(), plugin.AuthoredManifest});
        bool hasCode = false;
        std::error_code error;
        const auto canonicalRoot = std::filesystem::weakly_canonical(plugin.Root, error);
        if (error || !std::filesystem::is_directory(canonicalRoot)) {
            plan.Diagnostics.push_back({plugin.Root, "plugin installation is unavailable"}); continue;
        }
        std::vector<std::filesystem::path> excludedRoots;
        for (const auto& module : plugin.Modules) {
            const auto sourceRoot = module.SourceRoot.empty() ? plugin.Root : (plugin.Root / module.SourceRoot).lexically_normal();
            if (product == ProductKind::Runtime && module.Kind == ModuleKind::Editor) {
                // A shared plugin root still contains common inputs. Exclude only
                // a distinct editor subtree, never the entire installation.
                if (sourceRoot != plugin.Root) excludedRoots.push_back(sourceRoot);
                continue;
            }
            plan.Artifacts.push_back({plugin.Id, module.Name, module.Kind, sourceRoot, plugin.Root, module.Dependencies});
            hasCode |= module.Kind != ModuleKind::Content;
            const auto canonicalSource = std::filesystem::weakly_canonical(sourceRoot, error);
            const auto relative = canonicalSource.lexically_relative(canonicalRoot);
            if (error || !std::filesystem::exists(sourceRoot) || relative.empty() || *relative.begin() == "..") {
                plan.Diagnostics.push_back({sourceRoot, "module source root is missing or escapes plugin installation"});
            }
        }
        // CMake includes, shared headers and authored configure inputs outside a
        // module's subtree affect its output too. Freeze the whole installation,
        // excluding generated caches and product-ineligible module subtrees.
        for (std::filesystem::recursive_directory_iterator it(plugin.Root, error), end; it != end; it.increment(error)) {
            if (error) { plan.Diagnostics.push_back({plugin.Root, "unable to enumerate plugin inputs: " + error.message()}); break; }
            const auto path = it->path().lexically_normal();
            const auto name = PathUtf8(path.filename());
            const auto physical = std::filesystem::weakly_canonical(path, error);
            const auto relative = physical.lexically_relative(canonicalRoot);
            if (error || relative.empty() || *relative.begin() == "..") {
                plan.Diagnostics.push_back({path, "input escapes plugin installation"});
                if (it->is_directory()) it.disable_recursion_pending();
                error.clear(); continue;
            }
            const bool excluded = std::any_of(excludedRoots.begin(), excludedRoots.end(), [&](const auto& excludedRoot) {
                const auto rel = path.lexically_relative(excludedRoot);
                return !rel.empty() && *rel.begin() != "..";
            });
            if (it->is_directory(error) && (excluded || name == ".git" || name == "Intermediate" || name == "Binaries" || name == "build" || name == "CMakeFiles")) {
                it.disable_recursion_pending(); continue;
            }
            if (excluded || !it->is_regular_file(error)) continue;
            if (path == plugin.ManifestPath.lexically_normal()) {
                continue;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file) { plan.Diagnostics.push_back({path, "unable to read plugin input"}); continue; }
            std::ostringstream bytes; bytes << file.rdbuf();
            if (file.bad()) { plan.Diagnostics.push_back({path, "unable to read complete plugin input"}); continue; }
            plan.Inputs.push_back({path, bytes.str()});
        }
        if (hasCode) {
            const auto cmakePath = plugin.Root / "CMakeLists.txt";
            std::ifstream cmakeFile(cmakePath, std::ios::binary);
            if (!cmakeFile) { plan.Diagnostics.push_back({cmakePath, "selected code plugin requires CMakeLists.txt"}); continue; }
            plan.CMakeRoots.push_back(plugin.Root);
        }
    }
    if (!plan.Diagnostics.empty()) return plan;
    std::sort(plan.Inputs.begin(), plan.Inputs.end(), [](const auto& a, const auto& b) { return PathUtf8(a.Path) < PathUtf8(b.Path); });
    std::sort(plan.Artifacts.begin(), plan.Artifacts.end(), [](const auto& a, const auto& b) {
        return std::tie(a.PluginId, a.ModuleName) < std::tie(b.PluginId, b.ModuleName);
    });
    // Module-specific content stamps preserve incremental builds across selection
    // changes and do not depend on filesystem timestamps.
    std::map<std::string, std::size_t> artifactByName;
    for (std::size_t i = 0; i < plan.Artifacts.size(); ++i) artifactByName.emplace(plan.Artifacts[i].ModuleName, i);
    std::set<std::string> computing;
    std::function<std::string(std::size_t)> moduleFingerprint = [&](std::size_t index) -> std::string {
        auto& artifact = plan.Artifacts[index];
        if (!artifact.Fingerprint.empty()) return artifact.Fingerprint;
        if (!computing.insert(artifact.ModuleName).second) { plan.Diagnostics.push_back({{}, "module fingerprint dependency cycle"}); return {}; }
        std::vector<BuildInput> inputs;
        inputs.push_back({"module-identity", artifact.PluginId + ":" + artifact.ModuleName + ":" + ToString(artifact.Kind)});
        for (const auto& input : plan.Inputs) {
            const auto relative = input.Path.lexically_relative(artifact.CMakeRoot);
            if (!relative.empty() && *relative.begin() != "..") inputs.push_back(input);
        }
        for (const auto& dependency : artifact.Dependencies) {
            const auto it = artifactByName.find(dependency);
            if (it == artifactByName.end()) { plan.Diagnostics.push_back({{}, "missing artifact dependency: " + dependency}); continue; }
            inputs.push_back({"dependency/" + dependency, moduleFingerprint(it->second)});
        }
        computing.erase(artifact.ModuleName);
        artifact.Fingerprint = HashInputs(inputs, product, plan.Configuration, plan.SdkFingerprint, toolchainFingerprint);
        return artifact.Fingerprint;
    };
    for (std::size_t i = 0; i < plan.Artifacts.size(); ++i) moduleFingerprint(i);
    plan.Fingerprint = HashInputs(plan.Inputs, product, plan.Configuration, plan.SdkFingerprint, toolchainFingerprint);
    return plan;
}

const char* ToString(ModuleKind kind) {
    switch (kind) { case ModuleKind::Runtime: return "runtime"; case ModuleKind::Editor: return "editor"; case ModuleKind::Content: return "content"; }
    return "unknown";
}

const char* ToString(ProductKind kind) {
    switch (kind) { case ProductKind::Editor: return "editor"; case ProductKind::Runtime: return "runtime"; case ProductKind::Tests: return "tests"; }
    return "unknown";
}

bool WriteBuildReceipt(const BuildPlan& plan, const BuildArtifact& artifact,
                       const std::filesystem::path& receiptPath, std::string& error,
                       const std::filesystem::path& binaryPath) {
    if (!plan.IsValid()) { error = "cannot publish a receipt for an invalid plan"; return false; }
    ArtifactIdentity identity;
    identity.PluginId = artifact.PluginId; identity.ModuleName = artifact.ModuleName;
    identity.Kind = artifact.Kind; identity.Product = plan.Product;
    identity.Configuration = plan.Configuration; identity.SdkFingerprint = plan.SdkFingerprint;
    identity.ToolchainFingerprint = plan.ToolchainFingerprint; identity.Fingerprint = plan.Fingerprint;
    identity.SourceRoot = artifact.SourceRoot;
    return WriteArtifactReceipt(identity, binaryPath.empty()
        ? receiptPath.parent_path() / (artifact.ModuleName + (plan.Product == ProductKind::Runtime ? ".lib" : ".dll"))
        : binaryPath, receiptPath, error);
}

bool GenerateCMakeInputs(const BuildPlan& plan, const std::filesystem::path& outputPath, std::string& error) {
    if (!plan.IsValid() || plan.Fingerprint.empty() || outputPath.empty()) {
        error = "cannot generate a module graph from an invalid or incomplete plan"; return false;
    }
    const auto quote = [](const std::string& value) {
        std::string equal = "=";
        while (value.find("]" + equal + "]") != std::string::npos) equal += '=';
        return "[" + equal + "[" + value + "]" + equal + "]";
    };
    std::ostringstream out;
    out << "# Frozen Axle plugin module graph\n# Fingerprint: " << plan.Fingerprint << '\n';
    out << "set(AXLE_SELECTED_PLUGIN_COUNT " << plan.Artifacts.size() << ")\n";
    out << "set(AXLE_SELECTED_PLUGIN_FINGERPRINT " << quote(plan.Fingerprint) << ")\n";
    out << "set(AXLE_SELECTED_PLUGIN_MODULES";
    for (const auto& artifact : plan.Artifacts) if (artifact.Kind != ModuleKind::Content) out << ' ' << quote(artifact.ModuleName);
    out << ")\n";
    for (const auto& artifact : plan.Artifacts) {
        out << "set(AXLE_PLUGIN_" << artifact.ModuleName << "_SOURCE_ROOT " << quote(PathUtf8(artifact.SourceRoot)) << ")\n";
        out << "set(AXLE_PLUGIN_" << artifact.ModuleName << "_INSTALL_ROOT " << quote(PathUtf8(artifact.CMakeRoot)) << ")\n";
        out << "set(AXLE_PLUGIN_" << artifact.ModuleName << "_KIND " << quote(ToString(artifact.Kind)) << ")\n";
        out << "set(AXLE_PLUGIN_" << artifact.ModuleName << "_ID " << quote(artifact.PluginId) << ")\n";
        out << "set(AXLE_PLUGIN_" << artifact.ModuleName << "_FINGERPRINT " << quote(artifact.Fingerprint) << ")\n";
    }
    out << "set(AXLE_FROZEN_PLUGIN_ROOTS";
    for (const auto& root : plan.CMakeRoots) out << ' ' << quote(PathUtf8(root));
    out << ")\n";
    for (std::size_t index = 0; index < plan.CMakeRoots.size(); ++index) {
        out << "add_subdirectory(" << quote(PathUtf8(plan.CMakeRoots[index])) << " plugin-" << index << ")\n";
    }
    for (const auto& artifact : plan.Artifacts) {
        if (artifact.Kind == ModuleKind::Content) continue;
        out << "if(NOT TARGET " << artifact.ModuleName << ")\nmessage(FATAL_ERROR \"Missing selected module: " << artifact.ModuleName << "\")\nendif()\n";
        out << "get_target_property(moduleType " << artifact.ModuleName << " TYPE)\n"
            << "get_target_property(moduleImported " << artifact.ModuleName << " IMPORTED)\n"
            << "if(NOT moduleType STREQUAL \"SHARED_LIBRARY\" OR moduleImported)\n"
            << "message(FATAL_ERROR \"Selected module must build a native shared library: " << artifact.ModuleName << "\")\nendif()\n";
        out << "axle_validate_plugin_module(" << artifact.ModuleName << ")\n";
        if (!artifact.Dependencies.empty()) {
            out << "target_link_libraries(" << artifact.ModuleName << " PRIVATE";
            for (const auto& dependency : artifact.Dependencies) out << ' ' << dependency;
            out << ")\n";
        }
    }
    return FileSystem::WriteTextFileAtomically(outputPath, out.str(), &error);
}

} // namespace Axle::Plugin
