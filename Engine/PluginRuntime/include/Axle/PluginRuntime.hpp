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
    void* ServiceUser = nullptr;
    const void* (*GetService)(void*, std::uint64_t owner, std::uint32_t service_id, std::uint32_t minimum_size) = nullptr;
    std::shared_ptr<void> ServiceLifetime;
    void (*OwnerInitialized)(void*, std::uint64_t owner, bool succeeded) = nullptr;
};

struct StaticModule {
    GetDescriptorFn Descriptor = nullptr;
    StartFn Start = nullptr;
    StopFn Stop = nullptr;
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
    std::shared_ptr<void> GetOwnerLease(Owner owner) const;
    bool IsOwnerActive(Owner owner) const;
    static bool OwnerIsActive(const std::shared_ptr<State>& state, Owner owner);
    static std::shared_ptr<void> LeaseFor(const std::shared_ptr<State>& state, Owner owner);
    static std::string PluginIdFor(const std::shared_ptr<State>& state, Owner owner);
    static bool RegisterFor(const std::shared_ptr<State>& state, Owner owner, const char* id, void* resource, void (*destroy)(void*));

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
    bool LoadStatic(const LoadRequest& request, const StaticModule& module,
                    RegistrationRegistry& registry, std::string& diagnostic);
    void Stop() noexcept;
    bool AbortStartup() noexcept;
    // Pump on the session owner thread each frame and before host shutdown.
    // Worker releases never invoke plugin teardown or the native loader.
    static void CollectRetiredModules() noexcept;
    bool IsActive() const noexcept;
    const std::string& ModuleId() const noexcept { return m_ModuleId; }
    std::shared_ptr<void> GetCodeLease() const { return m_Module; }
    const PluginHostApi* GetHostApi() const noexcept;

private:
    bool LoadImpl(const LoadRequest&, const StaticModule*, RegistrationRegistry&, std::string&);
    struct ModuleState;
    std::shared_ptr<ModuleState> m_Module;
    std::string m_ModuleId;
    bool m_Active = false;
};

} // namespace Axle::PluginRuntime

