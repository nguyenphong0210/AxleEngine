#include <cstddef>

struct HostApi {
    unsigned int abi_version;
    void* user_data;
    bool (*register_resource)(void*, const char*, void*, void (*)(void*));
};
struct Descriptor {
    unsigned int struct_size;
    unsigned int abi_version;
    const char* module_id;
    const char* build_config;
};

namespace {
int* g_resource = nullptr;
void Destroy(void* resource) { delete static_cast<int*>(resource); g_resource = nullptr; }
}

#if defined(_WIN32)
#define AXLE_EXPORT extern "C" __declspec(dllexport)
#else
#define AXLE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

AXLE_EXPORT const Descriptor* ax_plugin_get_descriptor() {
    static const Descriptor descriptor{sizeof(Descriptor), 1, "fixture.counter", "Debug"};
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

AXLE_EXPORT void ax_plugin_stop() {}

