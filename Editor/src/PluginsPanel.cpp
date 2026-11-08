#include "PluginsPanel.hpp"

#include <Axle/Log.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace Axle {

namespace {

bool CaseInsensitiveContains(std::string_view text, std::string_view query) {
    if (query.empty()) return true;
    auto it = std::search(
        text.begin(), text.end(),
        query.begin(), query.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) ==
                   std::tolower(static_cast<unsigned char>(ch2));
        }
    );
    return it != text.end();
}

} // namespace

PluginsPanel::PluginsPanel() {
    m_SelectionModel = std::make_unique<Plugin::SelectionModel>(m_Catalog, Plugin::ProductKind::Editor, std::vector<std::string>{});
}

void PluginsPanel::SetActiveProject(std::shared_ptr<Project> project, std::filesystem::path projectPath) {
    m_Project = std::move(project);
    m_ProjectPath = std::move(projectPath);
    m_NeedsRestart = false;
    m_ErrorMessage.clear();
    m_StatusMessage.clear();
    m_PendingDisable.clear(); m_ActiveIds.clear();
    RefreshCatalog();
}

void PluginsPanel::SetCatalog(Plugin::Catalog catalog, bool preserveDraft) {
    m_Catalog = std::move(catalog);
    if (preserveDraft && m_SelectionModel) { m_SelectionModel->RefreshCatalog(m_Catalog); return; }
    std::vector<std::string> desired;
    if (m_Project) {
        desired = m_Project->GetConfig().SelectedPlugins;
    }
    m_SelectionModel = std::make_unique<Plugin::SelectionModel>(m_Catalog, Plugin::ProductKind::Editor, desired);
}

void PluginsPanel::RefreshCatalog() {
    Plugin::DiscoveryOptions options;
    if (m_Project && !m_Project->GetProjectDirectoryPath().empty()) {
        options.Roots.push_back(m_Project->GetProjectDirectoryPath() / "Plugins");
        options.ProjectRoots.push_back(m_Project->GetProjectDirectoryPath() / "Plugins");
    }
    if (!m_EnginePluginRoot.empty()) options.Roots.push_back(m_EnginePluginRoot);

    m_Catalog = Plugin::Discover(options);
    std::vector<std::string> desired;
    if (m_Project) {
        desired = m_Project->GetConfig().SelectedPlugins;
    }
    m_SelectionModel = std::make_unique<Plugin::SelectionModel>(m_Catalog, Plugin::ProductKind::Editor, desired);
}

bool PluginsPanel::HasDraftChanges() const {
    return m_SelectionModel && m_SelectionModel->HasDraftChanges();
}

bool PluginsPanel::CanApply() const {
    return !m_Busy && m_Project && !m_ProjectPath.empty() && m_SelectionModel &&
           m_SelectionModel->DraftResolution().IsValid();
}

bool PluginsPanel::TogglePluginDraft(std::string_view id, bool enabled, bool isSimulationActive) {
    if (m_Busy) { m_ErrorMessage = "Plugin changes are frozen while building, exporting or restarting."; return false; }
    if (isSimulationActive) {
        m_ErrorMessage = "Cannot modify plugin selection while simulation is active. Stop Play mode first.";
        return false;
    }
    if (!m_SelectionModel) {
        m_ErrorMessage = "No project selection model active.";
        return false;
    }
    bool ok = m_SelectionModel->SetDraftSelected(id, enabled);
    if (!ok) {
        m_ErrorMessage = m_SelectionModel->LastError();
        if (!enabled && !m_SelectionModel->DependentSelections(id).empty()) m_PendingDisable = id;
    } else {
        m_ErrorMessage.clear();
    }
    return ok;
}

