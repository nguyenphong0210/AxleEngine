#pragma once
#include <Axle/Project.hpp>

namespace Axle {
    // Empty selection means a fresh unsaved workspace, never nearby-project discovery.
    inline std::shared_ptr<Project> OpenEditorStartupProject(const std::filesystem::path& explicitProject = {}) {
        return explicitProject.empty() ? Project::CreateUntitled() : Project::Load(explicitProject);
    }
}
