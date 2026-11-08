#include "PluginsPanel.hpp"

#include <Axle/Log.hpp>
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

void PluginsPanel::SetActiveProject(std::shared_ptr<Project> project) {
    m_Project = std::move(project);
    RefreshCatalog();
}

void PluginsPanel::SetCatalog(Plugin::Catalog catalog) {
    m_Catalog = std::move(catalog);
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
    }
    options.Roots.push_back(std::filesystem::current_path() / "Plugins");

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
    return m_SelectionModel && m_SelectionModel->DraftResolution().IsValid();
}

bool PluginsPanel::TogglePluginDraft(std::string_view id, bool enabled, bool isSimulationActive) {
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
    } else {
        m_ErrorMessage.clear();
    }
    return ok;
}

Plugin::ApplyReport PluginsPanel::ApplyDraft(bool isSimulationActive) {
    Plugin::ApplyReport report;
    if (isSimulationActive) {
        report.Error = "Cannot apply plugin selection while simulation is active. Stop Play mode first.";
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
    if (m_OnSaveScene) {
        if (!m_OnSaveScene()) {
            report.Error = "Applying plugin changes cancelled: saving current scene failed.";
            m_ErrorMessage = report.Error;
            return report;
        }
    }

    report = m_SelectionModel->Apply();
    if (!report.Applied) {
        m_ErrorMessage = report.Error;
        return report;
    }

    // Persist desired selections to Project configuration
    if (m_Project) {
        m_Project->GetConfig().SelectedPlugins = m_SelectionModel->Desired();
        const auto projFile = m_Project->GetProjectDirectoryPath() / (m_Project->GetConfig().Name + ".axleproj");
        if (!Project::SaveActive(projFile)) {
            AXLE_WARN("PluginsPanel: Project configuration could not be saved to '{0}'", projFile.string());
        }
    }

    m_NeedsRestart = true;
    m_StatusMessage = "Plugin changes applied successfully. Editor restart required.";
    m_ErrorMessage.clear();
    return report;
}

void PluginsPanel::RevertDraft() {
    if (m_SelectionModel) {
        m_SelectionModel->Revert();
    }
    m_ErrorMessage.clear();
}

void PluginsPanel::RequestRestart() {
    if (m_OnRestart) {
        m_OnRestart();
    }
}

void PluginsPanel::OnImGuiRender(bool* pOpen, bool isSimulationActive) {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", pOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
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
    if (ImGui::Button("Refresh")) {
        RefreshCatalog();
    }

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
    const float footerHeight = 70.0f;
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
            bool isProjectPlugin = false;
            if (m_Project && !m_Project->GetProjectDirectoryPath().empty()) {
                isProjectPlugin = plugin.ManifestPath.string().find(m_Project->GetProjectDirectoryPath().string()) != std::string::npos;
            }
            if (m_SourceFilter == SourceFilter::Engine && isProjectPlugin) continue;
            if (m_SourceFilter == SourceFilter::Project && !isProjectPlugin) continue;

            // Apply status filter
            const bool isDraftSelected = m_SelectionModel && m_SelectionModel->IsDraft(plugin.Id);
            if (m_StatusFilter == StatusFilter::Enabled && !isDraftSelected) continue;
            if (m_StatusFilter == StatusFilter::Disabled && isDraftSelected) continue;

            ImGui::PushID(plugin.Id.c_str());

            // Checkbox (disabled during simulation)
            bool checked = isDraftSelected;
            if (isSimulationActive) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Checkbox("##Enabled", &checked)) {
                TogglePluginDraft(plugin.Id, checked, isSimulationActive);
            }
            if (isSimulationActive) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "%s", plugin.Name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("v%s (%s)", plugin.Version.c_str(), plugin.Id.c_str());

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
        }
    }

    ImGui::EndChild();

    // 7. Actions & Status Footer
    ImGui::Separator();
    const bool hasChanges = HasDraftChanges();
    const bool canApply = CanApply();

    if (hasChanges) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Pending draft changes to plugin selection.");
    } else {
        ImGui::TextDisabled("Plugin selections are up to date.");
    }

    ImGui::SameLine();
    const float buttonsWidth = 210.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonsWidth - 15.0f);

    // Apply Button
    const bool applyEnabled = hasChanges && canApply && !isSimulationActive;
    if (!applyEnabled) ImGui::BeginDisabled();
    if (ImGui::Button("Apply", ImVec2(95.0f, 26.0f))) {
        ApplyDraft(isSimulationActive);
    }
    if (!applyEnabled) ImGui::EndDisabled();

    ImGui::SameLine();

    // Revert Button
    const bool revertEnabled = hasChanges && !isSimulationActive;
    if (!revertEnabled) ImGui::BeginDisabled();
    if (ImGui::Button("Revert", ImVec2(95.0f, 26.0f))) {
        RevertDraft();
    }
    if (!revertEnabled) ImGui::EndDisabled();

    // 8. Restart Notification
    if (m_NeedsRestart) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.95f, 0.55f, 1.0f));
        ImGui::Text("Changes applied. Restart required to load native modules.");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Restart Now")) {
            RequestRestart();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Later")) {
            ClearRestart();
        }
    }

    ImGui::End();
}

} // namespace Axle
