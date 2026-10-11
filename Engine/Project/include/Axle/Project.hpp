#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <filesystem>
#include <memory>

namespace Axle {

    struct ProjectConfig {
        std::string Name = "Untitled";
        std::filesystem::path StartScene = "scenes/Playground.axlescene";
        std::filesystem::path AssetDirectory = "assets";
    };

    class Project {
    public:
        Project() = default;
        ~Project() = default;

        const ProjectConfig& GetConfig() const { return m_Config; }
        ProjectConfig& GetConfig() { return m_Config; }

        const std::filesystem::path& GetProjectDirectoryPath() const { return m_ProjectDirectory; }

        static const std::filesystem::path& GetProjectDirectory();
        static std::filesystem::path GetAssetDirectory();
        static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& path);

        static std::shared_ptr<Project> GetActive() { return s_ActiveProject; }
        static void SetActive(std::shared_ptr<Project> project);

        static std::shared_ptr<Project> New();
        static std::shared_ptr<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;

        inline static std::shared_ptr<Project> s_ActiveProject = nullptr;

        friend class ProjectSerializer;
    };

}
