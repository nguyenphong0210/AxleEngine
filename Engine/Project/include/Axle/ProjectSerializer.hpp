#pragma once
#include <Axle/Project.hpp>
#include <filesystem>
#include <iosfwd>
#include <memory>

namespace Axle {

    class ProjectSerializer {
    public:
        ProjectSerializer(std::shared_ptr<Project> project);

        bool Serialize(const std::filesystem::path& filepath);
        bool Deserialize(const std::filesystem::path& filepath);

        static void AppendRenderSettingsYaml(std::ostream& out, const ProjectRenderSettings& settings);

    private:
        std::shared_ptr<Project> m_Project;
    };

}
