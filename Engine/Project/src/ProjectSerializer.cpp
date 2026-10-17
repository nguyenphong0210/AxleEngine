#include <Axle/ProjectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Axle {

    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    ProjectSerializer::ProjectSerializer(std::shared_ptr<Project> project)
        : m_Project(project) {
    }

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) {
        if (!m_Project) return false;

        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AXLE_ERROR("ProjectSerializer: Could not open file for writing: '{0}'", filepath.string());
            return false;
        }

        const auto& config = m_Project->GetConfig();

        fout << "# Axle Engine Project Configuration File\n";
        fout << "Project:\n";
        fout << "  Name: " << config.Name << "\n";
        fout << "  StartScene: " << config.StartScene.string() << "\n";
        fout << "  AssetDirectory: " << config.AssetDirectory.string() << "\n";

        fout.close();
        AXLE_INFO("ProjectSerializer: Successfully saved project to '{0}'", filepath.string());
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) {
        if (!m_Project) return false;

        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            AXLE_ERROR("ProjectSerializer: Could not read file: '{0}'", filepath.string());
            return false;
        }

        std::istringstream fin(content);
        std::string line;
        bool inProjectSection = false;

        while (std::getline(fin, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed == "Project:") {
                inProjectSection = true;
                continue;
            }

            if (inProjectSection) {
                if (trimmed.rfind("Name:", 0) == 0) {
                    m_Project->GetConfig().Name = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("StartScene:", 0) == 0) {
                    m_Project->GetConfig().StartScene = Trim(trimmed.substr(11));
                } else if (trimmed.rfind("AssetDirectory:", 0) == 0) {
                    m_Project->GetConfig().AssetDirectory = Trim(trimmed.substr(15));
                }
            }
        }

        m_Project->m_ProjectDirectory = filepath.parent_path();
        if (m_Project->m_ProjectDirectory.empty()) {
            m_Project->m_ProjectDirectory = std::filesystem::current_path();
        }

        AXLE_INFO("ProjectSerializer: Successfully loaded project '{0}' from '{1}'", 
            m_Project->GetConfig().Name, filepath.string());
        return true;
    }

}