Plugin::ApplyReport PluginsPanel::ApplyDraft(bool isSimulationActive) {
    Plugin::ApplyReport report;
    if (m_Busy) { report.Error = m_ErrorMessage = "Plugin changes are frozen while building, exporting or restarting."; return report; }
    if (isSimulationActive) {
        report.Error = "Cannot apply plugin selection while simulation is active. Stop Play mode first.";
        m_ErrorMessage = report.Error;
        return report;
    }
    if (!m_Project || m_ProjectPath.empty()) {
        report.Error = "Save Project As before applying plugin changes.";
        m_ErrorMessage = report.Error;
        return report;
    }
    if (!m_SelectionModel) {
        report.Error = "No active plugin selection model.";
        m_ErrorMessage = report.Error;
        return report;
    }
    if (!m_SelectionModel->DraftResolution().IsValid()) {
        report.Error = "Plugin selection cannot be applied until dependencies are resolved.";
        m_ErrorMessage = report.Error;
        return report;
    }

    // Document guard: Save scene first if callback provided
    if (m_DocumentGuard) {
        std::string error;
        if (!m_DocumentGuard(error)) { report.Error = m_ErrorMessage = std::move(error); return report; }
    } else if (m_OnSaveScene) {
        if (!m_OnSaveScene()) {
            report.Error = "Applying plugin changes cancelled: saving current scene failed.";
            m_ErrorMessage = report.Error;
            return report;
        }
    }

    // Persist the draft to the actual project file before advancing Desired.
    // The panel owns its project reference; saving a different global active
    // project or deriving the filename from a mutable display name is unsafe.
    auto previousSelection = m_Project->GetConfig().SelectedPlugins;
    m_Project->GetConfig().SelectedPlugins = m_SelectionModel->Draft();
    bool saved = false;
    try {
        saved = ProjectSerializer(m_Project).Serialize(m_ProjectPath);
    } catch (...) {
        saved = false;
    }
    if (!saved) {
        m_Project->GetConfig().SelectedPlugins = std::move(previousSelection);
        report.Error = "Could not save plugin selection. Draft changes are still pending.";
        m_ErrorMessage = report.Error;
        return report;
    }
    report = m_SelectionModel->Apply();

    m_NeedsRestart = true;
    m_StatusMessage = "Plugin changes applied successfully. Editor restart required.";
    m_ErrorMessage.clear();
    if (report.Applied && m_OnApplied) {
        try { m_OnApplied(); }
        catch (const std::exception& error) { m_ErrorMessage = std::string("Selection was saved, but refresh failed: ") + error.what(); }
    }
    return report;
}

void PluginsPanel::RevertDraft() {
    if (m_Busy) { m_ErrorMessage = "Plugin changes are frozen while building, exporting or restarting."; return; }
    if (m_SelectionModel) {
        m_SelectionModel->Revert();
    }
    m_ErrorMessage.clear();
}

void PluginsPanel::RequestRestart() {
    if (m_Busy) return;
    if (m_OnRestart) {
        m_OnRestart();
    }
}
bool PluginsPanel::ConfirmDependentDisable(bool isSimulationActive) {
    if (m_Busy || isSimulationActive || m_PendingDisable.empty() || !m_SelectionModel) return false;
    m_SelectionModel->RemoveDraftWithDependents(m_PendingDisable);
    m_PendingDisable.clear(); m_ErrorMessage.clear(); return true;
}

