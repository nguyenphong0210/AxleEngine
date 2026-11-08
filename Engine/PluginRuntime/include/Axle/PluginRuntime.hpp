#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <Axle/PluginABI.hpp>
#include <Axle/PluginArtifact.hpp>

namespace Axle::PluginRuntime {

struct LoadRequest {
    std::filesystem::path ModulePath;
    std::filesystem::path ReceiptPath;
    Plugin::ArtifactIdentity Expected;
};

class RegistrationRegistry final {
public:
    using Owner = std::uint64_t;
    struct State;

    RegistrationRegistry();
    RegistrationRegistry(const RegistrationRegistry&) = delete;
    RegistrationRegistry& operator=(const RegistrationRegistry&) = delete;
    ~RegistrationRegistry();

    Owner BeginOwner();
    bool Register(Owner owner, const char* resource_id, void* resource,
                  void (*destroy)(void*));
    void Rollback(Owner owner) noexcept;
    bool Has(const std::string& resource_id) const;
    std::size_t Size() const noexcept;

    std::shared_ptr<State> GetState() const noexcept { return m_State; }

private:
    std::shared_ptr<State> m_State;
    friend class NativePlugin;
};

class NativePlugin final {
public:
    NativePlugin() = default;
    NativePlugin(const NativePlugin&) = delete;
    NativePlugin& operator=(const NativePlugin&) = delete;
    ~NativePlugin();

    bool Load(const LoadRequest& request, RegistrationRegistry& registry, std::string& diagnostic);
    void Stop() noexcept;
    bool IsActive() const noexcept { return m_Active; }
    const std::string& ModuleId() const noexcept { return m_ModuleId; }

private:
    void* m_Handle = nullptr;
    StopFn m_Stop = nullptr;
    std::shared_ptr<RegistrationRegistry::State> m_State;
    RegistrationRegistry::Owner m_Owner = 0;
    std::string m_ModuleId;
    bool m_Active = false;
    PluginHostApi m_Host{};
    std::string m_SdkFingerprint;
};

} // namespace Axle::PluginRuntime

