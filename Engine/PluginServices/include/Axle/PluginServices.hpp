#pragma once
#include <Axle/PluginRuntime.hpp>
#include <Axle/PluginSDK.hpp>
#include <memory>
#include <string>
namespace Axle {
class Entity;
class PluginServiceSession final {
public:
    struct State;
    explicit PluginServiceSession(PluginRuntime::RegistrationRegistry& registry);
    ~PluginServiceSession();
    PluginServiceSession(const PluginServiceSession&) = delete;
    PluginServiceSession& operator=(const PluginServiceSession&) = delete;
    bool Bind(std::string& error);
    void Attach(PluginRuntime::LoadRequest& request) const;
    void Drain();
    void RehydrateScenes();
    void Stop();
    const SDK::RuntimeServices& Services() const;
    void SetEditorServices(const SDK::EditorServices* services, std::shared_ptr<void> lifetime);
private:
    std::shared_ptr<State> m_State;
};
}
