#pragma once
#include <cstdint>

// Windows x64 uses the platform C calling convention. No ownership, exceptions,
// STL, vendor objects or compiler RTTI cross this boundary. All strings are UTF-8,
// NUL-terminated, borrowed and valid until module/session teardown. Start must
// join initialization work before returning; failure transfers no work to host.
namespace Axle::PluginRuntime {
inline constexpr std::uint32_t kPluginAbiVersion = 2;
struct PluginHostApi {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    void* user_data;
    std::uint64_t owner_id;
    const char* sdk_fingerprint;
    bool (*register_resource)(void*, const char*, void*, void (*)(void*));
    const void* (*get_service)(void*, std::uint32_t service_id, std::uint32_t minimum_size);
};
struct PluginDescriptor {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    const char* plugin_id;
    const char* module_id;
    const char* build_config;
    const char* sdk_fingerprint;
    const char* toolchain_fingerprint;
};
using GetDescriptorFn = const PluginDescriptor* (*)();
using StartFn = bool (*)(const PluginHostApi*);
using StopFn = void (*)();
}
