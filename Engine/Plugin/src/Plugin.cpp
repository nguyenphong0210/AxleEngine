#include <Axle/Plugin.hpp>

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
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' || c == '/')) return false;
    }
    return true;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
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
    for (const auto& input : inputs) { add(input.Path.generic_string()); add(input.Content); }
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
    manifest.Id = *id; manifest.Version = *version; manifest.Name = *name;
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
    std::vector<std::filesystem::path> manifests;
    for (const auto& root : options.Roots) {
        std::error_code error;
        if (!std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)) {
            catalog.Diagnostics.push_back({root, "plugin discovery root does not exist or is not a directory"});
            continue;
        }
        for (std::filesystem::recursive_directory_iterator it(root, error), end; it != end; it.increment(error)) {
            if (error) { catalog.Diagnostics.push_back({root, "plugin discovery failed: " + error.message()}); error.clear(); continue; }
            if (it->is_regular_file(error) && it->path().extension() == ".axleplugin") manifests.push_back(it->path());
        }
    }
    std::sort(manifests.begin(), manifests.end());
    std::set<std::string, std::less<>> ids;
    for (const auto& path : manifests) {
        Diagnostic diagnostic;
        auto manifest = ParseManifest(path, diagnostic);
        if (!manifest) { catalog.Diagnostics.push_back(std::move(diagnostic)); continue; }
        if (!ids.insert(manifest->Id).second) { catalog.Diagnostics.push_back({path, "duplicate plugin id '" + manifest->Id + "'"}); continue; }
        catalog.Plugins.push_back(std::move(*manifest));
    }
    return catalog;
}

Resolution Resolve(const Catalog& catalog, const std::vector<std::string>& selectedIds, ProductKind product) {
    Resolution result;
    std::map<std::string, const Manifest*, std::less<>> byId;
    for (const auto& plugin : catalog.Plugins) byId.emplace(plugin.Id, &plugin);
    std::set<std::string, std::less<>> selected;
    for (const auto& id : selectedIds) {
        if (!selected.insert(id).second) { result.Diagnostics.push_back({{}, "duplicate selected plugin '" + id + "'"}); continue; }
        if (!byId.contains(id)) { result.Diagnostics.push_back({{}, "selected plugin is not discovered: '" + id + "'"}); }
    }
    std::set<std::string, std::less<>> visiting, visited;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (visited.contains(id)) return;
        if (!visiting.insert(id).second) { result.Diagnostics.push_back({{}, "plugin dependency cycle at '" + id + "'"}); return; }
        const auto it = byId.find(id); if (it == byId.end()) { visiting.erase(id); return; }
        for (const auto& dependency : it->second->Dependencies) {
            const auto dep = byId.find(dependency.Id);
            if (dep == byId.end()) { result.Diagnostics.push_back({it->second->ManifestPath, "missing dependency '" + dependency.Id + "'"}); continue; }
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
    BuildPlan plan; plan.Product = product; plan.Configuration = std::move(configuration); plan.SdkFingerprint = std::move(sdkFingerprint);
    if (!resolution.IsValid()) { plan.Diagnostics = resolution.Diagnostics; return plan; }
    for (const auto& plugin : resolution.Plugins) {
        std::ifstream manifestFile(plugin.ManifestPath, std::ios::binary);
        std::ostringstream manifestContents;
        if (!manifestFile) {
            plan.Diagnostics.push_back({plugin.ManifestPath, "unable to read manifest for build fingerprint"});
            continue;
        }
        manifestContents << manifestFile.rdbuf();
        BuildInput manifestInput{plugin.ManifestPath.lexically_normal(), manifestContents.str()};
        plan.Inputs.push_back(manifestInput);
        for (const auto& module : plugin.Modules) {
            if (product == ProductKind::Runtime && module.Kind == ModuleKind::Editor) continue;
            const auto sourceRoot = module.SourceRoot.empty() ? plugin.Root : (plugin.Root / module.SourceRoot).lexically_normal();
            plan.Artifacts.push_back({plugin.Id, module.Name, module.Kind, sourceRoot});
            std::error_code error;
            if (!std::filesystem::exists(sourceRoot, error)) {
                plan.Diagnostics.push_back({sourceRoot, "module source root does not exist for build fingerprint"});
                continue;
            }
            if (std::filesystem::is_regular_file(sourceRoot, error)) {
                std::ifstream sourceFile(sourceRoot, std::ios::binary);
                std::ostringstream sourceContents; sourceContents << sourceFile.rdbuf();
                plan.Inputs.push_back({sourceRoot, sourceContents.str()});
            } else if (std::filesystem::is_directory(sourceRoot, error)) {
                for (std::filesystem::recursive_directory_iterator it(sourceRoot, error), end; it != end; it.increment(error)) {
                    if (error) { plan.Diagnostics.push_back({sourceRoot, "unable to enumerate module source root: " + error.message()}); error.clear(); continue; }
                    if (!it->is_regular_file(error)) continue;
                    std::ifstream sourceFile(it->path(), std::ios::binary);
                    std::ostringstream sourceContents; sourceContents << sourceFile.rdbuf();
                    plan.Inputs.push_back({it->path().lexically_normal(), sourceContents.str()});
                }
            }
        }
    }
    if (!plan.Diagnostics.empty()) return plan;
    std::sort(plan.Inputs.begin(), plan.Inputs.end(), [](const auto& a, const auto& b) { return a.Path.generic_string() < b.Path.generic_string(); });
    std::sort(plan.Artifacts.begin(), plan.Artifacts.end(), [](const auto& a, const auto& b) {
        return std::tie(a.PluginId, a.ModuleName) < std::tie(b.PluginId, b.ModuleName);
    });
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

} // namespace Axle::Plugin
