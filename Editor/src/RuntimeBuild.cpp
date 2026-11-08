#include "RuntimeBuild.hpp"
#include "RuntimeBuildConfig.hpp"
#include "OwnedProcess.hpp"
#include <Axle/AssetCooker.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Plugin.hpp>
#include <Axle/PluginArtifact.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/Path.hpp>
#include <Axle/UUID.hpp>
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axle {
struct RuntimeProductSnapshot {
    struct File { std::filesystem::path Original, Destination; std::string Bytes; };
    std::vector<File> Files;
    Plugin::BuildPlan Plan;
    std::vector<std::string> Selection;
    std::string Identity, SourceIdentity, PackagedProject;
    std::filesystem::path AssetRoot;
};
namespace {
std::atomic_bool building{false};
struct BuildLock { bool Owned = !building.exchange(true); ~BuildLock() { if (Owned) building = false; } };
std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read product input: " + PathToUtf8(path));
    std::ostringstream out; out << in.rdbuf();
    if (in.bad()) throw std::runtime_error("Incomplete product input: " + PathToUtf8(path));
    return out.str();
}
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(bytes.data(), bytes.size()); out.close();
    if (!out) throw std::runtime_error("Cannot write product snapshot: " + PathToUtf8(path));
}
bool Within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto rel = std::filesystem::weakly_canonical(child).lexically_relative(std::filesystem::weakly_canonical(parent));
    return !rel.empty() && *rel.begin() != "..";
}
bool VirtualWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto rel = child.lexically_normal().lexically_relative(parent.lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}
// Length-framed content identity; inventories are sorted, independent of mtimes.
struct Digest {
    std::uint64_t Value = 1469598103934665603ull;
    void Add(std::string_view text) {
        auto size = static_cast<std::uint64_t>(text.size());
        for (int i = 0; i < 8; ++i) { Value ^= (size >> (i * 8)) & 255; Value *= 1099511628211ull; }
        for (unsigned char c : text) { Value ^= c; Value *= 1099511628211ull; }
    }
    std::string Text() const { std::ostringstream out; out << std::hex << Value; return out.str(); }
};
void AddTree(RuntimeProductSnapshot& snapshot, const std::filesystem::path& root, const std::filesystem::path& destination, bool required) {
    if (!std::filesystem::is_directory(root)) {
        if (required) throw std::runtime_error("Product input directory is unavailable: " + PathToUtf8(root));
        return;
    }
    for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->is_symlink()) throw std::runtime_error("Product snapshot cannot contain filesystem links: " + PathToUtf8(it->path()));
        const auto name = PathToUtf8(it->path().filename());
        if (it->is_directory() && (name == ".git" || name == ".cache" || name == "build" || name == "Intermediate" || name == "Binaries" || name == "CMakeFiles")) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file()) snapshot.Files.push_back({it->path(), destination / it->path().lexically_relative(root), Read(it->path())});
    }
}
std::string Quote(std::string_view text) {
    std::string equal = "=";
    while (text.find("]" + equal + "]") != std::string_view::npos) equal += '=';
    return "[" + equal + "[" + std::string(text) + "]" + equal + "]";
}
std::string CppString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + '"';
}
std::string Symbol(const std::string& name) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "axle_static_";
    for (unsigned char c : name) { result += hex[c >> 4]; result += hex[c & 15]; }
    return result;
}
std::string Receipt(const RuntimeBuildRequest& request, const std::filesystem::path& directory) {
    std::map<std::string, std::string> hashes;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto relative = entry.path().lexically_relative(directory);
        if (relative == "AxleRuntime.build.receipt" || entry.path().extension() == ".pdb" || entry.path().extension() == ".map") continue;
        std::string error;
        const auto hash = Plugin::HashArtifactFile(entry.path(), error);
        if (hash.empty()) throw std::runtime_error(error);
        hashes.emplace(PathToUtf8(relative), hash);
    }
    std::ostringstream out;
    out << "schema=2\nsnapshot=" << request.Snapshot->Identity << "\nconfiguration=" << request.Configuration << "\nscripts=static\n";
    for (const auto& plugin : request.Snapshot->Plan.FrozenResolution.Plugins) out << "plugin=" << plugin.Id << '@' << plugin.Version << '\n';
    for (const auto& artifact : request.Snapshot->Plan.Artifacts) out << "module=" << artifact.PluginId << ':' << artifact.ModuleName << ':' << Plugin::ToString(artifact.Kind) << '\n';
    for (const auto& [name, hash] : hashes) out << name << '=' << hash << '\n';
    return out.str();
}
void GenerateComposition(const RuntimeProductSnapshot& snapshot, const std::filesystem::path& root) {
    auto graph = snapshot.Plan;
    std::map<std::filesystem::path, std::filesystem::path> installations;
    for (const auto& plugin : graph.FrozenResolution.Plugins) installations.emplace(plugin.Root, root / "plugins" / plugin.Id);
    for (auto& artifact : graph.Artifacts) {
        const auto original = artifact.CMakeRoot;
        artifact.CMakeRoot = installations.at(original);
        artifact.SourceRoot = artifact.CMakeRoot / artifact.SourceRoot.lexically_relative(original);
    }
    for (auto& path : graph.CMakeRoots) path = installations.at(path);
    std::string error;
    const auto cmake = root / "SelectedPlugins.cmake";
    if (!Plugin::GenerateCMakeInputs(graph, cmake, error)) throw std::runtime_error(error);
    std::map<std::string, const Plugin::BuildArtifact*> modules;
    for (const auto& artifact : graph.Artifacts) if (artifact.Kind == Plugin::ModuleKind::Runtime) modules.emplace(artifact.ModuleName, &artifact);
    std::vector<const Plugin::BuildArtifact*> ordered;
    std::set<std::string> visited;
    std::function<void(const Plugin::BuildArtifact&)> visit = [&](const auto& artifact) {
        if (!visited.insert(artifact.ModuleName).second) return;
        const auto plugin = std::find_if(graph.FrozenResolution.Plugins.begin(), graph.FrozenResolution.Plugins.end(), [&](const auto& p) { return p.Id == artifact.PluginId; });
        for (const auto& dependency : plugin->Dependencies)
            for (const auto& [name, candidate] : modules) if (candidate->PluginId == dependency.Id) visit(*candidate);
        for (const auto& dependency : artifact.Dependencies) visit(*modules.at(dependency));
        ordered.push_back(&artifact);
    };
    for (const auto& [name, artifact] : modules) visit(*artifact);
    std::ostringstream cpp;
    cpp << "#include \"RuntimeComposition.hpp\"\n#include \"PluginSDKConfig.hpp\"\n#include <stdexcept>\n";
    for (const auto* a : ordered) {
        cpp << "extern \"C\" const Axle::PluginRuntime::PluginDescriptor* " << Symbol(a->ModuleName) << "_get_descriptor() noexcept;\n"
            << "extern \"C\" bool " << Symbol(a->ModuleName) << "_start(const Axle::PluginRuntime::PluginHostApi*) noexcept;\n"
            << "extern \"C\" void " << Symbol(a->ModuleName) << "_stop() noexcept;\n";
    }
    cpp << "namespace Axle {\nstd::string CompiledRuntimeSnapshot() { return " << CppString(snapshot.Identity) << "; }\nstd::vector<std::string> CompiledRuntimeSelection() { return {";
    for (const auto& id : snapshot.Selection) cpp << CppString(id) << ',';
    cpp << "}; }\nvoid RegisterRuntimeModules(PluginRuntime::RegistrationRegistry& registry, PluginServiceSession& services, std::vector<std::unique_ptr<PluginRuntime::NativePlugin>>& modules) {\n";
    for (const auto* a : ordered) {
        cpp << "{ PluginRuntime::LoadRequest request; request.Expected.PluginId=" << CppString(a->PluginId)
            << "; request.Expected.ModuleName=" << CppString(a->ModuleName) << "; request.Expected.Product=Plugin::ProductKind::Runtime;\n"
            << "request.Expected.Configuration=AXLE_PLUGIN_BUILD_CONFIGURATION; request.Expected.SdkFingerprint=AXLE_PLUGIN_SDK_FINGERPRINT; request.Expected.ToolchainFingerprint=AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT; services.Attach(request);\n"
            << "auto module=std::make_unique<PluginRuntime::NativePlugin>(); std::string error; if(!module->LoadStatic(request, {&" << Symbol(a->ModuleName) << "_get_descriptor,&" << Symbol(a->ModuleName) << "_start,&" << Symbol(a->ModuleName) << "_stop}, registry, error)) throw std::runtime_error(error); modules.push_back(std::move(module)); }\n";
    }
    cpp << "}\n}\n";
    Write(root / "RuntimeModules.cpp", cpp.str());
    Write(cmake, "set(AXLE_RUNTIME_COMPOSITION_SOURCE " + Quote(PathToUtf8(root / "RuntimeModules.cpp")) + ")\n" + Read(cmake));
}
bool FrozenGenerationMatches(const RuntimeBuildRequest& request, const RuntimeBuildResult& result, std::string& error) {
    const auto& snapshot = *request.Snapshot;
    std::set<std::filesystem::path> assets;
    std::set<std::filesystem::path> sources;
    std::set<std::filesystem::path> resources;
    for (const auto& file : snapshot.Files) {
        const auto path = !result.CookOnly && VirtualWithin(file.Destination, "source")
            ? result.FrozenSourceDirectory / file.Destination.lexically_relative("source") : result.GenerationDirectory / file.Destination;
        if (result.CookOnly && !VirtualWithin(file.Destination, "project/assets") && !VirtualWithin(file.Destination, "source/Engine/Resources")) continue;
        if (Read(path) != file.Bytes) { error = "Frozen product input was modified: " + PathToUtf8(file.Destination); return false; }
        if (VirtualWithin(file.Destination, "project/assets")) assets.insert(path.lexically_normal());
        if (!result.CookOnly && VirtualWithin(file.Destination, "source")) sources.insert(path.lexically_normal());
        if (result.CookOnly && VirtualWithin(file.Destination, "source/Engine/Resources")) resources.insert(path.lexically_normal());
    }
    if (!result.CookOnly) for (const auto& entry : std::filesystem::recursive_directory_iterator(result.FrozenSourceDirectory))
        if (entry.is_symlink() || (entry.is_regular_file() && !sources.contains(entry.path().lexically_normal()))) { error = "Untracked source entered the immutable Engine cache"; return false; }
    if (result.CookOnly && std::filesystem::exists(result.GenerationDirectory / "source/Engine/Resources"))
        for (const auto& entry : std::filesystem::recursive_directory_iterator(result.GenerationDirectory / "source/Engine/Resources"))
            if (entry.is_symlink() || (entry.is_regular_file() && !resources.contains(entry.path().lexically_normal()))) { error = "Untracked resource entered the cook snapshot"; return false; }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(result.GenerationDirectory / "project/assets"))
        if (entry.is_symlink() || (entry.is_regular_file() && !assets.contains(entry.path().lexically_normal()))) { error = "Untracked content entered the product snapshot"; return false; }
    if (Read(result.GenerationDirectory / "project/Project.axleproj") != snapshot.PackagedProject) { error = "Frozen project configuration was modified"; return false; }
    if (!result.CookOnly && (Read(result.GenerationDirectory / "SelectedPlugins.cmake") != result.GeneratedGraph ||
        Read(result.GenerationDirectory / "RuntimeModules.cpp") != result.GeneratedRegistration)) { error = "Generated product registration changed"; return false; }
    return true;
}
}
RuntimeBuildRequest GetRuntimeBuildRequest() {
    RuntimeBuildRequest r;
    r.CMake = AXLE_RUNTIME_CMAKE; r.BuildDirectory = AXLE_RUNTIME_BUILD_DIRECTORY;
    r.Configuration = AXLE_RUNTIME_CONFIGURATION; r.EngineSourceDirectory = AXLE_RUNTIME_SOURCE_DIRECTORY;
    r.Generator = AXLE_RUNTIME_GENERATOR; r.GeneratorToolset = AXLE_RUNTIME_TOOLSET; r.GeneratorInstance = AXLE_RUNTIME_INSTANCE;
    r.Compiler = AXLE_RUNTIME_COMPILER; r.CompilerVersion = AXLE_RUNTIME_COMPILER_VERSION; r.WindowsSDK = AXLE_RUNTIME_WINDOWS_SDK;
    if (r.GeneratorToolset.empty() && !r.Compiler.empty()) {
        const auto version = r.Compiler.parent_path().parent_path().parent_path().parent_path().filename();
        r.GeneratorToolset = std::string(AXLE_RUNTIME_PLATFORM_TOOLSET) + ",version=" + PathToUtf8(version);
    }
    return r;
}
bool FreezeRuntimeBuildRequest(RuntimeBuildRequest& request, std::string& error) {
    try {
        if (request.Configuration != "Debug" && request.Configuration != "Release" && request.Configuration != "RelWithDebInfo" && request.Configuration != "MinSizeRel") throw std::runtime_error("Invalid Runtime configuration");
        request.EngineSourceDirectory = std::filesystem::canonical(request.EngineSourceDirectory);
        request.ProjectPath = std::filesystem::canonical(request.ProjectPath);
        auto project = std::make_shared<Project>();
        if (!ProjectSerializer(project).Deserialize(request.ProjectPath)) throw std::runtime_error("Cannot read Runtime project");
        const auto& config = project->GetConfig();
        const auto projectRoot = request.ProjectPath.parent_path();
        const auto assets = config.AssetDirectory.is_absolute() ? config.AssetDirectory : projectRoot / config.AssetDirectory;
        if (config.StartScene.is_absolute() || (!config.StartScene.empty() && !Within(assets / config.StartScene, assets))) throw std::runtime_error("Startup scene must be relative to the asset root");
        if (!config.StartScene.empty() && !std::filesystem::is_regular_file(assets / config.StartScene)) throw std::runtime_error("Required startup scene is missing");
        auto snapshot = std::make_shared<RuntimeProductSnapshot>();
        snapshot->AssetRoot = std::filesystem::weakly_canonical(assets);
        const auto catalog = Plugin::Discover({{request.EngineSourceDirectory / "Plugins", projectRoot / "Plugins"}, {projectRoot / "Plugins"}});
        const auto resolution = Plugin::Resolve(catalog, config.SelectedPlugins, Plugin::ProductKind::Runtime);
        snapshot->Plan = Plugin::CreateBuildPlan(resolution, Plugin::ProductKind::Runtime, request.Configuration, "source-static", "source-toolchain");
        if (!snapshot->Plan.IsValid()) throw std::runtime_error(snapshot->Plan.Diagnostics.front().Message);
        snapshot->Selection = config.SelectedPlugins;
        snapshot->Files.push_back({request.EngineSourceDirectory / "CMakeLists.txt", "source/CMakeLists.txt", Read(request.EngineSourceDirectory / "CMakeLists.txt")});
        for (const auto* name : {"Engine", "Runtime", "ThirdParty"}) AddTree(*snapshot, request.EngineSourceDirectory / name, std::filesystem::path("source") / name, true);
        AddTree(*snapshot, assets, "project/assets", true);
        AddTree(*snapshot, projectRoot / "Scripts", "project/Scripts", false);
        snapshot->Files.push_back({request.ProjectPath, "project/Authored.axleproj", Read(request.ProjectPath)});
        for (const auto& input : snapshot->Plan.Inputs) {
            const auto owner = std::find_if(resolution.Plugins.begin(), resolution.Plugins.end(), [&](const auto& plugin) { return Within(input.Path, plugin.Root); });
            if (owner == resolution.Plugins.end()) throw std::runtime_error("Plugin input has no owning installation");
            snapshot->Files.push_back({input.Path, std::filesystem::path("plugins") / owner->Id / input.Path.lexically_relative(owner->Root), input.Content});
        }
        for (const auto& plugin : resolution.Plugins) {
            bool declaredContent = false;
            for (const auto& module : plugin.Modules) if (module.Kind == Plugin::ModuleKind::Content) {
                declaredContent = true;
                AddTree(*snapshot, module.SourceRoot.empty() ? plugin.Root : plugin.Root / module.SourceRoot,
                    std::filesystem::path("project/assets/Plugins") / plugin.Id, true);
            }
            if (!declaredContent) AddTree(*snapshot, plugin.Root / "Content", std::filesystem::path("project/assets/Plugins") / plugin.Id, false);
        }
        std::sort(snapshot->Files.begin(), snapshot->Files.end(), [](const auto& a, const auto& b) { return a.Destination < b.Destination; });
        Digest hash;
        Digest sourceHash;
        if (request.Compiler.empty() || request.CompilerVersion.empty() || request.WindowsSDK.empty()) throw std::runtime_error("Matched Runtime compiler and Windows SDK identities are required");
        std::string hashError;
        const auto compilerHash = Plugin::HashArtifactFile(request.Compiler, hashError);
        const auto linkerHash = Plugin::HashArtifactFile(request.Compiler.parent_path() / "link.exe", hashError);
        if (compilerHash.empty() || linkerHash.empty()) throw std::runtime_error("Matched Runtime toolchain is unavailable: " + hashError);
        const auto toolchain = PathToUtf8(request.Compiler) + ";" + request.CompilerVersion + ";" + request.WindowsSDK + ";" + compilerHash + ";" + linkerHash;
        hash.Add(toolchain); sourceHash.Add(toolchain);
        hash.Add(request.Configuration); hash.Add(request.Generator); hash.Add(request.GeneratorToolset); hash.Add(PathToUtf8(request.GeneratorInstance));
        sourceHash.Add(request.Configuration); sourceHash.Add(request.Generator); sourceHash.Add(request.GeneratorToolset); sourceHash.Add(PathToUtf8(request.GeneratorInstance));
        hash.Add(PathToUtf8(request.ProjectPath)); hash.Add(snapshot->Plan.Fingerprint);
        for (std::size_t i = 0; i < snapshot->Files.size(); ++i) {
            const auto& file = snapshot->Files[i];
            if (i && file.Destination == snapshot->Files[i-1].Destination) throw std::runtime_error("Product snapshot content path collision");
            hash.Add(PathToUtf8(file.Destination)); hash.Add(file.Bytes);
            if (VirtualWithin(file.Destination, "source")) { sourceHash.Add(PathToUtf8(file.Destination)); sourceHash.Add(file.Bytes); }
        }
        snapshot->Identity = hash.Text();
        snapshot->SourceIdentity = sourceHash.Text();
        std::ostringstream packaged;
        packaged << "Project:\n  SchemaVersion: 1\n  Name: " << config.Name << "\n  StartScene: " << PathToUtf8(config.StartScene) << "\n  AssetDirectory: assets\n  RHIBackend: " << config.RHIBackend << "\n  RuntimeSnapshot: " << snapshot->Identity << "\n  Plugins:\n";
        for (const auto& id : config.SelectedPlugins) packaged << "    - " << id << '\n';
        snapshot->PackagedProject = packaged.str();
        request.BuildDirectory = request.EngineSourceDirectory / "build/p";
        if (Within(request.BuildDirectory, assets)) throw std::runtime_error("Runtime build output overlaps the authored asset tree");
        request.Snapshot = std::move(snapshot); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool ValidateRuntimeBuildRequest(const RuntimeBuildRequest& request, std::string& error) {
    if (!request.Snapshot) { error = "Runtime request has no frozen snapshot"; return false; }
    auto current = request; current.Snapshot.reset();
    if (!FreezeRuntimeBuildRequest(current, error)) return false;
    if (current.Snapshot->Identity != request.Snapshot->Identity) { error = "Project, selection, source or content changed since the Runtime snapshot; export again"; return false; }
    return true;
}
bool ValidateRuntimeBuildDocuments(const RuntimeBuildRequest& request,
    const std::function<bool(const std::filesystem::path&, std::string&)>& validator, std::string& error) {
    if (!request.Snapshot) { error = "Missing product snapshot"; return false; }
    for (const auto& file : request.Snapshot->Files) {
        if (!VirtualWithin(file.Destination, "project/assets")) continue;
        auto extension = PathToUtf8(file.Destination.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension != ".axlescene" && extension != ".axle" && extension != ".axleasset") continue;
        if (file.Bytes.find("AxlePlugin_") == std::string::npos) continue;
        if (!validator) { error = "Required plugin data needs a composition-aware document preflight"; return false; }
        if (!validator(file.Original, error)) return false;
    }
    return true;
}
RuntimeBuildResult BuildRuntime(const RuntimeBuildRequest& request, const std::atomic_bool& cancel) {
    RuntimeBuildResult result; result.Request = std::make_shared<RuntimeBuildRequest>(request);
    BuildLock lock;
    if (!lock.Owned) { result.Error = "Another Runtime build is already in progress"; return result; }
    try {
        if (!request.Snapshot) throw std::runtime_error("Runtime build requires a frozen product request");
        if (!ValidateRuntimeBuildRequest(request, result.Error)) return result;
        if (cancel) throw std::runtime_error("Runtime build cancelled");
        std::ostringstream generationName; generationName << std::hex << static_cast<std::uint64_t>(UUID());
        const auto generation = request.BuildDirectory / generationName.str();
        std::filesystem::create_directories(request.BuildDirectory);
        if (!std::filesystem::create_directory(generation)) throw std::runtime_error("Runtime generation already exists");
        std::filesystem::create_directories(generation / "project/assets");
        result.GenerationDirectory = generation; result.LogPath = generation / "build.log";
        const auto cache = request.EngineSourceDirectory / "build/r" / request.Snapshot->SourceIdentity;
        result.FrozenSourceDirectory = cache / "s";
        result.ProductBuildDirectory = cache / "b";
        std::filesystem::create_directories(cache);
#ifdef _WIN32
        struct CacheLock { HANDLE File = INVALID_HANDLE_VALUE; ~CacheLock() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); } } cacheLock;
        cacheLock.File = CreateFileW((cache / ".build.lock").c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (cacheLock.File == INVALID_HANDLE_VALUE) throw std::runtime_error("Another Runtime build owns the Engine cache");
#endif
        result.ExecutableDirectory = generation / "bin" / request.Configuration;
        result.ReceiptPath = result.ExecutableDirectory / "AxleRuntime.build.receipt";
        for (const auto& file : request.Snapshot->Files) {
            if (cancel) throw std::runtime_error("Runtime build cancelled");
            if (VirtualWithin(file.Destination, "source")) {
                const auto path = result.FrozenSourceDirectory / file.Destination.lexically_relative("source");
                if (std::filesystem::exists(path)) {
                    if (Read(path) != file.Bytes) throw std::runtime_error("Immutable Engine source cache was modified");
                } else Write(path, file.Bytes);
            } else Write(generation / file.Destination, file.Bytes);
        }
        Write(generation / "project/Project.axleproj", request.Snapshot->PackagedProject);
        GenerateComposition(*request.Snapshot, generation);
        result.GeneratedGraph = Read(generation / "SelectedPlugins.cmake");
        result.GeneratedRegistration = Read(generation / "RuntimeModules.cpp");
        std::vector<std::wstring> args{L"-S", result.FrozenSourceDirectory.wstring(), L"-B", result.ProductBuildDirectory.wstring(),
            L"-DAXLE_BUILD_EDITOR=OFF", L"-DAXLE_BUILD_TESTS=OFF", L"-DAXLE_BUILD_RUNTIME=ON",
            L"-DCMAKE_SYSTEM_VERSION=" + PathFromUtf8(request.WindowsSDK).wstring(),
            L"-DAXLE_RUNTIME_PLUGIN_GRAPH=" + (generation / "SelectedPlugins.cmake").wstring(),
            L"-DAXLE_RUNTIME_OUTPUT_DIRECTORY=" + (generation / "bin").wstring(),
            L"-DAXLE_RUNTIME_SCRIPTS_ROOT=" + (generation / "project/Scripts").wstring()};
        if (!request.Generator.empty()) { args.push_back(L"-G"); args.push_back(PathFromUtf8(request.Generator).wstring()); }
        if (!request.GeneratorToolset.empty()) { args.push_back(L"-T"); args.push_back(PathFromUtf8(request.GeneratorToolset).wstring()); }
        if (!request.GeneratorInstance.empty()) args.push_back(L"-DCMAKE_GENERATOR_INSTANCE=" + request.GeneratorInstance.wstring());
        auto process = RunOwnedProcess(request.CMake, args, generation, result.LogPath, cancel);
        result.ExitCode = process.ExitCode;
        if (process.ExitCode != 0 || process.Cancelled) throw std::runtime_error(process.Cancelled ? "Runtime build cancelled" : "Runtime configure failed: " + process.Error);
        process = RunOwnedProcess(request.CMake, {L"--build", result.ProductBuildDirectory.wstring(), L"--config", PathFromUtf8(request.Configuration).wstring(), L"--target", L"AxleRuntime", L"--parallel", L"12"}, generation, generation / "compile.log", cancel);
        result.LogPath = generation / "compile.log"; result.ExitCode = process.ExitCode;
        if (process.ExitCode != 0 || process.Cancelled) throw std::runtime_error(process.Cancelled ? "Runtime build cancelled" : "Runtime build failed: " + process.Error);
        if (!std::filesystem::is_regular_file(result.ExecutableDirectory / "AxleRuntime.exe")) throw std::runtime_error("Runtime build did not produce its player");
        process = RunOwnedProcess(result.ExecutableDirectory / "AxleRuntime.exe", {L"--check-content", L"--project=" + (generation / "project/Project.axleproj").wstring()}, result.ExecutableDirectory, generation / "content-preflight.log", cancel);
        result.ExitCode = process.ExitCode;
        if (process.ExitCode != 0 || process.Cancelled) { result.LogPath = generation / "content-preflight.log"; throw std::runtime_error(process.Cancelled ? "Runtime build cancelled" : "Compiled Runtime rejected required content or scripts"); }
        if (!ValidateRuntimeBuildRequest(request, result.Error) || !FrozenGenerationMatches(request, result, result.Error)) return result;
        Write(result.ReceiptPath, Receipt(request, result.ExecutableDirectory)); result.Success = true;
    } catch (const std::exception& exception) { result.Error = exception.what(); }
    return result;
}
bool BindRuntimeExport(const RuntimeBuildRequest& request, const RuntimeBuildResult& result, CookSettings& settings, std::string& error) {
    try {
        if (!result.Success || !ValidateRuntimeBuildRequest(request, error) || !FrozenGenerationMatches(request, result, error)) return false;
        const auto output = std::filesystem::weakly_canonical(settings.OutputDirectory.is_absolute() ? settings.OutputDirectory : request.ProjectPath.parent_path() / settings.OutputDirectory);
        const std::vector<std::filesystem::path> protectedRoots{
            request.Snapshot->AssetRoot, request.ProjectPath,
            request.ProjectPath.parent_path() / "Scripts", request.ProjectPath.parent_path() / "Plugins",
            request.EngineSourceDirectory / "Engine", request.EngineSourceDirectory / "Runtime",
            request.EngineSourceDirectory / "ThirdParty", request.EngineSourceDirectory / "Plugins",
            request.EngineSourceDirectory / "build/p", request.EngineSourceDirectory / "build/r"};
        for (const auto& root : protectedRoots)
            if (Within(output, root) || Within(root, output)) { error = "Export output overlaps authored product inputs or an owned build cache"; return false; }
        if (!result.CookOnly && Read(result.ReceiptPath) != Receipt(request, result.ExecutableDirectory)) { error = "Runtime receipt or product artifacts are stale"; return false; }
        settings.ProjectPath = result.GenerationDirectory / "project/Project.axleproj";
        settings.AssetDirectory = result.GenerationDirectory / "project/assets";
        settings.ExecutableDirectory = result.ExecutableDirectory; settings.RequireScriptsDll = false;
        settings.ShaderDirectory = result.CookOnly ? result.GenerationDirectory / "source/Engine/Resources/Shaders" : result.ExecutableDirectory / "Resources/Shaders";
        const auto prior = settings.Preflight;
        settings.Preflight = [request, result, prior](std::string& diagnostic) {
            try {
                if (prior && !prior(diagnostic)) return false;
                if (!ValidateRuntimeBuildRequest(request, diagnostic) || !FrozenGenerationMatches(request, result, diagnostic)) return false;
                if (!result.CookOnly && Read(result.ReceiptPath) != Receipt(request, result.ExecutableDirectory)) { diagnostic = "Runtime receipt or artifacts changed before export"; return false; }
                return true;
            } catch (const std::exception& exception) { diagnostic = exception.what(); return false; }
        };
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool BindRuntimeCook(const RuntimeBuildRequest& request, CookSettings& settings, std::string& error) {
    try {
        if (!ValidateRuntimeBuildRequest(request, error)) return false;
        RuntimeBuildResult result; result.Success = result.CookOnly = true;
        result.GenerationDirectory = request.BuildDirectory / ("cook-" + std::to_string(static_cast<std::uint64_t>(UUID())));
        std::filesystem::create_directories(result.GenerationDirectory / "project/assets");
        for (const auto& file : request.Snapshot->Files)
            if (VirtualWithin(file.Destination, "project/assets") || VirtualWithin(file.Destination, "source/Engine/Resources")) Write(result.GenerationDirectory / file.Destination, file.Bytes);
        Write(result.GenerationDirectory / "project/Project.axleproj", request.Snapshot->PackagedProject);
        settings.CopyExecutables = false;
        return BindRuntimeExport(request, result, settings, error);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
