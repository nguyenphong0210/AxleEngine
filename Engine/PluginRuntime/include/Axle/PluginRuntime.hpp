#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Axle::PluginRuntime {

inline constexpr std::uint32_t kPluginAbiVersion = 1;

// This is the only ABI exchanged with a native plugin. Keep it C-compatible:
// no STL types, ownership, exceptions or compiler-generated type information.
struct PluginHostApi {
    std::uint32_t abi_version;
    void* user_data;
    bool (*register_resource)(void* user_data, const char* resource_id,
                             void* resource, void (*destroy)(void*));
};

struct PluginDescriptor {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    const char* module_id;
    const char* build_config;
};

using GetDescriptorFn = const PluginDescriptor* (*)();
using StartFn = bool (*)(const PluginHostApi*);
using StopFn = void (*)();

class RegistrationRegistry final {
public:
    using Owner = std::uint64_t;

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

private:
    struct Entry;
    std::uint64_t m_NextOwner = 1;
    std::vector<std::unique_ptr<Entry>> m_Entries;
};

class NativePlugin final {
public:
    NativePlugin() = default;
    NativePlugin(const NativePlugin&) = delete;
    NativePlugin& operator=(const NativePlugin&) = delete;
    ~NativePlugin();

    bool Load(const std::filesystem::path& module_path, std::string expected_id,
              std::string expected_config, RegistrationRegistry& registry,
              std::string& diagnostic);
    void Stop() noexcept;
    bool IsActive() const noexcept { return m_Active; }
    const std::string& ModuleId() const noexcept { return m_ModuleId; }

private:
    void* m_Handle = nullptr;
    StopFn m_Stop = nullptr;
    RegistrationRegistry* m_Registry = nullptr;
    RegistrationRegistry::Owner m_Owner = 0;
    std::string m_ModuleId;
    bool m_Active = false;
};

} // namespace Axle::PluginRuntime

