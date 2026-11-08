#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Axle {

    struct ProjectConfig {
        static constexpr int CurrentSchemaVersion = 1;
        int SchemaVersion = CurrentSchemaVersion;
        std::string Name = "Untitled";
        std::filesystem::path StartScene;
        std::filesystem::path AssetDirectory = "assets";
        std::string RHIBackend = "Direct3D12"; // "Direct3D12", "Vulkan", "OpenGL"
        std::vector<std::string> SelectedPlugins;
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

        static std::shared_ptr<Project> GetActive();
        static void SetActive(std::shared_ptr<Project> project);

        static std::shared_ptr<Project> New(const std::filesystem::path& directory = {});
        static std::shared_ptr<Project> CreateUntitled();
        static std::shared_ptr<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;

        static std::shared_ptr<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };

}
