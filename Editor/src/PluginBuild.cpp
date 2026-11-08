#include "PluginBuild.hpp"
#include "OwnedProcess.hpp"
#include <Axle/PluginArtifact.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace Axle {
namespace {
std::atomic_bool building{false};
struct BuildLock { bool Owned = !building.exchange(true); ~BuildLock() { if (Owned) building = false; } };
std::string QuoteText(const std::string& text) {
    std::string equals = "=";
    while (text.find("]" + equals + "]") != std::string::npos) equals += '=';
    return "[" + equals + "[" + text + "]" + equals + "]";
}
std::string QuotePath(const std::filesystem::path& path) { return QuoteText(Plugin::PathUtf8(path)); }
Plugin::ArtifactIdentity Identity(const Plugin::BuildPlan& plan, const Plugin::BuildArtifact& artifact) {
    Plugin::ArtifactIdentity id;
    id.PluginId = artifact.PluginId; id.ModuleName = artifact.ModuleName; id.Kind = artifact.Kind;
    id.Product = plan.Product; id.Configuration = plan.Configuration; id.SdkFingerprint = plan.SdkFingerprint;
    id.ToolchainFingerprint = plan.ToolchainFingerprint; id.Fingerprint = plan.Fingerprint; id.SourceRoot = artifact.SourceRoot;
    return id;
}
bool ValidateGeneration(const Plugin::BuildPlan& plan, const std::filesystem::path& directory, std::vector<std::filesystem::path>& receipts) {
    std::string error;
    for (const auto& artifact : plan.Artifacts) {
        if (artifact.Kind == Plugin::ModuleKind::Content) continue;
        const auto receipt = directory / (artifact.ModuleName + ".build.receipt");
        if (!Plugin::ValidateArtifact(Identity(plan, artifact), directory / (artifact.ModuleName + ".dll"), receipt, error)) return false;
        receipts.push_back(receipt);
    }
    return true;
}
bool Within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    std::error_code ec;
    const auto normalizedParent = std::filesystem::weakly_canonical(parent, ec);
    if (ec) return false;
    const auto normalizedChild = std::filesystem::weakly_canonical(child, ec);
    if (ec) return false;
    const auto relative = normalizedChild.lexically_relative(normalizedParent);
    return !relative.empty() && relative != "." && *relative.begin() != "..";
}
bool FreezeSources(const Plugin::BuildPlan& plan, const std::filesystem::path& snapshot,
                   Plugin::BuildPlan& graph, std::string& error) {
    graph = plan;
    std::map<std::filesystem::path, std::filesystem::path> roots;
    for (const auto& plugin : plan.FrozenResolution.Plugins)
        roots.emplace(plugin.Root.lexically_normal(), snapshot / plugin.Id);
    std::set<std::filesystem::path> files;
    for (const auto& input : plan.Inputs) {
        auto root = std::find_if(roots.begin(), roots.end(), [&](const auto& pair) { return Within(input.Path, pair.first); });
        if (root == roots.end()) { error = "Frozen input is outside its plugin installation"; return false; }
        const auto destination = root->second / input.Path.lexically_relative(root->first);
        if (!Within(destination, snapshot)) { error = "Invalid frozen input destination"; return false; }
        files.insert(destination.lexically_normal());
        // Preserve timestamps only when the exact frozen bytes remain unchanged.
        // The authored files are never passed to the compiler or CMake.
        if (std::filesystem::is_regular_file(destination) && FileSystem::ReadTextFile(destination) == input.Content) continue;
        std::filesystem::create_directories(destination.parent_path());
        if (!FileSystem::WriteTextFileAtomically(destination, input.Content, &error)) return false;
    }
    if (std::filesystem::exists(snapshot)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(snapshot)) {
            if (entry.is_symlink()) { error = "Unexpected link in owned source snapshot"; return false; }
            if (entry.is_regular_file() && !files.contains(entry.path().lexically_normal())) {
                if (!Within(entry.path(), snapshot)) { error = "Invalid snapshot cleanup path"; return false; }
                std::filesystem::remove(entry.path());
            }
        }
    }
    for (auto& root : graph.CMakeRoots) root = roots.at(root.lexically_normal());
    for (auto& artifact : graph.Artifacts) {
        const auto original = artifact.CMakeRoot.lexically_normal();
        artifact.SourceRoot = roots.at(original) / artifact.SourceRoot.lexically_relative(original);
        artifact.CMakeRoot = roots.at(original);
    }
    return true;
}
}
bool IsPluginBuildInProgress() { return building.load(); }
PluginBuildResult BuildPlugins(const PluginBuildRequest& request, const std::atomic_bool& cancel) {
    PluginBuildResult result;
    BuildLock memoryLock;
    if (!memoryLock.Owned) { result.Error = "Simultaneous plugin build rejected"; return result; }
    const auto& frozen = request.Plan;
    if (!frozen.IsValid() || frozen.Fingerprint.empty() || frozen.Configuration.empty() || frozen.SdkFingerprint.empty() ||
        frozen.ToolchainFingerprint.empty() || frozen.Product == Plugin::ProductKind::Runtime) {
        result.Error = "A complete validated native product plan is required"; return result;
    }
    if (frozen.Configuration != "Debug" && frozen.Configuration != "Release" &&
        frozen.Configuration != "RelWithDebInfo" && frozen.Configuration != "MinSizeRel") {
        result.Error = "Unsupported native plugin build configuration"; return result;
    }
    const auto plan = Plugin::CreateBuildPlan(frozen.FrozenResolution, frozen.Product, frozen.Configuration, frozen.SdkFingerprint, frozen.ToolchainFingerprint);
    if (!plan.IsValid() || plan.Fingerprint != frozen.Fingerprint) { result.Error = "Plugin sources differ from the frozen request"; return result; }
    if (cancel.load()) { result.Cancelled = true; result.Error = "Plugin build cancelled"; return result; }
    const bool hasNative = std::any_of(plan.Artifacts.begin(), plan.Artifacts.end(), [](const auto& a) { return a.Kind != Plugin::ModuleKind::Content; });
    if (!hasNative) { result.Success = result.UpToDate = true; result.ExitCode = 0; return result; }
    if (request.ProjectDirectory.empty() || !request.ProjectDirectory.is_absolute()) { result.Error = "A saved absolute project root is required"; return result; }
#ifndef _WIN32
    result.Error = "Native plugin builds require Windows"; return result;
#else
    try {
        const auto root = std::filesystem::absolute(request.BuildDirectory.empty()
            ? request.ProjectDirectory / "Intermediate/Plugins" : request.BuildDirectory);
        const auto generations = root / "Generations";
        std::filesystem::create_directories(generations);
        struct DiskLock {
            HANDLE Handle = INVALID_HANDLE_VALUE;
            ~DiskLock() { if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle); }
        } diskLock;
        diskLock.Handle = CreateFileW((root / ".build.lock").c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (diskLock.Handle == INVALID_HANDLE_VALUE) { result.Error = "Simultaneous plugin build rejected by project writer lock"; return result; }
        const auto index = root / (plan.Fingerprint + ".generation");
        std::ifstream indexStream(index, std::ios::binary);
        const std::string cachedText((std::istreambuf_iterator<char>(indexStream)), std::istreambuf_iterator<char>());
        if (!cachedText.empty()) {
            const auto cached = std::filesystem::u8path(cachedText);
            if (Within(cached, generations) && ValidateGeneration(plan, cached, result.PublishedReceipts)) {
                result.Success = result.UpToDate = true; result.ExitCode = 0; result.GenerationDirectory = cached; return result;
            }
            result.PublishedReceipts.clear();
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(request.CMake, ec) ||
            !std::filesystem::is_regular_file(request.SdkDirectory / "AxlePluginSDK.cmake", ec)) {
            result.Error = "CMake and matching native SDK package are required"; return result;
        }
        const auto stamp = std::to_string(static_cast<std::uint64_t>(UUID{}));
        if (plan.SdkFingerprint.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) {
            result.Error = "Invalid SDK cache key"; return result;
        }
        // Leave path budget for MSBuild/FileTracker's nested scratch and log
        // filenames, including for Unicode projects outside the Engine tree.
        const auto work = root / "W" / (plan.SdkFingerprint.substr(0, 16) + "-" + plan.Configuration + "-" + Plugin::ToString(plan.Product));
        const auto source = work / "s", build = work / "b", output = work / "o";
        const auto stage = root / "Staging" / stamp;
        std::filesystem::create_directories(source); std::filesystem::create_directories(output);
        result.LogPath = root / ("build-" + stamp + ".log");
        std::string error;
        Plugin::BuildPlan snapshotGraph;
        if (!FreezeSources(plan, work / "i", snapshotGraph, error) ||
            !Plugin::GenerateCMakeInputs(snapshotGraph, source / "SelectedPlugins.cmake", error)) { result.Error = error; return result; }
        std::ostringstream bootstrap;
        bootstrap << "cmake_minimum_required(VERSION 3.22)\nproject(AxleSelectedPlugins LANGUAGES CXX)\n"
            << "include(" << QuotePath(request.SdkDirectory / "AxlePluginSDK.cmake") << ")\n"
            << "if(NOT AXLE_SDK_FINGERPRINT STREQUAL " << QuoteText(plan.SdkFingerprint) << ")\n"
            << "message(FATAL_ERROR \"SDK differs from the frozen plugin plan\")\nendif()\n"
            << "if(NOT AXLE_SDK_TOOLCHAIN_FINGERPRINT STREQUAL " << QuoteText(plan.ToolchainFingerprint) << ")\n"
            << "message(FATAL_ERROR \"Toolchain differs from the frozen plugin plan\")\nendif()\n"
            << "set(AXLE_PLUGIN_OUTPUT_DIRECTORY " << QuotePath(output) << ")\n"
            << "include(${CMAKE_CURRENT_LIST_DIR}/SelectedPlugins.cmake)\n";
        if (!FileSystem::WriteTextFileAtomically(source / "CMakeLists.txt", bootstrap.str(), &error)) { result.Error = error; return result; }
        std::vector<std::wstring> configure{L"-S", source.wstring(), L"-B", build.wstring(), L"-A", L"x64"};
        if (!request.Generator.empty()) { configure.push_back(L"-G"); configure.push_back(std::filesystem::u8path(request.Generator).wstring()); }
        if (!request.GeneratorToolset.empty()) { configure.push_back(L"-T"); configure.push_back(std::filesystem::u8path(request.GeneratorToolset).wstring()); }
        if (!request.GeneratorInstance.empty()) configure.push_back(L"-DCMAKE_GENERATOR_INSTANCE=" + request.GeneratorInstance.wstring());
        auto process = RunOwnedProcess(request.CMake, configure, root, result.LogPath, cancel);
        result.ExitCode = process.ExitCode; result.Cancelled = process.Cancelled;
        if (process.ExitCode != 0 || process.Cancelled) { result.Error = process.Error; return result; }
        // A successful target is not evidence that it emitted a current binary.
        // Remove only mutable expected outputs; immutable active generations are
        // never touched. Missing outputs force correctly configured targets to
        // relink and cannot be re-receipted from an earlier invocation.
        for (const auto& artifact : plan.Artifacts) if (artifact.Kind != Plugin::ModuleKind::Content) {
            for (const auto& extension : {".dll", ".lib", ".pdb"}) {
                const auto file = output / (artifact.ModuleName + extension);
                if (!Within(file, output)) { result.Error = "Invalid native output path"; return result; }
                std::filesystem::remove(file);
            }
        }
        std::vector<std::wstring> arguments{L"--build", build.wstring(), L"--config", std::filesystem::u8path(plan.Configuration).wstring(), L"--target"};
        for (const auto& artifact : plan.Artifacts) if (artifact.Kind != Plugin::ModuleKind::Content) arguments.push_back(std::filesystem::u8path(artifact.ModuleName).wstring());
        process = RunOwnedProcess(request.CMake, arguments, root, result.LogPath, cancel);
        result.ExitCode = process.ExitCode; result.Cancelled = process.Cancelled;
        if (process.ExitCode != 0 || process.Cancelled) { result.Error = process.Error; return result; }
        const auto current = Plugin::CreateBuildPlan(plan.FrozenResolution, plan.Product, plan.Configuration, plan.SdkFingerprint, plan.ToolchainFingerprint);
        if (!current.IsValid() || current.Fingerprint != plan.Fingerprint) { result.Error = "Plugin sources changed during the frozen build"; return result; }
        if (cancel.load()) { result.Cancelled = true; result.Error = "Plugin build cancelled before publication"; return result; }
        for (const auto& artifact : plan.Artifacts) {
            if (artifact.Kind == Plugin::ModuleKind::Content) continue;
            std::filesystem::create_directories(stage);
            for (const auto& extension : {".dll", ".lib", ".pdb"}) {
                const auto file = output / (artifact.ModuleName + extension);
                if (std::filesystem::is_regular_file(file)) std::filesystem::copy_file(file, stage / file.filename(), std::filesystem::copy_options::overwrite_existing);
            }
            if (!Plugin::WriteBuildReceipt(plan, artifact, stage / (artifact.ModuleName + ".build.receipt"), error, stage / (artifact.ModuleName + ".dll"))) { result.Error = error; return result; }
        }
        const auto destination = generations / (plan.Fingerprint + "-" + stamp);
        std::filesystem::rename(stage, destination);
        if (!ValidateGeneration(plan, destination, result.PublishedReceipts)) { result.Error = "Staged artifact validation failed"; return result; }
        if (!FileSystem::WriteTextFileAtomically(index, Plugin::PathUtf8(destination), &error)) { result.Error = error; return result; }
        result.GenerationDirectory = destination; result.Success = true; result.ExitCode = 0;
    } catch (const std::exception& exception) { result.Error = exception.what(); }
    return result;
#endif
}
}
