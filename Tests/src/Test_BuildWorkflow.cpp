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

    std::ofstream(failure) << "fail";
    result = Axle::BuildRuntime(request, cancel);
    std::filesystem::remove(failure);
    CHECK(!result.Success && result.ExitCode != 0, "A failed build must fail even when stale executable files exist");

    std::ofstream(slow) << "slow";
    const auto started = std::chrono::steady_clock::now();
    auto pending = std::async(std::launch::async, [&] { return Axle::BuildRuntime(request, cancel); });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cancel.store(true);
    CHECK(pending.wait_for(std::chrono::seconds(8)) == std::future_status::ready, "Cancellation stops the build tree promptly");
    result = pending.get();
    std::filesystem::remove(slow);
    CHECK(!result.Success && result.Error.find("cancelled") != std::string::npos, "Cancellation is reported");
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10), "No wait for the slow build's full duration");

    const auto assets = request.BuildDirectory / "fixture-assets";
    std::filesystem::create_directories(assets);
    std::ofstream(assets / "fixture.txt") << "authored fixture";
    Axle::FileSystem::SetAssetDirectory(assets);
    Axle::CookSettings settings;
    settings.AssetDirectory = assets;
    settings.OutputDirectory = request.BuildDirectory / "package";
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
    std::cout << "Build success/failure/cancellation and exact-output packaging: PASS\n";
    return 0;
}
