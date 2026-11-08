#include <Axle/PluginRuntime.hpp>
#include <Axle/PluginSDK.hpp>
#include <iostream>
using namespace Axle::PluginRuntime;
namespace {
int destroyed = 0, stopped = 0;
bool failStart = false;
PluginDescriptor descriptor{sizeof(PluginDescriptor), kPluginAbiVersion, "test.static", "static-module", "test", "sdk", "toolchain"};
const PluginDescriptor* Describe() { return &descriptor; }
bool Start(const PluginHostApi* host) {
    if (!host->register_resource(host->user_data, "owned.static", new int(7), [](void* value) { delete static_cast<int*>(value); ++destroyed; })) return false;
    return !failStart;
}
void Stop() { ++stopped; }
}
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL line " << __LINE__ << '\n'; return 1; } } while(false)
int main() {
    RegistrationRegistry registry;
    LoadRequest request;
    request.Expected.Product = Axle::Plugin::ProductKind::Runtime;
    request.Expected.PluginId = "test.static"; request.Expected.ModuleName = "static-module";
    request.Expected.Configuration = "test"; request.Expected.SdkFingerprint = "sdk"; request.Expected.ToolchainFingerprint = "toolchain";
    NativePlugin module;
    std::string error;
    CHECK(module.LoadStatic(request, {Describe, Start, Stop}, registry, error));
    CHECK(module.IsActive() && registry.Has("owned.static"));
    auto lease = module.GetCodeLease();
    module.Stop();
    CHECK(!module.IsActive() && registry.Size() == 0 && destroyed == 1 && stopped == 0);
    lease.reset(); NativePlugin::CollectRetiredModules();
    CHECK(stopped == 1);
    failStart = true;
    CHECK(!module.LoadStatic(request, {Describe, Start, Stop}, registry, error));
    CHECK(registry.Size() == 0 && destroyed == 2 && stopped == 2);
    failStart = false; descriptor.abi_version = 99;
    CHECK(!module.LoadStatic(request, {Describe, Start, Stop}, registry, error));
    CHECK(destroyed == 2 && stopped == 2);
    std::cout << "Static module identity, ownership, delayed stop and startup rollback: PASS\n";
}
