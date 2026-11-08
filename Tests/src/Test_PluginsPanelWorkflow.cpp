#include "PluginsPanel.hpp"
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>

#include <iostream>
#include <memory>

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << "FAIL: " << message << '\n'; return 1; } } while (false)

namespace {

Axle::Plugin::Manifest MakeManifest(std::string id, std::string name,
                                    std::vector<std::string> dependencies = {},
                                    std::filesystem::path manifestPath = {}) {
    Axle::Plugin::Manifest m;
    m.SchemaVersion = 1;
    m.Id = std::move(id);
    m.Name = std::move(name);
    m.Version = "1.0.0";
    m.ManifestPath = std::move(manifestPath);
    for (auto& dep : dependencies) {
        m.Dependencies.push_back({ std::move(dep), "1.0.0" });
    }
    return m;
}

} // namespace

int main() {
    Axle::PluginsPanel detachedPanel;
    CHECK(!detachedPanel.ApplyDraft().Applied, "Apply requires a saved project");

    Axle::Plugin::Catalog catalog;
    catalog.Plugins.push_back(MakeManifest("core.base", "Core Base", {}, "Engine/Plugins/CoreBase.axleplugin"));
    catalog.Plugins.push_back(MakeManifest("game.gameplay", "Gameplay", { "core.base" }, "Projects/MyGame/Plugins/Gameplay.axleplugin"));
    catalog.Plugins.push_back(MakeManifest("tools.editor", "Editor Tools", {}, "Engine/Plugins/EditorTools.axleplugin"));

    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" /
        ("PluginsPanel-" + std::to_string(static_cast<uint64_t>(Axle::UUID{})));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path Root;
        ~Cleanup() { Axle::Project::SetActive(nullptr); std::error_code ec; std::filesystem::remove_all(Root, ec); }
    } cleanup{root};
    auto project = Axle::Project::New(root);
    project->GetConfig().Name = "TestGameProject";
    const auto projectPath = root / "ActualFilename.axleproj";
    CHECK(Axle::ProjectSerializer(project).Serialize(projectPath), "Fixture project saved");
    const auto previousBytes = Axle::FileSystem::ReadTextFile(projectPath);
    auto unrelatedProject = Axle::Project::New(root);
    unrelatedProject->GetConfig().Name = "UnrelatedActiveProject";
    Axle::Project::SetActive(unrelatedProject);

    Axle::PluginsPanel panel;
    panel.SetActiveProject(project);
    panel.SetCatalog(catalog);
    CHECK(!panel.CanApply(), "Untitled project requires Save As");
    CHECK(!panel.ApplyDraft().Applied, "Unsaved project cannot Apply");
    panel.SetProjectPath(projectPath);

    // 1. Initial State: clean, empty desired selection
    CHECK(!panel.HasDraftChanges(), "Panel starts without draft changes");
    CHECK(!panel.NeedsRestart(), "Panel starts without restart prompt");
    CHECK(project->GetConfig().SelectedPlugins.empty(), "Explicit empty selection list initially");

    // 2. Draft Checkbox Toggle and Dependency Transaction
    bool toggled = panel.TogglePluginDraft("game.gameplay", true);
    CHECK(toggled, "Toggling valid plugin draft succeeds");
    CHECK(panel.HasDraftChanges(), "Draft changes pending after toggle");
    CHECK(panel.GetSelectionModel()->IsDraft("game.gameplay"), "game.gameplay is draft selected");
    CHECK(panel.GetSelectionModel()->DraftResolution().IsValid(), "Draft resolution resolves successfully");
    CHECK(panel.GetSelectionModel()->DependencyReason("core.base") == "Required by game.gameplay",
          "core.base dependency reason points to game.gameplay");

    // 3. Cancelled Dependent Removal (cannot disable core.base while game.gameplay depends on it)
    bool untoggled = panel.TogglePluginDraft("core.base", false);
    CHECK(!untoggled, "Cannot deselect core.base while required by game.gameplay");
    CHECK(panel.GetErrorMessage().find("required by") != std::string::npos &&
          panel.GetErrorMessage().find("game.gameplay") != std::string::npos,
          "Rejection reason is visible in error message");
    CHECK(panel.PendingDisable() == "core.base", "Dependent removal requires an explicit whole-draft decision");
    panel.CancelDependentDisable();
    CHECK(panel.GetSelectionModel()->IsDraft("game.gameplay"), "Cancelling dependent disable preserves the draft");
    panel.SetBusy(true, "Building selected plugins");
    CHECK(!panel.TogglePluginDraft("tools.editor", true) && !panel.ApplyDraft().Applied, "Build/export/restart freeze is enforced by the model APIs");
    panel.SetBusy(false);

    // 4. Play-Disabled Changes Guard (simulation active)
    bool simToggle = panel.TogglePluginDraft("tools.editor", true, true);
    CHECK(!simToggle, "Draft modification rejected while simulation is active");
    CHECK(panel.GetErrorMessage().find("simulation is active") != std::string::npos,
          "Simulation active reason reported");

    auto simApply = panel.ApplyDraft(true);
    CHECK(!simApply.Applied, "Apply rejected while simulation is active");
    CHECK(simApply.Error.find("simulation is active") != std::string::npos,
          "Simulation active error returned");

    // 5. Dirty Document Guard (Save scene callback failure aborts Apply)
    panel.SetSaveSceneCallback([]() { return false; });
    auto abortApply = panel.ApplyDraft(false);
    CHECK(!abortApply.Applied, "Apply aborts when saving scene fails");
    CHECK(abortApply.Error.find("saving current scene failed") != std::string::npos,
          "Save scene failure reason reported");
    CHECK(project->GetConfig().SelectedPlugins.empty(), "Project config not mutated on save failure");

    // 6. Successful Apply commits Desired state and sets Restart prompt
    panel.SetSaveSceneCallback([]() { return true; });
    const auto blockedPath = root / "Blocked.axleproj";
    std::filesystem::create_directory(blockedPath);
    panel.SetProjectPath(blockedPath);
    auto failedSave = panel.ApplyDraft();
    CHECK(!failedSave.Applied, "Project save failure rejects Apply");
    CHECK(panel.HasDraftChanges() && panel.GetSelectionModel()->Desired().empty(), "Save failure preserves draft and Desired");
    CHECK(project->GetConfig().SelectedPlugins.empty() && !panel.NeedsRestart(), "Save failure rolls back config without restart");
    CHECK(Axle::FileSystem::ReadTextFile(projectPath) == previousBytes, "Save failure preserves previous project bytes");
    panel.SetProjectPath(projectPath);
    auto successApply = panel.ApplyDraft(false);
    CHECK(successApply.Applied, "Apply succeeds when scene save succeeds");
    CHECK(!panel.HasDraftChanges(), "Draft changes cleared after Apply");
    CHECK(panel.NeedsRestart(), "Restart prompt active after Apply");
    CHECK(project->GetConfig().SelectedPlugins.size() == 1 &&
          project->GetConfig().SelectedPlugins[0] == "game.gameplay",
          "Desired plugin committed to project config");
    auto persisted = std::make_shared<Axle::Project>();
    CHECK(Axle::ProjectSerializer(persisted).Deserialize(projectPath), "Applied project can reopen");
    CHECK(persisted->GetConfig().SelectedPlugins == project->GetConfig().SelectedPlugins, "Apply persists Desired to actual filename");
    CHECK(unrelatedProject->GetConfig().SelectedPlugins.empty(), "Apply leaves global unrelated project alone");
    CHECK(!std::filesystem::exists(root / "TestGameProject.axleproj") &&
          !std::filesystem::exists(root / "UnrelatedActiveProject.axleproj"), "Apply never infers a project filename from display name");

    bool restartCalled = false;
    panel.SetRestartCallback([&]() { restartCalled = true; });
    panel.RequestRestart();
    CHECK(restartCalled, "Restart callback invoked upon request");

    // 7. Revert restores Desired state
    panel.TogglePluginDraft("tools.editor", true);
    CHECK(panel.HasDraftChanges(), "Draft changed after toggling tools.editor");
    panel.RevertDraft();
    CHECK(!panel.HasDraftChanges(), "Revert clears draft changes");
    CHECK(!panel.GetSelectionModel()->IsDraft("tools.editor"), "tools.editor no longer draft selected");
    CHECK(panel.GetSelectionModel()->IsDraft("game.gameplay"), "game.gameplay remains selected as desired");

    // 8. Filters and Search verification
    panel.SetSearchFilter("Tools");
    CHECK(panel.GetSearchFilter() == "Tools", "Search filter stored");

    panel.SetSourceFilter(Axle::PluginsPanel::SourceFilter::Engine);
    CHECK(panel.GetSourceFilter() == Axle::PluginsPanel::SourceFilter::Engine, "Source filter stored");

    panel.SetStatusFilter(Axle::PluginsPanel::StatusFilter::Enabled);
    CHECK(panel.GetStatusFilter() == Axle::PluginsPanel::StatusFilter::Enabled, "Status filter stored");

    // 9. Safe Mode flag
    panel.SetSafeMode(true);
    CHECK(panel.IsSafeMode(), "Safe mode flag set");
    panel.SetActiveProject(unrelatedProject);
    CHECK(!panel.NeedsRestart() && panel.GetErrorMessage().empty(), "Project switch clears old pending notifications");

    std::cout << "Plugins panel workflow (transactions, guards, dirty doc, filters, safe mode): PASS\n";
    return 0;
}
