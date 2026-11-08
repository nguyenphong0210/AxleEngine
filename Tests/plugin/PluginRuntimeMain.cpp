#include <Axle/PluginRuntime.hpp>
#include <Axle/PluginArtifact.hpp>
#include <Axle/UUID.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Axle;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " << __LINE__ << ": " << #c << '\n'; return 1; } } while(false)
int main(int argc, char** argv) {
    CHECK(argc == 4);
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" / ("PluginRuntime-" + std::to_string(static_cast<uint64_t>(UUID{})));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path Root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Root, ec); } } cleanup{root};
    Plugin::ArtifactIdentity identity;
    identity.PluginId = "fixture.counter"; identity.ModuleName = "fixture.counter";
    identity.Configuration = argv[3]; identity.SdkFingerprint = "fixture-sdk";
    identity.ToolchainFingerprint = "fixture-toolchain"; identity.Fingerprint = "fixture-plan"; identity.SourceRoot = root;
    std::string error;
    PluginRuntime::LoadRequest goodRequest{std::filesystem::absolute(argv[1]), root / "good.receipt", identity};
    PluginRuntime::LoadRequest failRequest{std::filesystem::absolute(argv[2]), root / "fail.receipt", identity};
    CHECK(Plugin::WriteArtifactReceipt(identity, goodRequest.ModulePath, goodRequest.ReceiptPath, error));
    CHECK(Plugin::WriteArtifactReceipt(identity, failRequest.ModulePath, failRequest.ReceiptPath, error));
    PluginRuntime::RegistrationRegistry registry;
    PluginRuntime::NativePlugin good;
    CHECK(good.Load(goodRequest, registry, error));
    CHECK(good.IsActive() && registry.Has("fixture.counter"));
    good.Stop(); CHECK(!good.IsActive() && registry.Size() == 0);
    PluginRuntime::NativePlugin failing;
    CHECK(!failing.Load(failRequest, registry, error));
    CHECK(registry.Size() == 0);
    for (const auto& field : {"plugin", "module", "config", "sdk", "toolchain", "fingerprint"}) {
        auto request = goodRequest;
        if (std::string(field) == "plugin") request.Expected.PluginId = "other.plugin";
        if (std::string(field) == "module") request.Expected.ModuleName = "other.module";
        if (std::string(field) == "config") request.Expected.Configuration = "incompatible";
        if (std::string(field) == "sdk") request.Expected.SdkFingerprint = "wrong-sdk";
        if (std::string(field) == "toolchain") request.Expected.ToolchainFingerprint = "wrong-toolchain";
        if (std::string(field) == "fingerprint") request.Expected.Fingerprint = "wrong-plan";
        PluginRuntime::NativePlugin rejected;
        CHECK(!rejected.Load(request, registry, error) && registry.Size() == 0);
    }
    // A well-formed sidecar must also agree with the loaded descriptor.
    auto wrongDescriptor = goodRequest;
    wrongDescriptor.Expected.SdkFingerprint = "wrong-sdk";
    wrongDescriptor.ReceiptPath = root / "wrong-sdk.receipt";
    CHECK(Plugin::WriteArtifactReceipt(wrongDescriptor.Expected, goodRequest.ModulePath, wrongDescriptor.ReceiptPath, error));
    PluginRuntime::NativePlugin mismatch;
    CHECK(!mismatch.Load(wrongDescriptor, registry, error));
    CHECK(error == "plugin SDK mismatch");
    auto partial = goodRequest;
    partial.ReceiptPath = root / "partial.receipt";
    std::ofstream(partial.ReceiptPath) << "schema=1\n";
    CHECK(!mismatch.Load(partial, registry, error));
    partial.ReceiptPath.clear(); CHECK(!mismatch.Load(partial, registry, error));
    {
        PluginRuntime::NativePlugin lease;
        { PluginRuntime::RegistrationRegistry shorterRegistry; CHECK(lease.Load(goodRequest, shorterRegistry, error)); }
        lease.Stop(); CHECK(!lease.IsActive());
    }
    std::cout << "Plugin runtime: validated artifact bytes/identity, real configuration handshake, rollback and lifetime PASS\n";
    return 0;
}
