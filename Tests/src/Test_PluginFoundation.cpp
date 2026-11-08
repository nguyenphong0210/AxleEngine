#include <Axle/Plugin.hpp>
#include <Axle/PluginArtifact.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iostream>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" / "PluginFoundationTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "a", error);
    std::filesystem::create_directories(root / "b", error);

    Write(root / "a" / "base.axleplugin", R"({
        "schema": 1, "id": "test.base", "version": "1.0.0", "name": "B\u0061se",
        "modules": [{"name": "base-runtime", "kind": "runtime", "sourceRoot": "src"}]
    })");
    Write(root / "b" / "game.axleplugin", R"({
        "schema": 1, "id": "test.game", "version": "2.0.0", "name": "Game",
        "dependencies": [{"id": "test.base", "version": "1.0.0"}],
        "modules": [{"name": "game-runtime", "kind": "runtime", "sourceRoot": "src", "dependencies": ["base-runtime"]},
                    {"name": "game-editor", "kind": "editor"}]
    })");
    Write(root / "bad.axleplugin", R"({"schema": 1, "id": "test.bad", "version": "1", "name": "Bad", "modules": [})");
    std::filesystem::create_directories(root / "a" / "src", error);
    std::filesystem::create_directories(root / "b" / "src", error);
    Write(root / "a" / "CMakeLists.txt", "# selected module metadata fixture\n");
    Write(root / "b" / "CMakeLists.txt", "# selected module metadata fixture\n");
    Write(root / "a" / "src" / "base.cpp", "base-v1");
    Write(root / "b" / "src" / "game.cpp", "game-v1");

    const auto catalog = Axle::Plugin::Discover({{root}});
    bool ok = true;
    ok &= Check(catalog.Plugins.size() == 2, "valid manifests are discovered");
    ok &= Check(catalog.Diagnostics.size() == 1, "malformed manifest is retained as a diagnostic");
    ok &= Check(catalog.Plugins.front().Name == "Base", "unicode escapes decode to UTF-8 metadata");
    const auto resolution = Axle::Plugin::Resolve(catalog, {"test.game"}, Axle::Plugin::ProductKind::Runtime);
    ok &= Check(resolution.IsValid() && resolution.Plugins.size() == 2, "valid selections resolve despite unrelated malformed manifests");

    std::filesystem::remove(root / "bad.axleplugin", error);
    const auto cleanCatalog = Axle::Plugin::Discover({{root}});
    auto projectRoot = root / "b";
