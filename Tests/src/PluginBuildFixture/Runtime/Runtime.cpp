#include <Axle/PluginSDK.hpp>
#include "Value.hpp"
#include <memory>
#include <string>
#ifdef AXLE_FIXTURE_COMPILE_FAILURE
#error Intentional external plugin compiler failure
#endif
namespace {
int live = 0;
struct Counter {
    std::string Name = "nontrivial project-owned payload";
    Counter() { ++live; }
    ~Counter() { --live; }
};
void Destroy(void* value) { delete static_cast<Counter*>(value); }
}
AXLE_PLUGIN_DESCRIPTOR(AXLE_PLUGIN_ID, AXLE_PLUGIN_MODULE_NAME)
AXLE_PLUGIN_EXPORT bool ax_plugin_start(const Axle::PluginRuntime::PluginHostApi* host) noexcept {
    try {
        if (!host || host->struct_size != sizeof(*host) || host->abi_version != Axle::PluginRuntime::kPluginAbiVersion) return false;
        auto object = std::make_unique<Counter>();
        const auto id = std::string(AXLE_PLUGIN_ID) + "/counter";
        if (!host->register_resource(host->user_data, id.c_str(), object.get(), Destroy)) return false;
        object.release(); return true;
    } catch (...) { return false; }
}
AXLE_PLUGIN_EXPORT void ax_plugin_stop() noexcept {}
AXLE_PLUGIN_EXPORT int ax_fixture_live() noexcept { return live; }
AXLE_PLUGIN_EXPORT int ax_fixture_value() noexcept { return 7; }
AXLE_PLUGIN_EXPORT int ax_fixture_shared_value() noexcept { return SharedValue; }
AXLE_PLUGIN_EXPORT const void* ax_fixture_runtime_table() noexcept { return AxleRuntimeHost_GetV1(); }
