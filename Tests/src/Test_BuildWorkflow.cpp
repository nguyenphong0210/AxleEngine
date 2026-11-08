#include "TestWorkspace.hpp"
#include "RuntimeBuild.hpp"
#include <Axle/AssetCooker.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/UUID.hpp>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)
static void Write(const std::filesystem::path& file, const std::string& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary); out << bytes;
}
static std::string Read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary); return {(std::istreambuf_iterator<char>(in)), {}};
}
int main(int argc, char** argv) {
    Axle::Tests::InitializeWorkspace();
    CHECK(argc == 4, "Expected CMake, fixture workspace and configuration");
    const auto root = std::filesystem::absolute(argv[2]) / ("product-" + std::to_string(static_cast<std::uint64_t>(Axle::UUID())));
    const auto engine = root / "engine", project = root / "project", control = root / "control";
    std::filesystem::create_directories(engine / "Engine");
    std::filesystem::create_directories(engine / "Runtime");
    std::filesystem::create_directories(control);
    Write(engine / "Engine/Resources/Shaders/fixture.hlsl", "// frozen shader fixture\n");
    Write(engine / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.22)\nproject(ProductFixture NONE)\nadd_custom_target(AxleRuntime COMMAND \"${CMAKE_COMMAND}\" \"-DOUTPUT=${AXLE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>\" \"-DFLAGS=" + control.generic_string() + "\" -P \"${CMAKE_SOURCE_DIR}/ThirdParty/WriteRuntime.cmake\" VERBATIM)\n");
    Write(engine / "ThirdParty/WriteRuntime.cmake", "if(EXISTS \"${FLAGS}/fail\")\nmessage(FATAL_ERROR \"Intentional compiler failure\")\nendif()\nif(EXISTS \"${FLAGS}/slow\")\nexecute_process(COMMAND \"${CMAKE_COMMAND}\" -E sleep 20)\nendif()\nfile(MAKE_DIRECTORY \"${OUTPUT}\")\nfile(COPY \"" + std::filesystem::path(AXLE_TEST_RUNTIME_EXE).generic_string() + "\" DESTINATION \"${OUTPUT}\")\nfile(COPY \"" + std::filesystem::path(AXLE_TEST_RUNTIME_HOST).generic_string() + "\" DESTINATION \"${OUTPUT}\")\n");
    const std::string config = "Project:\n  SchemaVersion: 1\n  Name: Fixture\n  AssetDirectory: assets\n  Plugins:\n";
    Write(project / "Project.axleproj", config);
    Write(project / "assets/fixture.txt", "authored content");
    Axle::RuntimeBuildRequest request = Axle::GetRuntimeBuildRequest();
    request.CMake = argv[1]; request.EngineSourceDirectory = engine; request.ProjectPath = project / "Project.axleproj"; request.Configuration = argv[3];
    std::string error;
    CHECK(Axle::FreezeRuntimeBuildRequest(request, error), error);
    CHECK(Axle::ValidateRuntimeBuildRequest(request, error), error);
    Write(project / "assets/fixture.txt", "changed content");
    CHECK(!Axle::ValidateRuntimeBuildRequest(request, error), "Content changes invalidate a frozen request");
    Write(project / "assets/fixture.txt", "authored content");
    Write(project / "Project.axleproj", config + "    - missing.plugin\n");
    CHECK(!Axle::ValidateRuntimeBuildRequest(request, error), "Selection changes invalidate the request");
    Write(project / "Project.axleproj", config);
    std::atomic_bool cancel{false};
    const auto built = Axle::BuildRuntime(request, cancel);
    CHECK(built.Success && built.ExitCode == 0, built.Error + " " + built.LogPath.string());
    CHECK(Read(built.ReceiptPath).find("schema=2") != std::string::npos, "Receipt binds exact static product");
    Axle::CookSettings settings; settings.OutputDirectory = project / "Export"; settings.BakeMeshesToBinary = false; settings.IncludeShaders = false;
    CHECK(Axle::BindRuntimeExport(request, built, settings, error), error);
    auto cooked = Axle::AssetCooker::CookProject(settings);
    CHECK(cooked.Success && cooked.RuntimeCopied && !cooked.ScriptsCopied, cooked.ErrorMessage);
    CHECK(!std::filesystem::exists(settings.OutputDirectory / "AxleScripts.dll"), "Static exports do not ship script DLLs");
    const auto goodPackage = Read(cooked.PackagePath), goodPlayer = Read(settings.OutputDirectory / "AxleRuntime.exe");
    Axle::Package package; CHECK(package.Open(cooked.PackagePath), "Read cooked package");
    CHECK(package.ReadEntryAsText("fixture.txt") == "authored content", "Cooked payload uses frozen bytes"); package.Close();
    Write(built.GenerationDirectory / "project/assets/fixture.txt", "mixed generation");
    CHECK(!Axle::AssetCooker::CookProject(settings).Success, "Modified frozen data is rejected");
    CHECK(Read(cooked.PackagePath) == goodPackage, "Rejected generation preserves successful package");
    Write(built.GenerationDirectory / "project/assets/fixture.txt", "authored content");
    Write(built.GenerationDirectory / "project/assets/injected.axlebin", "unrelated binary");
    CHECK(!Axle::AssetCooker::CookProject(settings).Success, "Untracked binary caches cannot enter a frozen package");
    std::filesystem::remove(built.GenerationDirectory / "project/assets/injected.axlebin");
    const auto playerBytes = Read(built.ExecutableDirectory / "AxleRuntime.exe");
    Write(built.ExecutableDirectory / "AxleRuntime.exe", "stale artifact");
    CHECK(!Axle::AssetCooker::CookProject(settings).Success, "Stale artifact receipt is rejected");
    Write(built.ExecutableDirectory / "AxleRuntime.exe", playerBytes);
    auto overlap = settings; overlap.OutputDirectory = project / "assets/Export";
    CHECK(!Axle::BindRuntimeExport(request, built, overlap, error), "Original authored asset roots remain protected");
    overlap.OutputDirectory = project / "Scripts/Export";
    CHECK(!Axle::BindRuntimeExport(request, built, overlap, error), "Authored script roots remain protected");
    overlap.OutputDirectory = engine / "Engine/Export";
    CHECK(!Axle::BindRuntimeExport(request, built, overlap, error), "Authored Engine source roots remain protected");
    auto dataOnly = settings; dataOnly.OutputDirectory = project / "CookOnly"; dataOnly.Preflight = {};
    CHECK(Axle::BindRuntimeCook(request, dataOnly, error), error);
    CHECK(Axle::AssetCooker::CookProject(dataOnly).Success, "Asset-only cook uses a frozen product snapshot without a compiler");
    Write(dataOnly.ShaderDirectory / "injected.hlsl", "untracked shader");
    CHECK(!Axle::AssetCooker::CookProject(dataOnly).Success, "Asset-only resource inventory rejects injected shaders");
    std::filesystem::remove(dataOnly.ShaderDirectory / "injected.hlsl");
    auto collision = settings; collision.OutputDirectory = project / "foreign"; Write(collision.OutputDirectory / "mine.txt", "owned by user");
    CHECK(!Axle::AssetCooker::CookProject(collision).Success && Read(collision.OutputDirectory / "mine.txt") == "owned by user", "Foreign output collision is preserved");
    auto copyFailure = settings; copyFailure.Preflight = {};
    copyFailure.ExecutableDirectory = root / "missing-player";
    CHECK(!Axle::AssetCooker::CookProject(copyFailure).Success && Read(cooked.PackagePath) == goodPackage, "Copy preflight failure preserves export");
    auto lateFailure = settings; int checks = 0;
    lateFailure.Preflight = [&](std::string& diagnostic) { if (++checks == 2) { diagnostic = "Content changed before commit"; return false; } return true; };
    CHECK(!Axle::AssetCooker::CookProject(lateFailure).Success && Read(cooked.PackagePath) == goodPackage, "Late failure rolls back staged export");
    Write(control / "fail", "fail");
    const auto failed = Axle::BuildRuntime(request, cancel);
    CHECK(!failed.Success && failed.ExitCode != 0 && Read(settings.OutputDirectory / "AxleRuntime.exe") == goodPlayer, "Compiler failure preserves successful export");
    std::filesystem::remove(control / "fail"); Write(control / "slow", "slow");
    auto pending = std::async(std::launch::async, [&] { return Axle::BuildRuntime(request, cancel); });
    std::this_thread::sleep_for(std::chrono::seconds(2)); cancel = true;
    CHECK(pending.wait_for(std::chrono::seconds(8)) == std::future_status::ready, "Cancellation kills owned process tree promptly");
    CHECK(!pending.get().Success && Read(cooked.PackagePath) == goodPackage, "Cancellation preserves successful export");
    Write(project / "Project.axleproj", "Project:\n  SchemaVersion: 1\n  AssetDirectory: assets\n  StartScene: missing.axlescene\n");
    CHECK(!Axle::FreezeRuntimeBuildRequest(request, error), "Missing required startup content is rejected before compiler dispatch");
    Write(project / "Plugins/Content/Payload/fixture.dat", "selected plugin content");
    Write(project / "Plugins/Content/Content.axleplugin", R"({"schema":1,"id":"test.content","name":"Content","version":"1.0.0","modules":[{"name":"ContentData","kind":"content","sourceRoot":"Payload"}]})");
    Write(project / "Project.axleproj", config + "    - test.content\n");
    CHECK(Axle::FreezeRuntimeBuildRequest(request, error), error);
    Axle::CookSettings contentOnly; contentOnly.OutputDirectory = project / "ContentCook"; contentOnly.IncludeShaders = false;
    CHECK(Axle::BindRuntimeCook(request, contentOnly, error), error);
    cooked = Axle::AssetCooker::CookProject(contentOnly); CHECK(cooked.Success, cooked.ErrorMessage);
    CHECK(package.Open(cooked.PackagePath), "Open content-only package");
    CHECK(package.ReadEntryAsText("Plugins/test.content/fixture.dat") == "selected plugin content", "Selected plugin content is mounted in an asset-only cook"); package.Close();
    std::cout << "Frozen selection/content, static receipt, transactional failures/collision/cancellation: PASS\n";
    return 0;
}
