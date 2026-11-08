#include "PluginSession.hpp"
#include <Axle/PluginArtifact.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace Axle {
namespace {
bool HasAbandoned(const std::filesystem::path& directory, bool clear) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) return false;
    bool abandoned = false;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pending") continue;
        // Live sessions keep a deny-write/delete handle. Only an abandoned
        // marker can be acquired here; other Editors are left alone.
        const auto file = CreateFileW(entry.path().c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { if (GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_LOCK_VIOLATION) abandoned = true; continue; }
        abandoned = true;
        if (clear) { FILE_DISPOSITION_INFO disposition{TRUE}; SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)); }
        CloseHandle(file);
    }
    return abandoned;
}
std::vector<const Plugin::BuildArtifact*> OrderedModules(const Plugin::BuildPlan& plan) {
    std::map<std::string, const Plugin::BuildArtifact*> modules;
    for (const auto& artifact : plan.Artifacts) if (artifact.Kind != Plugin::ModuleKind::Content) modules.emplace(artifact.ModuleName, &artifact);
    std::set<std::string> visited;
    std::vector<const Plugin::BuildArtifact*> ordered;
    std::function<void(const Plugin::BuildArtifact&)> visit = [&](const auto& artifact) {
        if (!visited.insert(artifact.ModuleName).second) return;
        // Plugin-level dependencies establish phase order even when a module
        // has no direct linker dependency on the other plugin.
        const auto plugin = std::find_if(plan.FrozenResolution.Plugins.begin(), plan.FrozenResolution.Plugins.end(), [&](const auto& p) { return p.Id == artifact.PluginId; });
        if (plugin != plan.FrozenResolution.Plugins.end()) for (const auto& dependency : plugin->Dependencies)
            for (const auto& [name, module] : modules) if (module->PluginId == dependency.Id) visit(*module);
        for (const auto& dependency : artifact.Dependencies) visit(*modules.at(dependency));
        ordered.push_back(&artifact);
    };
    for (const auto& plugin : plan.FrozenResolution.Plugins) {
        for (auto kind : {Plugin::ModuleKind::Runtime, Plugin::ModuleKind::Editor})
            for (const auto& [name, artifact] : modules) if (artifact->PluginId == plugin.Id && artifact->Kind == kind) visit(*artifact);
    }
    return ordered;
}
}
struct PluginProjectSession::Marker {
    HANDLE File = INVALID_HANDLE_VALUE;
    std::filesystem::path Path;
    bool Keep = false;
    bool Owned = false;
    ~Marker() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); if (Owned && !Keep && !Path.empty()) { std::error_code error; std::filesystem::remove(Path, error); } }
    bool Open(const std::filesystem::path& directory, const std::string& fingerprint) {
        std::filesystem::create_directories(directory);
        Path = directory / (std::to_string(static_cast<std::uint64_t>(UUID{})) + ".pending");
        File = CreateFileW(Path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE) return false;
        Owned = true;
        const std::string text = "schema=1\nplan=" + fingerprint + "\n";
        DWORD written = 0;
        return WriteFile(File, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size() && FlushFileBuffers(File);
    }
};
PluginProjectSession::PluginProjectSession(PluginSessionEnvironment environment)
    : m_Environment(std::move(environment)), m_Services(m_Registrations), m_EditorServices(m_Services, m_Registrations) {}
