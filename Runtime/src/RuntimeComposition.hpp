#pragma once
#include <Axle/PluginServices.hpp>
#include <string>
#include <vector>
namespace Axle {
std::vector<std::string> CompiledRuntimeSelection();
std::string CompiledRuntimeSnapshot();
void RegisterRuntimeModules(PluginRuntime::RegistrationRegistry&, PluginServiceSession&,
                            std::vector<std::unique_ptr<PluginRuntime::NativePlugin>>&);
void InitializeCompiledScripts();
void ValidateCompiledRuntimeContent(const std::filesystem::path& assets);
class RuntimeComposition final {
public:
    RuntimeComposition();
    ~RuntimeComposition();
    void Drain() { m_Services.Drain(); }
private:
    PluginRuntime::RegistrationRegistry m_Registry;
    PluginServiceSession m_Services{m_Registry};
    std::vector<std::unique_ptr<PluginRuntime::NativePlugin>> m_Modules;
};
}
