#include "RuntimeComposition.hpp"
#include <stdexcept>
#include <Axle/SceneDataDocument.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/ComponentCodec.hpp>
#include <Axle/Entity.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/Path.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <algorithm>
#include <sstream>
namespace Axle {
void ValidateCompiledRuntimeContent(const std::filesystem::path& assets) {
    std::vector<std::filesystem::path> documents;
    if (const auto* package = FileSystem::GetMountedPackage()) {
        for (const auto& [name, entry] : package->GetEntries()) documents.push_back(assets / PathFromUtf8(name));
    } else {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assets)) if (entry.is_regular_file()) documents.push_back(entry.path());
    }
    for (const auto& path : documents) {
        auto extension = PathToUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension != ".axlescene" && extension != ".axle" && extension != ".axleasset") continue;
        const auto text = FileSystem::ReadTextFile(path);
        std::string error;
        if (text.find("AxlePlugin_") != std::string::npos && !ValidatePluginAssetDocument(path, error)) throw std::runtime_error(error);
        if (extension != ".axlescene" && !AssemblySerializer::IsAssemblyFile(path)) continue;
        SceneData document;
        if (!SceneDataDocument::Deserialize(text, document)) throw std::runtime_error("Invalid cooked scene document");
        for (auto entity : document.GetRegistry().view<PassiveExtensionComponent>())
            for (const auto& section : document.GetRegistry().get<PassiveExtensionComponent>(entity).Sections) {
                if (section.SectionName != "NativeScriptComponent") continue;
                for (const auto& line : section.Lines) {
                    const auto first = line.find_first_not_of(" \t\r\n");
                    if (first == std::string::npos || line.compare(first, 11, "ScriptName:") != 0) continue;
                    const auto begin = line.find_first_not_of(" \t", first + 11);
                    if (begin == std::string::npos) continue;
                    const auto name = line.substr(begin, line.find_last_not_of(" \t\r\n") - begin + 1);
                    if (!name.empty() && !ScriptRegistry::Has(name)) throw std::runtime_error("Required project script is absent from the compiled player: " + name);
                }
            }
    }
}
RuntimeComposition::RuntimeComposition() {
    std::string error;
    if (!m_Services.Bind(error)) throw std::runtime_error(error);
    try {
        RegisterRuntimeModules(m_Registry, m_Services, m_Modules);
        m_Services.RehydrateScenes();
    } catch (...) {
        while (!m_Modules.empty()) { m_Modules.back()->AbortStartup(); m_Modules.pop_back(); }
        m_Services.Stop();
        throw;
    }
}
RuntimeComposition::~RuntimeComposition() {
    m_Services.Quiesce();
    while (!m_Modules.empty()) { m_Modules.back()->Stop(); m_Modules.pop_back(); }
    m_Services.Stop();
    PluginRuntime::NativePlugin::CollectRetiredModules();
}
}
