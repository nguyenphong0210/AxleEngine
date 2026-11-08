#include <Axle/Path.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/UUID.hpp>

namespace Axle {

    std::shared_ptr<Project> Project::s_ActiveProject;
    std::shared_ptr<Project> Project::GetActive() { return s_ActiveProject; }

    const std::filesystem::path& Project::GetProjectDirectory() {
        if (s_ActiveProject) {
            return s_ActiveProject->m_ProjectDirectory;
        }
        static std::filesystem::path empty;
        return empty;
    }

    std::filesystem::path Project::GetAssetDirectory() {
        if (s_ActiveProject) {
            const auto& configured = s_ActiveProject->m_Config.AssetDirectory;
            return configured.is_absolute() ? configured
                : (s_ActiveProject->m_ProjectDirectory / configured).lexically_normal();
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
                project->GetConfig().Name, PathToUtf8(project->GetAssetDirectory()));
        }
    }

    std::shared_ptr<Project> Project::New(const std::filesystem::path& directory) {
        auto project = std::make_shared<Project>();
        project->m_Config.Name = "Untitled Project";
        project->m_Config.AssetDirectory = "assets";
        project->m_Config.StartScene.clear();
        project->m_ProjectDirectory = std::filesystem::absolute(directory.empty() ? std::filesystem::current_path() : directory);
        return project;
    }

    std::shared_ptr<Project> Project::CreateUntitled() {
        const auto directory = std::filesystem::temp_directory_path() / "AxleEngine" / "UntitledProjects"
            / std::to_string(static_cast<uint64_t>(UUID{}));
        auto project = New(directory);
        std::filesystem::create_directories(directory / project->m_Config.AssetDirectory);
        SetActive(project);
        return project;
    }

    std::shared_ptr<Project> Project::Load(const std::filesystem::path& path) {
        auto project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        if (serializer.Deserialize(std::filesystem::absolute(path))) {
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
        const auto target = std::filesystem::absolute(path);
        const auto oldAssets = GetAssetDirectory();
        const auto newAssets = s_ActiveProject->m_Config.AssetDirectory.is_absolute()
            ? oldAssets : target.parent_path() / s_ActiveProject->m_Config.AssetDirectory;
        const auto oldPlugins = s_ActiveProject->m_ProjectDirectory / "Plugins";
        const auto newPlugins = target.parent_path() / "Plugins";
        if (oldPlugins.lexically_normal() != newPlugins.lexically_normal() && std::filesystem::exists(oldPlugins)) {
            const auto relativeDestination = std::filesystem::weakly_canonical(newPlugins)
                .lexically_relative(std::filesystem::weakly_canonical(oldPlugins));
            if ((!relativeDestination.empty() && *relativeDestination.begin() != "..") ||
                (std::filesystem::exists(newPlugins) && !std::filesystem::is_empty(newPlugins))) {
                AXLE_WARN("Project: Save As requires an empty plugin destination outside its source tree"); return false;
            }
        }
        std::error_code error;
        if (oldAssets.lexically_normal() != newAssets.lexically_normal() && std::filesystem::exists(oldAssets)) {
            const auto relativeDestination = std::filesystem::weakly_canonical(newAssets)
                .lexically_relative(std::filesystem::weakly_canonical(oldAssets));
            if (!relativeDestination.empty() && *relativeDestination.begin() != "..") {
                AXLE_WARN("Project: Save As cannot copy assets into their own subtree: {0}", PathToUtf8(newAssets));
                return false;
            }
            if (std::filesystem::exists(newAssets) && !std::filesystem::is_empty(newAssets)) {
                AXLE_WARN("Project: Save As requires an empty destination asset directory: {0}", PathToUtf8(newAssets));
                return false;
            }
            std::filesystem::create_directories(newAssets.parent_path(), error);
            std::filesystem::copy(oldAssets, newAssets, std::filesystem::copy_options::recursive, error);
            if (error) {
                AXLE_ERROR("Project: Failed to preserve assets on Save As: {0}", error.message());
                return false;
            }
        }
        if (oldPlugins.lexically_normal() != newPlugins.lexically_normal() && std::filesystem::exists(oldPlugins)) {
            std::filesystem::create_directories(newPlugins.parent_path(), error);
            std::filesystem::copy(oldPlugins, newPlugins, std::filesystem::copy_options::recursive, error);
            if (error) { AXLE_ERROR("Project: Failed to preserve project plugins on Save As: {0}", error.message()); return false; }
        }
        ProjectSerializer serializer(s_ActiveProject);
        if (serializer.Serialize(target)) {
            s_ActiveProject->m_ProjectDirectory = target.parent_path();
            FileSystem::SetAssetDirectory(GetAssetDirectory());
            return true;
        }
        return false;
    }

}
