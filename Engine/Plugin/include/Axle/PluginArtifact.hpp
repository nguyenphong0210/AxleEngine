#pragma once
#include <Axle/Plugin.hpp>
#include <map>

namespace Axle::Plugin {

// The sidecar and the binary form one immutable artifact. Every consumer uses
// this parser; presence of a DLL or a subset of receipt fields is insufficient.
struct ArtifactIdentity {
    std::string PluginId;
    std::string ModuleName;
    ModuleKind Kind = ModuleKind::Runtime;
    ProductKind Product = ProductKind::Editor;
    std::string Configuration;
    std::string SdkFingerprint;
    std::string ToolchainFingerprint;
    std::string Fingerprint;
    std::string BinaryHash;
    std::filesystem::path SourceRoot;
};

std::string HashArtifactFile(const std::filesystem::path& path, std::string& error);
bool ReadArtifactReceipt(const std::filesystem::path& path, ArtifactIdentity& identity, std::string& error);
bool WriteArtifactReceipt(const ArtifactIdentity& identity, const std::filesystem::path& binary,
                          const std::filesystem::path& receipt, std::string& error);
bool ValidateArtifact(const ArtifactIdentity& expected, const std::filesystem::path& binary,
                      const std::filesystem::path& receipt, std::string& error);

} // namespace Axle::Plugin
