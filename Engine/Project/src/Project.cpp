#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>

namespace Axle {

    const std::filesystem::path& Project::GetProjectDirectory() {
        if (s_ActiveProject) {
            return s_ActiveProject->m_ProjectDirectory;
        }
        static std::filesystem::path empty;
        return empty;
    }

    std::filesystem::path Project::GetAssetDirectory() {
        if (s_ActiveProject) {
            if (s_ActiveProject->m_Config.AssetDirectory.is_absolute()) {
                if (std::filesystem::exists(s_ActiveProject->m_Config.AssetDirectory)) {
                    return s_ActiveProject->m_Config.AssetDirectory;
                }
            } else {
                auto resolved = s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.AssetDirectory;
                if (std::filesystem::exists(resolved)) {
                    return resolved;
                }
            }
            // If the configured path doesn't exist, check if AssetDirectory exists relative to current working directory
            if (std::filesystem::exists(s_ActiveProject->m_Config.AssetDirectory)) {
                return std::filesystem::absolute(s_ActiveProject->m_Config.AssetDirectory);
            }
            // Fall back to FileSystem auto-detection
            auto fsAssetDir = FileSystem::GetAssetDirectory();
            if (std::filesystem::exists(fsAssetDir)) {
                return fsAssetDir;
            }
            return s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.AssetDirectory;
        }
        return FileSystem::GetAssetDirectory();
    }

    std::filesystem::path Project::GetAssetFileSystemPath(const std::filesystem::path& path) {
        return GetAssetDirectory() / path;
    }

    void Project::SetActive(std::shared_ptr<Project> project) {
        s_ActiveProject = project;
        if (project) {
            FileSystem::SetAssetDirectory(project->GetAssetDirectory());
            AXLE_INFO("Project: Activated project '{0}' (Asset root: '{1}')", 
                project->GetConfig().Name, project->GetAssetDirectory().string());
        }
    }

    std::shared_ptr<Project> Project::New() {
        auto project = std::make_shared<Project>();
        project->m_Config.Name = "Untitled Project";
        project->m_Config.AssetDirectory = "assets";
        project->m_Config.StartScene = "scenes/Playground.axlescene";
        project->m_ProjectDirectory = std::filesystem::current_path();
        return project;
    }

    std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
        auto project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        if (serializer.Deserialize(path)) {
            SetActive(project);
            return project;
        }
        return nullptr;
    }

    bool Project::SaveActive(const std::filesystem::path& path) {
        if (!s_ActiveProject) {
            AXLE_WARN("Project: Cannot save project - no active project!");
            return false;
        }
        ProjectSerializer serializer(s_ActiveProject);
        if (serializer.Serialize(path)) {
            s_ActiveProject->m_ProjectDirectory = path.parent_path();
            return true;
        }
        return false;
    }

}
