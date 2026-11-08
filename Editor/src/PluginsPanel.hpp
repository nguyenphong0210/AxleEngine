#pragma once

#include <Axle/Plugin.hpp>
#include <Axle/PluginSelection.hpp>
#include <Axle/Project.hpp>
#include "ProjectPluginCreator.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Axle {

class PluginsPanel {
public:
    enum class SourceFilter { All, Engine, Project };
    enum class StatusFilter { All, Enabled, Disabled };

    PluginsPanel();
    ~PluginsPanel() = default;

    void SetActiveProject(std::shared_ptr<Project> project, std::filesystem::path projectPath = {});
    void SetProjectPath(std::filesystem::path projectPath) { m_ProjectPath = std::move(projectPath); }
    void SetCatalog(Plugin::Catalog catalog, bool preserveDraft = false);
    void SetEnginePluginRoot(std::filesystem::path root) { m_EnginePluginRoot = std::move(root); }
    void RefreshCatalog();
    void SetPluginTemplateDirectory(std::filesystem::path path) { m_PluginTemplate = std::move(path); }
    bool CreatePlugin(std::string name, std::string id, std::string module, bool isSimulationActive = false);
    const std::string& GetCreateError() const { return m_CreateError; }

    void OnImGuiRender(bool* pOpen = nullptr, bool isSimulationActive = false);

    // View model state & inspection
    Plugin::SelectionModel* GetSelectionModel() { return m_SelectionModel.get(); }
    const Plugin::SelectionModel* GetSelectionModel() const { return m_SelectionModel.get(); }
    const Plugin::Catalog& GetCatalog() const { return m_Catalog; }

    bool HasDraftChanges() const;
    bool CanApply() const;
    Plugin::ApplyReport ApplyDraft(bool isSimulationActive = false);
    void RevertDraft();
    bool ConfirmDependentDisable(bool isSimulationActive = false);
    void CancelDependentDisable() { m_PendingDisable.clear(); m_ErrorMessage.clear(); }
    const std::string& PendingDisable() const { return m_PendingDisable; }
    void SetBusy(bool busy, std::string status = {}) { m_Busy = busy; m_BuildStatus = std::move(status); }
    void SetActiveIds(std::vector<std::string> ids) { m_ActiveIds = std::move(ids); }

    // Search and filter controls
    const std::string& GetSearchFilter() const { return m_SearchFilter; }
    void SetSearchFilter(std::string filter) { m_SearchFilter = std::move(filter); }
    SourceFilter GetSourceFilter() const { return m_SourceFilter; }
    void SetSourceFilter(SourceFilter filter) { m_SourceFilter = filter; }
    StatusFilter GetStatusFilter() const { return m_StatusFilter; }
    void SetStatusFilter(StatusFilter filter) { m_StatusFilter = filter; }

    // Toggle plugin draft state (returns false if blocked by dependencies or simulation)
    bool TogglePluginDraft(std::string_view id, bool enabled, bool isSimulationActive = false);

    // Callbacks & notification state
    void SetSaveSceneCallback(std::function<bool()> callback) { m_OnSaveScene = std::move(callback); }
    void SetDocumentGuard(std::function<bool(std::string&)> callback) { m_DocumentGuard = std::move(callback); }
    void SetRestartCallback(std::function<void()> callback) { m_OnRestart = std::move(callback); }
    void SetBuildCallback(std::function<void()> callback) { m_OnBuild = std::move(callback); }
    void SetCancelBuildCallback(std::function<void()> callback) { m_OnCancelBuild = std::move(callback); }
    void SetRefreshCallback(std::function<void()> callback) { m_OnRefresh = std::move(callback); }
    void SetAppliedCallback(std::function<void()> callback) { m_OnApplied = std::move(callback); }
    void SetBuildLog(std::filesystem::path path, std::function<void()> open) { m_BuildLog = std::move(path); m_OnOpenLog = std::move(open); }
    void SetExternalError(std::string error) { m_ErrorMessage = std::move(error); }

    bool NeedsRestart() const { return m_NeedsRestart; }
    void ClearRestart() { m_NeedsRestart = false; }
    void SetNeedsRestart(bool needed) { m_NeedsRestart = needed; }
    void RequestRestart();

    const std::string& GetErrorMessage() const { return m_ErrorMessage; }
    void ClearErrorMessage() { m_ErrorMessage.clear(); }

    bool IsSafeMode() const { return m_SafeMode; }
    void SetSafeMode(bool safeMode) { m_SafeMode = safeMode; }

private:
    void DrawCreateDialog(bool isSimulationActive);
    void RefreshPreservingDraft();
    std::filesystem::path m_PluginTemplate = ProjectPluginTemplateDirectory();
    bool m_CreateDialogOpen = false, m_CreateModuleEdited = false, m_CreateIdEdited = false;
    char m_CreateName[129] = "My Plugin", m_CreateModule[65] = "MyPlugin", m_CreateId[121] = "project.myplugin";
    std::string m_CreateError;
    std::shared_ptr<Project> m_Project;
    std::filesystem::path m_ProjectPath;
    std::filesystem::path m_EnginePluginRoot = std::filesystem::current_path() / "Plugins";
    Plugin::Catalog m_Catalog;
    std::unique_ptr<Plugin::SelectionModel> m_SelectionModel;

    std::string m_SearchFilter;
    SourceFilter m_SourceFilter = SourceFilter::All;
    StatusFilter m_StatusFilter = StatusFilter::All;

    std::string m_ErrorMessage;
    std::string m_StatusMessage;
    bool m_NeedsRestart = false;
    bool m_SafeMode = false;
    bool m_Busy = false;
    std::string m_BuildStatus, m_PendingDisable;
    std::vector<std::string> m_ActiveIds;
    std::filesystem::path m_BuildLog;

    std::function<bool()> m_OnSaveScene;
    std::function<bool(std::string&)> m_DocumentGuard;
    std::function<void()> m_OnRestart;
    std::function<void()> m_OnBuild, m_OnCancelBuild, m_OnRefresh, m_OnApplied, m_OnOpenLog;
};

} // namespace Axle
