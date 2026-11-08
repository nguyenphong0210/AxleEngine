#include <Axle/PluginRuntime.hpp>
#include <Axle/PluginArtifact.hpp>
#include <Axle/UUID.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

using namespace Axle;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " << __LINE__ << ": " << #c << '\n'; return 1; } } while(false)
int main(int argc, char** argv) {
    CHECK(argc == 4);
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" / ("PluginRuntime-" + std::to_string(static_cast<uint64_t>(Axle::UUID{})));
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
    {
        CHECK(good.Load(goodRequest, registry, error));
        auto codeLease = good.GetCodeLease();
        const auto* retainedHost = good.GetHostApi();
        const auto owner = retainedHost->owner_id;
        CHECK(registry.GetOwnerLease(owner) && registry.IsOwnerActive(owner));
        good.Stop();
        CHECK(!registry.IsOwnerActive(owner) && !registry.GetOwnerLease(owner));
        CHECK(!retainedHost->register_resource(retainedHost->user_data, "after-stop", nullptr, nullptr));
        CHECK(!retainedHost->get_service(retainedHost->user_data, 1, 1));
        codeLease.reset();
    }
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
    int serviceCalls = 0;
    auto serviceRequest = goodRequest;
    serviceRequest.ServiceUser = &serviceCalls;
    serviceRequest.GetService = [](void* user, std::uint64_t, std::uint32_t, std::uint32_t) -> const void* {
        ++*static_cast<int*>(user); return user;
    };
    {
        PluginRuntime::NativePlugin serviceModule;
        CHECK(serviceModule.Load(serviceRequest, registry, error));
        const auto* host = serviceModule.GetHostApi();
        CHECK(host->get_service(host->user_data, 1, 1) == &serviceCalls && serviceCalls == 1);
        registry.Rollback(host->owner_id);
        CHECK(!serviceModule.IsActive());
        CHECK(!host->get_service(host->user_data, 1, 1) && serviceCalls == 1);
    }
    {
        PluginRuntime::NativePlugin lease;
        const PluginRuntime::PluginHostApi* host = nullptr;
        { PluginRuntime::RegistrationRegistry shorterRegistry; CHECK(lease.Load(serviceRequest, shorterRegistry, error)); host = lease.GetHostApi(); }
        CHECK(!lease.IsActive() && !host->get_service(host->user_data, 1, 1) && serviceCalls == 1);
        lease.Stop(); CHECK(!lease.IsActive());
    }
#if defined(_WIN32)
    {
        auto pinnedRequest = goodRequest;
        pinnedRequest.ModulePath = root / "pinned.dll";
        pinnedRequest.ReceiptPath = root / "pinned.receipt";
        std::filesystem::copy_file(goodRequest.ModulePath, pinnedRequest.ModulePath);
        CHECK(Plugin::WriteArtifactReceipt(identity, pinnedRequest.ModulePath, pinnedRequest.ReceiptPath, error));
        PluginRuntime::NativePlugin pinned;
        CHECK(pinned.Load(pinnedRequest, registry, error));
        auto lease = pinned.GetCodeLease(); pinned.Stop();
        const auto writer = CreateFileW(pinnedRequest.ModulePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
        CHECK(writer == INVALID_HANDLE_VALUE);
        CHECK(!MoveFileW(pinnedRequest.ModulePath.c_str(), (root / "renamed.dll").c_str()));
        lease.reset();
        CHECK(MoveFileW(pinnedRequest.ModulePath.c_str(), (root / "renamed.dll").c_str()));
    }
    // Teardown must stay on the Engine thread even when the last code-bearing
    // job releases its lease from a worker, or a transferred session stops there.
    for (bool workerStop : {false, true}) {
        PluginRuntime::NativePlugin affine;
        CHECK(affine.Load(goodRequest, registry, error));
        auto retained = affine.GetCodeLease();
        const auto native = GetModuleHandleW(goodRequest.ModulePath.c_str());
        const auto observe = reinterpret_cast<void(*)(std::thread::id*)>(GetProcAddress(native, "ax_fixture_observe_stop"));
        CHECK(observe);
        std::thread::id stopThread;
        observe(&stopThread);
        if (!workerStop) affine.Stop();
        std::thread worker([&, lease = std::move(retained)]() mutable {
            if (workerStop) affine.Stop();
            lease.reset();
            PluginRuntime::NativePlugin::CollectRetiredModules();
        });
        worker.join();
        CHECK(stopThread == std::thread::id{} && GetModuleHandleW(goodRequest.ModulePath.c_str()));
        PluginRuntime::NativePlugin::CollectRetiredModules();
        CHECK(stopThread == std::this_thread::get_id() && registry.Size() == 0);
        CHECK(!GetModuleHandleW(goodRequest.ModulePath.c_str()));
    }
#endif
    std::cout << "Plugin runtime: validated artifact bytes/identity, real configuration handshake, rollback and lifetime PASS\n";
    return 0;
}
