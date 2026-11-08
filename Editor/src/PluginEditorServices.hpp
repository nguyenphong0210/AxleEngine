#pragma once
#include <Axle/PluginServices.hpp>
#include <Axle/Entity.hpp>
#include <memory>
namespace Axle {
class PluginEditorServices final {
public:
    struct State;
    PluginEditorServices(PluginServiceSession& runtime, PluginRuntime::RegistrationRegistry& registry);
    ~PluginEditorServices();
    bool Bind(std::string& error);
    void Stop();
    bool DrawComponents(Entity entity);
    bool DrawAddComponents(Entity entity);
private:
    PluginServiceSession& m_Runtime;
    std::shared_ptr<State> m_State;
};
}
