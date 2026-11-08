#include <Axle/PluginRuntime.hpp>

#include <filesystem>
#include <fstream>
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

    // Registry lifetime lease test: registry destroyed before NativePlugin
    {
        NativePlugin leasePlugin;
        {
            RegistrationRegistry localRegistry;
            if (!Check(leasePlugin.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Debug", localRegistry, diagnostic), "lease load")) return 1;
            if (!Check(localRegistry.Size() == 1, "local registry has entry")) return 1;
        }
        leasePlugin.Stop();
        if (!Check(!leasePlugin.IsActive(), "lease plugin stopped safely after registry destruction")) return 1;
    }

    // Build receipt validation tests
    const auto tempDir = std::filesystem::temp_directory_path() / "axle_plugin_runtime_test";
    std::filesystem::create_directories(tempDir);
    const auto validReceipt = tempDir / "valid.receipt";
    const auto badSchemaReceipt = tempDir / "bad_schema.receipt";
    const auto mismatchReceipt = tempDir / "mismatch.receipt";
    {
        std::ofstream stream(validReceipt);
        stream << "schema=1\nplugin_id=fixture.counter\nconfiguration=Debug\n";
    }
    {
        std::ofstream stream(badSchemaReceipt);
        stream << "schema=99\nplugin_id=fixture.counter\nconfiguration=Debug\n";
    }
    {
        std::ofstream stream(mismatchReceipt);
        stream << "schema=1\nplugin_id=other.plugin\nconfiguration=Debug\n";
    }

    NativePlugin receiptPlugin;
    if (!Check(receiptPlugin.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Debug", registry, diagnostic, validReceipt), "load with valid receipt")) return 1;
    receiptPlugin.Stop();

    NativePlugin badSchemaPlugin;
    if (!Check(!badSchemaPlugin.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Debug", registry, diagnostic, badSchemaReceipt), "bad schema receipt rejected")) return 1;

    NativePlugin mismatchPlugin;
    if (!Check(!mismatchPlugin.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Debug", registry, diagnostic, mismatchReceipt), "mismatch receipt rejected")) return 1;

    NativePlugin nonExistentReceiptPlugin;
    if (!Check(!nonExistentReceiptPlugin.Load(std::filesystem::absolute(argv[1]), "fixture.counter", "Debug", registry, diagnostic, tempDir / "missing.receipt"), "missing receipt rejected")) return 1;

    std::filesystem::remove_all(tempDir);

    std::cout << "PluginRuntimeTests: PASS\n";
    return 0;
}
