#include <Axle/PluginSelection.hpp>

#include <iostream>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

Axle::Plugin::Manifest MakeManifest(std::string id, std::string name,
                                std::vector<std::string> dependencies = {}) {
    Axle::Plugin::Manifest result;
    result.Id = std::move(id);
    result.Name = std::move(name);
    result.Version = "1.0.0";
    for (auto& dependency : dependencies) result.Dependencies.push_back({std::move(dependency), "1.0.0"});
    return result;
}
}

int main() {
    using namespace Axle::Plugin;
    Catalog catalog;
    catalog.Plugins.push_back(MakeManifest("test.base", "Base"));
    catalog.Plugins.push_back(MakeManifest("test.game", "Game", {"test.base"}));

    SelectionModel model(catalog, ProductKind::Editor, {});
    bool ok = true;
    ok &= Check(!model.HasDraftChanges(), "empty selection starts clean");
    ok &= Check(model.SetDraftSelected("test.game", true), "selecting a known plugin succeeds");
    ok &= Check(model.HasDraftChanges() && model.IsDraft("test.game"), "selection is draft-only before Apply");
    ok &= Check(model.DependencyReason("test.game") == "Selected explicitly (pending Apply)", "explicit draft reason is visible");
    ok &= Check(model.DraftResolution().IsValid() && model.DraftResolution().Plugins.size() == 2, "dependencies resolve in draft state");
    ok &= Check(model.DependencyReason("test.base") == "Required by test.game", "dependency reason names its owner");
    ok &= Check(!model.SetDraftSelected("test.base", false), "required dependency removal is rejected");
    ok &= Check(model.LastError().find("required by") != std::string::npos, "rejected removal has a reason");

    const auto applied = model.Apply();
    ok &= Check(applied.Applied && model.IsDesired("test.game"), "Apply commits the explicit selection");
    ok &= Check(!model.HasDraftChanges(), "Apply clears the draft delta");
    ok &= Check(model.SetDraftSelected("test.game", false), "deselecting the root succeeds");
    model.Revert();
    ok &= Check(model.IsDraft("test.game") && !model.HasDraftChanges(), "Revert restores Desired state");
    ok &= Check(!model.SetDraftSelected("missing", true), "unknown plugin selection is rejected");
    const auto roots = model.DependentSelections("test.base");
    ok &= Check(roots == std::vector<std::string>{"test.game"}, "dependent disable identifies the complete explicit transaction");
    ok &= Check(model.RemoveDraftWithDependents("test.base") && model.Draft().empty() && model.IsDesired("test.game"), "confirmed disable changes the entire draft while Desired stays durable");
    model.RefreshCatalog(catalog);
    ok &= Check(model.Draft().empty() && model.IsDesired("test.game"), "catalog refresh preserves unsaved draft decisions");
    SelectionModel missing(catalog, ProductKind::Editor, {"missing.plugin"});
    ok &= Check(missing.SetDraftSelected(missing.Draft().front(), false) && missing.DraftResolution().IsValid(), "a missing saved selection can be removed safely through an aliased row ID");
    if (ok) std::cout << "Plugin selection Draft/Desired/Apply/Revert transaction: PASS\n";
    return ok ? 0 : 1;
}
