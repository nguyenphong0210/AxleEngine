#include "ProjectPluginCreator.hpp"
#include <Axle/Plugin.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/UUID.hpp>
#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Axle {
namespace {
std::string Lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    return text;
}
void Require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
bool Inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto relative = std::filesystem::weakly_canonical(path).lexically_relative(std::filesystem::canonical(root));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void Replace(std::string& text, std::string_view token, const std::string& value) {
    std::size_t offset = 0;
    while ((offset = text.find(token,offset)) != std::string::npos) {
        text.replace(offset,token.size(),value); offset += value.size();
    }
}
}
std::filesystem::path ProjectPluginTemplateDirectory() {
    std::wstring executable(32768,L'\0');
    const auto size = GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if (!size || size >= executable.size()) return {};
    executable.resize(size);
    return std::filesystem::path(executable).parent_path()/"Resources/ProjectPlugin";
}
bool CreateProjectPlugin(const ProjectPluginCreateRequest& request,
    std::filesystem::path& createdDirectory, std::string& error) {
    createdDirectory.clear(); error.clear();
    struct CreationLock {
        HANDLE File = INVALID_HANDLE_VALUE;
        ~CreationLock() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); }
    } lock;
    struct Staging {
        std::filesystem::path Path;
        ~Staging() { if (!Path.empty()) { std::error_code ec; std::filesystem::remove_all(Path,ec); } }
    } staging;
    try {
        Require(request.Id.size() <= 120 && std::regex_match(request.Id,std::regex("[a-z][a-z0-9]*([.-][a-z0-9]+)*")),
            "Use a lowercase plugin ID, for example project.myplugin.");
        Require(request.Module.size() <= 64 && std::regex_match(request.Module,std::regex("[A-Za-z][A-Za-z0-9_]*")),
            "Folder/module must start with a letter and contain only letters, numbers or underscores.");
        const auto lower = Lower(request.Module);
        Require(!std::regex_match(lower,std::regex("con|prn|aux|nul|com[1-9]|lpt[1-9]")), "Choose a folder name that is not a Windows reserved name.");
        Require(!request.Name.empty() && request.Name.size() <= 128 &&
            std::any_of(request.Name.begin(),request.Name.end(),[](unsigned char c) { return c > ' '; }) &&
            std::none_of(request.Name.begin(),request.Name.end(),[](unsigned char c) { return c < ' ' || c == '"' || c == '\\' || c == '@'; }),
            "Enter a display name without quotes, backslashes, control characters or @.");
        const auto projectFile = std::filesystem::canonical(request.ProjectFile);
        auto project = std::make_shared<Project>();
        Require(std::filesystem::is_regular_file(projectFile) && ProjectSerializer(project).Deserialize(projectFile),
            "Save Project As before creating a plugin.");
        const auto root = projectFile.parent_path();
        const auto plugins = root/"Plugins";
        const auto temporary = root/"Intermediate/Plugins/Creation";
        Require(Inside(plugins,root) && Inside(temporary,root), "Project plugin directories must stay inside the project.");
        const auto destination = plugins/request.Module;
        std::filesystem::create_directories(temporary.parent_path());
        lock.File = CreateFileW((temporary.parent_path()/"CreatePlugin.lock").c_str(),
            GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        Require(lock.File != INVALID_HANDLE_VALUE,"Another Editor is creating a plugin, or the project is read-only. Try again when it finishes.");
        Require(!std::filesystem::exists(destination), "That plugin folder already exists. Choose another folder/module.");
        Plugin::DiscoveryOptions options{{plugins},{plugins}};
        if (!request.EnginePluginRoot.empty()) options.Roots.push_back(request.EnginePluginRoot);
        const auto catalog = Plugin::Discover(options);
        for (const auto& plugin : catalog.Plugins) {
            Require(plugin.Id != request.Id,"That plugin ID already exists. Choose another ID.");
            for (const auto& module : plugin.Modules)
                Require(Lower(module.Name) != lower+"_runtime" && Lower(module.Name) != lower+"_editor",
                    "That module name is already used by another plugin.");
        }
        Require(std::filesystem::is_directory(request.TemplateDirectory), "Plugin template is missing. Rebuild or reinstall the Editor.");
        std::vector<std::pair<std::filesystem::path,std::string>> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(request.TemplateDirectory)) {
            Require(!entry.is_symlink(),"Plugin template must not contain symbolic links.");
            if (!entry.is_regular_file()) continue;
            std::ifstream input(entry.path(),std::ios::binary);
            Require(input.is_open(),"Could not read the plugin template.");
            std::string text((std::istreambuf_iterator<char>(input)),{});
            Require(!input.bad(),"Could not read the plugin template.");
            Replace(text,"@PLUGIN_ID@",request.Id); Replace(text,"@MODULE@",request.Module); Replace(text,"@PLUGIN_NAME@",request.Name);
            auto relative = entry.path().lexically_relative(request.TemplateDirectory);
            if (relative == "Plugin.axleplugin.in") relative = request.Module+".axleplugin";
            files.emplace_back(relative,std::move(text));
        }
        for (const auto& required : {std::filesystem::path("CMakeLists.txt"),std::filesystem::path(request.Module+".axleplugin"),
            std::filesystem::path("Runtime/Mover.cpp"),std::filesystem::path("Editor/Inspector.cpp")})
            Require(std::any_of(files.begin(),files.end(),[&](const auto& file) { return file.first == required; }),"Plugin template is incomplete. Rebuild or reinstall the Editor.");
        std::filesystem::create_directories(temporary);
        const auto candidate = temporary/std::to_string(static_cast<std::uint64_t>(UUID{}));
        Require(std::filesystem::create_directory(candidate),"Could not reserve a plugin creation directory.");
        staging.Path = candidate;
        for (const auto& [relative,text] : files) {
            const auto target = staging.Path/relative;
            std::filesystem::create_directories(target.parent_path());
            std::ofstream output(target,std::ios::binary);
            output.write(text.data(),static_cast<std::streamsize>(text.size())); output.close();
            Require(!output.fail(),"Could not write plugin files. Check the project folder permissions.");
        }
        const auto staged = Plugin::Discover({{staging.Path},{staging.Path}});
        Require(staged.IsValid() && staged.Plugins.size()==1 && staged.Plugins[0].Id==request.Id,
            "Generated plugin metadata is invalid; no plugin was published.");
        std::filesystem::create_directories(plugins);
        // No replacement, including a folder created concurrently by another Editor.
        Require(MoveFileExW(staging.Path.c_str(),destination.c_str(),0)!=0,
            "Could not publish the plugin. Its folder may already exist or be read-only.");
        staging.Path.clear(); createdDirectory = destination; return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