#ifdef _WIN32
    auto mixedCase = projectRoot.wstring();
    std::transform(mixedCase.begin(), mixedCase.end(), mixedCase.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    projectRoot = mixedCase;
#endif
    const auto originCatalog = Axle::Plugin::Discover({{root, root / "b"}, {projectRoot}});
    const auto reorderedOriginCatalog = Axle::Plugin::Discover({{projectRoot, root}, {projectRoot}});
    ok &= Check(originCatalog.Plugins.size() == reorderedOriginCatalog.Plugins.size(), "origin discovery is input-order independent");
    if (originCatalog.Plugins.size() == reorderedOriginCatalog.Plugins.size()) {
        for (size_t i = 0; i < originCatalog.Plugins.size(); ++i)
            ok &= Check(originCatalog.Plugins[i].ManifestPath == reorderedOriginCatalog.Plugins[i].ManifestPath,
                        "equivalent path spelling is stable across root order");
    }
    ok &= Check(originCatalog.IsValid() && originCatalog.Plugins.size() == 2, "overlapping physical roots do not duplicate manifests");
    const auto projectManifest = std::find_if(originCatalog.Plugins.begin(), originCatalog.Plugins.end(), [](const auto& p) { return p.Id == "test.game"; });
    ok &= Check(projectManifest != originCatalog.Plugins.end() && projectManifest->Source == Axle::Plugin::Origin::Project,
                "project origin uses filesystem identity including mixed-case roots");
    const auto validResolution = Axle::Plugin::Resolve(cleanCatalog, {"test.game"}, Axle::Plugin::ProductKind::Runtime);
    ok &= Check(validResolution.IsValid() && validResolution.Plugins.size() == 2, "dependency closure resolves deterministically");
    ok &= Check(validResolution.Plugins.front().Id == "test.base", "dependencies precede dependents");
    const auto plan = Axle::Plugin::CreateBuildPlan(validResolution, Axle::Plugin::ProductKind::Runtime,
                                                    "Debug", "sdk-a", "toolchain-a");
    ok &= Check(plan.IsValid() && plan.Artifacts.size() == 2, "runtime build plan excludes editor modules");
    ok &= Check(!plan.Fingerprint.empty(), "build plan has a stable identity");
    const auto frozenManifest = validResolution.Plugins.back().AuthoredManifest;
    Write(root / "b/game.axleplugin", "{}\n");
    ok &= Check(!Axle::Plugin::CreateBuildPlan(validResolution, Axle::Plugin::ProductKind::Runtime, "Debug", "sdk-a", "toolchain-a").IsValid(),
        "changed manifest cannot be paired with an old resolved graph");
    std::filesystem::remove(root / "b/game.axleplugin");
    ok &= Check(!Axle::Plugin::CreateBuildPlan(validResolution, Axle::Plugin::ProductKind::Runtime, "Debug", "sdk-a", "toolchain-a").IsValid(),
        "deleted manifest invalidates its resolved graph");
    Write(root / "b/game.axleplugin", frozenManifest);
    Write(root / "b" / "src" / "game.cpp", "game-v2");
    const auto changedCatalog = Axle::Plugin::Discover({{root}});
    const auto changedResolution = Axle::Plugin::Resolve(changedCatalog, {"test.game"}, Axle::Plugin::ProductKind::Runtime);
    const auto changedPlan = Axle::Plugin::CreateBuildPlan(changedResolution, Axle::Plugin::ProductKind::Runtime,
                                                           "Debug", "sdk-a", "toolchain-a");
    ok &= Check(changedPlan.Fingerprint != plan.Fingerprint, "module source changes invalidate the build fingerprint");

    std::string receiptError;
    const auto receiptPath = root / "receipts" / "game.receipt";
    const auto receiptBinary = root / "receipt-fixture.bin";
    Write(receiptBinary, "binary identity fixture");
    ok &= Check(Axle::Plugin::WriteBuildReceipt(plan, plan.Artifacts.front(), receiptPath, receiptError, receiptBinary), "build receipt writes successfully");
    ok &= Check(std::filesystem::is_regular_file(receiptPath), "published build receipt exists on disk");
    std::ifstream receiptStream(receiptPath);
    std::string receiptContent((std::istreambuf_iterator<char>(receiptStream)), std::istreambuf_iterator<char>());
    ok &= Check(receiptContent.find("schema=1") != std::string::npos, "receipt contains schema version");
    ok &= Check(receiptContent.find("configuration=Debug") != std::string::npos, "receipt contains configuration");
    ok &= Check(receiptContent.find(plan.Fingerprint) != std::string::npos, "receipt contains plan fingerprint");
    Axle::Plugin::ArtifactIdentity identity;
    ok &= Check(Axle::Plugin::ReadArtifactReceipt(receiptPath, identity, receiptError), "complete artifact receipt parses");
    ok &= Check(Axle::Plugin::ValidateArtifact(identity, receiptBinary, receiptPath, receiptError), "receipt validates its actual bytes");
    Write(receiptBinary, "changed bytes");
    ok &= Check(!Axle::Plugin::ValidateArtifact(identity, receiptBinary, receiptPath, receiptError), "changed artifact bytes are rejected");
    Write(root / "partial.receipt", "schema=1\n");
    ok &= Check(!Axle::Plugin::ReadArtifactReceipt(root / "partial.receipt", identity, receiptError), "partial artifact receipts are rejected");

    std::string cmakeError;
    const auto cmakePath = root / "cmake" / "AxleSelectedPlugins.cmake";
    ok &= Check(Axle::Plugin::GenerateCMakeInputs(plan, cmakePath, cmakeError), "cmake inputs generate successfully");
    ok &= Check(std::filesystem::is_regular_file(cmakePath), "published cmake inputs exist on disk");
    std::ifstream cmakeStream(cmakePath);
    std::string cmakeContent((std::istreambuf_iterator<char>(cmakeStream)), std::istreambuf_iterator<char>());
    ok &= Check(cmakeContent.find("set(AXLE_SELECTED_PLUGIN_COUNT 2)") != std::string::npos, "cmake inputs contain selected count");
    ok &= Check(cmakeContent.find(plan.Fingerprint) != std::string::npos, "cmake inputs contain plan fingerprint");
    ok &= Check(cmakeContent.find("AXLE_PLUGIN_game-runtime_SOURCE_ROOT") != std::string::npos, "cmake inputs declare selected module");
    ok &= Check(cmakeContent.find("game-editor") == std::string::npos, "runtime cmake inputs exclude editor module");

    auto project = std::make_shared<Axle::Project>();
    project->GetConfig().Name = "PluginProject";
    project->GetConfig().SelectedPlugins = {"test.base", "test.game"};
    const auto projectPath = root / "Project.axleproj";
    Axle::ProjectSerializer serializer(project);
    ok &= Check(serializer.Serialize(projectPath), "project with plugin selections serializes");
    auto loaded = std::make_shared<Axle::Project>();
    Axle::ProjectSerializer loader(loaded);
    ok &= Check(loader.Deserialize(projectPath), "project with plugin selections deserializes");
    ok &= Check(loaded->GetConfig().SelectedPlugins == project->GetConfig().SelectedPlugins, "plugin selections round trip");

    Write(root / "Unsupported.axleproj", "Project:\n  SchemaVersion: 99\n");
    ok &= Check(!loader.Deserialize(root / "Unsupported.axleproj"), "unsupported project schema is rejected");
    ok &= Check(loaded->GetConfig().Name == "PluginProject" && loaded->GetConfig().SelectedPlugins == project->GetConfig().SelectedPlugins,
                "failed project load leaves the current configuration unchanged");
    Write(root / "MissingSchema.axleproj", "Project:\n  Name: MissingSchema\n");
    ok &= Check(!loader.Deserialize(root / "MissingSchema.axleproj"), "missing project schema is rejected");
    ok &= Check(loaded->GetConfig().Name == "PluginProject", "missing schema failure preserves current configuration");
    std::filesystem::remove_all(root, error);
    if (ok) std::cout << "Plugin metadata, resolution, build plan and project selection: PASS\n";
    return ok ? 0 : 1;
}
