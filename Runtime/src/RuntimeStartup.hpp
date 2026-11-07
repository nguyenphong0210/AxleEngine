#pragma once
#include <Axle/Project.hpp>
#include <Axle/RHI/RHISwitcher.hpp>

namespace Axle {
    struct RuntimeStartup {
        std::filesystem::path ProjectPath;
        std::shared_ptr<Project> ProjectDefinition;
        RHI::RHIBackendConfig Backend;
    };

    // Resolve CPU project data (including a mounted export) before constructing
    // any backend candidate. An explicit CLI backend remains authoritative.
    RuntimeStartup PrepareRuntimeStartup(const std::filesystem::path& projectPath,
                                         RHI::RHIBackendConfig backend);
}
