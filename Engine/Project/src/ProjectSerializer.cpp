#include <Axle/Path.hpp>
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

        const auto& config = m_Project->GetConfig();
        std::stringstream fout;
        fout << "# Axle Engine Project Configuration File\n";
        fout << "Project:\n";
        fout << "  SchemaVersion: " << config.SchemaVersion << "\n";
        fout << "  Name: " << config.Name << "\n";
        fout << "  StartScene: " << PathToUtf8(config.StartScene) << "\n";
        fout << "  AssetDirectory: " << PathToUtf8(config.AssetDirectory) << "\n";
        fout << "  RHIBackend: " << config.RHIBackend << "\n";
        fout << "  Plugins:\n";
        for (const auto& id : config.SelectedPlugins) fout << "    - " << id << "\n";

        std::string outError;
        if (!FileSystem::WriteTextFileAtomically(filepath, fout.str(), &outError)) {
            AXLE_ERROR("ProjectSerializer: Failed to atomically save project to '{0}': {1}", PathToUtf8(filepath), outError);
            return false;
        }

        AXLE_INFO("ProjectSerializer: Successfully saved project to '{0}'", PathToUtf8(filepath));
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) {
        if (!m_Project) return false;

        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            AXLE_ERROR("ProjectSerializer: Could not read file: '{0}'", PathToUtf8(filepath));
            return false;
        }

        std::istringstream fin(content);
        auto parsedConfig = m_Project->GetConfig();
        std::string line;
        bool inProjectSection = false;
        bool inPlugins = false;
        bool sawSchema = false;
        std::vector<std::string> selectedPlugins;

        while (std::getline(fin, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed == "Project:") {
                inProjectSection = true;
                inPlugins = false;
                continue;
            }

            if (inProjectSection) {
                if (trimmed == "Plugins:") {
                    inPlugins = true;
                } else if (inPlugins && trimmed.rfind("- ", 0) == 0) {
                    const auto id = Trim(trimmed.substr(2));
                    if (id.empty() || std::find(selectedPlugins.begin(), selectedPlugins.end(), id) != selectedPlugins.end()) {
                        AXLE_WARN("ProjectSerializer: invalid or duplicate plugin selection in '{0}'", PathToUtf8(filepath));
                        return false;
                    }
                    selectedPlugins.push_back(id);
                } else if (trimmed.rfind("SchemaVersion:", 0) == 0) {
                    try { parsedConfig.SchemaVersion = std::stoi(Trim(trimmed.substr(14))); }
                    catch (...) { AXLE_WARN("ProjectSerializer: invalid schema version in '{0}'", PathToUtf8(filepath)); return false; }
                    sawSchema = true;
                    inPlugins = false;
                } else if (trimmed.rfind("Name:", 0) == 0) {
                    inPlugins = false;
                    parsedConfig.Name = Trim(trimmed.substr(5));
                } else if (trimmed.rfind("StartScene:", 0) == 0) {
                    parsedConfig.StartScene = PathFromUtf8(Trim(trimmed.substr(11)));
                } else if (trimmed.rfind("AssetDirectory:", 0) == 0) {
                    parsedConfig.AssetDirectory = PathFromUtf8(Trim(trimmed.substr(15)));
                } else if (trimmed.rfind("RHIBackend:", 0) == 0) {
                    parsedConfig.RHIBackend = Trim(trimmed.substr(11));
                    inPlugins = false;
                }
            }
        }

        if (!sawSchema || parsedConfig.SchemaVersion != ProjectConfig::CurrentSchemaVersion) {
            AXLE_WARN("ProjectSerializer: unsupported or missing project schema in '{0}'", PathToUtf8(filepath));
            return false;
        }
        parsedConfig.SelectedPlugins = std::move(selectedPlugins);
        m_Project->GetConfig() = std::move(parsedConfig);

        m_Project->m_ProjectDirectory = filepath.parent_path();
        if (m_Project->m_ProjectDirectory.empty()) {
            m_Project->m_ProjectDirectory = std::filesystem::current_path();
        }

        AXLE_INFO("ProjectSerializer: Successfully loaded project '{0}' from '{1}'",
            m_Project->GetConfig().Name, PathToUtf8(filepath));
        return true;
    }

}
