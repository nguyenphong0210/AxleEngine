#include "PluginBuild.hpp"

#include <Axle/FileSystem.hpp>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << "FAIL: " << message << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: AxlePluginBuildWorkflowTests <CMake> <BuildDir> <Config>\n";
        return 1;
    }

    const std::filesystem::path cmakePath = argv[1];
    const std::filesystem::path buildDir = argv[2];
    const std::string config = argv[3];
    const auto outputDir = buildDir / "output";

    std::error_code ec;
    std::filesystem::remove_all(outputDir, ec);
    std::filesystem::create_directories(outputDir, ec);

    Axle::PluginBuildItem item;
    item.PluginId = "custom.plugin";
    item.ModuleName = "CustomPlugin_Runtime";
    item.Kind = Axle::Plugin::ModuleKind::Runtime;
    item.SourceRoot = buildDir / "src";
    item.Fingerprint = "fp_initial_1234";
    item.SdkFingerprint = "sdk_test_5678";

    Axle::PluginBuildRequest request;
    request.CMake = cmakePath;
    request.BuildDirectory = buildDir;
    request.BinaryDirectory = outputDir;
    request.Configuration = config;
    request.TargetName = "CustomPlugin_Runtime";
    request.Items.push_back(item);

    const auto failureFlag = buildDir / "FailBuild.flag";
    const auto slowFlag = buildDir / "SlowBuild.flag";
    std::filesystem::remove(failureFlag);
    std::filesystem::remove(slowFlag);

    std::atomic_bool cancel{ false };

    // Test 1: Successful initial build and receipt publishing
    auto result = Axle::BuildPlugins(request, cancel);
    CHECK(result.Success && result.ExitCode == 0, result.Error);
    CHECK(!result.UpToDate, "Initial build is not up-to-date");
    CHECK(std::filesystem::is_regular_file(result.LogPath), "Build log was captured");
    CHECK(!result.PublishedReceipts.empty(), "Receipt was published");

    const auto receiptPath = outputDir / "CustomPlugin_Runtime.build.receipt";
    CHECK(std::filesystem::is_regular_file(receiptPath), "Receipt exists on disk");

    std::string receiptInitial;
    {
        std::ifstream receiptIn(receiptPath);
        receiptInitial = std::string((std::istreambuf_iterator<char>(receiptIn)), std::istreambuf_iterator<char>());
    }
    CHECK(receiptInitial.find("schema=1") != std::string::npos, "Receipt has schema=1");
    CHECK(receiptInitial.find("plugin_id=custom.plugin") != std::string::npos, "Receipt has plugin_id");
    CHECK(receiptInitial.find("fingerprint=fp_initial_1234") != std::string::npos, "Receipt has initial fingerprint");

    // Test 2: Prebuilt No-Op
    auto noOpResult = Axle::BuildPlugins(request, cancel);
    CHECK(noOpResult.Success && noOpResult.UpToDate, "Prebuilt with matching receipt is an up-to-date no-op");

    // Test 3: Stale source fingerprint triggers rebuild
    request.Items[0].Fingerprint = "fp_updated_9999";
    auto updateResult = Axle::BuildPlugins(request, cancel);
    CHECK(updateResult.Success && !updateResult.UpToDate, updateResult.Error);

    std::string receiptUpdated;
    {
        std::ifstream updatedReceiptIn(receiptPath);
        receiptUpdated = std::string((std::istreambuf_iterator<char>(updatedReceiptIn)), std::istreambuf_iterator<char>());
    }
    CHECK(receiptUpdated.find("fingerprint=fp_updated_9999") != std::string::npos, "Updated receipt has new fingerprint");

    // Test 4: Compiler failure preserves last-known-good receipt
    std::ofstream(failureFlag) << "fail";
    request.Items[0].Fingerprint = "fp_failing_0000";
    auto failResult = Axle::BuildPlugins(request, cancel);
    std::filesystem::remove(failureFlag);
    CHECK(!failResult.Success && failResult.ExitCode != 0, "Build fails when flag set");

    std::string receiptAfterFail;
    {
        std::ifstream failedReceiptIn(receiptPath);
        receiptAfterFail = std::string((std::istreambuf_iterator<char>(failedReceiptIn)), std::istreambuf_iterator<char>());
    }
    CHECK(receiptAfterFail == receiptUpdated, "Failed build preserves last-known-good receipt");

    // Test 5: Cancellation terminates process promptly and preserves receipt
    std::ofstream(slowFlag) << "slow";
    const auto startTime = std::chrono::steady_clock::now();
    auto pending = std::async(std::launch::async, [&] { return Axle::BuildPlugins(request, cancel); });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cancel.store(true);
    CHECK(pending.wait_for(std::chrono::seconds(8)) == std::future_status::ready, "Cancellation stops promptly");
    auto cancelResult = pending.get();
    std::filesystem::remove(slowFlag);
    CHECK(!cancelResult.Success && cancelResult.Error.find("cancelled") != std::string::npos, "Cancellation reported");

    std::string receiptAfterCancel;
    {
        std::ifstream cancelledReceiptIn(receiptPath);
        receiptAfterCancel = std::string((std::istreambuf_iterator<char>(cancelledReceiptIn)), std::istreambuf_iterator<char>());
    }
    CHECK(receiptAfterCancel == receiptUpdated, "Cancelled build preserves last-known-good receipt");
    CHECK(std::chrono::steady_clock::now() - startTime < std::chrono::seconds(10), "Cancellation did not wait full duration");

    // Test 6: Simultaneous build rejection
    cancel.store(false);
    std::atomic_bool backgroundStarted{ false };
    std::atomic_bool releaseBackground{ false };
    std::atomic_bool backgroundCancel{ false };
    std::ofstream(slowFlag) << "slow";

    auto bgTask = std::async(std::launch::async, [&] {
        backgroundStarted.store(true);
        return Axle::BuildPlugins(request, backgroundCancel);
    });

    while (!Axle::IsPluginBuildInProgress()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto simultaneousResult = Axle::BuildPlugins(request, cancel);
    CHECK(!simultaneousResult.Success, "Simultaneous call must fail");
    CHECK(simultaneousResult.Error.find("Simultaneous plugin build rejected") != std::string::npos,
          "Simultaneous call returns rejection reason");

    backgroundCancel.store(true);
    bgTask.wait();
    std::filesystem::remove(slowFlag);

    std::cout << "Plugin build workflow (no-op, stale rebuild, failure rollback, cancellation, lock): PASS\n";
    return 0;
}