PluginProjectSession::~PluginProjectSession() { Shutdown(); }
bool PluginProjectSession::Bind(std::string& error) {
    if (m_Bound) return true;
    if (!m_Services.Bind(error)) return false;
    if (!m_EditorServices.Bind(error)) { m_Services.Stop(); return false; }
    m_Bound = true; return true;
}
PluginBuildRequest PluginProjectSession::Request(bool tools) const {
    PluginBuildRequest request;
    request.ProjectDirectory = m_Project ? m_Project->GetProjectDirectoryPath() : std::filesystem::path{};
    request.Plan = m_Plan;
    if (tools) { request.CMake = m_Environment.CMake; request.SdkDirectory = m_Environment.SdkDirectory; }
    request.Generator = m_Environment.Generator; request.GeneratorToolset = m_Environment.GeneratorToolset; request.GeneratorInstance = m_Environment.GeneratorInstance;
    return request;
}
bool PluginProjectSession::Refresh() {
    if (IsBuilding()) { m_Error = "Plugin selection is frozen while a build is running"; return false; }
    m_Error.clear();
    if (!m_Project) { m_Error = "No active project"; return false; }
    const auto projectPlugins = m_Project->GetProjectDirectoryPath() / "Plugins";
    Plugin::DiscoveryOptions options;
    if (!m_Environment.EnginePlugins.empty()) options.Roots.push_back(m_Environment.EnginePlugins);
    if (!m_Project->GetProjectDirectoryPath().empty()) { options.Roots.push_back(projectPlugins); options.ProjectRoots.push_back(projectPlugins); }
    m_Catalog = Plugin::Discover(options);
    const auto resolution = Plugin::Resolve(m_Catalog, m_Project->GetConfig().SelectedPlugins, Plugin::ProductKind::Editor);
    m_Plan = Plugin::CreateBuildPlan(resolution, Plugin::ProductKind::Editor, m_Environment.Configuration, m_Environment.SdkFingerprint, m_Environment.ToolchainFingerprint);
    if (!m_Plan.IsValid()) { for (const auto& error : m_Plan.Diagnostics) { if (!m_Error.empty()) m_Error += '\n'; m_Error += error.Message; } return false; }
    if (m_Status != Status::Recovery) {
        m_Status = !m_ActiveFingerprint.empty() && m_ActiveFingerprint == m_Plan.Fingerprint ? Status::Active :
            m_StagedFingerprint == m_Plan.Fingerprint ? Status::NeedsRestart : Status::NeedsBuild;
    }
    return true;
}
bool PluginProjectSession::SetProjectFile(std::filesystem::path path) {
    if (!m_Project || IsBuilding() || path.empty()) { m_Error = "Cannot change the project path during a plugin build"; return false; }
    std::error_code error;
    path = std::filesystem::absolute(path, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(path, error) || error ||
        !std::filesystem::equivalent(path.parent_path(), m_Project->GetProjectDirectoryPath(), error) || error) {
        m_Error = "Saved project file does not belong to the active project directory"; return false;
    }
    m_ProjectPath = std::move(path);
    if (m_Marker && m_Marker->Path.parent_path() != m_Project->GetProjectDirectoryPath() / "Intermediate/Plugins/Sessions") {
        try {
            auto moved = std::make_unique<Marker>();
            if (!moved->Open(m_Project->GetProjectDirectoryPath() / "Intermediate/Plugins/Sessions", m_ActiveFingerprint)) {
                m_Error = "Could not create the relocated project's recovery marker"; return false;
            }
            moved->Keep = m_Marker->Keep;
            m_Marker = std::move(moved);
        } catch (const std::exception& exception) { m_Error = exception.what(); return false; }
    }
    if (!Refresh()) { m_Status = Status::Error; return false; }
    return true;
}
bool PluginProjectSession::Open(std::shared_ptr<Project> project, std::filesystem::path path, bool safeMode, std::optional<PluginRestartSnapshot> expected) {
    if (!m_Bound || IsBuilding() || m_Opened) { m_Error = "The active session must be restarted before changing projects"; return false; }
    m_Opened = true;
    try {
    m_Project = std::move(project); m_ProjectPath = std::move(path);
    m_ActiveIds.clear(); m_ActiveFingerprint.clear(); m_Status = Status::Empty;
    if (!Refresh()) { m_Status = Status::Error; return false; }
    if (safeMode || (!m_ProjectPath.empty() && HasAbandoned(m_Project->GetProjectDirectoryPath() / "Intermediate/Plugins/Sessions", false))) {
        m_Status = Status::Recovery; m_Error = "Native plugins are disabled for recovery. Review selections, build and restart to retry."; return false;
    }
    std::atomic_bool cancel{false};
    m_LastBuild = BuildPlugins(Request(false), cancel);
    if (!m_LastBuild.Success) { m_Status = Status::NeedsBuild; m_Error = "Selected native modules need a plugin build. " + m_LastBuild.Error; return false; }
    PluginRestartSnapshot snapshot; std::vector<std::string> modules;
    for (const auto& artifact : m_Plan.Artifacts) if (artifact.Kind != Plugin::ModuleKind::Content) modules.push_back(artifact.ModuleName);
    if (!CapturePluginRestartSnapshot(m_LastBuild.GenerationDirectory,modules,snapshot,m_Error)) { m_Status = Status::Recovery; return false; }
    if (expected && (expected->ReceiptSet != snapshot.ReceiptSet ||
        (expected->Generation.empty() != snapshot.Generation.empty()) ||
        (!snapshot.Generation.empty() && !std::filesystem::equivalent(expected->Generation,snapshot.Generation)))) {
        m_Error = "Restart staged generation or receipt set changed before activation"; m_Status = Status::Recovery; return false;
    }
    m_StagedFingerprint = m_Plan.Fingerprint; return Activate(m_LastBuild,snapshot);
    } catch (const std::exception& error) {
        while (!m_Modules.empty()) { m_Modules.back()->AbortStartup(); m_Modules.pop_back(); }
        if (m_Marker) { m_Marker->Keep = true; m_Marker.reset(); }
        m_Status = Status::Recovery; m_Error = std::string("Native plugin startup was disabled for recovery: ") + error.what(); return false;
    }
}
bool PluginProjectSession::Activate(const PluginBuildResult& generation, const PluginRestartSnapshot& snapshot) {
    const auto ordered = OrderedModules(m_Plan);
    if (!ordered.empty()) {
        if (m_AttemptedNative) { m_Status = Status::NeedsRestart; m_Error = "Native initialization is limited to one attempt per process. Restart to activate this build."; return false; }
        m_AttemptedNative = true;
        m_Marker = std::make_unique<Marker>();
        if (!m_Marker->Open(m_Project->GetProjectDirectoryPath() / "Intermediate/Plugins/Sessions", m_Plan.Fingerprint)) { m_Error = "Could not create the native session recovery marker"; m_Status = Status::Recovery; return false; }
    }
    for (const auto* artifact : ordered) {
        PluginRuntime::LoadRequest request;
        request.ModulePath = generation.GenerationDirectory / (artifact->ModuleName + ".dll"); request.ReceiptPath = generation.GenerationDirectory / (artifact->ModuleName + ".build.receipt");
        auto& identity = request.Expected;
        identity.PluginId = artifact->PluginId; identity.ModuleName = artifact->ModuleName; identity.Kind = artifact->Kind; identity.SourceRoot = artifact->SourceRoot;
        identity.Product = m_Plan.Product; identity.Configuration = m_Plan.Configuration; identity.SdkFingerprint = m_Plan.SdkFingerprint; identity.ToolchainFingerprint = m_Plan.ToolchainFingerprint; identity.Fingerprint = m_Plan.Fingerprint;
        identity.BinaryHash = snapshot.BinaryHashes.at(artifact->ModuleName);
        m_Services.Attach(request);
        auto module = std::make_unique<PluginRuntime::NativePlugin>();
        if (!module->Load(request, m_Registrations, m_Error)) {
            while (!m_Modules.empty()) { m_Modules.back()->AbortStartup(); m_Modules.pop_back(); }
            if (m_Marker) {
                m_Marker->Keep = true;
                if (m_Marker->File != INVALID_HANDLE_VALUE) { CloseHandle(m_Marker->File); m_Marker->File = INVALID_HANDLE_VALUE; }
            }
            m_Status = Status::Recovery; return false;
        }
        m_Modules.push_back(std::move(module));
    }
    m_Services.RehydrateScenes();
    m_ActiveIds.clear();
    for (const auto& plugin : m_Plan.FrozenResolution.Plugins) m_ActiveIds.push_back(plugin.Id);
    std::sort(m_ActiveIds.begin(), m_ActiveIds.end());
    m_ActiveFingerprint = m_Plan.Fingerprint; m_Status = Status::Active; m_Error.clear(); return true;
}
bool PluginProjectSession::MatchesDesired() {
    const auto previousError = m_Error;
    if (IsBuilding() || !Refresh()) return false;
    if (m_Status == Status::Recovery) m_Error = previousError;
    return m_Status == Status::Active && m_ActiveFingerprint == m_Plan.Fingerprint;
}
bool PluginProjectSession::BeginBuild() {
    if (IsBuilding()) { m_Error = "A plugin build is already running"; return false; }
    if (m_ProjectPath.empty() || !Refresh()) { if (m_Error.empty()) m_Error = "Save Project As before building plugins"; return false; }
    auto request = Request(true);
    m_Progress = std::make_shared<PluginBuildProgress>(); m_Progress->Set("Validating selected plugins..."); request.Progress = m_Progress;
    m_Cancel = std::make_shared<std::atomic_bool>(false);
    m_Build = std::async(std::launch::async, [request, cancel = m_Cancel] { return BuildPlugins(request, *cancel); });
    m_Error.clear(); return true;
}
void PluginProjectSession::CancelBuild() { if (m_Cancel) *m_Cancel = true; }
std::pair<std::string, std::filesystem::path> PluginProjectSession::BuildProgress() const {
    if (!m_Progress) return {};
    std::lock_guard lock(m_Progress->Mutex); return {m_Progress->Stage, m_Progress->Log};
}
void PluginProjectSession::Pump() {
    m_Services.Drain();
    if (m_Build.valid() && m_Build.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try { m_LastBuild = m_Build.get(); } catch (const std::exception& error) { m_LastBuild = {}; m_LastBuild.Error = error.what(); }
        m_Cancel.reset(); m_Error = m_LastBuild.Error;
        if (m_LastBuild.Success) { m_StagedFingerprint = m_Plan.Fingerprint; m_Status = m_ActiveFingerprint == m_Plan.Fingerprint ? Status::Active : Status::NeedsRestart; }
    }
}
bool PluginProjectSession::ClearAbandonedSessions() {
    if (!m_Project || m_ProjectPath.empty()) return false;
    if (m_Modules.empty() && m_Marker && m_Marker->Keep) { m_Marker->Keep = false; m_Marker.reset(); }
    try {
    HasAbandoned(m_Project->GetProjectDirectoryPath() / "Intermediate/Plugins/Sessions", true);
    return !HasAbandoned(m_Project->GetProjectDirectoryPath() / "Intermediate/Plugins/Sessions", false);
    } catch (const std::exception& exception) { m_Error = exception.what(); return false; }
}
void PluginProjectSession::Shutdown() {
    Quiesce();
    while (!m_Modules.empty()) { m_Modules.back()->Stop(); m_Modules.pop_back(); }
    m_EditorServices.Stop(); m_Services.Stop(); m_Marker.reset();
    m_Bound = false;
}
void PluginProjectSession::Quiesce() {
    CancelBuild(); if (m_Build.valid()) { m_Build.wait(); try { m_Build.get(); } catch (...) {} }
    m_Services.Quiesce();
}
}
