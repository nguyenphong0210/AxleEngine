#include "RuntimeStartup.hpp"
#include <Axle/FileSystem.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/Path.hpp>
#include <stdexcept>

namespace Axle {
    RuntimeStartup PrepareRuntimeStartup(const std::filesystem::path& requested, RHI::RHIBackendConfig backend) {
        RuntimeStartup startup; startup.ProjectPath = requested; startup.Backend = std::move(backend);
        const auto directory = requested.has_parent_path() ? requested.parent_path() : std::filesystem::current_path();
        if (!FileSystem::HasMountedPackage()) {
            for (const auto* name : {"Data.axlepak", "Game.axlepak", "assets.axlepak", "Export/Data.axlepak"}) {
                const auto candidate = directory / name;
                if (std::filesystem::exists(candidate)) {
                    if (!FileSystem::MountPackage(candidate)) throw std::runtime_error("Runtime asset package cannot be mounted: " + PathToUtf8(candidate));
                    break;
                }
            }
        }
        struct RestoreAssetRoot {
            std::filesystem::path Previous = FileSystem::GetAssetDirectory();
            ~RestoreAssetRoot() { FileSystem::SetAssetDirectory(Previous); }
        } restore;
        // The export's project configuration sits at its root; payload entries
        // use the project's asset root after activation. Keep these two lookups
        // distinct, including when --project is an absolute virtual file path.
        if (FileSystem::HasMountedPackage()) FileSystem::SetAssetDirectory(std::filesystem::absolute(directory));
        if (startup.ProjectPath.empty()) {
            for (const auto* name : {"Sandbox.axleproj", "Project.axleproj"})
                if (FileSystem::Exists(name)) { startup.ProjectPath = name; break; }
        }
        if (startup.ProjectPath.empty()) return startup;
        if (!FileSystem::Exists(startup.ProjectPath)) throw std::runtime_error("Runtime project is missing: " + PathToUtf8(startup.ProjectPath));
        auto project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        if (!serializer.Deserialize(startup.ProjectPath)) throw std::runtime_error("Runtime project cannot be read: " + PathToUtf8(startup.ProjectPath));
        if (!project->GetConfig().SelectedPlugins.empty())
            throw std::runtime_error("This player cannot contain selected native plugins; selected static composition is scheduled for PLG-22");
        if (startup.Backend.RawArgument.empty()) {
            const auto type = RHI::StringToRHIType(project->GetConfig().RHIBackend);
            if (type == RHI::RHIType::None) throw std::runtime_error("Unsupported project rendering backend: " + project->GetConfig().RHIBackend);
            startup.Backend.PreferredBackend = type;
        }
        startup.ProjectDefinition = std::move(project);
        return startup;
    }
}
