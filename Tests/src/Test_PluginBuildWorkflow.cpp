#include "PluginBuild.hpp"
#include "OwnedProcess.hpp"
#include "PluginSDKConfig.hpp"
#include <Axle/PluginRuntime.hpp>
#include <Axle/PluginSDK.hpp>
#include <Axle/PluginArtifact.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define CHECK(c, message) do { if (!(c)) { std::cerr << "FAIL " << __LINE__ << ": " << message << '\n'; return 1; } } while(false)
namespace {
void Write(const std::filesystem::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }
Axle::PluginBuildRequest Request(const std::filesystem::path& root, const std::filesystem::path& cmake) {
    Axle::PluginBuildRequest request;
    request.CMake = cmake; request.ProjectDirectory = root; request.SdkDirectory = AXLE_PLUGIN_SDK_DIRECTORY;
    request.Generator = AXLE_PLUGIN_CMAKE_GENERATOR;
    request.GeneratorToolset = AXLE_PLUGIN_CMAKE_TOOLSET;
    request.GeneratorInstance = AXLE_PLUGIN_CMAKE_INSTANCE;
    auto catalog = Axle::Plugin::Discover({{root / "Plugins"}, {root / "Plugins"}});
    auto resolution = Axle::Plugin::Resolve(catalog, {"custom.plugin"}, Axle::Plugin::ProductKind::Tests);
    request.Plan = Axle::Plugin::CreateBuildPlan(resolution, Axle::Plugin::ProductKind::Tests,
        AXLE_PLUGIN_BUILD_CONFIGURATION, AXLE_PLUGIN_SDK_FINGERPRINT, AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT);
    return request;
}
Axle::PluginRuntime::LoadRequest Load(const Axle::PluginBuildRequest& request, const std::filesystem::path& generation) {
    Axle::PluginRuntime::LoadRequest load;
    const auto& artifact = request.Plan.Artifacts.front();
    load.ModulePath = generation / (artifact.ModuleName + ".dll");
    load.ReceiptPath = generation / (artifact.ModuleName + ".build.receipt");
    load.Expected.PluginId = artifact.PluginId; load.Expected.ModuleName = artifact.ModuleName;
    load.Expected.Kind = artifact.Kind; load.Expected.Product = request.Plan.Product;
    load.Expected.Configuration = request.Plan.Configuration; load.Expected.SdkFingerprint = request.Plan.SdkFingerprint;
    load.Expected.ToolchainFingerprint = request.Plan.ToolchainFingerprint;
    load.Expected.Fingerprint = request.Plan.Fingerprint; load.Expected.SourceRoot = artifact.SourceRoot;
    return load;
}
}
int Run(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--writer-probe") {
        auto request = Request(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]));
        std::atomic_bool cancel{false}; const auto result = Axle::BuildPlugins(request, cancel);
        return !result.Success && result.Error.find("writer lock") != std::string::npos ? 0 : 1;
    }
    CHECK(argc == 4, "usage: plugin-build-tests cmake fixture-source configuration");
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" /
        (std::filesystem::path(u8"External Plugin \u00E9 \u6E38 ") += std::to_string(static_cast<std::uint64_t>(Axle::UUID{})));
    std::filesystem::create_directories(root / "Plugins/Custom");
    bool done = false;
    struct Cleanup {
        std::filesystem::path Root; bool* Done;
        ~Cleanup() {
            if (!*Done) { std::cerr << "Preserved failed fixture: " << Axle::Plugin::PathUtf8(Root) << '\n'; return; }
            std::error_code ec; std::filesystem::remove_all(Root, ec);
        }
    } cleanup{root, &done};
    const auto plugin = root / "Plugins/Custom";
    std::filesystem::copy(std::filesystem::path(argv[2]), plugin, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    Write(plugin / "Custom.axleplugin", R"({"schema":1,"id":"custom.plugin","version":"1.0.0","name":"External custom plugin","modules":[{"name":"CustomPlugin_Runtime","kind":"runtime","sourceRoot":"Runtime"}]})");
    const auto disabled = root / "Plugins/Disabled";
    std::filesystem::create_directories(disabled);
    Write(disabled / "Disabled.axleplugin", R"({"schema":1,"id":"custom.disabled","version":"1.0.0","name":"Disabled","modules":[{"name":"NeverConfigured","kind":"runtime"}]})");
    Write(disabled / "CMakeLists.txt", R"(file(WRITE "${CMAKE_CURRENT_LIST_DIR}/Executed.flag" "bad")
message(FATAL_ERROR "Unselected plugin configured")
)");
    auto request = Request(root, argv[1]);
    CHECK(request.Plan.IsValid(), "external fixture resolves with a frozen SDK/toolchain plan");
    std::atomic_bool cancel{false};
    auto first = Axle::BuildPlugins(request, cancel);
    CHECK(first.Success && !first.UpToDate, first.Error + " log=" + Axle::Plugin::PathUtf8(first.LogPath));
    CHECK(first.PublishedReceipts.size() == 1 && std::filesystem::is_regular_file(first.LogPath), "real build has one validated receipt and log");
    CHECK(!std::filesystem::exists(disabled / "Executed.flag"), "unselected configure code never executes");
    const auto goodBinary = first.GenerationDirectory / "CustomPlugin_Runtime.dll";
    std::string error;
    const auto goodHash = Axle::Plugin::HashArtifactFile(goodBinary, error);
    const auto goodReceipt = Axle::FileSystem::ReadBinaryFile(first.PublishedReceipts.front());
    CHECK(!goodHash.empty(), "actual native DLL produced");
    Axle::PluginRuntime::RegistrationRegistry registry;
    Axle::PluginRuntime::NativePlugin module;
    CHECK(module.Load(Load(request, first.GenerationDirectory), registry, error), error);
    CHECK(registry.Has("custom.plugin/counter"), "compiled external DLL registers a nontrivial owned resource");
    const auto native = GetModuleHandleW(goodBinary.c_str());
    CHECK(native, "real DLL is loaded");
    const auto live = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_live"));
    const auto sharedTable = reinterpret_cast<const void*(*)()>(GetProcAddress(native, "ax_fixture_runtime_table"));
    CHECK(live && live() == 1 && sharedTable && sharedTable() == AxleRuntimeHost_GetV1(), "external DLL imports canonical shared SDK table");
    auto code = module.GetCodeLease(); module.Stop();
    CHECK(live() == 0 && registry.Size() == 0, "nontrivial plugin destructor runs before its code lease is released");
    code.reset();
    auto noTools = request; noTools.CMake.clear(); noTools.SdkDirectory.clear();
    auto noOp = Axle::BuildPlugins(noTools, cancel);
    CHECK(noOp.Success && noOp.UpToDate && noOp.GenerationDirectory == first.GenerationDirectory, "compatible prebuilt is a no-op without compiler tools");
    Write(plugin / "ConfigureFailure.flag", "fail");
    auto failure = Axle::BuildPlugins(Request(root, argv[1]), cancel);
    CHECK(!failure.Success && failure.ExitCode != 0, "real configure failure is propagated");
    std::filesystem::remove(plugin / "ConfigureFailure.flag");
    Write(plugin / "CompileFailure.flag", "fail");
    failure = Axle::BuildPlugins(Request(root, argv[1]), cancel);
    CHECK(!failure.Success && failure.ExitCode != 0, "real compiler failure is propagated");
    std::filesystem::remove(plugin / "CompileFailure.flag");
    CHECK(Axle::Plugin::HashArtifactFile(goodBinary, error) == goodHash && Axle::FileSystem::ReadBinaryFile(first.PublishedReceipts.front()) == goodReceipt,
        "configure/compiler failures preserve known-good DLL bytes and receipt");
    const auto runtimeSource = plugin / "Runtime/Runtime.cpp";
    const auto originalTime = std::filesystem::last_write_time(runtimeSource);
    auto changedSource = Axle::FileSystem::ReadTextFile(runtimeSource);
    const auto valuePosition = changedSource.find("return 7;");
    CHECK(valuePosition != std::string::npos, "fixture has an observable compiled value");
    changedSource.replace(valuePosition, 9, "return 9;");
    Write(runtimeSource, changedSource);
    std::filesystem::last_write_time(runtimeSource, originalTime);
    request = Request(root, argv[1]);
    auto updated = Axle::BuildPlugins(request, cancel);
    CHECK(updated.Success && !updated.UpToDate && updated.GenerationDirectory != first.GenerationDirectory, updated.Error);
    CHECK(module.Load(Load(request, updated.GenerationDirectory), registry, error), error);
    const auto updatedNative = GetModuleHandleW((updated.GenerationDirectory / "CustomPlugin_Runtime.dll").c_str());
    const auto updatedValue = reinterpret_cast<int(*)()>(GetProcAddress(updatedNative, "ax_fixture_value"));
    CHECK(updatedValue && updatedValue() == 9, "content fingerprint forces compilation even with preserved source mtime");
    module.Stop();
    CHECK(Axle::Plugin::HashArtifactFile(goodBinary, error) == goodHash, "source rebuild does not overwrite active artifact generation");
    const auto commonHeader = plugin / "include/Value.hpp";
    const auto headerTime = std::filesystem::last_write_time(commonHeader);
    auto changedHeader = Axle::FileSystem::ReadTextFile(commonHeader);
    const auto headerValue = changedHeader.find("SharedValue = 11"); CHECK(headerValue != std::string::npos, "shared authored header contains observable value");
    changedHeader.replace(headerValue, 16, "SharedValue = 13"); Write(commonHeader, changedHeader);
    std::filesystem::last_write_time(commonHeader, headerTime);
    const auto sharedRequest = Request(root, argv[1]);
    CHECK(sharedRequest.Plan.Fingerprint != request.Plan.Fingerprint && sharedRequest.Plan.Artifacts.front().Fingerprint != request.Plan.Artifacts.front().Fingerprint,
        "shared include outside Runtime subtree invalidates plan and module fingerprints");
    const auto sharedBuild = Axle::BuildPlugins(sharedRequest, cancel);
    CHECK(sharedBuild.Success && !sharedBuild.UpToDate, sharedBuild.Error);
    CHECK(module.Load(Load(sharedRequest, sharedBuild.GenerationDirectory), registry, error), error);
    const auto sharedNative = GetModuleHandleW((sharedBuild.GenerationDirectory / "CustomPlugin_Runtime.dll").c_str());
    const auto sharedValue = reinterpret_cast<int(*)()>(GetProcAddress(sharedNative, "ax_fixture_shared_value"));
    CHECK(sharedValue && sharedValue() == 13, "shared header with preserved mtime compiles new bytes");
    module.Stop();
    const auto originalCmake = Axle::FileSystem::ReadTextFile(plugin / "CMakeLists.txt");
    Write(plugin / "CMakeLists.txt", "add_custom_target(CustomPlugin_Runtime)\n");
    const auto noOutput = Axle::BuildPlugins(Request(root, argv[1]), cancel);
    CHECK(!noOutput.Success && noOutput.PublishedReceipts.empty(), "successful no-output target cannot publish a stale DLL with a fresh receipt");
    Write(plugin / "CMakeLists.txt", originalCmake);
    Write(plugin / "SnapshotHold.flag", "hold");
    const auto snapshotRequest = Request(root, argv[1]);
    auto snapshotBuild = std::async(std::launch::async, [&] { return Axle::BuildPlugins(snapshotRequest, cancel); });
    bool held = false;
    const auto holdDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!held && std::chrono::steady_clock::now() < holdDeadline) {
        for (const auto& entry : std::filesystem::directory_iterator(root / "Intermediate/Plugins")) {
            if (entry.path().extension() == ".log" && Axle::FileSystem::ReadTextFile(entry.path()).find("Snapshot fixture compilation held") != std::string::npos) held = true;
        }
        if (!held) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(held, "configure reads isolated source snapshot before live source mutation");
    auto transientSource = changedSource;
    transientSource.replace(transientSource.find("return 9;"), 9, "return 4;");
    Write(runtimeSource, transientSource);
    const auto frozenResult = snapshotBuild.get();
    Write(runtimeSource, changedSource);
    std::filesystem::last_write_time(runtimeSource, originalTime);
    // Publication rejects a currently changed installation, while the actual
    // compiler output must still contain only the frozen inputs.
    CHECK(!frozenResult.Success && frozenResult.Error.find("changed during") != std::string::npos, "changed installation cannot publish a build");
    const auto snapshotOutput = root / "Intermediate/Plugins/W" /
        (snapshotRequest.Plan.SdkFingerprint.substr(0, 16) + "-" + snapshotRequest.Plan.Configuration + "-tests/o/CustomPlugin_Runtime.dll");
    const auto snapshotNative = LoadLibraryW(snapshotOutput.c_str());
    CHECK(snapshotNative, "frozen compiler output is a native DLL");
    const auto snapshotValue = reinterpret_cast<int(*)()>(GetProcAddress(snapshotNative, "ax_fixture_value"));
    const bool frozenValue = snapshotValue && snapshotValue() == 9;
    FreeLibrary(snapshotNative);
    CHECK(frozenValue, "concurrent authored source mutation cannot enter frozen compiler output");
    std::filesystem::remove(plugin / "SnapshotHold.flag");
    Write(plugin / "SlowBuild.flag", "slow");
    auto slowRequest = Request(root, argv[1]);
    std::atomic_bool backgroundCancel{false};
    auto pending = std::async(std::launch::async, [&] { return Axle::BuildPlugins(slowRequest, backgroundCancel); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto lock = root / "Intermediate/Plugins/.build.lock";
    while (!std::filesystem::exists(lock) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(std::filesystem::exists(lock), "background builder acquired project writer lock");
    auto sameProcess = Axle::BuildPlugins(slowRequest, cancel);
    CHECK(!sameProcess.Success && sameProcess.Error.find("Simultaneous") != std::string::npos, "same-process writer rejected");
    const auto child = Axle::RunOwnedProcess(std::filesystem::absolute(argv[0]),
        {L"--writer-probe", root.wstring(), std::filesystem::path(argv[1]).wstring()}, root, root / "second-writer.log", cancel);
    CHECK(child.ExitCode == 0, "separate process writer rejected by disk lock");
    const auto cancelStart = std::chrono::steady_clock::now(); backgroundCancel = true;
    CHECK(pending.wait_for(std::chrono::seconds(8)) == std::future_status::ready, "cancellation terminates configure/build process tree promptly");
    const auto cancelled = pending.get();
    CHECK(!cancelled.Success && cancelled.Cancelled, "owned process cancellation is reported");
    CHECK(std::chrono::steady_clock::now() - cancelStart < std::chrono::seconds(8) && !std::filesystem::exists(lock), "cancelled process releases writer lock");
    CHECK(Axle::Plugin::HashArtifactFile(goodBinary, error) == goodHash && Axle::FileSystem::ReadBinaryFile(first.PublishedReceipts.front()) == goodReceipt,
        "cancelled source build preserves original artifact and receipt");
    auto invalid = request; invalid.Plan.Diagnostics.push_back({{}, "invalid selection"});
    CHECK(!Axle::BuildPlugins(invalid, cancel).Success, "invalid plans cannot report success");
    done = true;
    std::cout << "Real external DLL build, SDK imports, no-op, immutable generations, configure/compiler failures, Unicode/spaces, cancellation and cross-process lock: PASS\n";
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try { return Run(argc, argv); }
    catch (const std::exception& error) { std::cerr << "Build fixture exception: " << error.what() << '\n'; return 1; }
}
