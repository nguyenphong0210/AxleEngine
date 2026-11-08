#pragma once

#include <Axle/Plugin.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Axle::Plugin {

struct ApplyReport {
    bool Applied = false;
    std::string Error;
    Resolution Resolved;
};

// UI-independent transaction model for the Plugins window.  Draft contains
// explicit user choices; dependencies are resolved by the production resolver
// and never written back as explicit selections.
class SelectionModel {
public:
    SelectionModel(Catalog catalog, ProductKind product, std::vector<std::string> desired = {});

    const Catalog& GetCatalog() const { return m_Catalog; }
    ProductKind GetProduct() const { return m_Product; }
    const std::vector<std::string>& Desired() const { return m_Desired; }
    const std::vector<std::string>& Draft() const { return m_Draft; }
    const Resolution& DraftResolution() const { return m_DraftResolution; }
    const std::string& LastError() const { return m_LastError; }

    bool IsDesired(std::string_view id) const;
    bool IsDraft(std::string_view id) const;
    bool HasDraftChanges() const { return m_Draft != m_Desired; }

    // A dependent explicit selection prevents removing its required plugin.
    // This keeps a checkbox transaction valid and lets the UI show the reason.
    bool SetDraftSelected(std::string_view id, bool selected);
    ApplyReport Apply();
    void Revert();
    void RefreshCatalog(Catalog catalog);
    std::vector<std::string> DependentSelections(std::string_view id) const;
    bool RemoveDraftWithDependents(std::string_view id);

    // Returns a stable explanation suitable for a row in the Plugins window.
    // Empty means the plugin is neither selected nor a known dependency.
    std::string DependencyReason(std::string_view id) const;

private:
    bool Contains(const std::vector<std::string>& ids, std::string_view id) const;
    const Manifest* Find(std::string_view id) const;
    bool Requires(std::string_view root, std::string_view dependency,
                  std::vector<std::string>& visiting) const;
    void RecomputeDraftResolution();

    Catalog m_Catalog;
    ProductKind m_Product = ProductKind::Editor;
    std::vector<std::string> m_Desired;
    std::vector<std::string> m_Draft;
    Resolution m_DraftResolution;
    std::string m_LastError;
};

} // namespace Axle::Plugin
