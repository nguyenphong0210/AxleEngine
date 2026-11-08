#include <Axle/Plugin.hpp>
#include <Axle/PluginRuntime.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/FileSystem.hpp>
#include "PluginBuild.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: AxleOpt06ReviewProbes good-module isolated-data-directory\n";
        return 2;
    }
    const auto root = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(root / "a");
    std::filesystem::create_directories(root / "b");
    int observed = 0;
    auto probe = [&](const char* name, bool reproduced) {
        std::cout << name << ": " << (reproduced ? "FAIL (defect reproduced)" : "PASS") << '\n';
        observed += reproduced;
    };
    auto write = [](const auto& path, const auto& text) { std::ofstream(path) << text; };
    const std::string manifest = R"({"schema":1,"id":"probe.shared","name":"Shared","version":"1.0.0","engineApiVersion":999,"platforms":["unsupported-platform"],"modules":[{"name":"Shared","kind":"runtime"}]})";
    write(root / "a/a.axleplugin", manifest);
    write(root / "b/b.axleplugin", manifest);
    auto catalog = Axle::Plugin::Discover({{root / "a", root / "b"}});
    auto resolution = Axle::Plugin::Resolve(catalog, {"probe.shared"}, Axle::Plugin::ProductKind::Editor);
    probe("duplicate catalog ID accepted by resolver", !catalog.IsValid() && resolution.IsValid());
    catalog = Axle::Plugin::Discover({{root / "a"}});
    resolution = Axle::Plugin::Resolve(catalog, {"probe.shared"}, Axle::Plugin::ProductKind::Editor);
    probe("unsupported API/platform accepted", resolution.IsValid());

    write(root / "schema-only.receipt", "schema=1\n");
    Axle::PluginRuntime::RegistrationRegistry registrations;
    Axle::PluginRuntime::NativePlugin plugin;
    std::string diagnostic;
    Axle::PluginRuntime::LoadRequest loadRequest;
    loadRequest.ModulePath = std::filesystem::absolute(argv[1]); loadRequest.ReceiptPath = root / "schema-only.receipt";
    loadRequest.Expected.PluginId = "fixture.counter"; loadRequest.Expected.ModuleName = "fixture.counter";
    loadRequest.Expected.Configuration = "Debug"; loadRequest.Expected.SdkFingerprint = "fixture-sdk";
    loadRequest.Expected.ToolchainFingerprint = "fixture-toolchain"; loadRequest.Expected.Fingerprint = "fixture-plan";
    loadRequest.Expected.SourceRoot = root;
    const bool loaded = plugin.Load(loadRequest, registrations, diagnostic);
    probe("schema-only receipt loads native DLL", loaded);
    plugin.Stop();

    write(root / "Fake.dll", "not a native DLL");
    write(root / "Fake.build.receipt", "schema=1\nplugin_id=probe.shared\nmodule_name=Fake\nfingerprint=fp\nsdk_fingerprint=sdk\n");
    Axle::PluginBuildRequest request;
    request.ProjectDirectory = root;
    request.BuildDirectory = root / "generated";
    request.Plan.Configuration = "Debug";
    request.Plan.SdkFingerprint = "sdk"; request.Plan.ToolchainFingerprint = "toolchain";
    request.Plan.Fingerprint = "fp";
    request.Plan.Artifacts.push_back({"probe.shared", "Fake", Axle::Plugin::ModuleKind::Runtime, root, root, {}});
    std::atomic_bool cancel{false};
    auto build = Axle::BuildPlugins(request, cancel);
    probe("receipt missing configuration accepted as prebuilt", build.Success && build.UpToDate);
    request.Plan.Artifacts.clear();
    request.Plan.Diagnostics.push_back({{}, "invalid selection"});
    build = Axle::BuildPlugins(request, cancel);
    probe("invalid build plan reports success", build.Success);

    Axle::Scene scene;
    auto entity = scene.CreateEntity("NestedPluginData");
    scene.StorePassiveSection(entity, "CustomPluginComponent", {"      Value: 4", "      Children:", "        Label: nested", "      Tail: keep"});
    const auto assemblyPath = root / "Nested.axle";
    if (!Axle::AssemblySerializer::Serialize(assemblyPath, entity)) return 3;
    const auto data = Axle::AssemblySerializer::LoadAssemblyData(assemblyPath);
    bool retained = data.IsValid && data.RootNode.PassiveSections.size() == 1 && data.RootNode.PassiveSections[0].Lines.size() == 4;
    probe("nested plugin Children data lost in assembly", !retained);

    auto& codecs = Axle::ComponentCodecRegistry::Get();
    struct ProbeComponent {};
    auto add = [&](auto owner, const char* name) { codecs.RegisterCodec(owner, std::make_unique<Axle::FunctionalComponentCodec<ProbeComponent, void(*)(const Axle::ComponentCopyContext&)>>(name, [](const Axle::ComponentCopyContext&) {})); };
    const auto ownerA = codecs.BeginOwner(), ownerB = codecs.BeginOwner();
    add(ownerA, "ProbeA"); add(ownerB, "ProbeB");
    codecs.UnregisterOwner(ownerA);
    if (!codecs.HasCodec("ProbeB")) return 3;
    codecs.UnregisterOwner(ownerB);
    probe("scoped codec removal leaves callbacks registered", codecs.HasCodec("ProbeB"));
    codecs.UnregisterOwner(ownerA);
    std::cout << "Observed " << observed << "/7 defects. OPT-06 requires zero failures.\n";
    return observed == 0 ? 0 : 1;
}
