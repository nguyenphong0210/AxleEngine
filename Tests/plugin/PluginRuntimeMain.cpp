#include <Axle/PluginRuntime.hpp>

#include <filesystem>
#include <iostream>
#include <string>

using Axle::PluginRuntime::NativePlugin;
using Axle::PluginRuntime::RegistrationRegistry;

static bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "[PLUGIN RUNTIME FAIL] " << message << '\n';
    return condition;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: PluginRuntimeTests good-module fail-module\n"; return 2; }
    RegistrationRegistry registry;
    NativePlugin good;
    std::string diagnostic;
    if (!Check(good.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Debug", registry, diagnostic), diagnostic.c_str())) return 1;
    if (!Check(good.IsActive() && good.ModuleId() == "fixture.counter", "good plugin is active")) return 1;
    if (!Check(registry.Size() == 1 && registry.Has("fixture.counter"), "good plugin registration exists")) return 1;
    good.Stop();
    if (!Check(!good.IsActive() && registry.Size() == 0, "stop removes owner registrations")) return 1;

    NativePlugin failing;
    if (!Check(!failing.Load(std::filesystem::absolute(argv[2]), "fixture.counter", "Debug", registry, diagnostic), "failing plugin is rejected")) return 1;
    if (!Check(registry.Size() == 0, "failed start rolls back registrations")) return 1;

    NativePlugin wrong_config;
    if (!Check(!wrong_config.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Release", registry, diagnostic), "configuration mismatch is rejected")) return 1;
    if (!Check(registry.Size() == 0, "descriptor rejection has no registration")) return 1;
    std::cout << "PluginRuntimeTests: PASS\n";
    return 0;
}

