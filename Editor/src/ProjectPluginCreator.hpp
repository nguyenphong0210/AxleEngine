#pragma once
#include <filesystem>
#include <string>

namespace Axle {
struct ProjectPluginCreateRequest {
    std::filesystem::path ProjectFile, TemplateDirectory, EnginePluginRoot;
    std::string Name, Id, Module;
};
std::filesystem::path ProjectPluginTemplateDirectory();
bool CreateProjectPlugin(const ProjectPluginCreateRequest& request,
    std::filesystem::path& createdDirectory, std::string& error);
}
