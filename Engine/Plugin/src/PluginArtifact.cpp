#include <Axle/PluginArtifact.hpp>
#include <Axle/FileSystem.hpp>
#include <array>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace Axle::Plugin {
namespace {
bool ValidField(const std::string& value) {
    return !value.empty() && value.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool Complete(const ArtifactIdentity& id) {
    return ValidField(id.PluginId) && ValidField(id.ModuleName) && ValidField(id.Configuration) &&
        ValidField(id.SdkFingerprint) && ValidField(id.ToolchainFingerprint) && ValidField(id.Fingerprint) &&
        !id.SourceRoot.empty() && ValidField(id.SourceRoot.generic_string()) &&
        (id.Kind == ModuleKind::Runtime || id.Kind == ModuleKind::Editor) &&
        (id.Product == ProductKind::Editor || id.Product == ProductKind::Tests || id.Product == ProductKind::Runtime);
}
}

std::string HashArtifactFile(const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) { error = "artifact is not a regular file: " + path.generic_string(); return {}; }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot read artifact: " + path.generic_string(); return {}; }
    std::uint64_t hash = 1469598103934665603ull;
    std::array<char, 16384> buffer;
    std::uint64_t size = 0;
    while (in) {
        in.read(buffer.data(), buffer.size());
        for (std::streamsize i = 0; i < in.gcount(); ++i) { hash ^= static_cast<unsigned char>(buffer[i]); hash *= 1099511628211ull; }
        size += static_cast<std::uint64_t>(in.gcount());
    }
    if (!in.eof() || !size) { error = "artifact is empty or unreadable: " + path.generic_string(); return {}; }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash << '-' << size;
    return out.str();
}

bool ReadArtifactReceipt(const std::filesystem::path& path, ArtifactIdentity& identity, std::string& error) {
    error.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot read artifact receipt: " + path.generic_string(); return false; }
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos || !eq || !fields.emplace(line.substr(0, eq), line.substr(eq + 1)).second) {
            error = "malformed or duplicate artifact receipt field"; return false;
        }
    }
    if (!in.eof()) { error = "artifact receipt read failed"; return false; }
    static const std::set<std::string> required = {"schema", "plugin_id", "module_name", "kind", "configuration",
        "product", "sdk_fingerprint", "toolchain_fingerprint", "fingerprint", "source_root", "binary_hash"};
    if (fields.size() != required.size()) { error = "incomplete artifact receipt"; return false; }
    for (const auto& key : required) if (!fields.contains(key) || !ValidField(fields.at(key))) {
        error = "missing artifact receipt field: " + key; return false;
    }
    if (fields.at("schema") != "1") { error = "unsupported artifact receipt schema"; return false; }
    ArtifactIdentity parsed;
    parsed.PluginId = fields.at("plugin_id"); parsed.ModuleName = fields.at("module_name");
    if (fields.at("kind") == "runtime") parsed.Kind = ModuleKind::Runtime;
    else if (fields.at("kind") == "editor") parsed.Kind = ModuleKind::Editor;
    else { error = "invalid artifact module kind"; return false; }
    if (fields.at("product") == "editor") parsed.Product = ProductKind::Editor;
    else if (fields.at("product") == "tests") parsed.Product = ProductKind::Tests;
    else if (fields.at("product") == "runtime") parsed.Product = ProductKind::Runtime;
    else { error = "invalid native artifact product"; return false; }
    parsed.Configuration = fields.at("configuration"); parsed.SdkFingerprint = fields.at("sdk_fingerprint");
    parsed.ToolchainFingerprint = fields.at("toolchain_fingerprint"); parsed.Fingerprint = fields.at("fingerprint");
    parsed.SourceRoot = std::filesystem::u8path(fields.at("source_root")); parsed.BinaryHash = fields.at("binary_hash");
    if (!Complete(parsed)) { error = "invalid artifact identity"; return false; }
    identity = std::move(parsed);
    return true;
}

bool WriteArtifactReceipt(const ArtifactIdentity& identity, const std::filesystem::path& binary,
                          const std::filesystem::path& receipt, std::string& error) {
    error.clear();
    if (!Complete(identity)) { error = "cannot publish an incomplete artifact identity"; return false; }
    const auto hash = HashArtifactFile(binary, error);
    if (hash.empty()) return false;
    std::ostringstream out;
    out << "schema=1\nplugin_id=" << identity.PluginId << "\nmodule_name=" << identity.ModuleName
        << "\nkind=" << ToString(identity.Kind) << "\nconfiguration=" << identity.Configuration
        << "\nproduct=" << ToString(identity.Product) << "\nsdk_fingerprint=" << identity.SdkFingerprint
        << "\ntoolchain_fingerprint=" << identity.ToolchainFingerprint << "\nfingerprint=" << identity.Fingerprint
        << "\nsource_root=" << identity.SourceRoot.generic_string() << "\nbinary_hash=" << hash << '\n';
    return FileSystem::WriteTextFileAtomically(receipt, out.str(), &error);
}

bool ValidateArtifact(const ArtifactIdentity& expected, const std::filesystem::path& binary,
                      const std::filesystem::path& receipt, std::string& error) {
    if (!Complete(expected)) { error = "artifact validation requires a complete expected identity"; return false; }
    ArtifactIdentity actual;
    if (!ReadArtifactReceipt(receipt, actual, error)) return false;
    if (actual.PluginId != expected.PluginId || actual.ModuleName != expected.ModuleName || actual.Kind != expected.Kind ||
        actual.Product != expected.Product || actual.Configuration != expected.Configuration ||
        actual.SdkFingerprint != expected.SdkFingerprint || actual.ToolchainFingerprint != expected.ToolchainFingerprint ||
        actual.Fingerprint != expected.Fingerprint || actual.SourceRoot.lexically_normal() != expected.SourceRoot.lexically_normal()) {
        error = "artifact receipt does not match the frozen build identity"; return false;
    }
    const auto hash = HashArtifactFile(binary, error);
    if (hash.empty()) return false;
    if (hash != actual.BinaryHash || (!expected.BinaryHash.empty() && expected.BinaryHash != hash)) {
        error = "artifact bytes do not match its receipt"; return false;
    }
    return true;
}

} // namespace Axle::Plugin
