#include <Axle/Plugin.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>

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
        "schema": 1, "id": "test.base", "version": "1.0.0", "name": "Base",
        "modules": [{"name": "base-runtime", "kind": "runtime"}]
    })");
    Write(root / "b" / "game.axleplugin", R"({
        "schema": 1, "id": "test.game", "version": "2.0.0", "name": "Game",
        "dependencies": [{"id": "test.base", "version": "1.0.0"}],
        "modules": [{"name": "game-runtime", "kind": "runtime", "dependencies": ["base-runtime"]},
                    {"name": "game-editor", "kind": "editor"}]
    })");
    Write(root / "bad.axleplugin", R"({"schema": 1, "id": "test.bad", "version": "1", "name": "Bad", "modules": [})");

    const auto catalog = Axle::Plugin::Discover({{root}});
    bool ok = true;
    ok &= Check(catalog.Plugins.size() == 2, "valid manifests are discovered");
    ok &= Check(catalog.Diagnostics.size() == 1, "malformed manifest is retained as a diagnostic");
    const auto resolution = Axle::Plugin::Resolve(catalog, {"test.game"}, Axle::Plugin::ProductKind::Runtime);
    ok &= Check(resolution.IsValid() && resolution.Plugins.size() == 2, "valid selections resolve despite unrelated malformed manifests");

    std::filesystem::remove(root / "bad.axleplugin", error);
    const auto cleanCatalog = Axle::Plugin::Discover({{root}});
    const auto validResolution = Axle::Plugin::Resolve(cleanCatalog, {"test.game"}, Axle::Plugin::ProductKind::Runtime);
    ok &= Check(validResolution.IsValid() && validResolution.Plugins.size() == 2, "dependency closure resolves deterministically");
    ok &= Check(validResolution.Plugins.front().Id == "test.base", "dependencies precede dependents");
    const auto plan = Axle::Plugin::CreateBuildPlan(validResolution, Axle::Plugin::ProductKind::Runtime,
                                                    "Debug", "sdk-a", "toolchain-a");
    ok &= Check(plan.IsValid() && plan.Artifacts.size() == 2, "runtime build plan excludes editor modules");
    ok &= Check(!plan.Fingerprint.empty(), "build plan has a stable identity");

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