void PluginsPanel::OnImGuiRender(bool* pOpen, bool isSimulationActive) {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", pOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (!m_PendingDisable.empty()) ImGui::OpenPopup("Disable required plugins?");
    if (ImGui::BeginPopupModal("Disable required plugins?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Disabling %s also removes these explicit selections:", m_PendingDisable.c_str());
        if (m_SelectionModel) for (const auto& id : m_SelectionModel->DependentSelections(m_PendingDisable)) ImGui::BulletText("%s", id.c_str());
        ImGui::BeginDisabled(m_Busy || isSimulationActive);
        if (ImGui::Button("Disable all")) { ConfirmDependentDisable(isSimulationActive); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Cancel")) { CancelDependentDisable(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    const float windowWidth = ImGui::GetContentRegionAvail().x;
    const bool isNarrow = (windowWidth < 420.0f);

    // 1. Safe Mode Notification
    if (m_SafeMode) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("SAFE MODE: External plugins were not loaded automatically. Review selections or revert changes.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // 2. Simulation Active Warning
    if (isSimulationActive) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
        ImGui::TextWrapped("NOTICE: Simulation is active. Plugin selections are read-only. Stop Play mode (F7) to make changes.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // 3. Error Banner
    if (!m_ErrorMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("Error: %s", m_ErrorMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) {
            m_ErrorMessage.clear();
        }
        ImGui::Separator();
    }

    // 4. Search and Filters Toolbar
    char searchBuf[128];
    strncpy_s(searchBuf, sizeof(searchBuf), m_SearchFilter.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(isNarrow ? -1.0f : 200.0f);
    if (ImGui::InputTextWithHint("##PluginSearch", "Search plugins...", searchBuf, sizeof(searchBuf))) {
        m_SearchFilter = searchBuf;
    }

    if (!isNarrow) ImGui::SameLine();

    // Source Filter
    const char* sourceFilterItems[] = { "All Sources", "Engine", "Project" };
    int currentSource = static_cast<int>(m_SourceFilter);
    ImGui::SetNextItemWidth(isNarrow ? -1.0f : 120.0f);
    if (ImGui::Combo("##SourceFilter", &currentSource, sourceFilterItems, IM_ARRAYSIZE(sourceFilterItems))) {
        m_SourceFilter = static_cast<SourceFilter>(currentSource);
    }

    if (!isNarrow) ImGui::SameLine();

    // Status Filter
    const char* statusFilterItems[] = { "All Status", "Enabled", "Disabled" };
    int currentStatus = static_cast<int>(m_StatusFilter);
    ImGui::SetNextItemWidth(isNarrow ? -1.0f : 110.0f);
    if (ImGui::Combo("##StatusFilter", &currentStatus, statusFilterItems, IM_ARRAYSIZE(statusFilterItems))) {
        m_StatusFilter = static_cast<StatusFilter>(currentStatus);
    }

    if (!isNarrow) ImGui::SameLine();
    ImGui::BeginDisabled(m_Busy);
    if (ImGui::Button("Refresh")) {
        if (m_OnRefresh) m_OnRefresh(); else {
            Plugin::DiscoveryOptions options{{m_EnginePluginRoot}, {}};
            if (m_Project && !m_Project->GetProjectDirectoryPath().empty()) {
                const auto root = m_Project->GetProjectDirectoryPath() / "Plugins";
                options.Roots.push_back(root); options.ProjectRoots.push_back(root);
            }
            SetCatalog(Plugin::Discover(options), true);
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    // 5. Diagnostics if Draft Resolution is invalid
    if (m_SelectionModel && !m_SelectionModel->DraftResolution().IsValid()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        for (const auto& diag : m_SelectionModel->DraftResolution().Diagnostics) {
            ImGui::BulletText("%s", diag.Message.c_str());
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // 6. Plugin Entries List
    const float footerHeight = m_OnBuild ? (m_NeedsRestart ? (isNarrow ? 260.0f : 190.0f) : (isNarrow ? 200.0f : 150.0f)) : (m_NeedsRestart ? 140.0f : 95.0f);
    ImGui::BeginChild("PluginListRegion", ImVec2(0.0f, -footerHeight), true);

    if (m_Catalog.Plugins.empty()) {
        ImGui::TextDisabled("No plugins discovered.");
    } else {
        for (const auto& plugin : m_Catalog.Plugins) {
            // Apply search filter
            if (!m_SearchFilter.empty()) {
                bool matches = CaseInsensitiveContains(plugin.Name, m_SearchFilter) ||
                               CaseInsensitiveContains(plugin.Id, m_SearchFilter);
                for (const auto& mod : plugin.Modules) {
                    if (CaseInsensitiveContains(mod.Name, m_SearchFilter)) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) continue;
            }

            // Apply source filter
            const bool isProjectPlugin = plugin.Source == Plugin::Origin::Project;
            if (m_SourceFilter == SourceFilter::Engine && isProjectPlugin) continue;
            if (m_SourceFilter == SourceFilter::Project && !isProjectPlugin) continue;

            // Apply status filter
            const bool isDraftSelected = m_SelectionModel && m_SelectionModel->IsDraft(plugin.Id);
            const auto& draftGraph = m_SelectionModel->DraftResolution().Plugins;
            const bool draftEnabled = std::any_of(draftGraph.begin(), draftGraph.end(), [&](const auto& item) { return item.Id == plugin.Id; });
            if (m_StatusFilter == StatusFilter::Enabled && !draftEnabled) continue;
            if (m_StatusFilter == StatusFilter::Disabled && draftEnabled) continue;

            ImGui::PushID(plugin.Id.c_str());
            const auto rowPath = plugin.ManifestPath.u8string();
            ImGui::PushID(reinterpret_cast<const char*>(rowPath.c_str()));

            // Checkbox (disabled during simulation)
            bool checked = draftEnabled || isDraftSelected;
            if (isSimulationActive || m_Busy) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Checkbox("##Enabled", &checked)) {
                TogglePluginDraft(plugin.Id, checked, isSimulationActive);
            }
            if (isSimulationActive || m_Busy) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "%s", plugin.Name.c_str());
            if (!isNarrow) ImGui::SameLine();
            ImGui::TextDisabled("v%s (%s)", plugin.Version.c_str(), plugin.Id.c_str());
            ImGui::PopTextWrapPos();
            const bool active = std::find(m_ActiveIds.begin(), m_ActiveIds.end(), plugin.Id) != m_ActiveIds.end();
            ImGui::Indent(28.0f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s | %s | %s", isProjectPlugin ? "Project" : "Engine", active ? "Active" : "Inactive",
                m_SelectionModel->IsDesired(plugin.Id) ? "Desired explicitly" : "Dependency or not desired");
            ImGui::PopTextWrapPos();
            if (!plugin.Description.empty()) ImGui::TextWrapped("%s", plugin.Description.c_str());
            ImGui::Unindent(28.0f);

            // Dependency explanation
            if (m_SelectionModel) {
                const std::string reason = m_SelectionModel->DependencyReason(plugin.Id);
                if (!reason.empty()) {
                    ImGui::Indent(28.0f);
                    ImGui::TextDisabled("Reason: %s", reason.c_str());
                    ImGui::Unindent(28.0f);
                }
            }

            // Modules summary
            if (!plugin.Modules.empty()) {
                ImGui::Indent(28.0f);
                std::string modSummary = "Modules: ";
                for (size_t i = 0; i < plugin.Modules.size(); ++i) {
                    if (i > 0) modSummary += ", ";
                    modSummary += plugin.Modules[i].Name + " [" + Plugin::ToString(plugin.Modules[i].Kind) + "]";
                }
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", modSummary.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Unindent(28.0f);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    if (m_SelectionModel) for (const auto& id : m_SelectionModel->Draft()) {
        if (std::any_of(m_Catalog.Plugins.begin(), m_Catalog.Plugins.end(), [&](const auto& item) { return item.Id == id; })) continue;
        ImGui::PushID(id.c_str()); ImGui::TextWrapped("Missing selection: %s", id.c_str());
        ImGui::BeginDisabled(m_Busy || isSimulationActive);
        if (ImGui::SmallButton("Remove missing selection")) { TogglePluginDraft(id, false, isSimulationActive); ImGui::EndDisabled(); ImGui::PopID(); break; }
        ImGui::EndDisabled(); ImGui::PopID();
    }

    ImGui::EndChild();

    // 7. Actions & Status Footer
    ImGui::Separator();
    const bool hasChanges = HasDraftChanges();
    const bool canApply = CanApply();

    if (hasChanges) {
        ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Pending draft changes to plugin selection."); ImGui::PopTextWrapPos();
    } else {
        ImGui::TextDisabled("Plugin selections are up to date.");
    }
    if (m_ProjectPath.empty()) {
        ImGui::TextWrapped("Save Project As before applying plugin changes.");
    }

    if (!isNarrow) ImGui::SameLine();
    const float buttonsWidth = 210.0f;
    if (!isNarrow) ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonsWidth - 15.0f);

    // Apply Button
    const bool applyEnabled = hasChanges && canApply && !isSimulationActive;
    if (!applyEnabled) ImGui::BeginDisabled();
    if (ImGui::Button("Apply", ImVec2(95.0f, 26.0f))) {
        ApplyDraft(isSimulationActive);
    }
    if (!applyEnabled) ImGui::EndDisabled();

    ImGui::SameLine();

    // Revert Button
    const bool revertEnabled = hasChanges && !isSimulationActive && !m_Busy;
    if (!revertEnabled) ImGui::BeginDisabled();
    if (ImGui::Button("Revert", ImVec2(95.0f, 26.0f))) {
        RevertDraft();
    }
    if (!revertEnabled) ImGui::EndDisabled();
    if (m_OnBuild) {
        ImGui::Spacing();
        if (!m_BuildStatus.empty()) ImGui::TextWrapped("%s", m_BuildStatus.c_str());
        ImGui::BeginDisabled(m_Busy || isSimulationActive || hasChanges || m_ProjectPath.empty());
        if (ImGui::Button("Build selected plugins")) m_OnBuild();
        ImGui::EndDisabled();
        if (m_Busy && m_OnCancelBuild) { if (!isNarrow) ImGui::SameLine(); if (ImGui::Button("Cancel build")) m_OnCancelBuild(); }
        if (!m_BuildLog.empty() && m_OnOpenLog) { if (!isNarrow) ImGui::SameLine(); if (ImGui::Button("Open build log")) m_OnOpenLog(); }
    }

    // 8. Restart Notification
    if (m_NeedsRestart) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.95f, 0.55f, 1.0f));
        ImGui::TextWrapped("Changes applied. Restart required to load native modules.");
        ImGui::PopStyleColor();
        if (!isNarrow) ImGui::SameLine();
        ImGui::BeginDisabled(m_Busy || isSimulationActive);
        if (ImGui::SmallButton("Restart Now")) {
            RequestRestart();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Later")) {
            ClearRestart();
        }
        ImGui::EndDisabled();
    }

    ImGui::End();
}

} // namespace Axle
