#include "TestWorkspace.hpp"
#include "RuntimeBuild.hpp"
#include <Axle/AssetCooker.hpp>
#include <Axle/FileSystem.hpp>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
    Axle::Tests::InitializeWorkspace();
    if (argc == 2 && std::string(argv[1]) == "--real-runtime") {
        std::atomic_bool cancel{false};
        const auto result = Axle::BuildRuntime(Axle::GetRuntimeBuildRequest(), cancel);
        std::cout << "Runtime build log: " << result.LogPath << '\n';
        CHECK(result.Success && result.ExitCode == 0, result.Error);
        std::cout << "Configured Runtime build through the Editor's production builder: PASS\n";
        return 0;
    }
    CHECK(argc == 4, "Expected CMake, fixture build directory and configuration");
    Axle::RuntimeBuildRequest request{argv[1], argv[2], std::filesystem::path(argv[2]) / "output", argv[3]};
    std::atomic_bool cancel{false};
    const auto failure = request.BuildDirectory / "FailBuild.flag";
    const auto slow = request.BuildDirectory / "SlowBuild.flag";
    std::filesystem::remove(failure);
    std::filesystem::remove(slow);
    auto result = Axle::BuildRuntime(request, cancel);
    CHECK(result.Success && result.ExitCode == 0, result.Error);
    CHECK(std::filesystem::is_regular_file(result.LogPath), "Build output is captured to a log");
    CHECK(std::filesystem::is_regular_file(result.ReceiptPath), "Successful build publishes a receipt");
    std::ifstream receipt(result.ReceiptPath);
    const std::string receiptBefore((std::istreambuf_iterator<char>(receipt)), std::istreambuf_iterator<char>());
    CHECK(receiptBefore.find("schema=1") != std::string::npos, "Build receipt has a schema marker");

    std::ofstream(failure) << "fail";
    result = Axle::BuildRuntime(request, cancel);
    std::filesystem::remove(failure);
    CHECK(!result.Success && result.ExitCode != 0, "A failed build must fail even when stale executable files exist");
    std::ifstream failedReceipt(result.ReceiptPath);
    const std::string receiptAfterFailure((std::istreambuf_iterator<char>(failedReceipt)), std::istreambuf_iterator<char>());
    CHECK(receiptAfterFailure == receiptBefore, "Failed build preserves the last-known-good receipt");

    std::ofstream(slow) << "slow";
    const auto started = std::chrono::steady_clock::now();
    auto pending = std::async(std::launch::async, [&] { return Axle::BuildRuntime(request, cancel); });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cancel.store(true);
    CHECK(pending.wait_for(std::chrono::seconds(8)) == std::future_status::ready, "Cancellation stops the build tree promptly");
    result = pending.get();
    std::filesystem::remove(slow);
    CHECK(!result.Success && result.Error.find("cancelled") != std::string::npos, "Cancellation is reported");
    std::ifstream cancelledReceipt(result.ReceiptPath);
    const std::string receiptAfterCancel((std::istreambuf_iterator<char>(cancelledReceipt)), std::istreambuf_iterator<char>());
    CHECK(receiptAfterCancel == receiptBefore, "Cancelled build preserves the last-known-good receipt");
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10), "No wait for the slow build's full duration");

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto assets = request.BuildDirectory / ("fixture-assets-" + suffix);
    std::filesystem::create_directories(assets);
    std::ofstream(assets / "fixture.txt") << "authored fixture";
    Axle::FileSystem::SetAssetDirectory(assets);
    Axle::CookSettings settings;
    settings.AssetDirectory = assets;
    settings.OutputDirectory = request.BuildDirectory / ("package-" + suffix);
    settings.IncludeShaders = false;
    settings.BakeMeshesToBinary = false;
    settings.ExecutableDirectory = request.ExecutableDirectory;
    auto cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(cooked.Success && cooked.RuntimeCopied && cooked.ScriptsCopied, cooked.ErrorMessage);
    std::filesystem::remove(request.ExecutableDirectory / "AxleRuntime.exe");
    cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(!cooked.Success, "Explicit output selection cannot fall back to an unrelated old Runtime");
    settings.CopyExecutables = false;
    cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(cooked.Success && !cooked.RuntimeCopied, "Asset-only export needs no Runtime build");
    const auto package = cooked.PackagePath;
    std::ifstream savedPackage(package, std::ios::binary);
    const std::string beforePreflight((std::istreambuf_iterator<char>(savedPackage)), {});
    savedPackage.close();
    std::ofstream(assets / "Required.AXLESCENE") << "Entities:\n  - Entity: 1\n    AxlePlugin_73747564696f2e6d6f7665722f4d6f766572:\n      Required: true\n";
    cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(!cooked.Success && cooked.ErrorMessage.find("preflight") != std::string::npos, "Missing plugin-aware preflight rejects native data before output mutation");
    std::ifstream rejectedPackage(package, std::ios::binary);
    CHECK(std::string((std::istreambuf_iterator<char>(rejectedPackage)), {}) == beforePreflight, "Rejected plugin data preserves the published package");
    settings.ValidatePluginDocument = [](const auto&, std::string& error) { error = "Required plugin schema is unavailable"; return false; };
    cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(!cooked.Success && cooked.ErrorMessage.find("schema") != std::string::npos, "Composition validator failure is propagated");
    settings.ValidatePluginDocument = [](const auto&, std::string&) { return true; };
    cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(cooked.Success, "Validated plugin documents can be cooked as data");
    settings.AssetDirectory = request.BuildDirectory / ("missing-assets-" + suffix);
    cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(!cooked.Success && cooked.ErrorMessage.find("unavailable") != std::string::npos, "Explicit missing asset root cannot fall back to a different unchecked tree");
    std::cout << "Build success/failure/cancellation and exact-output packaging: PASS\n";
    return 0;
}
