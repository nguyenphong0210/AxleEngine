#include <Axle/PluginSelection.hpp>

#include <algorithm>
#include <string_view>

namespace Axle::Plugin {
namespace {

void Normalize(std::vector<std::string>& ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

} // namespace

SelectionModel::SelectionModel(Catalog catalog, ProductKind product, std::vector<std::string> desired)
    : m_Catalog(std::move(catalog)), m_Product(product), m_Desired(std::move(desired)), m_Draft(m_Desired) {
    Normalize(m_Desired);
    Normalize(m_Draft);
    RecomputeDraftResolution();
}

bool SelectionModel::Contains(const std::vector<std::string>& ids, std::string_view id) const {
    return std::binary_search(ids.begin(), ids.end(), id);
}

const Manifest* SelectionModel::Find(std::string_view id) const {
    const auto it = std::find_if(m_Catalog.Plugins.begin(), m_Catalog.Plugins.end(),
        [id](const Manifest& plugin) { return plugin.Id == id; });
    return it == m_Catalog.Plugins.end() ? nullptr : &*it;
}

bool SelectionModel::IsDesired(std::string_view id) const { return Contains(m_Desired, id); }
bool SelectionModel::IsDraft(std::string_view id) const { return Contains(m_Draft, id); }

bool SelectionModel::Requires(std::string_view root, std::string_view dependency,
                              std::vector<std::string>& visiting) const {
    if (root == dependency) return false;
    if (std::find(visiting.begin(), visiting.end(), root) != visiting.end()) return false;
    const auto* manifest = Find(root);
    if (!manifest) return false;
    visiting.emplace_back(root);
    for (const auto& required : manifest->Dependencies) {
        if (required.Id == dependency || Requires(required.Id, dependency, visiting)) {
            visiting.pop_back();
            return true;
        }
    }
    visiting.pop_back();
    return false;
}

bool SelectionModel::SetDraftSelected(std::string_view id, bool selected) {
    m_LastError.clear();
    const auto* manifest = Find(id);
    if (!manifest) {
        m_LastError = "Unknown plugin: " + std::string(id);
        return false;
    }
    const bool currentlySelected = IsDraft(id);
    if (!selected) {
        for (const auto& other : m_Draft) {
            if (other == id) continue;
            std::vector<std::string> visiting;
            if (Requires(other, id, visiting)) {
                m_LastError = "Cannot deselect '" + std::string(id) + "'; required by '" + other + "'.";
                return false;
            }
        }
    }
    if (currentlySelected == selected) return true;
    if (selected) m_Draft.emplace_back(id);
    else m_Draft.erase(std::remove(m_Draft.begin(), m_Draft.end(), id), m_Draft.end());
    Normalize(m_Draft);
    RecomputeDraftResolution();
    return true;
}

ApplyReport SelectionModel::Apply() {
    ApplyReport report;
    report.Resolved = m_DraftResolution;
    if (!m_DraftResolution.IsValid()) {
        report.Error = "Plugin selection cannot be applied until dependencies are resolved.";
        m_LastError = report.Error;
        return report;
    }
    m_Desired = m_Draft;
    report.Applied = true;
    m_LastError.clear();
    return report;
}

void SelectionModel::Revert() {
    m_Draft = m_Desired;
    m_LastError.clear();
    RecomputeDraftResolution();
}

void SelectionModel::RecomputeDraftResolution() {
    m_DraftResolution = Resolve(m_Catalog, m_Draft, m_Product);
}

std::string SelectionModel::DependencyReason(std::string_view id) const {
    if (IsDraft(id)) {
        if (IsDesired(id)) return "Selected explicitly";
        return "Selected explicitly (pending Apply)";
    }
    for (const auto& explicitId : m_Draft) {
        std::vector<std::string> visiting;
        if (Requires(explicitId, id, visiting)) return "Required by " + explicitId;
    }
    return {};
}

} // namespace Axle::Plugin
