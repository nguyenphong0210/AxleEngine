#pragma once
#include <Axle/Project.hpp>

namespace Axle {
    // Explicit projects win; only the configured development default may be auto-opened.
    inline std::filesystem::path ResolveEditorStartupProject(
        const std::filesystem::path& explicitProject,
        const std::filesystem::path& developerDefault = {}, bool emptyProject = false) {
        if (!explicitProject.empty()) return std::filesystem::absolute(explicitProject);
        std::error_code error;
        if (!emptyProject && !developerDefault.empty() && std::filesystem::is_regular_file(developerDefault, error))
            return std::filesystem::absolute(developerDefault);
        return {};
    }

    // Empty selection means a fresh unsaved workspace, never nearby-project discovery.
    inline std::shared_ptr<Project> OpenEditorStartupProject(const std::filesystem::path& explicitProject = {}) {
        return explicitProject.empty() ? Project::CreateUntitled() : Project::Load(explicitProject);
    }
}
