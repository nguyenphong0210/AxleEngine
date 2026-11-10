#include <cstddef>
#include <thread>

#include <Axle/PluginABI.hpp>
using HostApi = Axle::PluginRuntime::PluginHostApi;
using Descriptor = Axle::PluginRuntime::PluginDescriptor;
namespace {
int* g_resource = nullptr;
std::thread::id* g_stopThread = nullptr;
void Destroy(void* resource) { delete static_cast<int*>(resource); g_resource = nullptr; }
}

#if defined(_WIN32)
#define AXLE_EXPORT extern "C" __declspec(dllexport)
#else
#define AXLE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

AXLE_EXPORT const Descriptor* ax_plugin_get_descriptor() {
    static const Descriptor descriptor{sizeof(Descriptor), Axle::PluginRuntime::kPluginAbiVersion,
        "fixture.counter", "fixture.counter", AXLE_PLUGIN_BUILD_CONFIGURATION, "fixture-sdk", "fixture-toolchain"};
    return &descriptor;
}

AXLE_EXPORT bool ax_plugin_start(const HostApi* host) {
#if defined(AXLE_FIXTURE_FAIL)
    if (!host->register_resource(host->user_data, "fixture.before-failure", new int(7), Destroy)) return false;
    return false;
#else
    g_resource = new int(42);
    return host->register_resource(host->user_data, "fixture.counter", g_resource, Destroy);
#endif
}

AXLE_EXPORT void ax_fixture_observe_stop(std::thread::id* output) { g_stopThread = output; }
AXLE_EXPORT void ax_fixture_render_work(void* user, void (*emit)(void*, unsigned)) { emit(user, 0xC1106001u); }
AXLE_EXPORT void ax_plugin_stop() { if (g_stopThread) *g_stopThread = std::this_thread::get_id(); }

