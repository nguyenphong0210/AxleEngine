#pragma once

#include <Axle/Plugin.hpp>
#include <Axle/PluginSelection.hpp>
#include <Axle/Project.hpp>

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

    void SetActiveProject(std::shared_ptr<Project> project);
    void SetCatalog(Plugin::Catalog catalog);
    void RefreshCatalog();

    void OnImGuiRender(bool* pOpen = nullptr, bool isSimulationActive = false);

    // View model state & inspection
    Plugin::SelectionModel* GetSelectionModel() { return m_SelectionModel.get(); }
    const Plugin::SelectionModel* GetSelectionModel() const { return m_SelectionModel.get(); }
    const Plugin::Catalog& GetCatalog() const { return m_Catalog; }

    bool HasDraftChanges() const;
    bool CanApply() const;
    Plugin::ApplyReport ApplyDraft(bool isSimulationActive = false);
    void RevertDraft();

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
    void SetRestartCallback(std::function<void()> callback) { m_OnRestart = std::move(callback); }

    bool NeedsRestart() const { return m_NeedsRestart; }
    void ClearRestart() { m_NeedsRestart = false; }
    void RequestRestart();

    const std::string& GetErrorMessage() const { return m_ErrorMessage; }
    void ClearErrorMessage() { m_ErrorMessage.clear(); }

    bool IsSafeMode() const { return m_SafeMode; }
    void SetSafeMode(bool safeMode) { m_SafeMode = safeMode; }

private:
    std::shared_ptr<Project> m_Project;
    Plugin::Catalog m_Catalog;
    std::unique_ptr<Plugin::SelectionModel> m_SelectionModel;

    std::string m_SearchFilter;
    SourceFilter m_SourceFilter = SourceFilter::All;
    StatusFilter m_StatusFilter = StatusFilter::All;

    std::string m_ErrorMessage;
    std::string m_StatusMessage;
    bool m_NeedsRestart = false;
    bool m_SafeMode = false;

    std::function<bool()> m_OnSaveScene;
    std::function<void()> m_OnRestart;
};

} // namespace Axle
