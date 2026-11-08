#pragma once

#include <Axle/Types.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Axle::Plugin {

inline constexpr int kManifestSchemaVersion = 1;
inline constexpr int kProjectSelectionSchemaVersion = 1;

enum class ModuleKind {
    Runtime,
    Editor,
    Content,
};

enum class ProductKind {
    Editor,
    Runtime,
    Tests,
};

enum class Origin { Engine, Project };

struct Dependency {
    std::string Id;
    std::string Version;
};

struct ModuleDescriptor {
    std::string Name;
    ModuleKind Kind = ModuleKind::Runtime;
    std::filesystem::path SourceRoot;
    std::vector<std::string> Dependencies;
};

struct Manifest {
    int SchemaVersion = kManifestSchemaVersion;
    std::string Id;
    std::string Version;
    std::string Name;
    std::string Description;
    Origin Source = Origin::Engine;
    int EngineApiVersion = 1;
    std::vector<std::string> Platforms = {"windows-x64"};
    std::filesystem::path ManifestPath;
    std::filesystem::path Root;
    std::string AuthoredManifest;
    std::vector<Dependency> Dependencies;
    std::vector<ModuleDescriptor> Modules;
};

struct Diagnostic {
    std::filesystem::path Path;
    std::string Message;
};

struct Catalog {
    std::vector<Manifest> Plugins;
    std::vector<Diagnostic> Diagnostics;
    std::vector<std::string> AmbiguousIds;

    bool IsValid() const { return Diagnostics.empty(); }
};

struct DiscoveryOptions {
    std::vector<std::filesystem::path> Roots;
    std::vector<std::filesystem::path> ProjectRoots;
};

Catalog Discover(const DiscoveryOptions& options);
std::optional<Manifest> ParseManifest(const std::filesystem::path& path, Diagnostic& diagnostic);

struct Resolution {
    std::vector<Manifest> Plugins;
    std::vector<Diagnostic> Diagnostics;

    bool IsValid() const { return Diagnostics.empty(); }
};

Resolution Resolve(const Catalog& catalog, const std::vector<std::string>& selectedIds,
                   ProductKind product);

struct BuildInput {
    std::filesystem::path Path;
    std::string Content;
};

struct BuildArtifact {
    std::string PluginId;
    std::string ModuleName;
    ModuleKind Kind = ModuleKind::Runtime;
    std::filesystem::path SourceRoot;
    std::filesystem::path CMakeRoot;
    std::vector<std::string> Dependencies;
    std::string Fingerprint;
};

struct BuildPlan {
    ProductKind Product = ProductKind::Editor;
    std::string Configuration;
    std::string SdkFingerprint;
    std::string ToolchainFingerprint;
    std::string Fingerprint;
    std::vector<BuildArtifact> Artifacts;
    std::vector<BuildInput> Inputs;
    std::vector<Diagnostic> Diagnostics;
    Resolution FrozenResolution;
    std::vector<std::filesystem::path> CMakeRoots;

    bool IsValid() const { return Diagnostics.empty(); }
};

BuildPlan CreateBuildPlan(const Resolution& resolution, ProductKind product,
                          std::string configuration, std::string sdkFingerprint,
                          std::string toolchainFingerprint);

bool WriteBuildReceipt(const BuildPlan& plan, const BuildArtifact& artifact,
                       const std::filesystem::path& receiptPath, std::string& error,
                       const std::filesystem::path& binaryPath = {});

bool GenerateCMakeInputs(const BuildPlan& plan, const std::filesystem::path& outputPath,
                         std::string& error);

const char* ToString(ModuleKind kind);
const char* ToString(ProductKind kind);

} // namespace Axle::Plugin
